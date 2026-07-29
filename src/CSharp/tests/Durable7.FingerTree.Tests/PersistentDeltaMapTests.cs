using Durable7.FingerTree.Experimental;
using Xunit;
using BclIntMap = System.Collections.Generic.SortedDictionary<int, int>;

namespace Durable7.FingerTree.Tests;

/// <summary>
/// Verifies the checkpoint-relative change index, representative rules, persistence, model behavior,
/// and output-sensitive enumeration contract of <see cref="PersistentDeltaMap{TKey, TValue}"/>.
/// </summary>
public sealed class PersistentDeltaMapTests
{
    /// <summary>Classifies additions, updates, and removals with exact before and after endpoints.</summary>
    [Fact]
    public void PointEdits_ClassifyAddedUpdatedAndRemovedKeys()
    {
        var source = PersistentDeltaMap<int, string>.CreateRange(
        [
            KeyValuePair.Create(1, "one"),
            KeyValuePair.Create(2, "two"),
        ]);

        var changed = source
            .SetItem(0, "zero")
            .SetItem(1, "ONE")
            .Remove(2);

        Assert.Equal(3, changed.ChangeCount);
        Assert.True(changed.HasChanges);
        Assert.Equal(
            [KeyValuePair.Create(0, "zero"), KeyValuePair.Create(1, "ONE")],
            changed.ToArray());

        var changes = changed.GetChanges().ToArray();
        Assert.Equal([0, 1, 2], changes.Select(change => change.Key).ToArray());

        Assert.Equal(PersistentMapChangeKind.Added, changes[0].Kind);
        AssertAbsent(changes[0].Before);
        AssertPresent("zero", changes[0].After);

        Assert.Equal(PersistentMapChangeKind.Updated, changes[1].Kind);
        AssertPresent("one", changes[1].Before);
        AssertPresent("ONE", changes[1].After);

        Assert.Equal(PersistentMapChangeKind.Removed, changes[2].Kind);
        AssertPresent("two", changes[2].Before);
        AssertAbsent(changes[2].After);

        Assert.True(changed.TryGetChange(0, out var added));
        Assert.Equal(changes[0], added);
        Assert.True(changed.TryGetChange(1, out var updated));
        Assert.Equal(changes[1], updated);
        Assert.True(changed.TryGetChange(2, out var removed));
        Assert.Equal(changes[2], removed);
        Assert.False(changed.TryGetChange(99, out _));

        Assert.False(source.HasChanges);
        Assert.Empty(source.GetChanges());
        Assert.Equal(
            [KeyValuePair.Create(1, "one"), KeyValuePair.Create(2, "two")],
            source.ToArray());
    }

    /// <summary>Distinguishes a present null from absence and rejects reading an absent endpoint.</summary>
    [Fact]
    public void EndpointValues_DistinguishPresentNullFromAbsence()
    {
        var source = PersistentDeltaMap<string, string?>.CreateRange(
            [KeyValuePair.Create<string, string?>("present", null)]);
        var changed = source.Remove("present").SetItem("added", null);

        var changes = changed.GetChanges().ToArray();
        Assert.Equal(["added", "present"], changes.Select(change => change.Key).ToArray());

        var added = changes[0];
        Assert.Equal(PersistentMapChangeKind.Added, added.Kind);
        AssertAbsent(added.Before);
        AssertPresent<string?>(null, added.After);

        var removed = changes[1];
        Assert.Equal(PersistentMapChangeKind.Removed, removed.Kind);
        AssertPresent<string?>(null, removed.Before);
        AssertAbsent(removed.After);

        Assert.Throws<InvalidOperationException>(() => DeltaMapValue<string?>.Absent.Value);
        Assert.Throws<InvalidOperationException>(() =>
            new PersistentMapChange<int, int>(
                1,
                DeltaMapValue<int>.Absent,
                DeltaMapValue<int>.Absent).Kind);
    }

