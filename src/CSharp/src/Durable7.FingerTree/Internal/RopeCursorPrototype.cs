// The reference-shaped prototype cursor: immutable, each movement producing a new cursor.

using System.Diagnostics.CodeAnalysis;
using System.Runtime.CompilerServices;

namespace Durable7.FingerTree;

/// <summary>
/// How a prototype cursor is configured, so the alternatives can be compared under identical
/// settings.
/// </summary>
internal readonly record struct RopeCursorPrototypeConfiguration(int FocusCapacity, int FlushChunkSize)
{
    /// <summary>Creates an element rope using the supplied policies, which it retains.</summary>
    internal static RopeCursorPrototypeConfiguration Create(int focusCapacity, int flushChunkSize)
    {
        if (focusCapacity is < 2 or > 128)
            throw new ArgumentOutOfRangeException(nameof(focusCapacity));
        if (flushChunkSize is < RopeChunking.MinChunkSize or > RopeChunking.MaxChunkSize)
            throw new ArgumentOutOfRangeException(nameof(flushChunkSize));
        if (focusCapacity > flushChunkSize)
            throw new ArgumentException("The focus capacity cannot exceed the flush chunk size.");
        return new RopeCursorPrototypeConfiguration(focusCapacity, flushChunkSize);
    }
}

/// <summary>
/// The path from the root down to the cursor's chunk, so a local edit rebuilds only that path.
/// </summary>
internal readonly record struct RopeZipperContext<T>(
    FingerTree<Chunk<T>, int, ChunkLengthMeasure<T>> LeftTree,
    T[] LeftCarry,
    T[] Active,
    int Gap,
    T[] RightCarry,
    FingerTree<Chunk<T>, int, ChunkLengthMeasure<T>> RightTree,
    int Position,
    RopeCursorPrototypeConfiguration Configuration);

/// <summary>
/// The rope version a prototype cursor is positioned in, together with the cached path into it.
/// </summary>
internal sealed class CursorVersionState<T>
{
    private readonly RopeZipperContext<T> _snapshotSeed;
    private Rope<T>? _snapshot;

    /// <summary>Creates a new cursor version state.</summary>
    internal CursorVersionState(int count, RopeZipperContext<T> snapshotSeed, Rope<T>? snapshot)
    {
        Count = count;
        _snapshotSeed = snapshotSeed;
        _snapshot = snapshot;
    }

    /// <summary>Gets the number of elements in the collection.</summary>
    internal int Count { get; }

    /// <summary>Gets a value indicating whether cached snapshot.</summary>
    internal bool HasCachedSnapshot => Volatile.Read(ref _snapshot) is not null;

    /// <summary>Gets the snapshot seed for diagnostics.</summary>
    internal RopeZipperContext<T> SnapshotSeedForDiagnostics => _snapshotSeed;

    /// <summary>Gets the collection version this cursor is positioned in.</summary>
    internal Rope<T> Snapshot()
    {
        var cached = Volatile.Read(ref _snapshot);
        if (cached is not null)
            return cached;

        var candidate = RopeCursorPrototypeEngine<T>.BuildSnapshot(_snapshotSeed, Count);
        var winner = Interlocked.CompareExchange(ref _snapshot, candidate, null);
        return winner ?? candidate;
    }
}

/// <summary>One recorded prototype cursor edit.</summary>
internal readonly record struct RopeCursorPrototypeEdit<T>(
    CursorVersionState<T> Version,
    RopeZipperContext<T> Context);

/// <summary>
/// The shared mechanics the prototype cursors are built on, so the immutable, struct and mutable
/// shapes differ only in their surface.
/// </summary>
internal static class RopeCursorPrototypeEngine<T>
{
    /// <summary>Creates an element rope using the supplied policies, which it retains.</summary>
    internal static RopeCursorPrototypeEdit<T> Create(
        Rope<T> source,
        int position,
        RopeCursorPrototypeConfiguration configuration)
    {
        ArgumentNullException.ThrowIfNull(source);
        if ((uint)position > (uint)source.Count)
            throw new ArgumentOutOfRangeException(nameof(position));

        var context = CreateContext(source, position, configuration);
        return new RopeCursorPrototypeEdit<T>(
            new CursorVersionState<T>(source.Count, context, source),
            context);
    }

    /// <summary>Creates the context.</summary>
    internal static RopeZipperContext<T> CreateContext(
        Rope<T> source,
        int position,
        RopeCursorPrototypeConfiguration configuration)
    {
        if (source.IsEmpty)
        {
            return new RopeZipperContext<T>(
                FingerTree<Chunk<T>, int, ChunkLengthMeasure<T>>.Empty,
                [],
                [],
                0,
                [],
                FingerTree<Chunk<T>, int, ChunkLengthMeasure<T>>.Empty,
                0,
                configuration);
        }

        var activeLength = Math.Min(configuration.FocusCapacity, source.Count);
        var start = Math.Clamp(position - (activeLength / 2), 0, source.Count - activeLength);
        var (left, rest) = source.Split(start);
        var (active, right) = rest.Split(activeLength);
        var context = new RopeZipperContext<T>(
            left.TreeForCursorPrototype,
            [],
            active.ToArray(),
            position - start,
            [],
            right.TreeForCursorPrototype,
            position,
            configuration);
        Validate(context, source.Count);
        return context;
    }

