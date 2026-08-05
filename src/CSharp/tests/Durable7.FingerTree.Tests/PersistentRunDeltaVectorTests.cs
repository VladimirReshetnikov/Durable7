using System.Diagnostics.CodeAnalysis;
using System.Runtime.CompilerServices;
using Xunit;

namespace Durable7.FingerTree.Tests;

/// <summary>Semantic, retained-version, hunk, complexity, failure, and concurrency tests.</summary>
public sealed class PersistentRunDeltaVectorTests
{
    /// <summary>Checks empty/factory contracts, custom equality, canonical cancellation, and boundaries.</summary>
    [Fact]
    public void EmptyFactoriesAndCustomEquality_HaveExactContracts()
    {
        var empty = PersistentRunDeltaVector<int>.Empty;
        Assert.True(empty.IsEmpty);
        Assert.False(empty.HasChanges);
        Assert.Equal(0, empty.DirtyCount);
        Assert.Equal(0, empty.DirtyRunCount);
        Assert.Empty(empty);
        Assert.Empty(empty.EnumerateDirtyRuns());
        Assert.Same(empty, empty.Checkpoint());
        Assert.Same(empty, empty.Rollback());
        Assert.Throws<ArgumentOutOfRangeException>(() => _ = empty[0]);
        Assert.Throws<ArgumentOutOfRangeException>(() => empty.GetCheckpointValue(0));
        Assert.Throws<ArgumentOutOfRangeException>(() => empty.IsDirty(0));
        Assert.Throws<ArgumentOutOfRangeException>(() => empty.TryGetDirtyRunContaining(0, out _));
        Assert.Throws<ArgumentOutOfRangeException>(() => empty.GetDirtyRunAt(0));
        Assert.Throws<ArgumentOutOfRangeException>(() => empty.AcceptDirtyRunAt(0));
        Assert.Throws<ArgumentOutOfRangeException>(() => empty.RevertDirtyRunAt(0));
        Assert.Throws<ArgumentOutOfRangeException>(() => empty.SetItem(0, 1));
        Assert.Throws<ArgumentOutOfRangeException>(() => empty.ResetItem(0));
        Assert.Throws<ArgumentNullException>(() => PersistentRunDeltaVector<int>.CreateRange(null!));

        var source = PersistentRunDeltaVector<string>.CreateRange(
            ["Alpha", "Beta"],
            StringComparer.OrdinalIgnoreCase);
        Assert.Same(StringComparer.OrdinalIgnoreCase, source.ValueComparer);
        Assert.Same(source, source.SetItem(0, "ALPHA"));
        var dirty = source.SetItem(0, "Gamma");
        Assert.Equal("Gamma", dirty[0]);
        Assert.Equal("Alpha", dirty.GetCheckpointValue(0));
        Assert.Equal([new PersistentDirtyRun(0, 1)], dirty.EnumerateDirtyRuns());
        var cancelled = dirty.SetItem(0, "alpha");
        Assert.False(cancelled.HasChanges);
        Assert.Equal("Alpha", cancelled[0]);
        Assert.Same(cancelled, cancelled.Checkpoint());
        cancelled.ValidateInvariants();

        var checkpointRepresentative = new string(['x']);
        var replacementRepresentative = new string(['x']);
        Assert.Equal(checkpointRepresentative, replacementRepresentative);
        Assert.NotSame(checkpointRepresentative, replacementRepresentative);
        var identitySource = PersistentRunDeltaVector<string>.CreateRange(
            [checkpointRepresentative],
            new ReferenceComparer<string>());
        var identityDirty = identitySource.SetItem(0, replacementRepresentative);
        Assert.True(identityDirty.IsDirty(0));
        Assert.Same(replacementRepresentative, identityDirty[0]);
        Assert.Same(checkpointRepresentative, identityDirty.GetCheckpointValue(0));
        var identityReset = identityDirty.ResetItem(0);
        Assert.False(identityReset.HasChanges);
        Assert.Same(checkpointRepresentative, identityReset[0]);
        identityReset.ValidateInvariants();
    }