    /// <summary>Coalesces repeated writes and removes records when a key returns to its checkpoint state.</summary>
    [Fact]
    public void RepeatedWritesAndRoundTrips_CoalesceAndCancelChanges()
    {
        var source = PersistentDeltaMap<int, string>.CreateRange([KeyValuePair.Create(1, "one")]);

        Assert.Same(source, source.SetItem(1, "one"));
        Assert.Same(source, source.Remove(99));

        var first = source.SetItem(1, "two");
        var second = first.SetItem(1, "three");
        var change = Assert.Single(second.GetChanges());
        Assert.Equal(PersistentMapChangeKind.Updated, change.Kind);
        AssertPresent("one", change.Before);
        AssertPresent("three", change.After);
        Assert.Equal(1, second.ChangeCount);

        var restored = second.SetItem(1, "one");
        Assert.False(restored.HasChanges);
        Assert.Empty(restored.GetChanges());
        Assert.Same(source.CurrentSnapshot, restored.CurrentSnapshot);
        Assert.Same(restored.CurrentSnapshot, restored.CheckpointSnapshot);

        var addedThenRemoved = source
            .SetItem(2, "first")
            .SetItem(2, "second")
            .Remove(2);
        Assert.False(addedThenRemoved.HasChanges);
        Assert.Same(source.CurrentSnapshot, addedThenRemoved.CurrentSnapshot);

        var removedThenRestored = source.Remove(1).SetItem(1, "one");
        Assert.False(removedThenRestored.HasChanges);
        Assert.Same(source.CurrentSnapshot, removedThenRestored.CurrentSnapshot);

        Assert.True(first.HasChanges);
        AssertPresent("two", Assert.Single(first.GetChanges()).After);
        Assert.True(second.HasChanges);
        AssertPresent("three", Assert.Single(second.GetChanges()).After);
    }

    /// <summary>Enumerates changes once each in the exact order defined by the retained key comparer.</summary>
    [Fact]
    public void GetChanges_UsesRetainedSortedKeyOrder()
    {
        var descending = Comparer<int>.Create((left, right) => right.CompareTo(left));
        var source = PersistentDeltaMap<int, string>.CreateRange(
        [
            KeyValuePair.Create(1, "one"),
            KeyValuePair.Create(3, "three"),
            KeyValuePair.Create(5, "five"),
        ], descending);

        var changed = source
            .SetItem(6, "six")
            .SetItem(4, "four")
            .SetItem(3, "THREE")
            .Remove(1);

        Assert.Same(descending, changed.Comparer);
        Assert.Equal([6, 4, 3, 1], changed.GetChanges().Select(change => change.Key).ToArray());
        Assert.Equal(
            [
                PersistentMapChangeKind.Added,
                PersistentMapChangeKind.Added,
                PersistentMapChangeKind.Updated,
                PersistentMapChangeKind.Removed,
            ],
            changed.GetChanges().Select(change => change.Kind).ToArray());
    }

    /// <summary>Checkpoints and rollbacks share the exact selected map root and invoke no policy callbacks.</summary>
    [Fact]
    public void CheckpointAndRollback_ShareExactRootsAndCleanVersionsAreIdentity()
    {
        var keyComparer = new CountingComparer<int>(Comparer<int>.Default);
        var valueComparer = new CountingEqualityComparer<string>(StringComparer.Ordinal);
        var source = PersistentDeltaMap<int, string>.CreateRange(
        [
            KeyValuePair.Create(1, "one"),
            KeyValuePair.Create(2, "two"),
        ], keyComparer, valueComparer);
        var dirty = source.SetItem(1, "ONE").SetItem(3, "three");

        keyComparer.Reset();
        valueComparer.Reset();
        var checkpoint = dirty.Checkpoint();
        Assert.Equal(0, keyComparer.ComparisonCount);
        Assert.Equal(0, valueComparer.EqualityCount);
        Assert.Same(dirty.CurrentSnapshot, checkpoint.CurrentSnapshot);
        Assert.Same(checkpoint.CurrentSnapshot, checkpoint.CheckpointSnapshot);
        Assert.False(checkpoint.HasChanges);
        Assert.Same(checkpoint, checkpoint.Checkpoint());
        Assert.Same(checkpoint, checkpoint.Rollback());

        keyComparer.Reset();
        valueComparer.Reset();
        var rollback = dirty.Rollback();
        Assert.Equal(0, keyComparer.ComparisonCount);
        Assert.Equal(0, valueComparer.EqualityCount);
        Assert.Same(dirty.CheckpointSnapshot, rollback.CurrentSnapshot);
        Assert.Same(rollback.CurrentSnapshot, rollback.CheckpointSnapshot);
        Assert.False(rollback.HasChanges);
        Assert.Same(rollback, rollback.Checkpoint());
        Assert.Same(rollback, rollback.Rollback());

        Assert.True(dirty.HasChanges);
        Assert.Equal(2, dirty.ChangeCount);
        Assert.Same(source.CurrentSnapshot, dirty.CheckpointSnapshot);
        Assert.Equal(
            [KeyValuePair.Create(1, "one"), KeyValuePair.Create(2, "two")],
            rollback.ToArray());
        Assert.Equal(
            [
                KeyValuePair.Create(1, "ONE"),
                KeyValuePair.Create(2, "two"),
                KeyValuePair.Create(3, "three"),
            ],
            checkpoint.ToArray());
    }