    /// <summary>Reads the element immediately before the gap, reporting whether one exists.</summary>
    internal static bool TryPeekPrevious(
        in RopeZipperContext<T> context,
        [MaybeNullWhen(false)] out T value)
    {
        if (context.Position == 0)
        {
            value = default;
            return false;
        }

        if (context.Gap > 0)
        {
            value = context.Active[context.Gap - 1];
            return true;
        }

        if (context.LeftCarry.Length > 0)
        {
            value = context.LeftCarry[^1];
            return true;
        }

        RopeCursorDiagnostics.RecordNodeVisits();
        var chunk = context.LeftTree.Last;
        value = chunk[chunk.Length - 1];
        return true;
    }

    /// <summary>Reads the element immediately after the gap, reporting whether one exists.</summary>
    internal static bool TryPeekNext(
        in RopeZipperContext<T> context,
        int count,
        [MaybeNullWhen(false)] out T value)
    {
        if (context.Position == count)
        {
            value = default;
            return false;
        }

        if (context.Gap < context.Active.Length)
        {
            value = context.Active[context.Gap];
            return true;
        }

        if (context.RightCarry.Length > 0)
        {
            value = context.RightCarry[0];
            return true;
        }

        RopeCursorDiagnostics.RecordNodeVisits();
        var chunk = context.RightTree.First;
        value = chunk[0];
        return true;
    }

    /// <summary>
    /// Returns a cursor one position earlier. The receiver is unchanged; movement produces a new cursor over the same
    /// version.
    /// </summary>
    internal static RopeZipperContext<T> MovePrevious(in RopeZipperContext<T> source, int count)
    {
        if (source.Position == 0)
            throw new InvalidOperationException("The cursor is already at the start of the rope.");

        var context = source;
        if (context.Gap == 0)
        {
            context = PullFromLeft(context, Math.Max(1, context.Configuration.FocusCapacity / 2));
            context = Balance(context, preferLeft: false);
        }

        context = context with { Gap = context.Gap - 1, Position = context.Position - 1 };
        Validate(context, count);
        return context;
    }

    /// <summary>Advances to the next element, reporting whether there was one.</summary>
    internal static RopeZipperContext<T> MoveNext(in RopeZipperContext<T> source, int count)
    {
        if (source.Position == count)
            throw new InvalidOperationException("The cursor is already at the end of the rope.");

        var context = source;
        if (context.Gap == context.Active.Length)
        {
            context = PullFromRight(context, Math.Max(1, context.Configuration.FocusCapacity / 2));
            context = Balance(context, preferLeft: true);
        }

        context = context with { Gap = context.Gap + 1, Position = context.Position + 1 };
        Validate(context, count);
        return context;
    }

    /// <summary>Returns a cursor at the given position within the same rope version.</summary>
    internal static RopeZipperContext<T> Seek(
        CursorVersionState<T> version,
        in RopeZipperContext<T> source,
        int position)
    {
        if ((uint)position > (uint)version.Count)
            throw new ArgumentOutOfRangeException(nameof(position));
        if (position == source.Position)
            return source;

        RopeCursorDiagnostics.RecordNodeVisits();
        return CreateContext(version.Snapshot(), position, source.Configuration);
    }

    /// <summary>Returns a rope with the element inserted.</summary>
    internal static RopeCursorPrototypeEdit<T> Insert(
        CursorVersionState<T> version,
        in RopeZipperContext<T> source,
        T value)
    {
        if (version.Count == int.MaxValue)
            throw new OverflowException("The operation would create a rope with more than Int32.MaxValue elements.");

        var active = InsertAt(source.Active, source.Gap, value);
        RopeCursorDiagnostics.RecordFocusCopy(active.Length);
        var context = source with
        {
            Active = active,
            Gap = source.Gap + 1,
            Position = source.Position + 1,
        };
        context = Balance(context, preferLeft: true);
        return CreateEditedVersion(context, version.Count + 1);
    }

    /// <summary>
    /// Inserts a sequence's elements at the gap, producing a new version the returned cursor is positioned in.
    /// </summary>
    internal static RopeCursorPrototypeEdit<T> InsertRange(
        CursorVersionState<T> version,
        in RopeZipperContext<T> source,
        ReadOnlySpan<T> values)
    {
        if (values.IsEmpty)
            return new RopeCursorPrototypeEdit<T>(version, source);
        if (values.Length > int.MaxValue - version.Count)
            throw new OverflowException("The operation would create a rope with more than Int32.MaxValue elements.");

        var active = new T[checked(source.Active.Length + values.Length)];
        source.Active.AsSpan(0, source.Gap).CopyTo(active);
        values.CopyTo(active.AsSpan(source.Gap));
        source.Active.AsSpan(source.Gap).CopyTo(active.AsSpan(source.Gap + values.Length));
        RopeCursorDiagnostics.RecordFocusCopy(active.Length);
        var context = source with
        {
            Active = active,
            Gap = source.Gap + values.Length,
            Position = source.Position + values.Length,
        };
        context = Balance(context, preferLeft: true);
        return CreateEditedVersion(context, checked(version.Count + values.Length));
    }