    /// <summary>Exercises every local interval merge, split, shrink, insertion, and cancellation case.</summary>
    [Fact]
    public void PointEdits_MaintainExactMaximalDirtyRuns()
    {
        var vector = PersistentRunDeltaVector<int>.CreateRange(new int[12]);
        vector = vector.SetItem(2, 20).SetItem(4, 40);
        AssertRuns(vector, new PersistentDirtyRun(2, 1), new PersistentDirtyRun(4, 1));
        Assert.Throws<ArgumentOutOfRangeException>(() => vector.GetDirtyRunAt(-1));
        Assert.Throws<ArgumentOutOfRangeException>(() => vector.GetDirtyRunAt(vector.DirtyRunCount));

        vector = vector.SetItem(3, 30);
        AssertRuns(vector, new PersistentDirtyRun(2, 3));
        vector = vector.SetItem(6, 60).SetItem(5, 50);
        AssertRuns(vector, new PersistentDirtyRun(2, 5));

        vector = vector.ResetItem(4);
        AssertRuns(vector, new PersistentDirtyRun(2, 2), new PersistentDirtyRun(5, 2));
        vector = vector.ResetItem(2);
        AssertRuns(vector, new PersistentDirtyRun(3, 1), new PersistentDirtyRun(5, 2));
        vector = vector.ResetItem(6);
        AssertRuns(vector, new PersistentDirtyRun(3, 1), new PersistentDirtyRun(5, 1));
        vector = vector.ResetItem(3).ResetItem(5);
        Assert.False(vector.HasChanges);
        Assert.Empty(vector.EnumerateDirtyRuns());
        Assert.Equal(new int[12], vector);
        vector.ValidateInvariants();
    }

    /// <summary>Accepts and reverts selected hunks while retaining all source versions.</summary>
    [Fact]
    public void AcceptAndRevertDirtyRuns_ChangeOnlySelectedHunks()
    {
        var comparer = new CountingComparer<int>();
        var original = PersistentRunDeltaVector<int>.CreateRange(Enumerable.Range(0, 20), comparer);
        var edited = original;
        foreach (var index in Enumerable.Range(2, 4).Concat(Enumerable.Range(9, 5)).Append(17))
            edited = edited.SetItem(index, 1000 + index);
        AssertRuns(
            edited,
            new PersistentDirtyRun(2, 4),
            new PersistentDirtyRun(9, 5),
            new PersistentDirtyRun(17, 1));

        var callsBeforeSplice = comparer.Calls;
        var acceptedMiddle = edited.AcceptDirtyRunAt(1);
        Assert.Equal(callsBeforeSplice, comparer.Calls);
        AssertRuns(
            acceptedMiddle,
            new PersistentDirtyRun(2, 4),
            new PersistentDirtyRun(17, 1));
        for (var index = 9; index < 14; index++)
        {
            Assert.Equal(1000 + index, acceptedMiddle[index]);
            Assert.Equal(1000 + index, acceptedMiddle.GetCheckpointValue(index));
        }

        callsBeforeSplice = comparer.Calls;
        var revertedFirst = acceptedMiddle.RevertDirtyRunAt(0);
        Assert.Equal(callsBeforeSplice, comparer.Calls);
        AssertRuns(revertedFirst, new PersistentDirtyRun(17, 1));
        for (var index = 2; index < 6; index++)
            Assert.Equal(index, revertedFirst[index]);
        callsBeforeSplice = comparer.Calls;
        var clean = revertedFirst.AcceptDirtyRunAt(0);
        Assert.Equal(callsBeforeSplice, comparer.Calls);
        Assert.False(clean.HasChanges);
        Assert.Equal(1017, clean.GetCheckpointValue(17));

        AssertRuns(
            edited,
            new PersistentDirtyRun(2, 4),
            new PersistentDirtyRun(9, 5),
            new PersistentDirtyRun(17, 1));
        Assert.False(original.HasChanges);
        Assert.Equal(Enumerable.Range(0, 20), original);
        acceptedMiddle.ValidateInvariants();
        revertedFirst.ValidateInvariants();
        clean.ValidateInvariants();
    }