    /// <summary>Retained versions and branches keep independent current states, checkpoints, and deltas.</summary>
    [Fact]
    public void RetainedBranches_EvolveIndependentlyAcrossCheckpoints()
    {
        var root = PersistentDeltaMap<int, string>.CreateRange(
        [
            KeyValuePair.Create(1, "one"),
            KeyValuePair.Create(2, "two"),
        ]);
        var left = root.SetItem(1, "left");
        var right = root.SetItem(3, "right");

        Assert.Same(root.CurrentSnapshot, left.CheckpointSnapshot);
        Assert.Same(root.CurrentSnapshot, right.CheckpointSnapshot);
        Assert.Equal([1], left.GetChanges().Select(change => change.Key).ToArray());
        Assert.Equal([3], right.GetChanges().Select(change => change.Key).ToArray());
        Assert.False(root.HasChanges);

        var leftCheckpoint = left.Checkpoint();
        var leftA = leftCheckpoint.Remove(2);
        var leftB = leftCheckpoint.SetItem(1, "branch-b");

        Assert.Same(left.CurrentSnapshot, leftCheckpoint.CurrentSnapshot);
        Assert.Same(leftCheckpoint.CurrentSnapshot, leftA.CheckpointSnapshot);
        Assert.Same(leftCheckpoint.CurrentSnapshot, leftB.CheckpointSnapshot);
        Assert.Equal([2], leftA.GetChanges().Select(change => change.Key).ToArray());
        Assert.Equal([1], leftB.GetChanges().Select(change => change.Key).ToArray());

        Assert.Equal(
            [KeyValuePair.Create(1, "left"), KeyValuePair.Create(2, "two")],
            leftCheckpoint.ToArray());
        Assert.Equal([KeyValuePair.Create(1, "left")], leftA.ToArray());
        Assert.Equal(
            [KeyValuePair.Create(1, "branch-b"), KeyValuePair.Create(2, "two")],
            leftB.ToArray());
        Assert.Equal(
            [KeyValuePair.Create(1, "one"), KeyValuePair.Create(2, "two")],
            root.ToArray());
        Assert.Equal(
            [
                KeyValuePair.Create(1, "one"),
                KeyValuePair.Create(2, "two"),
                KeyValuePair.Create(3, "right"),
            ],
            right.ToArray());
    }

    /// <summary>Retains canonical key representatives under a case-insensitive key order.</summary>
    [Fact]
    public void CaseInsensitiveKeys_RetainCheckpointAndActiveAdditionRepresentatives()
    {
        var comparer = StringComparer.OrdinalIgnoreCase;
        var checkpointKey = new string("Alpha".ToCharArray());
        var equivalentProbe = new string("ALPHA".ToCharArray());
        var addedKey = new string("Beta".ToCharArray());
        var equivalentAddedKey = new string("BETA".ToCharArray());
        var source = PersistentDeltaMap<string, int>.CreateRange(
            [KeyValuePair.Create(checkpointKey, 1)],
            comparer);

        var updated = source.SetItem(equivalentProbe, 2);
        var currentEntry = Assert.Single(updated);
        var updatedChange = Assert.Single(updated.GetChanges());
        Assert.Same(comparer, updated.Comparer);
        Assert.Same(checkpointKey, currentEntry.Key);
        Assert.Same(checkpointKey, updatedChange.Key);
        Assert.Equal(2, currentEntry.Value);

        var removedAndReadded = updated.Remove(equivalentProbe).SetItem(equivalentProbe, 1);
        Assert.False(removedAndReadded.HasChanges);
        Assert.Same(source.CurrentSnapshot, removedAndReadded.CurrentSnapshot);
        Assert.Same(checkpointKey, Assert.Single(removedAndReadded).Key);

        var withOtherChange = source.SetItem("Omega", 9).Remove(equivalentProbe);
        var restoredBesideOtherChange = withOtherChange.SetItem(equivalentProbe, 1);
        Assert.Equal(["Omega"], restoredBesideOtherChange.GetChanges().Select(change => change.Key).ToArray());
        Assert.Same(
            checkpointKey,
            restoredBesideOtherChange.Single(entry => comparer.Compare(entry.Key, checkpointKey) == 0).Key);
        restoredBesideOtherChange.ValidateInvariants();

        var added = source.SetItem(addedKey, 3).SetItem(equivalentAddedKey, 4);
        Assert.Same(addedKey, added.Single(entry => comparer.Compare(entry.Key, addedKey) == 0).Key);
        Assert.Same(addedKey, Assert.Single(added.GetChanges()).Key);
        Assert.Equal(4, added[equivalentAddedKey]);

        var cancelled = PersistentDeltaMap<string, int>.Create(comparer)
            .SetItem(addedKey, 1)
            .Remove(equivalentAddedKey);
        var newEpisode = cancelled.SetItem(equivalentAddedKey, 2);
        Assert.False(cancelled.HasChanges);
        Assert.Same(equivalentAddedKey, Assert.Single(newEpisode).Key);
        Assert.Same(equivalentAddedKey, Assert.Single(newEpisode.GetChanges()).Key);
    }

