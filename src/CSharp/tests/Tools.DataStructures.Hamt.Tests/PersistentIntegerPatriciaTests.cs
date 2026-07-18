using Xunit;

namespace Tools.DataStructures.Hamt.Tests;

/// <summary>Model and boundary coverage for the 32-bit and 64-bit Patricia map/set families.</summary>
public sealed class PersistentIntegerPatriciaTests
{
    /// <summary>Verifies signed boundary keys enumerate in ascending signed order.</summary>
    [Fact]
    public void Maps_EnumerateInAscendingSignedOrder()
    {
        int[] intKeys = [0, -1, int.MaxValue, int.MinValue, 1, -2, 42];
        long[] longKeys = [0, -1, long.MaxValue, long.MinValue, 1, -2, 1L << 40, -(1L << 40)];

        var ints = PersistentIntMap<string>.CreateRange(intKeys.Select(key => KeyValuePair.Create(key, $"i{key}")));
        var longs = PersistentLongMap<string>.CreateRange(longKeys.Select(key => KeyValuePair.Create(key, $"l{key}")));

        Assert.Equal(intKeys.Order(), ints.Keys);
        Assert.Equal(longKeys.Order(), longs.Keys);
        foreach (var key in intKeys)
            Assert.Equal($"i{key}", ints[key]);
        foreach (var key in longKeys)
            Assert.Equal($"l{key}", longs[key]);
    }

    /// <summary>Checks randomized 32-bit histories and retained snapshots against a dictionary model.</summary>
    [Fact]
    public void IntMap_RandomizedHistoryMatchesModel()
    {
        var random = new Random(20260711);
        var map = PersistentIntMap<int>.Empty;
        var model = new Dictionary<int, int>();
        var snapshots = new List<(PersistentIntMap<int> Map, Dictionary<int, int> Model)>();

        for (var i = 0; i < 20_000; i++)
        {
            var key = random.Next(-2_000, 2_001);
            if (random.Next(4) == 0)
            {
                map = map.Remove(key);
                model.Remove(key);
            }
            else
            {
                var value = random.Next();
                map = map.SetItem(key, value);
                model[key] = value;
            }

            if (i % 997 == 0)
                snapshots.Add((map, new Dictionary<int, int>(model)));
        }

        AssertMap(model, map);
        foreach (var snapshot in snapshots)
            AssertMap(snapshot.Model, snapshot.Map);
    }

    /// <summary>Checks randomized 64-bit histories against a sorted model.</summary>
    [Fact]
    public void LongMap_RandomizedHistoryMatchesModel()
    {
        var random = new Random(20260712);
        var map = PersistentLongMap<long>.Empty;
        var model = new SortedDictionary<long, long>();
        for (var i = 0; i < 15_000; i++)
        {
            var key = random.NextInt64(long.MinValue, long.MaxValue);
            if (random.Next(5) == 0)
            {
                map = map.Remove(key);
                model.Remove(key);
            }
            else
            {
                var value = random.NextInt64();
                map = map.SetItem(key, value);
                model[key] = value;
            }
        }

        Assert.Equal(model.ToArray(), map.ToArray());
    }

    /// <summary>Verifies no-op identity and map merge-combining contracts.</summary>
    [Fact]
    public void MapAlgebra_PreservesNoOpIdentityAndCombinesValues()
    {
        var left = PersistentIntMap<int>.CreateRange(
            new[] { KeyValuePair.Create(1, 10), KeyValuePair.Create(2, 20), KeyValuePair.Create(4, 40) });
        var right = PersistentIntMap<int>.CreateRange(
            new[] { KeyValuePair.Create(2, 3), KeyValuePair.Create(3, 30), KeyValuePair.Create(4, 5) });

        Assert.Same(left, left.SetItem(2, 20));
        Assert.Same(left, left.Remove(999));
        Assert.Same(left.RootIdentity, left.Union(left).RootIdentity);
        Assert.Same(left.RootIdentity, left.Intersect(left).RootIdentity);

        var union = left.Union(right, static (_, l, r) => l + r);
        Assert.Equal(new[] { (1, 10), (2, 23), (3, 30), (4, 45) }, union.Select(pair => (pair.Key, pair.Value)));
        var intersection = left.Intersect(right, static (key, l, r) => key + l + r);
        Assert.Equal(new[] { (2, 25), (4, 49) }, intersection.Select(pair => (pair.Key, pair.Value)));
        Assert.Equal(new[] { 1 }, left.Except(right).Keys);
    }