    /// <summary>
    /// Removes the element before the gap, producing a new version the returned cursor is positioned in.
    /// </summary>
    internal static RopeCursorPrototypeEdit<T> DeletePrevious(
        CursorVersionState<T> version,
        in RopeZipperContext<T> source)
    {
        if (source.Position == 0)
            throw new InvalidOperationException("The cursor has no previous element to delete.");

        var context = source;
        if (context.Gap == 0)
            context = PullFromLeft(context, Math.Max(1, context.Configuration.FocusCapacity / 2));

        var active = RemoveAt(context.Active, context.Gap - 1);
        RopeCursorDiagnostics.RecordFocusCopy(active.Length);
        context = context with
        {
            Active = active,
            Gap = context.Gap - 1,
            Position = context.Position - 1,
        };
        context = Balance(context, preferLeft: false);
        context = Refill(context);
        return CreateEditedVersion(context, version.Count - 1);
    }

    /// <summary>
    /// Removes the element after the gap, producing a new version the returned cursor is positioned in.
    /// </summary>
    internal static RopeCursorPrototypeEdit<T> DeleteNext(
        CursorVersionState<T> version,
        in RopeZipperContext<T> source)
    {
        if (source.Position == version.Count)
            throw new InvalidOperationException("The cursor has no next element to delete.");

        var context = source;
        if (context.Gap == context.Active.Length)
            context = PullFromRight(context, Math.Max(1, context.Configuration.FocusCapacity / 2));

        var active = RemoveAt(context.Active, context.Gap);
        RopeCursorDiagnostics.RecordFocusCopy(active.Length);
        context = context with { Active = active };
        context = Balance(context, preferLeft: true);
        context = Refill(context);
        return CreateEditedVersion(context, version.Count - 1);
    }

    /// <summary>
    /// Replaces the element after the gap, producing a new version the returned cursor is positioned in.
    /// </summary>
    internal static RopeCursorPrototypeEdit<T> ReplaceNext(
        CursorVersionState<T> version,
        in RopeZipperContext<T> source,
        T value)
    {
        if (source.Position == version.Count)
            throw new InvalidOperationException("The cursor has no next element to replace.");

        var context = source;
        if (context.Gap == context.Active.Length)
        {
            context = PullFromRight(context, Math.Max(1, context.Configuration.FocusCapacity / 2));
            context = Balance(context, preferLeft: true);
        }

        var active = (T[])context.Active.Clone();
        active[context.Gap] = value;
        RopeCursorDiagnostics.RecordFocusCopy(active.Length);
        context = context with { Active = active };
        return CreateEditedVersion(context, version.Count);
    }

    /// <summary>Builds the snapshot.</summary>
    internal static Rope<T> BuildSnapshot(in RopeZipperContext<T> context, int count)
    {
        var middleLength = checked(context.LeftCarry.Length + context.Active.Length + context.RightCarry.Length);
        var middle = new T[middleLength];
        context.LeftCarry.CopyTo(middle, 0);
        context.Active.CopyTo(middle, context.LeftCarry.Length);
        context.RightCarry.CopyTo(middle, context.LeftCarry.Length + context.Active.Length);
        RopeCursorDiagnostics.RecordSnapshotNormalization();
        RopeCursorDiagnostics.RecordSpineAllocation(2);
        if (context.LeftCarry.Length > 0)
            RopeCursorDiagnostics.RecordCarryCopy(context.LeftCarry.Length);
        if (context.RightCarry.Length > 0)
            RopeCursorDiagnostics.RecordCarryCopy(context.RightCarry.Length);
        RopeCursorDiagnostics.RecordFocusCopy(context.Active.Length);

        var snapshot = Rope<T>.FromCursorPrototypeParts(context.LeftTree, middle, context.RightTree);
        if (snapshot.Count != count)
            throw new InvalidOperationException("The cursor snapshot does not match its logical version count.");
        return snapshot;
    }

    /// <summary>Returns the structural measurements a test asserts on.</summary>
    internal static RopeCursorPrototypeStateDiagnostics GetDiagnostics(
        CursorVersionState<T> version,
        in RopeZipperContext<T> context) =>
        new(
            Count: version.Count,
            Position: context.Position,
            ActiveLength: context.Active.Length,
            LeftCarryLength: context.LeftCarry.Length,
            RightCarryLength: context.RightCarry.Length,
            LeftOrdinaryChunkCount: context.LeftTree.Count(),
            RightOrdinaryChunkCount: context.RightTree.Count(),
            HasCachedSnapshot: version.HasCachedSnapshot,
            FocusCapacity: context.Configuration.FocusCapacity,
            FlushChunkSize: context.Configuration.FlushChunkSize,
            EstimatedRetainedBufferBytes: EstimateRetainedBufferBytes(
                version.SnapshotSeedForDiagnostics,
                context));