    /// <summary>CreateRange uses the last entry's representative and value within each comparer class.</summary>
    [Fact]
    public void CreateRange_LastComparatorEquivalentEntryWins()
    {
        var comparer = StringComparer.OrdinalIgnoreCase;
        var first = new string("Alpha".ToCharArray());
        var last = new string("ALPHA".ToCharArray());
        var map = PersistentDeltaMap<string, int>.CreateRange(
        [
            KeyValuePair.Create(first, 1),
            KeyValuePair.Create("Beta", 2),
            KeyValuePair.Create(last, 3),
        ], comparer);

        Assert.Equal(2, map.Count);
        Assert.Equal(3, map[first]);
        Assert.Same(last, map.First(entry => comparer.Compare(entry.Key, first) == 0).Key);
        Assert.False(map.HasChanges);
        map.ValidateInvariants();
    }

    /// <summary>Uses a custom value comparer for no-op identity and checkpoint-relative cancellation.</summary>
    [Fact]
    public void CustomValueComparer_DefinesNoOpsAndCancellation()
    {
        var values = StringComparer.OrdinalIgnoreCase;
        var source = PersistentDeltaMap<int, string>.CreateRange(
            [KeyValuePair.Create(1, "Alpha")],
            valueComparer: values);

        Assert.Same(values, source.ValueComparer);
        Assert.Same(source, source.SetItem(1, "ALPHA"));

        var dirty = source.SetItem(1, "Beta");
        Assert.Same(dirty, dirty.SetItem(1, "BETA"));
        var change = Assert.Single(dirty.GetChanges());
        AssertPresent("Alpha", change.Before);
        AssertPresent("Beta", change.After);

        var restored = dirty.SetItem(1, "alpha");
        Assert.False(restored.HasChanges);
        Assert.Same(source.CurrentSnapshot, restored.CurrentSnapshot);
        Assert.Equal("Alpha", restored[1]);

        var added = source.SetItem(2, "Value");
        Assert.Same(added, added.SetItem(2, "VALUE"));
        var cancelledAddition = added.Remove(2);
        Assert.False(cancelledAddition.HasChanges);
        Assert.Same(source.CurrentSnapshot, cancelledAddition.CurrentSnapshot);

        var twoKeys = PersistentDeltaMap<int, string>.CreateRange(
        [
            KeyValuePair.Create(1, "Alpha"),
            KeyValuePair.Create(2, "Other"),
        ], valueComparer: values);
        var twoDirty = twoKeys.SetItem(1, "Beta").SetItem(2, "Changed");
        var oneCancelled = twoDirty.SetItem(1, "alpha");
        Assert.Equal("alpha", oneCancelled[1]);
        Assert.Equal([2], oneCancelled.GetChanges().Select(item => item.Key).ToArray());
        oneCancelled.ValidateInvariants();

        var dirtyAgain = oneCancelled.SetItem(1, "Gamma");
        var redirtied = dirtyAgain.GetChanges().Single(item => item.Key == 1);
        Assert.True(values.Equals("Alpha", redirtied.Before.Value));
        Assert.Equal("alpha", redirtied.Before.Value);
        Assert.Equal("Gamma", redirtied.After.Value);
        dirtyAgain.ValidateInvariants();
    }