    /// <summary>Checks thousands of arbitrary retained branches against an independent array/run model.</summary>
    [Fact]
    public void RandomizedRetainedVersions_MatchIndependentModel()
    {
        const int length = 48;
        var random = new Random(unchecked((int)0xD17E_2026u));
        var initial = Enumerable.Range(0, length).Select(index => index % 7).ToArray();
        var versions = new List<ModelVersion>
        {
            new(PersistentRunDeltaVector<int>.CreateRange(initial), initial, initial)
        };

        for (var operation = 0; operation < 5_000; operation++)
        {
            var parent = versions[random.Next(versions.Count)];
            var current = (int[])parent.Current.Clone();
            var checkpoint = (int[])parent.Checkpoint.Clone();
            var result = parent.Value;

            switch (random.Next(6))
            {
                case 0:
                case 1:
                    {
                        var index = random.Next(length);
                        var value = random.Next(-8, 9);
                        result = result.SetItem(index, value);
                        current[index] = value;
                        break;
                    }
                case 2:
                    {
                        var index = random.Next(length);
                        result = result.ResetItem(index);
                        current[index] = checkpoint[index];
                        break;
                    }
                case 3:
                    result = result.Checkpoint();
                    checkpoint = (int[])current.Clone();
                    break;
                case 4:
                    result = result.Rollback();
                    current = (int[])checkpoint.Clone();
                    break;
                default:
                    {
                        var runs = ExpectedRuns(current, checkpoint);
                        if (runs.Count == 0)
                            break;
                        var rank = random.Next(runs.Count);
                        var run = runs[rank];
                        if (random.Next(2) == 0)
                        {
                            result = result.AcceptDirtyRunAt(rank);
                            Array.Copy(current, run.Start, checkpoint, run.Start, run.Length);
                        }
                        else
                        {
                            result = result.RevertDirtyRunAt(rank);
                            Array.Copy(checkpoint, run.Start, current, run.Start, run.Length);
                        }
                        break;
                    }
            }

            AssertMatchesModel(result, current, checkpoint);
            AssertMatchesModel(parent.Value, parent.Current, parent.Checkpoint);
            versions.Add(new(result, current, checkpoint));
            if (versions.Count > 512)
                versions.RemoveAt(random.Next(1, versions.Count - 1));
        }
    }

    /// <summary>Exposes the k-versus-r gap with one large clustered delta and one isolated position.</summary>
    [Fact]
    public void ClusteredDelta_UsesTwoDescriptorsForThousandsOfDirtyPositions()
    {
        const int length = 8_192;
        var source = PersistentRunDeltaVector<int>.CreateRange(new int[length]);
        var edited = source;
        for (var index = 0; index < length - 2; index++)
            edited = edited.SetItem(index, 1);
        edited = edited.SetItem(length - 1, 1);

        Assert.Equal(length - 1, edited.DirtyCount);
        AssertRuns(
            edited,
            new PersistentDirtyRun(0, length - 2),
            new PersistentDirtyRun(length - 1, 1));
        var accepted = edited.AcceptDirtyRunAt(0);
        AssertRuns(accepted, new PersistentDirtyRun(length - 1, 1));
        Assert.Equal(1, accepted.GetCheckpointValue(0));
        Assert.Equal(0, accepted.GetCheckpointValue(length - 1));

        Assert.Equal(new int[length], source);
        edited.ValidateInvariants();
        accepted.ValidateInvariants();
    }

    /// <summary>Checks comparer failure atomicity and exact restoration of retained representatives.</summary>
    [Fact]
    public void ComparerFailure_PublishesNoPartialVersion()
    {
        var comparer = new ThrowOnceComparer<int>();
        var source = PersistentRunDeltaVector<int>.CreateRange([1, 2, 3], comparer);
        comparer.ThrowOnCall(2);

        Assert.Throws<ComparerFailureException>(() => source.SetItem(1, 99));
        Assert.Equal([1, 2, 3], source);
        Assert.False(source.HasChanges);
        source.ValidateInvariants();

        var edited = source.SetItem(1, 99);
        Assert.Equal([1, 99, 3], edited);
        Assert.Equal([new PersistentDirtyRun(1, 1)], edited.EnumerateDirtyRuns());
        Assert.Equal([1, 2, 3], source);
    }

    /// <summary>Exercises concurrent readers while workers derive and validate private edit branches.</summary>
    [Fact]
    public void ConcurrentReadersAndIndependentWriters_ObserveStableSnapshots()
    {
        var initial = Enumerable.Range(0, 2_048).Select(index => index % 17).ToArray();
        var source = PersistentRunDeltaVector<int>.CreateRange(initial);
        for (var index = 100; index < 300; index++)
            source = source.SetItem(index, -index);
        var expectedCurrent = source.ToArray();
        var expectedCheckpoint = initial;

        Parallel.For(0, 8, worker =>
        {
            var branch = source;
            var current = (int[])expectedCurrent.Clone();
            var checkpoint = (int[])expectedCheckpoint.Clone();
            for (var iteration = 0; iteration < 400; iteration++)
            {
                var index = (worker * 97 + iteration * 31) % current.Length;
                Assert.Equal(expectedCurrent[index], source[index]);
                branch = branch.SetItem(index, worker * 10_000 + iteration);
                current[index] = worker * 10_000 + iteration;
                if ((iteration & 63) == 63)
                {
                    branch = branch.Checkpoint();
                    checkpoint = (int[])current.Clone();
                }
            }
            AssertMatchesModel(branch, current, checkpoint);
        });

        AssertMatchesModel(source, expectedCurrent, expectedCheckpoint);
    }