    /// <summary>Verifies self-algebra still invokes a supplied non-idempotent combiner once per key.</summary>
    [Fact]
    public void CombiningAlgebra_WithSameMapCombinesValuesAtBothWidths()
    {
        var ints = PersistentIntMap<int>.CreateRange(
            new[] { KeyValuePair.Create(-1, 10), KeyValuePair.Create(2, 20) });
        var longs = PersistentLongMap<long>.CreateRange(
            new[] { KeyValuePair.Create(-1L, 100L), KeyValuePair.Create(2L, 200L) });

        var intUnionCalls = 0;
        var unitedInts = ints.Union(ints, (key, left, right) =>
        {
            intUnionCalls++;
            return key + left + right;
        });
        var longUnionCalls = 0;
        var unitedLongs = longs.Union(longs, (key, left, right) =>
        {
            longUnionCalls++;
            return key + left + right;
        });

        Assert.Equal(ints.Count, intUnionCalls);
        Assert.Equal(new[] { (-1, 19), (2, 42) }, unitedInts.Select(pair => (pair.Key, pair.Value)));
        Assert.Equal(longs.Count, longUnionCalls);
        Assert.Equal(new[] { (-1L, 199L), (2L, 402L) }, unitedLongs.Select(pair => (pair.Key, pair.Value)));

        var intCalls = 0;
        var combinedInts = ints.Intersect(ints, (key, left, right) =>
        {
            intCalls++;
            return key + left + right;
        });
        var longCalls = 0;
        var combinedLongs = longs.Intersect(longs, (key, left, right) =>
        {
            longCalls++;
            return key + left + right;
        });

        Assert.Equal(ints.Count, intCalls);
        Assert.Equal(new[] { (-1, 19), (2, 42) }, combinedInts.Select(pair => (pair.Key, pair.Value)));
        Assert.Equal(longs.Count, longCalls);
        Assert.Equal(new[] { (-1L, 199L), (2L, 402L) }, combinedLongs.Select(pair => (pair.Key, pair.Value)));
    }

    /// <summary>Verifies combining algebra preserves unchanged structurally aligned roots.</summary>
    [Fact]
    public void CombiningAlgebra_ReusesReceiverRootWhenValuesRemainUnchanged()
    {
        var ints = PersistentIntMap<int>.CreateRange(
            new[] { KeyValuePair.Create(-8, 80), KeyValuePair.Create(1, 10), KeyValuePair.Create(4, 40) });
        var intSubset = PersistentIntMap<int>.CreateRange(
            new[] { KeyValuePair.Create(1, -10), KeyValuePair.Create(4, -40) });
        var intSuperset = intSubset.SetItem(-8, -80).SetItem(100, 1000);
        Assert.Same(ints.RootIdentity, ints.Union(intSubset, static (_, left, _) => left).RootIdentity);
        Assert.Same(ints.RootIdentity, ints.Intersect(intSuperset, static (_, left, _) => left).RootIdentity);

        var longs = PersistentLongMap<long>.CreateRange(
            new[] { KeyValuePair.Create(long.MinValue, 1L), KeyValuePair.Create(0L, 2L), KeyValuePair.Create(long.MaxValue, 3L) });
        var longSubset = PersistentLongMap<long>.CreateRange(
            new[] { KeyValuePair.Create(0L, -2L), KeyValuePair.Create(long.MaxValue, -3L) });
        var longSuperset = longSubset.SetItem(long.MinValue, -1L).SetItem(17L, 4L);
        Assert.Same(longs.RootIdentity, longs.Union(longSubset, static (_, left, _) => left).RootIdentity);
        Assert.Same(longs.RootIdentity, longs.Intersect(longSuperset, static (_, left, _) => left).RootIdentity);
    }