    /// <summary>Checks mixed persistent histories against independent current, checkpoint, and delta models.</summary>
    /// <param name="seed">Deterministic random seed.</param>
    [Theory]
    [InlineData(0)]
    [InlineData(1)]
    [InlineData(0x5eed)]
    [InlineData(0x7fffffff)]
    public void RandomizedHistories_MatchBaselineCurrentAndDeltaModels(int seed)
    {
        var initial = Enumerable.Range(-8, 17)
            .Where(key => (key & 1) == 0)
            .Select(key => KeyValuePair.Create(key, key * 10))
            .ToArray();
        var actual = PersistentDeltaMap<int, int>.CreateRange(initial);
        var checkpoint = new BclIntMap(initial.ToDictionary(entry => entry.Key, entry => entry.Value));
        var current = new BclIntMap(checkpoint);
        var random = new Random(seed);

        for (var step = 0; step < 750; step++)
        {
            var operation = random.Next(100);
            var key = random.Next(-25, 26);
            if (operation < 50)
            {
                var value = random.Next(-7, 8);
                var wasNoOp = current.TryGetValue(key, out var oldValue) && oldValue == value;
                var previous = actual;
                actual = actual.SetItem(key, value);
                current[key] = value;
                if (wasNoOp)
                    Assert.Same(previous, actual);
            }
            else if (operation < 75)
            {
                var wasAbsent = !current.Remove(key);
                var previous = actual;
                actual = actual.Remove(key);
                if (wasAbsent)
                    Assert.Same(previous, actual);
            }
            else if (operation < 86)
            {
                var wasClean = ModelEquals(checkpoint, current);
                var previous = actual;
                actual = actual.Checkpoint();
                checkpoint = new BclIntMap(current);
                if (wasClean)
                    Assert.Same(previous, actual);
            }
            else if (operation < 97)
            {
                var wasClean = ModelEquals(checkpoint, current);
                var previous = actual;
                actual = actual.Rollback();
                current = new BclIntMap(checkpoint);
                if (wasClean)
                    Assert.Same(previous, actual);
            }
            else
            {
                Assert.Equal(current.TryGetValue(key, out var expected), actual.TryGetValue(key, out var observed));
                Assert.Equal(expected, observed);
            }

            AssertMatchesModel(actual, checkpoint, current);
        }
    }

    /// <summary>
    /// Shows that enumerating a fixed-size delta invokes no key or value comparisons as the baseline grows.
    /// </summary>
    [Fact]
    public void GetChanges_ComparisonCountsAreIndependentOfBaselineSize()
    {
        var small = BuildThreeChangeMap(128);
        var large = BuildThreeChangeMap(16_384);

        small.KeyComparer.Reset();
        small.ValueComparer.Reset();
        var smallChanges = small.Map.GetChanges().ToArray();

        large.KeyComparer.Reset();
        large.ValueComparer.Reset();
        var largeChanges = large.Map.GetChanges().ToArray();

        Assert.Equal(3, smallChanges.Length);
        Assert.Equal(3, largeChanges.Length);
        Assert.Equal([-1, 64, 127], smallChanges.Select(change => change.Key).ToArray());
        Assert.Equal([-1, 8_192, 16_383], largeChanges.Select(change => change.Key).ToArray());
        Assert.Equal(0, small.KeyComparer.ComparisonCount);
        Assert.Equal(0, large.KeyComparer.ComparisonCount);
        Assert.Equal(0, small.ValueComparer.EqualityCount);
        Assert.Equal(0, large.ValueComparer.EqualityCount);
    }

