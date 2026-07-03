using Tools.DataStructures.FingerTree;
using Xunit;

namespace Tools.DataStructures.FingerTree.Tests;

/// <summary>
/// Verifies the public contract of <see cref="Rope{T}"/>: construction, indexed access, endpoint and indexed
/// mutation, slicing, splitting, concatenation, bulk import/export, enumeration, persistence, and degenerate
/// cases — with internal invariants checked after each mutation.
/// </summary>
public sealed class RopeTests
{
    private static void AssertSequence(IReadOnlyList<int> expected, Rope<int> actual)
    {
        actual.ValidateInvariants();
        Assert.Equal(expected.Count, actual.Count);
        Assert.Equal(expected.Count == 0, actual.IsEmpty);
        Assert.Equal(expected, actual.ToArray());
        Assert.Equal(expected, actual.ToList());   // exercises the enumerator
        for (var i = 0; i < expected.Count; i++)
            Assert.Equal(expected[i], actual[i]);
        if (expected.Count > 0)
        {
            Assert.Equal(expected[0], actual.First);
            Assert.Equal(expected[^1], actual.Last);
        }
    }

    /// <summary>Verifies construction from spans, enumerables, chunks, and the empty rope, across sizes that span chunk boundaries.</summary>
    [Theory]
    [InlineData(0)]
    [InlineData(1)]
    [InlineData(100)]
    [InlineData(5000)]
    [InlineData(20000)]
    public void Construction_PreservesElements(int size)
    {
        var values = Enumerable.Range(0, size).ToArray();
        AssertSequence(values, Rope<int>.Create(values));
        AssertSequence(values, Rope<int>.CreateRange(values.AsEnumerable()));
        AssertSequence(values, Rope<int>.CreateRange(Rope<int>.Create(values)));   // round-trips an existing rope
    }

    /// <summary>Verifies FromChunks imports memory blocks (including over-large ones) without losing elements.</summary>
    [Fact]
    public void FromChunks_ImportsBlocks()
    {
        ReadOnlyMemory<int>[] blocks =
        [
            new[] { 1, 2, 3 },
            Array.Empty<int>(),                       // skipped
            Enumerable.Range(4, 5000).ToArray(),      // exceeds the max chunk size; gets split
            new[] { 9004 },
        ];
        var expected = new[] { 1, 2, 3 }.Concat(Enumerable.Range(4, 5000)).Append(9004).ToArray();
        AssertSequence(expected, Rope<int>.FromChunks(blocks));
    }

    /// <summary>Verifies FromChunks copies caller memory so later array mutation cannot change a rope snapshot.</summary>
    [Fact]
    public void FromChunks_CopiesMutableBackingStorage()
    {
        var small = new[] { 1, 2, 3 };
        var large = Enumerable.Range(10, 5000).ToArray();
        var rope = Rope<int>.FromChunks(small, large);

        small[1] = -1;
        large[3000] = -3000;

        var expected = new[] { 1, 2, 3 }.Concat(Enumerable.Range(10, 5000)).ToArray();
        AssertSequence(expected, rope);
    }

    /// <summary>Verifies endpoint additions and removals build the expected order and preserve earlier versions.</summary>
    [Fact]
    public void Endpoints_AddAndRemove_ArePersistent()
    {
        var original = Rope<int>.Create(2, 3);
        var grown = original.AddFirst(1).AddLast(4);

        AssertSequence(new[] { 2, 3 }, original);          // unchanged
        AssertSequence(new[] { 1, 2, 3, 4 }, grown);
        AssertSequence(new[] { 2, 3, 4 }, grown.RemoveFirst());
        AssertSequence(new[] { 1, 2, 3 }, grown.RemoveLast());
    }

    /// <summary>Verifies insert/remove/set at every position over a multi-chunk rope.</summary>
    [Fact]
    public void IndexedMutations_MatchListModel()
    {
        var model = Enumerable.Range(0, 6000).ToList();
        var rope = Rope<int>.Create(model.ToArray());

        var insertModel = new List<int>(model);
        insertModel.Insert(3000, -1);
        AssertSequence(insertModel, rope.Insert(3000, -1));

        var removeModel = new List<int>(model);
        removeModel.RemoveAt(3000);
        AssertSequence(removeModel, rope.RemoveAt(3000));

        var setModel = new List<int>(model);
        setModel[3000] = -1;
        AssertSequence(setModel, rope.SetItem(3000, -1));
    }

    /// <summary>Verifies range insertion and removal against a list model, including span and rope sources.</summary>
    [Fact]
    public void RangeMutations_MatchListModel()
    {
        var model = Enumerable.Range(0, 4000).ToList();
        var rope = Rope<int>.Create(model.ToArray());

        var added = new[] { -1, -2, -3, -4 };
        var insModel = new List<int>(model);
        insModel.InsertRange(1500, added);
        AssertSequence(insModel, rope.InsertRange(1500, added.AsSpan()));
        AssertSequence(insModel, rope.InsertRange(1500, added.AsEnumerable()));
        AssertSequence(insModel, rope.InsertRange(1500, Rope<int>.Create(added)));

        var remModel = new List<int>(model);
        remModel.RemoveRange(1000, 2000);
        AssertSequence(remModel, rope.RemoveRange(1000, 2000));
    }