    /// <summary>Checks the rope's structural invariants. For tests and diagnostics.</summary>
    internal static void Validate(in RopeZipperContext<T> context, int count)
    {
        if ((uint)context.Gap > (uint)context.Active.Length)
            throw new InvalidOperationException("The cursor gap lies outside its active window.");
        if (context.Active.Length > context.Configuration.FocusCapacity)
            throw new InvalidOperationException("The cursor active window exceeds its configured capacity.");
        if (context.LeftCarry.Length >= context.Configuration.FlushChunkSize ||
            context.RightCarry.Length >= context.Configuration.FlushChunkSize)
        {
            throw new InvalidOperationException("A cursor carry reached its flush threshold.");
        }

        var computedPosition = checked(context.LeftTree.Measure + context.LeftCarry.Length + context.Gap);
        var computedCount = checked(
            context.LeftTree.Measure +
            context.LeftCarry.Length +
            context.Active.Length +
            context.RightCarry.Length +
            context.RightTree.Measure);
        if (computedPosition != context.Position || computedCount != count)
            throw new InvalidOperationException("The cursor decomposition does not match its logical position or count.");
    }

    private static RopeCursorPrototypeEdit<T> CreateEditedVersion(RopeZipperContext<T> context, int count)
    {
        Validate(context, count);
        var version = new CursorVersionState<T>(count, context, snapshot: null);
        return new RopeCursorPrototypeEdit<T>(version, context);
    }

    private static RopeZipperContext<T> Refill(RopeZipperContext<T> context)
    {
        var target = Math.Max(1, context.Configuration.FocusCapacity / 2);
        if (context.Active.Length > context.Configuration.FocusCapacity)
            context = Balance(context, context.Gap >= context.Active.Length / 2);
        if (context.Active.Length >= target)
            return context;

        var needed = target - context.Active.Length;
        var leftTarget = Math.Min(needed / 2, context.Position - context.Gap);
        if (leftTarget > 0)
            context = PullFromLeft(context, leftTarget);
        needed = target - context.Active.Length;
        if (needed > 0)
            context = PullFromRight(context, needed);
        needed = target - context.Active.Length;
        if (needed > 0)
            context = PullFromLeft(context, needed);
        return Balance(context, context.Gap >= context.Active.Length / 2);
    }

    private static RopeZipperContext<T> Balance(RopeZipperContext<T> context, bool preferLeft)
    {
        var overflow = context.Active.Length - context.Configuration.FocusCapacity;
        if (overflow <= 0)
            return context;

        if (preferLeft && overflow <= context.Gap)
            return SpillToLeft(context, overflow);
        if (!preferLeft && overflow <= context.Active.Length - context.Gap)
            return SpillToRight(context, overflow);
        return overflow <= context.Gap
            ? SpillToLeft(context, overflow)
            : SpillToRight(context, overflow);
    }

    private static RopeZipperContext<T> SpillToLeft(RopeZipperContext<T> context, int count)
    {
        var moved = context.Active.AsSpan(0, count);
        var combined = new T[checked(context.LeftCarry.Length + count)];
        context.LeftCarry.CopyTo(combined, 0);
        moved.CopyTo(combined.AsSpan(context.LeftCarry.Length));
        RopeCursorDiagnostics.RecordCarryCopy(combined.Length);

        var leftTree = context.LeftTree;
        var flushSize = context.Configuration.FlushChunkSize;
        var flushLength = combined.Length - (combined.Length % flushSize);
        for (var offset = 0; offset < flushLength; offset += flushSize)
        {
            var chunk = combined.AsSpan(offset, flushSize).ToArray();
            leftTree = leftTree.Append(new Chunk<T>(chunk));
            RopeCursorDiagnostics.RecordSpineAllocation();
        }

        var carry = combined.AsSpan(flushLength).ToArray();
        var active = context.Active.AsSpan(count).ToArray();
        RopeCursorDiagnostics.RecordFocusCopy(active.Length);
        return context with
        {
            LeftTree = leftTree,
            LeftCarry = carry,
            Active = active,
            Gap = context.Gap - count,
        };
    }

    private static RopeZipperContext<T> SpillToRight(RopeZipperContext<T> context, int count)
    {
        var activeLength = context.Active.Length - count;
        var combined = new T[checked(count + context.RightCarry.Length)];
        context.Active.AsSpan(activeLength, count).CopyTo(combined);
        context.RightCarry.CopyTo(combined, count);
        RopeCursorDiagnostics.RecordCarryCopy(combined.Length);

        var flushSize = context.Configuration.FlushChunkSize;
        var carryLength = combined.Length % flushSize;
        var carry = combined.AsSpan(0, carryLength).ToArray();
        var flushed = FingerTree<Chunk<T>, int, ChunkLengthMeasure<T>>.Empty;
        for (var offset = carryLength; offset < combined.Length; offset += flushSize)
        {
            var chunk = combined.AsSpan(offset, flushSize).ToArray();
            flushed = flushed.Append(new Chunk<T>(chunk));
            RopeCursorDiagnostics.RecordSpineAllocation();
        }

        var active = context.Active.AsSpan(0, activeLength).ToArray();
        RopeCursorDiagnostics.RecordFocusCopy(active.Length);
        return context with
        {
            Active = active,
            RightCarry = carry,
            RightTree = flushed.Concat(context.RightTree),
        };
    }