    /// <summary>Leaves every retained version unchanged when a policy callback fails at any reachable ordinal.</summary>
    [Fact]
    public void PolicyCallbackFailures_DoNotPublishPartialSuccessors()
    {
        var valueComparer = new FailpointEqualityComparer<string>(StringComparer.Ordinal);
        var byValue = PersistentDeltaMap<int, string>.CreateRange(
            [KeyValuePair.Create(1, "one")],
            valueComparer: valueComparer)
            .SetItem(1, "two");
        var valueFailures = ExerciseEveryFailure(
            valueComparer,
            () => byValue.SetItem(1, "one"),
            () =>
            {
                Assert.Equal("two", byValue[1]);
                AssertPresent("one", Assert.Single(byValue.GetChanges()).Before);
                AssertPresent("two", Assert.Single(byValue.GetChanges()).After);
            });

        Assert.True(valueFailures > 0);

        var keyComparer = new FailpointComparer<int>(Comparer<int>.Default);
        var byKey = PersistentDeltaMap<int, string>.CreateRange(
        [
            KeyValuePair.Create(1, "one"),
            KeyValuePair.Create(2, "two"),
            KeyValuePair.Create(3, "three"),
        ], keyComparer)
            .SetItem(2, "TWO");
        var keyFailures = ExerciseEveryFailure(
            keyComparer,
            () => byKey.SetItem(4, "four"),
            () =>
            {
                keyComparer.Disarm();
                Assert.Equal(
                    [
                        KeyValuePair.Create(1, "one"),
                        KeyValuePair.Create(2, "TWO"),
                        KeyValuePair.Create(3, "three"),
                    ],
                    byKey.ToArray());
                Assert.Equal([2], byKey.GetChanges().Select(change => change.Key).ToArray());
            });

        Assert.True(keyFailures > 0);

        var removeFailures = ExerciseEveryFailure(
            keyComparer,
            () => byKey.Remove(1),
            () =>
            {
                keyComparer.Disarm();
                Assert.Equal("one", byKey[1]);
                Assert.Equal([2], byKey.GetChanges().Select(change => change.Key).ToArray());
                byKey.ValidateInvariants();
            });

        Assert.True(removeFailures > 0);
    }

    private static int ExerciseEveryFailure<TPolicy, TResult>(
        TPolicy policy,
        Func<TResult> operation,
        Action assertSourceUnchanged)
        where TPolicy : IOrdinalFailpoint
    {
        for (var ordinal = 1; ordinal < 1_000; ordinal++)
        {
            policy.ArmAt(ordinal);
            try
            {
                _ = operation();
                policy.Disarm();
                return ordinal - 1;
            }
            catch (PolicyFailpointException)
            {
                policy.Disarm();
                assertSourceUnchanged();
            }
        }

        throw new InvalidOperationException("Policy operation did not complete within the failpoint ceiling.");
    }

    private static ComparisonFixture BuildThreeChangeMap(int count)
    {
        var keyComparer = new CountingComparer<int>(Comparer<int>.Default);
        var valueComparer = new CountingEqualityComparer<int>(EqualityComparer<int>.Default);
        var map = PersistentDeltaMap<int, int>.CreateRange(
            Enumerable.Range(0, count).Select(key => KeyValuePair.Create(key, key)),
            keyComparer,
            valueComparer)
            .SetItem(-1, -1)
            .SetItem(count / 2, -count)
            .Remove(count - 1);

        Assert.Equal(3, map.ChangeCount);
        return new ComparisonFixture(map, keyComparer, valueComparer);
    }

    private static void AssertMatchesModel(
        PersistentDeltaMap<int, int> actual,
        BclIntMap checkpoint,
        BclIntMap current)
    {
        actual.ValidateInvariants();
        Assert.Equal(current.ToArray(), actual.ToArray());
        Assert.Equal(checkpoint.ToArray(), actual.CheckpointSnapshot.ToArray());
        Assert.Equal(current.Count, actual.Count);
        Assert.Equal(current.Count == 0, actual.IsEmpty);

        var expected = GetExpectedChanges(checkpoint, current);
        var observed = actual.GetChanges().ToArray();
        Assert.Equal(expected.Length, actual.ChangeCount);
        Assert.Equal(expected.Length != 0, actual.HasChanges);
        Assert.Equal(expected.Length, observed.Length);

        for (var index = 0; index < expected.Length; index++)
        {
            Assert.Equal(expected[index].Key, observed[index].Key);
            Assert.Equal(expected[index].Kind, observed[index].Kind);
            AssertEndpoint(expected[index].Before, observed[index].Before);
            AssertEndpoint(expected[index].After, observed[index].After);
            Assert.True(actual.TryGetChange(expected[index].Key, out var found));
            Assert.Equal(observed[index], found);
        }

        Assert.False(actual.TryGetChange(10_000, out _));
    }