    private static void AssertMatchesModel(
        PersistentRunDeltaVector<int> vector,
        IReadOnlyList<int> current,
        IReadOnlyList<int> checkpoint)
    {
        Assert.Equal(current.Count, vector.Count);
        Assert.Equal(current, vector.ToArray());
        var runs = ExpectedRuns(current, checkpoint);
        for (var index = 0; index < current.Count; index++)
        {
            Assert.Equal(current[index], vector[index]);
            Assert.Equal(checkpoint[index], vector.GetCheckpointValue(index));
            var expectedRun = runs.SingleOrDefault(run => index >= run.Start && index < run.EndExclusive);
            var expectedDirty = expectedRun.Length != 0;
            Assert.Equal(expectedDirty, vector.IsDirty(index));
            Assert.Equal(expectedDirty, vector.TryGetDirtyRunContaining(index, out var actualRun));
            if (expectedDirty)
                Assert.Equal(expectedRun, actualRun);
        }

        Assert.Equal(runs.Sum(run => run.Length), vector.DirtyCount);
        Assert.Equal(runs.Count, vector.DirtyRunCount);
        Assert.Equal(runs, vector.EnumerateDirtyRuns());
        for (var rank = 0; rank < runs.Count; rank++)
            Assert.Equal(runs[rank], vector.GetDirtyRunAt(rank));
        vector.ValidateInvariants();
    }

    private static void AssertRuns(
        PersistentRunDeltaVector<int> vector,
        params PersistentDirtyRun[] expected)
    {
        Assert.Equal(expected.Sum(run => run.Length), vector.DirtyCount);
        Assert.Equal(expected.Length, vector.DirtyRunCount);
        Assert.Equal(expected, vector.EnumerateDirtyRuns());
        foreach (var run in expected)
        {
            for (var index = run.Start; index < run.EndExclusive; index++)
            {
                Assert.True(vector.IsDirty(index));
                Assert.True(vector.TryGetDirtyRunContaining(index, out var containing));
                Assert.Equal(run, containing);
            }
        }
        vector.ValidateInvariants();
    }

    private static List<PersistentDirtyRun> ExpectedRuns(
        IReadOnlyList<int> current,
        IReadOnlyList<int> checkpoint)
    {
        var result = new List<PersistentDirtyRun>();
        var index = 0;
        while (index < current.Count)
        {
            if (current[index] == checkpoint[index])
            {
                index++;
                continue;
            }

            var start = index++;
            while (index < current.Count && current[index] != checkpoint[index])
                index++;
            result.Add(new(start, index - start));
        }
        return result;
    }

    private sealed record ModelVersion(
        PersistentRunDeltaVector<int> Value,
        int[] Current,
        int[] Checkpoint);

    private sealed class CountingComparer<T> : IEqualityComparer<T>
    {
        public int Calls { get; private set; }

        public bool Equals(T? left, T? right)
        {
            Calls++;
            return EqualityComparer<T>.Default.Equals(left, right);
        }

        public int GetHashCode([DisallowNull] T value) => EqualityComparer<T>.Default.GetHashCode(value);
    }

    private sealed class ReferenceComparer<T> : IEqualityComparer<T>
        where T : class
    {
        public bool Equals(T? left, T? right) => ReferenceEquals(left, right);

        public int GetHashCode([DisallowNull] T value) => RuntimeHelpers.GetHashCode(value);
    }

    private sealed class ThrowOnceComparer<T> : IEqualityComparer<T>
    {
        private int _calls;
        private int _throwOn = -1;

        public void ThrowOnCall(int call)
        {
            _calls = 0;
            _throwOn = call;
        }

        public bool Equals(T? left, T? right)
        {
            _calls++;
            if (_calls == _throwOn)
            {
                _throwOn = -1;
                throw new ComparerFailureException();
            }
            return EqualityComparer<T>.Default.Equals(left, right);
        }

        public int GetHashCode([DisallowNull] T value) => EqualityComparer<T>.Default.GetHashCode(value);
    }

    private sealed class ComparerFailureException : Exception;
}