    /// <summary>Verifies both set widths implement persistent algebra and IReadOnlySet relations.</summary>
    [Fact]
    public void Sets_ProvideOrderedPersistentAlgebra()
    {
        var ints = PersistentIntSet.CreateRange([3, -1, 2, 3, int.MinValue]);
        var other = PersistentIntSet.CreateRange([2, 4]);
        Assert.Equal(new[] { int.MinValue, -1, 2, 3 }, ints);
        Assert.Equal(new[] { int.MinValue, -1, 2, 3, 4 }, ints.Union(other));
        Assert.Equal(new[] { 2 }, ints.Intersect(other));
        Assert.Equal(new[] { int.MinValue, -1, 3 }, ints.Except(other));
        Assert.True(ints.IsSupersetOf([-1, 2]));
        Assert.True(ints.Overlaps([100, 3]));

        var longs = PersistentLongSet.CreateRange([long.MaxValue, 0, long.MinValue, -1, 0]);
        Assert.Equal(new[] { long.MinValue, -1L, 0L, long.MaxValue }, longs);
        Assert.Same(longs.RootIdentity, longs.Add(0).RootIdentity);
        Assert.Same(longs.RootIdentity, longs.Remove(42).RootIdentity);
    }

    /// <summary>Checks prefix-aware structural algebra against dictionary models.</summary>
    [Fact]
    public void StructuralAlgebra_RandomizedMapsMatchModels()
    {
        var random = new Random(20260713);
        for (var trial = 0; trial < 200; trial++)
        {
            var leftModel = Enumerable.Range(0, random.Next(0, 200))
                .Select(_ => KeyValuePair.Create(random.Next(-500, 501), random.Next()))
                .GroupBy(pair => pair.Key).ToDictionary(group => group.Key, group => group.Last().Value);
            var rightModel = Enumerable.Range(0, random.Next(0, 200))
                .Select(_ => KeyValuePair.Create(random.Next(-500, 501), random.Next()))
                .GroupBy(pair => pair.Key).ToDictionary(group => group.Key, group => group.Last().Value);
            var left = PersistentIntMap<int>.CreateRange(leftModel);
            var right = PersistentIntMap<int>.CreateRange(rightModel);

            var unionModel = new Dictionary<int, int>(leftModel);
            foreach (var pair in rightModel)
                unionModel[pair.Key] = pair.Value;
            var intersectionModel = leftModel.Where(pair => rightModel.ContainsKey(pair.Key)).ToDictionary();
            var exceptModel = leftModel.Where(pair => !rightModel.ContainsKey(pair.Key)).ToDictionary();
            var combinedUnionModel = new Dictionary<int, int>(leftModel);
            foreach (var pair in rightModel)
            {
                combinedUnionModel[pair.Key] = leftModel.TryGetValue(pair.Key, out var leftValue)
                    ? Combine(pair.Key, leftValue, pair.Value)
                    : pair.Value;
            }
            var combinedIntersectionModel = leftModel
                .Where(pair => rightModel.ContainsKey(pair.Key))
                .ToDictionary(pair => pair.Key, pair => Combine(pair.Key, pair.Value, rightModel[pair.Key]));

            AssertMap(unionModel, left.Union(right));
            AssertMap(intersectionModel, left.Intersect(right));
            AssertMap(exceptModel, left.Except(right));
            AssertMap(combinedUnionModel, left.Union(right, Combine));
            AssertMap(combinedIntersectionModel, left.Intersect(right, Combine));
        }

        static int Combine(int key, int left, int right) =>
            unchecked(key * 397
                ^ (int)System.Numerics.BitOperations.RotateLeft((uint)left, 7)
                ^ (int)System.Numerics.BitOperations.RotateRight((uint)right, 11));
    }