    private static RopeZipperContext<T> PullFromLeft(RopeZipperContext<T> context, int maximum)
    {
        var pulled = new List<T>(maximum);
        var carry = context.LeftCarry;
        var tree = context.LeftTree;
        while (pulled.Count < maximum && (carry.Length > 0 || !tree.IsEmpty))
        {
            if (carry.Length > 0)
            {
                var take = Math.Min(maximum - pulled.Count, carry.Length);
                pulled.InsertRange(0, carry.AsSpan(carry.Length - take, take).ToArray());
                carry = carry.AsSpan(0, carry.Length - take).ToArray();
                continue;
            }

            RopeCursorDiagnostics.RecordNodeVisits();
            tree.TryViewRight(out var chunk, out var rest);
            var chunkTake = Math.Min(maximum - pulled.Count, chunk.Length);
            pulled.InsertRange(0, chunk.Data.Span[(chunk.Length - chunkTake)..].ToArray());
            tree = chunkTake == chunk.Length
                ? rest
                : rest.Append(chunk.Slice(0, chunk.Length - chunkTake));
            RopeCursorDiagnostics.RecordSpineAllocation();
        }

        if (pulled.Count == 0)
            return context;

        var active = new T[pulled.Count + context.Active.Length];
        pulled.CopyTo(active, 0);
        context.Active.CopyTo(active, pulled.Count);
        RopeCursorDiagnostics.RecordFocusCopy(active.Length);
        return context with
        {
            LeftTree = tree,
            LeftCarry = carry,
            Active = active,
            Gap = context.Gap + pulled.Count,
        };
    }

    private static RopeZipperContext<T> PullFromRight(RopeZipperContext<T> context, int maximum)
    {
        var pulled = new List<T>(maximum);
        var carry = context.RightCarry;
        var tree = context.RightTree;
        while (pulled.Count < maximum && (carry.Length > 0 || !tree.IsEmpty))
        {
            if (carry.Length > 0)
            {
                var take = Math.Min(maximum - pulled.Count, carry.Length);
                pulled.AddRange(carry.AsSpan(0, take).ToArray());
                carry = carry.AsSpan(take).ToArray();
                continue;
            }

            RopeCursorDiagnostics.RecordNodeVisits();
            tree.TryViewLeft(out var chunk, out var rest);
            var chunkTake = Math.Min(maximum - pulled.Count, chunk.Length);
            pulled.AddRange(chunk.Data.Span[..chunkTake].ToArray());
            tree = chunkTake == chunk.Length
                ? rest
                : rest.Prepend(chunk.Slice(chunkTake, chunk.Length - chunkTake));
            RopeCursorDiagnostics.RecordSpineAllocation();
        }

        if (pulled.Count == 0)
            return context;

        var active = new T[context.Active.Length + pulled.Count];
        context.Active.CopyTo(active, 0);
        pulled.CopyTo(active, context.Active.Length);
        RopeCursorDiagnostics.RecordFocusCopy(active.Length);
        return context with
        {
            Active = active,
            RightCarry = carry,
            RightTree = tree,
        };
    }

    private static T[] InsertAt(T[] source, int index, T value)
    {
        var result = new T[source.Length + 1];
        source.AsSpan(0, index).CopyTo(result);
        result[index] = value;
        source.AsSpan(index).CopyTo(result.AsSpan(index + 1));
        return result;
    }

    private static T[] RemoveAt(T[] source, int index)
    {
        var result = new T[source.Length - 1];
        source.AsSpan(0, index).CopyTo(result);
        source.AsSpan(index + 1).CopyTo(result.AsSpan(index));
        return result;
    }

    private static long EstimateArrayBytes(int length)
    {
        if (length == 0)
            return 0;
        var bytes = checked((3L * IntPtr.Size) + ((long)length * Unsafe.SizeOf<T>()));
        var alignment = IntPtr.Size;
        return checked((bytes + alignment - 1) & ~(alignment - 1));
    }

    private static long EstimateRetainedBufferBytes(
        in RopeZipperContext<T> seed,
        in RopeZipperContext<T> current)
    {
        var arrays = new HashSet<T[]>(ReferenceEqualityComparer.Instance);
        long bytes = 0;
        Add(seed.LeftCarry);
        Add(seed.Active);
        Add(seed.RightCarry);
        Add(current.LeftCarry);
        Add(current.Active);
        Add(current.RightCarry);
        return bytes;

        void Add(T[] array)
        {
            if (array.Length > 0 && arrays.Add(array))
                bytes = checked(bytes + EstimateArrayBytes(array.Length));
        }
    }
}

