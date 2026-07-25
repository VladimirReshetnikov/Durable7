using System.Text.Json;

namespace Durable7.FingerTree.Benchmarks;

/// <summary>
/// Writes the untimed operation-counter and retained-version artifacts required by the C0 exit
/// protocol. This path is deliberately separate from every BenchmarkDotNet method.
/// </summary>
internal static class Axis2C0EvidenceCollector
{
    private const string Command = "--axis2-c0-evidence";
    private const int EditBurst = 256;

    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = true,
    };

    internal static bool TryRun(string[] args)
    {
        if (args.Length == 0 || !string.Equals(args[0], Command, StringComparison.Ordinal))
            return false;

        if (args.Length is < 2 or > 5)
        {
            throw new ArgumentException(
                $"Usage: {Command} <artifact-directory> [focus-capacity] [flush-size] [commit].");
        }

        var artifactRoot = Path.GetFullPath(args[1]);
        var focusCapacity = args.Length >= 3 ? int.Parse(args[2]) : 16;
        var flushSize = args.Length >= 4 ? int.Parse(args[3]) : 256;
        var commit = args.Length >= 5 ? args[4] : "unspecified";

        // Validate the supplied tuning before creating artifact directories.
        _ = Rope<int>.Empty.GetClassCursorPrototype(0, focusCapacity, flushSize);

        var metadata = new C0EvidenceMetadata(
            Schema: "axis2-c0-evidence-v1",
            Commit: commit,
            CollectedUtc: DateTimeOffset.UtcNow,
            Runtime: Environment.Version.ToString(),
            FrameworkDescription: System.Runtime.InteropServices.RuntimeInformation.FrameworkDescription,
            OperatingSystem: System.Runtime.InteropServices.RuntimeInformation.OSDescription,
            ProcessArchitecture: System.Runtime.InteropServices.RuntimeInformation.ProcessArchitecture.ToString(),
            ProcessorCount: Environment.ProcessorCount,
            Seed: Axis2BenchmarkPolicy.Seed,
            FocusCapacity: focusCapacity,
            FlushSize: flushSize);

        var counterArtifact = new C0CounterArtifact(
            metadata,
            CollectCarrierRows(focusCapacity, flushSize),
            CollectTypingRows(focusCapacity, flushSize),
            CollectFanoutRows(focusCapacity, flushSize));
        var retainedArtifact = new C0RetainedArtifact(
            metadata,
            CollectRetainedRows(focusCapacity, flushSize));

        WriteArtifact(
            Path.Combine(artifactRoot, "counters", "axis2-c0-counters.json"),
            counterArtifact);
        WriteArtifact(
            Path.Combine(artifactRoot, "retained", "axis2-c0-retained.json"),
            retainedArtifact);

        Console.WriteLine($"Axis 2 C0 evidence written beneath '{artifactRoot}'.");
        return true;
    }

    private static C0CarrierCounterRow[] CollectCarrierRows(int focusCapacity, int flushSize)
    {
        var source = Rope<char>.Create(Axis2BenchmarkPolicy.CreateText(65_536));
        var positions = RopeCursorBenchmarkWorkload.CreatePositions(65_536, 8, EditBurst);
        return
        [
            MeasureCarrier("class", () => RopeCursorBenchmarkWorkload.RunClassCursor(
                source,
                positions,
                snapshotCadence: 16,
                focusCapacity,
                flushSize,
                useAbsoluteSeek: false)),
            MeasureCarrier("struct", () => RopeCursorBenchmarkWorkload.RunStructCursor(
                source,
                positions,
                snapshotCadence: 16,
                focusCapacity,
                flushSize,
                useAbsoluteSeek: false)),
            MeasureCarrier("mutable-control", () => RopeCursorBenchmarkWorkload.RunMutableCursor(
                source,
                positions,
                snapshotCadence: 16,
                focusCapacity,
                flushSize,
                useAbsoluteSeek: false)),
        ];
    }

    private static C0CarrierCounterRow MeasureCarrier(string carrier, Func<Rope<char>> action)
    {
        using var session = RopeCursorDiagnostics.BeginSession();
        var snapshot = action();
        var counters = session.Snapshot;
        return new C0CarrierCounterRow(
            Carrier: carrier,
            DocumentSize: snapshot.Count,
            LocalityWindow: 8,
            SnapshotCadence: 16,
            EditCount: EditBurst,
            SnapshotChecksum: SnapshotFingerprint(snapshot),
            Counters: counters);
    }

    private static C0TypingCounterRow[] CollectTypingRows(int focusCapacity, int flushSize)
    {
        var rows = new List<C0TypingCounterRow>();
        foreach (var direction in new[] { "forward", "reverse" })
        {
            using var session = RopeCursorDiagnostics.BeginSession();
            var cursor = Rope<int>.Empty.GetClassCursorPrototype(0, focusCapacity, flushSize);
            var operationCount = checked((flushSize * 2) + focusCapacity + 3);
            for (var value = 0; value < operationCount; value++)
                cursor = cursor.Insert(value);

            if (direction == "reverse")
            {
                while (!cursor.IsAtStart)
                    cursor = cursor.MovePrevious();
            }

            var snapshot = cursor.Snapshot();
            cursor.Validate();
            rows.Add(new C0TypingCounterRow(
                direction,
                operationCount,
                cursor.Position,
                SnapshotFingerprint(snapshot),
                cursor.GetDiagnostics(),
                session.Snapshot));
        }

        return [.. rows];
    }

    private static C0FanoutCounterRow[] CollectFanoutRows(int focusCapacity, int flushSize)
    {
        var rows = new List<C0FanoutCounterRow>();
        foreach (var documentSize in Axis2BenchmarkPolicy.CursorDocumentSizes)
        foreach (var branchCount in new[] { 1, 8, 64, 256 })
        {
            var source = Rope<int>.Create(Enumerable.Range(0, documentSize).ToArray());
            var parent = source.GetClassCursorPrototype(documentSize, focusCapacity, flushSize);

            // Fill the active window and the left carry to one element before a flush. Every child
            // must then pay its own boundary repair; potential consumed in one branch cannot pay for
            // a sibling.
            // A nonempty source cursor starts with a full active focus. Each insertion therefore
            // spills one element into the near carry; K - 1 edits leave the exact pre-flush state.
            var primingCount = flushSize - 1;
            for (var index = 0; index < primingCount; index++)
                parent = parent.Insert(-index - 1);

            var parentState = parent.GetDiagnostics();
            using var session = RopeCursorDiagnostics.BeginSession();
            long checksum = 0;
            long retainedBufferBytes = 0;
            for (var branch = 0; branch < branchCount; branch++)
            {
                var child = parent.Insert(int.MinValue + branch);
                var snapshot = child.Snapshot();
                child.Validate();
                checksum = unchecked((checksum * 1_099_511_628_211L) ^ SnapshotFingerprint(snapshot));
                retainedBufferBytes = checked(
                    retainedBufferBytes + child.GetDiagnostics().EstimatedRetainedBufferBytes);
            }

            rows.Add(new C0FanoutCounterRow(
                documentSize,
                branchCount,
                primingCount,
                parentState,
                checksum,
                retainedBufferBytes,
                session.Snapshot));
        }

        return [.. rows];
    }

    private static C0RetainedRow[] CollectRetainedRows(int focusCapacity, int flushSize)
    {
        var rows = new List<C0RetainedRow>();
        foreach (var documentSize in Axis2BenchmarkPolicy.CursorDocumentSizes)
        foreach (var branchCount in new[] { 1, 8, 64, 256 })
        {
            var source = Rope<int>.Create(Enumerable.Range(0, documentSize).ToArray());
            var parent = source.GetClassCursorPrototype(documentSize / 2, focusCapacity, flushSize);
            for (var index = 0; index < flushSize - 1; index++)
                parent = parent.Insert(-index - 1);

            var children = new RopeCursorPrototype<int>[branchCount];
            long estimatedBuffers = parent.GetDiagnostics().EstimatedRetainedBufferBytes;
            long snapshotChunkStorage = 0;
            var sharedStoresWithSource = 0;
            for (var branch = 0; branch < branchCount; branch++)
            {
                var child = parent.Insert(int.MinValue + branch);
                var snapshot = child.Snapshot();
                children[branch] = child;
                estimatedBuffers = checked(
                    estimatedBuffers + child.GetDiagnostics().EstimatedRetainedBufferBytes);
                snapshotChunkStorage = checked(
                    snapshotChunkStorage + snapshot.GetStructureDiagnostics().EstimatedChunkStorageBytes);
                sharedStoresWithSource = checked(
                    sharedStoresWithSource + source.CountSharedBackingStoresForDiagnostics(snapshot));
            }

            GC.KeepAlive(children);
            rows.Add(new C0RetainedRow(
                documentSize,
                branchCount,
                source.GetStructureDiagnostics(),
                parent.GetDiagnostics(),
                estimatedBuffers,
                snapshotChunkStorage,
                sharedStoresWithSource));
        }

        return [.. rows];
    }

    private static long SnapshotFingerprint<T>(Rope<T> snapshot)
    {
        var comparer = EqualityComparer<T>.Default;
        long checksum = snapshot.Count;
        if (snapshot.Count == 0)
            return checksum;

        checksum = unchecked((checksum * 31) + comparer.GetHashCode(snapshot[0]!));
        checksum = unchecked((checksum * 31) + comparer.GetHashCode(snapshot[snapshot.Count / 2]!));
        checksum = unchecked((checksum * 31) + comparer.GetHashCode(snapshot[snapshot.Count - 1]!));
        return checksum;
    }

    private static void WriteArtifact<T>(string path, T value)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllText(path, JsonSerializer.Serialize(value, JsonOptions));
    }

    private sealed record C0EvidenceMetadata(
        string Schema,
        string Commit,
        DateTimeOffset CollectedUtc,
        string Runtime,
        string FrameworkDescription,
        string OperatingSystem,
        string ProcessArchitecture,
        int ProcessorCount,
        int Seed,
        int FocusCapacity,
        int FlushSize);

    private sealed record C0CounterArtifact(
        C0EvidenceMetadata Metadata,
        C0CarrierCounterRow[] CarrierRows,
        C0TypingCounterRow[] TypingRows,
        C0FanoutCounterRow[] FanoutRows);

    private sealed record C0RetainedArtifact(
        C0EvidenceMetadata Metadata,
        C0RetainedRow[] Rows);

    private sealed record C0CarrierCounterRow(
        string Carrier,
        int DocumentSize,
        int LocalityWindow,
        int SnapshotCadence,
        int EditCount,
        long SnapshotChecksum,
        RopeCursorOperationDiagnostics Counters);

    private sealed record C0TypingCounterRow(
        string Direction,
        int OperationCount,
        int FinalPosition,
        long SnapshotChecksum,
        RopeCursorPrototypeStateDiagnostics State,
        RopeCursorOperationDiagnostics Counters);

    private sealed record C0FanoutCounterRow(
        int DocumentSize,
        int BranchCount,
        int PrimingEditCount,
        RopeCursorPrototypeStateDiagnostics ParentState,
        long SnapshotChecksum,
        long EstimatedRetainedBufferBytes,
        RopeCursorOperationDiagnostics Counters);

    private sealed record C0RetainedRow(
        int DocumentSize,
        int BranchCount,
        RopeStructureDiagnostics Source,
        RopeCursorPrototypeStateDiagnostics Parent,
        long ConservativeRetainedBufferBytes,
        long SummedSnapshotChunkStorageBytes,
        int SummedSharedBackingStoresWithSource);
}