    /// <summary>Verifies every rank gap, boundary, and signed-key search factory for the 32-bit map cursor.</summary>
    [Fact]
    public void IntMapCursor_UsesOrderedGapAndPresenceSafeSearchSemantics()
    {
        int[] keys = [int.MinValue, -1, 0, 17, int.MaxValue];
        var map = PersistentIntMap<string?>.CreateRange(
            keys.Select(key => KeyValuePair.Create<int, string?>(key, key == 0 ? null : key.ToString())));

        for (var position = 0; position <= keys.Length; position++)
        {
            var cursor = map.GetCursor(position);
            Assert.Equal(position, cursor.Position);
            Assert.Equal(keys.Length, cursor.Count);
            Assert.Equal(position == 0, cursor.IsAtStart);
            Assert.Equal(position == keys.Length, cursor.IsAtEnd);
            Assert.Same(map, cursor.Snapshot());

            Assert.Equal(position > 0, cursor.TryPeekPrevious(out var previous));
            if (position > 0)
                Assert.Equal(keys[position - 1], previous.Key);
            Assert.Equal(position < keys.Length, cursor.TryPeekNext(out var next));
            if (position < keys.Length)
                Assert.Equal(keys[position], next.Key);
        }

        Assert.Equal(0, map.GetLowerBoundCursor(int.MinValue).Position);
        Assert.Equal(1, map.GetUpperBoundCursor(int.MinValue).Position);
        Assert.Equal(1, map.GetLowerBoundCursor(-2).Position);
        Assert.Equal(2, map.GetUpperBoundCursor(-1).Position);
        Assert.Equal(4, map.GetLowerBoundCursor(18).Position);
        Assert.Equal(keys.Length, map.GetUpperBoundCursor(int.MaxValue).Position);

        var nullable = map.GetCursorAtKey(0, out var foundNull);
        Assert.True(foundNull);
        Assert.True(nullable.TryPeekNext(out var nullEntry));
        Assert.Null(nullEntry.Value);

        var miss = map.GetCursorAtKey(1, out var foundMiss);
        Assert.False(foundMiss);
        Assert.Equal(3, miss.Position);
        Assert.Equal(17, AssertNext(miss).Key);

        Assert.Same(map, map.GetCursorAtEnd().Snapshot());
        Assert.Throws<ArgumentOutOfRangeException>(() => map.GetCursor(-1));
        Assert.Throws<ArgumentOutOfRangeException>(() => map.GetCursor(map.Count + 1));
        Assert.Throws<InvalidOperationException>(() => map.GetCursor().MovePrevious());
        Assert.Throws<InvalidOperationException>(() => map.GetCursorAtEnd().MoveNext());
        Assert.Throws<InvalidOperationException>(() => default(PersistentIntMapCursor<string?>).Snapshot());
    }