/// <summary>
/// The reference-shaped prototype cursor: immutable, each movement producing a new cursor.
/// </summary>
internal sealed class RopeCursorPrototype<T>
{
    private readonly CursorVersionState<T> _version;
    private readonly RopeZipperContext<T> _context;

    private RopeCursorPrototype(CursorVersionState<T> version, RopeZipperContext<T> context)
    {
        _version = version;
        _context = context;
        RopeCursorDiagnostics.RecordWrapperAllocation();
    }

    /// <summary>Gets the number of elements in the rope.</summary>
    internal int Count => _version.Count;

    /// <summary>Gets the cursor's gap position.</summary>
    internal int Position => _context.Position;

    /// <summary>Gets a value indicating whether the gap precedes the first element.</summary>
    internal bool IsAtStart => Position == 0;

    /// <summary>Gets a value indicating whether the gap follows the last element.</summary>
    internal bool IsAtEnd => Position == Count;

    /// <summary>Gets the version identity.</summary>
    internal object VersionIdentity => _version;

    /// <summary>Creates an element rope using the supplied policies, which it retains.</summary>
    internal static RopeCursorPrototype<T> Create(
        Rope<T> source,
        int position,
        int focusCapacity,
        int flushChunkSize)
    {
        var configuration = RopeCursorPrototypeConfiguration.Create(focusCapacity, flushChunkSize);
        var created = RopeCursorPrototypeEngine<T>.Create(source, position, configuration);
        return new RopeCursorPrototype<T>(created.Version, created.Context);
    }

    /// <summary>Reads the element immediately before the gap, reporting whether one exists.</summary>
    internal bool TryPeekPrevious([MaybeNullWhen(false)] out T value) =>
        RopeCursorPrototypeEngine<T>.TryPeekPrevious(_context, out value);

    /// <summary>Reads the element immediately after the gap, reporting whether one exists.</summary>
    internal bool TryPeekNext([MaybeNullWhen(false)] out T value) =>
        RopeCursorPrototypeEngine<T>.TryPeekNext(_context, Count, out value);

    /// <summary>
    /// Returns a cursor one position earlier. The receiver is unchanged; movement produces a new cursor over the same
    /// version.
    /// </summary>
    internal RopeCursorPrototype<T> MovePrevious() =>
        new(_version, RopeCursorPrototypeEngine<T>.MovePrevious(_context, Count));

    /// <summary>Advances to the next element, reporting whether there was one.</summary>
    internal RopeCursorPrototype<T> MoveNext() =>
        new(_version, RopeCursorPrototypeEngine<T>.MoveNext(_context, Count));

    /// <summary>Returns a cursor at the given position within the same rope version.</summary>
    internal RopeCursorPrototype<T> Seek(int position) =>
        position == Position
            ? this
            : new RopeCursorPrototype<T>(_version, RopeCursorPrototypeEngine<T>.Seek(_version, _context, position));

    /// <summary>Returns a rope with the element inserted.</summary>
    internal RopeCursorPrototype<T> Insert(T value) => FromEdit(
        RopeCursorPrototypeEngine<T>.Insert(_version, _context, value));

    /// <summary>
    /// Inserts a sequence's elements at the gap, producing a new version the returned cursor is positioned in.
    /// </summary>
    internal RopeCursorPrototype<T> InsertRange(ReadOnlySpan<T> values) => values.IsEmpty
        ? this
        : FromEdit(RopeCursorPrototypeEngine<T>.InsertRange(_version, _context, values));

    /// <summary>
    /// Removes the element before the gap, producing a new version the returned cursor is positioned in.
    /// </summary>
    internal RopeCursorPrototype<T> DeletePrevious() => FromEdit(
        RopeCursorPrototypeEngine<T>.DeletePrevious(_version, _context));

    /// <summary>
    /// Removes the element after the gap, producing a new version the returned cursor is positioned in.
    /// </summary>
    internal RopeCursorPrototype<T> DeleteNext() => FromEdit(
        RopeCursorPrototypeEngine<T>.DeleteNext(_version, _context));

    /// <summary>
    /// Replaces the element after the gap, producing a new version the returned cursor is positioned in.
    /// </summary>
    internal RopeCursorPrototype<T> ReplaceNext(T value) => FromEdit(
        RopeCursorPrototypeEngine<T>.ReplaceNext(_version, _context, value));

    /// <summary>Gets the rope version this cursor is positioned in.</summary>
    internal Rope<T> Snapshot() => _version.Snapshot();

    /// <summary>Returns the structural measurements a test asserts on.</summary>
    internal RopeCursorPrototypeStateDiagnostics GetDiagnostics() =>
        RopeCursorPrototypeEngine<T>.GetDiagnostics(_version, _context);

    /// <summary>Checks the rope's structural invariants. For tests and diagnostics.</summary>
    internal void Validate() => RopeCursorPrototypeEngine<T>.Validate(_context, Count);

    private static RopeCursorPrototype<T> FromEdit(RopeCursorPrototypeEdit<T> edit) =>
        new(edit.Version, edit.Context);
}