    private static ExpectedChange[] GetExpectedChanges(BclIntMap checkpoint, BclIntMap current)
    {
        var keys = checkpoint.Keys.Concat(current.Keys).Distinct().Order().ToArray();
        var changes = new List<ExpectedChange>();
        foreach (var key in keys)
        {
            var hadBefore = checkpoint.TryGetValue(key, out var before);
            var hasAfter = current.TryGetValue(key, out var after);
            if (hadBefore == hasAfter && (!hadBefore || before == after))
                continue;

            var kind = hadBefore
                ? hasAfter ? PersistentMapChangeKind.Updated : PersistentMapChangeKind.Removed
                : PersistentMapChangeKind.Added;
            changes.Add(new ExpectedChange(
                key,
                hadBefore ? ModelEndpoint.Present(before) : ModelEndpoint.Absent,
                hasAfter ? ModelEndpoint.Present(after) : ModelEndpoint.Absent,
                kind));
        }
        return changes.ToArray();
    }

    private static bool ModelEquals(BclIntMap left, BclIntMap right) =>
        left.Count == right.Count && left.SequenceEqual(right);

    private static void AssertEndpoint(ModelEndpoint expected, DeltaMapValue<int> actual)
    {
        Assert.Equal(expected.HasValue, actual.HasValue);
        if (expected.HasValue)
            Assert.Equal(expected.Value, actual.Value);
        else
            Assert.Throws<InvalidOperationException>(() => actual.Value);
    }

    private static void AssertPresent<T>(T expected, DeltaMapValue<T> actual)
    {
        Assert.True(actual.HasValue);
        Assert.Equal(expected, actual.Value);
    }

    private static void AssertAbsent<T>(DeltaMapValue<T> actual)
    {
        Assert.False(actual.HasValue);
        Assert.Throws<InvalidOperationException>(() => actual.Value);
    }

    private readonly record struct ExpectedChange(
        int Key,
        ModelEndpoint Before,
        ModelEndpoint After,
        PersistentMapChangeKind Kind);

    private readonly record struct ModelEndpoint(bool HasValue, int Value)
    {
        internal static ModelEndpoint Absent => default;

        internal static ModelEndpoint Present(int value) => new(true, value);
    }

    private readonly record struct ComparisonFixture(
        PersistentDeltaMap<int, int> Map,
        CountingComparer<int> KeyComparer,
        CountingEqualityComparer<int> ValueComparer);

    private sealed class CountingComparer<T>(IComparer<T> inner) : IComparer<T>
    {
        internal int ComparisonCount { get; private set; }

        public int Compare(T? left, T? right)
        {
            ComparisonCount++;
            return inner.Compare(left!, right!);
        }

        internal void Reset() => ComparisonCount = 0;
    }

    private sealed class CountingEqualityComparer<T>(IEqualityComparer<T> inner) : IEqualityComparer<T>
    {
        internal int EqualityCount { get; private set; }

        public bool Equals(T? left, T? right)
        {
            EqualityCount++;
            return inner.Equals(left!, right!);
        }

        public int GetHashCode(T value) => value is null ? 0 : inner.GetHashCode(value);

        internal void Reset() => EqualityCount = 0;
    }

    private interface IOrdinalFailpoint
    {
        void ArmAt(int ordinal);

        void Disarm();
    }

    private sealed class FailpointComparer<T>(IComparer<T> inner) : IComparer<T>, IOrdinalFailpoint
    {
        private int? _remaining;

        public int Compare(T? left, T? right)
        {
            Hit();
            return inner.Compare(left!, right!);
        }

        public void ArmAt(int ordinal) => _remaining = ordinal;

        public void Disarm() => _remaining = null;

        private void Hit()
        {
            if (_remaining is not int remaining)
                return;
            if (remaining == 1)
                throw new PolicyFailpointException();
            _remaining = remaining - 1;
        }
    }

    private sealed class FailpointEqualityComparer<T>(IEqualityComparer<T> inner) : IEqualityComparer<T>, IOrdinalFailpoint
    {
        private int? _remaining;

        public bool Equals(T? left, T? right)
        {
            Hit();
            return inner.Equals(left!, right!);
        }

        public int GetHashCode(T value) => value is null ? 0 : inner.GetHashCode(value);

        public void ArmAt(int ordinal) => _remaining = ordinal;

        public void Disarm() => _remaining = null;

        private void Hit()
        {
            if (_remaining is not int remaining)
                return;
            if (remaining == 1)
                throw new PolicyFailpointException();
            _remaining = remaining - 1;
        }
    }

    private sealed class PolicyFailpointException : Exception
    {
    }
}