    /// <summary>Verifies map edits preserve gap continuity, no-op identity, and retained ancestor branches.</summary>
    [Fact]
    public void IntMapCursor_EditsBranchPersistentlyAtTheFocusedGap()
    {
        var source = PersistentIntMap<string?>.CreateRange(
        [
            KeyValuePair.Create<int, string?>(-10, "a"),
            KeyValuePair.Create<int, string?>(0, null),
            KeyValuePair.Create<int, string?>(10, "c"),
        ]);
        var atZero = source.GetCursorAtKey(0, out var found);
        Assert.True(found);
        Assert.Equal(1, atZero.Position);

        var noOp = atZero.SetNextValue(null);
        Assert.Same(source, noOp.Snapshot());
        var updated = atZero.SetNextValue("b");
        Assert.Equal(1, updated.Position);
        Assert.Equal("b", updated.Snapshot()[0]);
        Assert.Null(source[0]);

        var deletedNext = atZero.DeleteNext();
        Assert.Equal(1, deletedNext.Position);
        Assert.Equal(new[] { -10, 10 }, deletedNext.Snapshot().Keys);
        var deletedPrevious = atZero.DeletePrevious();
        Assert.Equal(0, deletedPrevious.Position);
        Assert.Equal(new[] { 0, 10 }, deletedPrevious.Snapshot().Keys);

        var missing = source.GetCursorAtKey(5, out found);
        Assert.False(found);
        var inserted = missing.Insert(5, "five");
        Assert.Equal(3, inserted.Position);
        Assert.Equal(new[] { -10, 0, 5, 10 }, inserted.Snapshot().Keys);
        Assert.Equal(new[] { -10, 0, 10 }, source.Keys);

        var setInserted = source.GetLowerBoundCursor(-5).SetItem(-5, "minus five");
        Assert.Equal(2, setInserted.Position);
        Assert.Equal("minus five", setInserted.Snapshot()[-5]);
        var setUpdated = atZero.SetItem(0, "zero");
        Assert.Equal(1, setUpdated.Position);
        Assert.Equal("zero", setUpdated.Snapshot()[0]);

        Assert.Throws<ArgumentException>(() => atZero.Insert(0, "duplicate"));
        Assert.Throws<InvalidOperationException>(() => source.GetCursor().Insert(5, "wrong gap"));
        Assert.Throws<InvalidOperationException>(() => source.GetCursorAtEnd().SetNextValue("none"));
        Assert.Throws<InvalidOperationException>(() => source.GetCursor().DeletePrevious());
        Assert.Throws<InvalidOperationException>(() => source.GetCursorAtEnd().DeleteNext());
    }

    /// <summary>Checks 64-bit map cursor rank/search/edit parity across the signed transform boundary.</summary>
    [Fact]
    public void LongMapCursor_CoversSignedBoundariesAndPersistentEdits()
    {
        long[] keys = [long.MinValue, -1, 0, 1L << 40, long.MaxValue];
        var map = PersistentLongMap<long>.CreateRange(keys.Select(key => KeyValuePair.Create(key, key)));

        Assert.Equal(0, map.GetLowerBoundCursor(long.MinValue).Position);
        Assert.Equal(1, map.GetUpperBoundCursor(long.MinValue).Position);
        Assert.Equal(2, map.GetLowerBoundCursor(0).Position);
        Assert.Equal(3, map.GetUpperBoundCursor(0).Position);
        Assert.Equal(4, map.GetLowerBoundCursor((1L << 40) + 1).Position);
        Assert.Equal(keys.Length, map.GetUpperBoundCursor(long.MaxValue).Position);

        var exact = map.GetCursorAtKey(1L << 40, out var found);
        Assert.True(found);
        Assert.Equal(3, exact.Position);
        Assert.Equal(1L << 40, AssertNext(exact).Key);
        var updated = exact.SetNextValue(42);
        Assert.Equal(42, updated.Snapshot()[1L << 40]);
        Assert.Equal(1L << 40, map[1L << 40]);

        var miss = map.GetCursorAtKey(-2, out found);
        Assert.False(found);
        var inserted = miss.Insert(-2, 99);
        Assert.Equal(2, inserted.Position);
        Assert.Equal(new[] { long.MinValue, -2, -1, 0, 1L << 40, long.MaxValue }, inserted.Snapshot().Keys);
        Assert.Equal(1, inserted.DeletePrevious().Position);
        Assert.Same(map, map.GetCursorAtEnd().Snapshot());
        Assert.Throws<InvalidOperationException>(() => default(PersistentLongMapCursor<long>).TryPeekNext(out _));
    }