/// <summary>
/// The value-typed prototype cursor, kept for comparison against the reference-shaped one.
/// </summary>
internal readonly struct RopeCursorStructPrototype<T>
{
    private readonly CursorVersionState<T>? _version;
    private readonly RopeZipperContext<T> _context;

    private RopeCursorStructPrototype(CursorVersionState<T> version, RopeZipperContext<T> context)
    {
        _version = version;
        _context = context;
    }

    /// <summary>Gets the number of elements in the rope.</summary>
    internal int Count => Version.Count;

    /// <summary>Gets the cursor's gap position.</summary>
    internal int Position => _context.Position;

    /// <summary>Gets a value indicating whether the gap precedes the first element.</summary>
    internal bool IsAtStart => Position == 0;

    /// <summary>Gets a value indicating whether the gap follows the last element.</summary>
    internal bool IsAtEnd => Position == Count;

    /// <summary>Creates an element rope using the supplied policies, which it retains.</summary>
    internal static RopeCursorStructPrototype<T> Create(
        Rope<T> source,
        int position,
        int focusCapacity,
        int flushChunkSize)
    {
        var configuration = RopeCursorPrototypeConfiguration.Create(focusCapacity, flushChunkSize);
        var created = RopeCursorPrototypeEngine<T>.Create(source, position, configuration);
        return new RopeCursorStructPrototype<T>(created.Version, created.Context);
    }

    /// <summary>
    /// Returns a cursor one position earlier. The receiver is unchanged; movement produces a new cursor over the same
    /// version.
    /// </summary>
    internal RopeCursorStructPrototype<T> MovePrevious() =>
        new(Version, RopeCursorPrototypeEngine<T>.MovePrevious(_context, Count));

    /// <summary>Advances to the next element, reporting whether there was one.</summary>
    internal RopeCursorStructPrototype<T> MoveNext() =>
        new(Version, RopeCursorPrototypeEngine<T>.MoveNext(_context, Count));

    /// <summary>Reads the element immediately before the gap, reporting whether one exists.</summary>
    internal bool TryPeekPrevious([MaybeNullWhen(false)] out T value) =>
        RopeCursorPrototypeEngine<T>.TryPeekPrevious(_context, out value);

    /// <summary>Reads the element immediately after the gap, reporting whether one exists.</summary>
    internal bool TryPeekNext([MaybeNullWhen(false)] out T value) =>
        RopeCursorPrototypeEngine<T>.TryPeekNext(_context, Count, out value);

    /// <summary>Returns a cursor at the given position within the same rope version.</summary>
    internal RopeCursorStructPrototype<T> Seek(int position) =>
        position == Position
            ? this
            : new RopeCursorStructPrototype<T>(
                Version,
                RopeCursorPrototypeEngine<T>.Seek(Version, _context, position));

    /// <summary>Returns a rope with the element inserted.</summary>
    internal RopeCursorStructPrototype<T> Insert(T value)
    {
        var edit = RopeCursorPrototypeEngine<T>.Insert(Version, _context, value);
        return new RopeCursorStructPrototype<T>(edit.Version, edit.Context);
    }

    /// <summary>
    /// Inserts a sequence's elements at the gap, producing a new version the returned cursor is positioned in.
    /// </summary>
    internal RopeCursorStructPrototype<T> InsertRange(ReadOnlySpan<T> values)
    {
        if (values.IsEmpty)
            return this;
        var edit = RopeCursorPrototypeEngine<T>.InsertRange(Version, _context, values);
        return new RopeCursorStructPrototype<T>(edit.Version, edit.Context);
    }

    /// <summary>
    /// Removes the element before the gap, producing a new version the returned cursor is positioned in.
    /// </summary>
    internal RopeCursorStructPrototype<T> DeletePrevious()
    {
        var edit = RopeCursorPrototypeEngine<T>.DeletePrevious(Version, _context);
        return new RopeCursorStructPrototype<T>(edit.Version, edit.Context);
    }

    /// <summary>
    /// Removes the element after the gap, producing a new version the returned cursor is positioned in.
    /// </summary>
    internal RopeCursorStructPrototype<T> DeleteNext()
    {
        var edit = RopeCursorPrototypeEngine<T>.DeleteNext(Version, _context);
        return new RopeCursorStructPrototype<T>(edit.Version, edit.Context);
    }

    /// <summary>
    /// Replaces the element after the gap, producing a new version the returned cursor is positioned in.
    /// </summary>
    internal RopeCursorStructPrototype<T> ReplaceNext(T value)
    {
        var edit = RopeCursorPrototypeEngine<T>.ReplaceNext(Version, _context, value);
        return new RopeCursorStructPrototype<T>(edit.Version, edit.Context);
    }

    /// <summary>Gets the rope version this cursor is positioned in.</summary>
    internal Rope<T> Snapshot() => Version.Snapshot();

    /// <summary>Returns the structural measurements a test asserts on.</summary>
    internal RopeCursorPrototypeStateDiagnostics GetDiagnostics() =>
        RopeCursorPrototypeEngine<T>.GetDiagnostics(Version, _context);

    private CursorVersionState<T> Version =>
        _version ?? throw new InvalidOperationException("The default cursor prototype is not initialized.");
}