    /// <summary>Verifies split reconstructs at every position, including mid-chunk, and that slicing matches.</summary>
    [Fact]
    public void SplitAndSlice_ReconstructAtEveryPosition()
    {
        var values = Enumerable.Range(0, 3000).ToArray();
        var rope = Rope<int>.Create(values);

        for (var index = 0; index <= values.Length; index += 137)
        {
            var (left, right) = rope.Split(index);
            Assert.Equal(index, left.Count);
            Assert.Equal(values.Length - index, right.Count);
            left.ValidateInvariants();
            right.ValidateInvariants();
            AssertSequence(values, left.Concat(right));
            AssertSequence(values[..index], left);
            AssertSequence(values[index..], right);
        }

        AssertSequence(values[500..2500], rope.Slice(500, 2000));
        Assert.Equal(values[500..2500], rope.GetRange(500, 2000));
    }

    /// <summary>Verifies concatenation of all size combinations preserves order and invariants (boundary coalescing).</summary>
    [Fact]
    public void Concat_AllSizeCombinations()
    {
        int[] sizes = [0, 1, 200, 3000];
        foreach (var a in sizes)
        {
            foreach (var b in sizes)
            {
                var av = Enumerable.Range(0, a).ToArray();
                var bv = Enumerable.Range(1000, b).ToArray();
                AssertSequence([.. av, .. bv], Rope<int>.Create(av).Concat(Rope<int>.Create(bv)));
            }
        }
    }

    /// <summary>Verifies CopyTo fills a destination span from an arbitrary offset.</summary>
    [Fact]
    public void CopyTo_FillsDestination()
    {
        var rope = Rope<int>.Create(Enumerable.Range(0, 5000).ToArray());
        var buffer = new int[2000];
        rope.CopyTo(1500, buffer);
        Assert.Equal(Enumerable.Range(1500, 2000), buffer);
    }

    /// <summary>Verifies Compact preserves elements and yields fresh chunks.</summary>
    [Fact]
    public void Compact_PreservesElements()
    {
        var rope = Rope<int>.Create(Enumerable.Range(0, 10000).ToArray()).Slice(2500, 5000);
        var compacted = rope.Compact();
        AssertSequence(rope.ToArray(), compacted);
    }

    /// <summary>Verifies it works for a non-numeric element type (a string rope).</summary>
    [Fact]
    public void WorksForReferenceElements()
    {
        var rope = Rope<string>.Create("a", "b", "c").AddLast("d").Insert(0, "z");
        Assert.Equal(new[] { "z", "a", "b", "c", "d" }, rope.ToArray());
        Assert.Equal("z", rope[0]);
        Assert.Equal("d", rope.Last);
    }

    /// <summary>Verifies empty-rope degenerate behavior and argument validation.</summary>
    [Fact]
    public void Empty_AndArgumentValidation()
    {
        var empty = Rope<int>.Empty;
        Assert.True(empty.IsEmpty);
        Assert.Empty(empty);
        Assert.Empty(empty.ToArray());
        Assert.Throws<InvalidOperationException>(() => empty.First);
        Assert.Throws<InvalidOperationException>(() => empty.RemoveFirst());
        Assert.Throws<ArgumentOutOfRangeException>(() => empty[0]);

        var rope = Rope<int>.Create(1, 2, 3);
        Assert.Throws<ArgumentOutOfRangeException>(() => rope[3]);
        Assert.Throws<ArgumentOutOfRangeException>(() => rope.Insert(4, 0));
        Assert.Throws<ArgumentOutOfRangeException>(() => rope.RemoveRange(2, 5));
        Assert.Throws<ArgumentOutOfRangeException>(() => rope.RemoveRange(2, int.MaxValue));
        Assert.Throws<ArgumentOutOfRangeException>(() => rope.Slice(2, int.MaxValue));
        Assert.Throws<ArgumentOutOfRangeException>(() => rope.GetRange(2, int.MaxValue));
        Assert.Throws<ArgumentOutOfRangeException>(() => rope.CopyTo(2, new int[2]));
        Assert.Throws<ArgumentNullException>(() => rope.Concat(null!));
        Assert.Throws<ArgumentNullException>(() => rope.InsertRange(0, (IEnumerable<int>)null!));

        // Splitting at the ends is degenerate but valid.
        var (l, r) = rope.Split(0);
        Assert.True(l.IsEmpty);
        AssertSequence(new[] { 1, 2, 3 }, r);
    }
}