    /// <summary>Verifies set cursors use lower-bound insertion, duplicate no-ops, and gap-stable deletion.</summary>
    [Fact]
    public void PatriciaSetCursors_ProvideOrderedPersistentGapEditingAtBothWidths()
    {
        var ints = PersistentIntSet.CreateRange([int.MinValue, -1, 0, int.MaxValue]);
        var intMiss = ints.GetCursorAtItem(-2, out var found);
        Assert.False(found);
        Assert.Equal(1, intMiss.Position);
        var intAdded = intMiss.Add(-2);
        Assert.Equal(2, intAdded.Position);
        Assert.Equal(new[] { int.MinValue, -2, -1, 0, int.MaxValue }, intAdded.Snapshot());
        Assert.Equal(new[] { int.MinValue, -1, 0, int.MaxValue }, ints);

        var intExact = ints.GetCursorAtItem(0, out found);
        Assert.True(found);
        Assert.Same(ints, intExact.Add(0).Snapshot());
        Assert.Equal(new[] { int.MinValue, -1, int.MaxValue }, intExact.DeleteNext().Snapshot());
        Assert.Equal(new[] { int.MinValue, 0, int.MaxValue }, intExact.DeletePrevious().Snapshot());
        Assert.Throws<InvalidOperationException>(() => ints.GetCursor().Add(17));

        var longs = PersistentLongSet.CreateRange([long.MinValue, -1, 0, long.MaxValue]);
        Assert.Equal(1, longs.GetUpperBoundCursor(long.MinValue).Position);
        var longMiss = longs.GetCursorAtItem(1, out found);
        Assert.False(found);
        var longAdded = longMiss.Add(1);
        Assert.Equal(4, longAdded.Position);
        Assert.Equal(new[] { long.MinValue, -1L, 0L, 1L, long.MaxValue }, longAdded.Snapshot());
        Assert.Equal(3, longAdded.MovePrevious().Position);
        Assert.Same(longs, longs.GetCursorAtEnd().Snapshot());
        Assert.Throws<InvalidOperationException>(() => default(PersistentLongSetCursor).Snapshot());
    }

    /// <summary>Compares lower/upper/exact cursor ranks with a sorted-array model over randomized prefix shapes.</summary>
    [Fact]
    public void IntMapCursor_RandomizedSearchRanksMatchSortedModel()
    {
        var random = new Random(20260717);
        for (var trial = 0; trial < 200; trial++)
        {
            var keys = Enumerable.Range(0, random.Next(0, 100))
                .Select(_ => random.Next(-500, 501))
                .Distinct()
                .Order()
                .ToArray();
            var map = PersistentIntMap<int>.CreateRange(keys.Select(key => KeyValuePair.Create(key, key)));

            for (var probe = -550; probe <= 550; probe += 11)
            {
                var lower = Array.FindIndex(keys, key => key >= probe);
                if (lower < 0)
                    lower = keys.Length;
                var upper = Array.FindIndex(keys, key => key > probe);
                if (upper < 0)
                    upper = keys.Length;

                Assert.Equal(lower, map.GetLowerBoundCursor(probe).Position);
                Assert.Equal(upper, map.GetUpperBoundCursor(probe).Position);
                var exact = map.GetCursorAtKey(probe, out var found);
                Assert.Equal(lower, exact.Position);
                Assert.Equal(Array.BinarySearch(keys, probe) >= 0, found);
            }
        }
    }

    private static KeyValuePair<int, TValue> AssertNext<TValue>(PersistentIntMapCursor<TValue> cursor)
    {
        Assert.True(cursor.TryPeekNext(out var entry));
        return entry;
    }

    private static KeyValuePair<long, TValue> AssertNext<TValue>(PersistentLongMapCursor<TValue> cursor)
    {
        Assert.True(cursor.TryPeekNext(out var entry));
        return entry;
    }

    private static void AssertMap(Dictionary<int, int> expected, PersistentIntMap<int> actual)
    {
        Assert.Equal(expected.Count, actual.Count);
        Assert.Equal(expected.OrderBy(pair => pair.Key), actual);
        foreach (var (key, value) in expected)
            Assert.Equal(value, actual[key]);
    }
}