/// <summary>
/// The mutable prototype cursor, kept for comparison. Deliberately not a snapshot: movement mutates
/// it in place.
/// </summary>
internal sealed class RopeMutableCursorPrototype<T>
{
    private CursorVersionState<T> _version;
    private RopeZipperContext<T> _context;

    private RopeMutableCursorPrototype(CursorVersionState<T> version, RopeZipperContext<T> context)
    {
        _version = version;
        _context = context;
    }

    /// <summary>Gets the number of elements in the rope.</summary>
    internal int Count => _version.Count;

    /// <summary>Gets the cursor's gap position.</summary>
    internal int Position => _context.Position;

    /// <summary>Gets a value indicating whether the gap precedes the first element.</summary>
    internal bool IsAtStart => Position == 0;

    /// <summary>Gets a value indicating whether the gap follows the last element.</summary>
    internal bool IsAtEnd => Position == Count;

    /// <summary>Creates an element rope using the supplied policies, which it retains.</summary>
    internal static RopeMutableCursorPrototype<T> Create(
        Rope<T> source,
        int position,
        int focusCapacity,
        int flushChunkSize)
    {
        var configuration = RopeCursorPrototypeConfiguration.Create(focusCapacity, flushChunkSize);
        var created = RopeCursorPrototypeEngine<T>.Create(source, position, configuration);
        return new RopeMutableCursorPrototype<T>(created.Version, created.Context);
    }

    /// <summary>
    /// Returns a cursor one position earlier. The receiver is unchanged; movement produces a new cursor over the same
    /// version.
    /// </summary>
    internal void MovePrevious() => _context = RopeCursorPrototypeEngine<T>.MovePrevious(_context, Count);

    /// <summary>Advances to the next element, reporting whether there was one.</summary>
    internal void MoveNext() => _context = RopeCursorPrototypeEngine<T>.MoveNext(_context, Count);

    /// <summary>Reads the element immediately before the gap, reporting whether one exists.</summary>
    internal bool TryPeekPrevious([MaybeNullWhen(false)] out T value) =>
        RopeCursorPrototypeEngine<T>.TryPeekPrevious(_context, out value);

    /// <summary>Reads the element immediately after the gap, reporting whether one exists.</summary>
    internal bool TryPeekNext([MaybeNullWhen(false)] out T value) =>
        RopeCursorPrototypeEngine<T>.TryPeekNext(_context, Count, out value);

    /// <summary>Returns a cursor at the given position within the same rope version.</summary>
    internal void Seek(int position)
    {
        if (position != Position)
            _context = RopeCursorPrototypeEngine<T>.Seek(_version, _context, position);
    }

    /// <summary>Returns a rope with the element inserted.</summary>
    internal void Insert(T value) => Apply(RopeCursorPrototypeEngine<T>.Insert(_version, _context, value));

    /// <summary>
    /// Inserts a sequence's elements at the gap, producing a new version the returned cursor is positioned in.
    /// </summary>
    internal void InsertRange(ReadOnlySpan<T> values)
    {
        if (!values.IsEmpty)
            Apply(RopeCursorPrototypeEngine<T>.InsertRange(_version, _context, values));
    }

    /// <summary>
    /// Removes the element before the gap, producing a new version the returned cursor is positioned in.
    /// </summary>
    internal void DeletePrevious() => Apply(RopeCursorPrototypeEngine<T>.DeletePrevious(_version, _context));

    /// <summary>
    /// Removes the element after the gap, producing a new version the returned cursor is positioned in.
    /// </summary>
    internal void DeleteNext() => Apply(RopeCursorPrototypeEngine<T>.DeleteNext(_version, _context));

    /// <summary>
    /// Replaces the element after the gap, producing a new version the returned cursor is positioned in.
    /// </summary>
    internal void ReplaceNext(T value) => Apply(RopeCursorPrototypeEngine<T>.ReplaceNext(_version, _context, value));

    /// <summary>Gets the rope version this cursor is positioned in.</summary>
    internal Rope<T> Snapshot() => _version.Snapshot();

    /// <summary>Returns the structural measurements a test asserts on.</summary>
    internal RopeCursorPrototypeStateDiagnostics GetDiagnostics() =>
        RopeCursorPrototypeEngine<T>.GetDiagnostics(_version, _context);

    private void Apply(RopeCursorPrototypeEdit<T> edit)
    {
        _version = edit.Version;
        _context = edit.Context;
    }
}

/// <summary>What a prototype cursor's cached state looked like at one point.</summary>
internal readonly record struct RopeCursorPrototypeStateDiagnostics(
    int Count,
    int Position,
    int ActiveLength,
    int LeftCarryLength,
    int RightCarryLength,
    int LeftOrdinaryChunkCount,
    int RightOrdinaryChunkCount,
    bool HasCachedSnapshot,
    int FocusCapacity,
    int FlushChunkSize,
    long EstimatedRetainedBufferBytes);
