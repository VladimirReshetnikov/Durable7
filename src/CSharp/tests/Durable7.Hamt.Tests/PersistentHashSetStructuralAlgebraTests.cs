// Tests for the persistent hash set structural algebra.

using Xunit;

namespace Durable7.Hamt.Tests;

/// <summary>Structural CHAMP algebra, identity pruning, and model coverage.</summary>
public sealed class PersistentHashSetStructuralAlgebraTests
{
    /// <summary>Proves same-root algebra and relations prune before invoking policy callbacks.</summary>
    [Fact]
    public void SameRootAlgebraAndRelationsInvokeNoComparerCallbacks()
    {
        var comparer = new CountingComparer();
        var set = PersistentHashSet<Key>.CreateRange(
            Enumerable.Range(0, 1_024).Select(value => new Key(value, Scramble(value))),
            comparer);
        comparer.Reset();

        Assert.Same(set, set.Union(set));
        Assert.Same(set, set.Intersect(set));
        Assert.Empty(set.Except(set));
        Assert.Empty(set.SymmetricExcept(set));
        Assert.True(set.IsSubsetOf(set));
        Assert.False(set.IsProperSubsetOf(set));
        Assert.True(set.IsSupersetOf(set));
        Assert.False(set.IsProperSupersetOf(set));
        Assert.True(set.Overlaps(set));
        Assert.True(set.SetEquals(set));
        Assert.Equal((0, 0), (comparer.HashCalls, comparer.EqualityCalls));
    }

    /// <summary>Proves shared descendants are skipped while only divergent paths are combined.</summary>
    [Fact]
    public void SharedAncestryAlgebraPrunesUntouchedSubtreesWithoutRehashing()
    {
        var comparer = new CountingComparer();
        var basis = PersistentHashSet<Key>.CreateRange(
            Enumerable.Range(0, 4_096).Select(value => new Key(value, Scramble(value))),
            comparer);
        var leftOnly = new Key(-1, 0x1357_2468);
        var rightOnly = new Key(-2, unchecked((int)0x89ab_cdef));
        var left = basis.Add(leftOnly);
        var right = basis.Add(rightOnly);
        comparer.Reset();

        var union = left.Union(right);
        var intersection = left.Intersect(right);
        var except = left.Except(right);
        var symmetric = left.SymmetricExcept(right);

        Assert.Equal(4_098, union.Count);
        Assert.Equal(4_096, intersection.Count);
        Assert.DoesNotContain(intersection, item => item.Id < 0);
        Assert.Equal([leftOnly], except);
        Assert.Equal(new[] { leftOnly.Id, rightOnly.Id }.Order(), symmetric.Select(item => item.Id).Order());
        Assert.Equal(0, comparer.HashCalls);
        Assert.InRange(comparer.EqualityCalls, 0, 256);
    }

    /// <summary>Checks representative precedence and comparer-identity admission.</summary>
    [Fact]
    public void StructuralAlgebraRetainsReceiverRepresentativesAndRejectsForeignPolicies()
    {
        var comparer = StringComparer.OrdinalIgnoreCase;
        var leftRepresentative = new string("Alpha".ToCharArray());
        var rightRepresentative = new string("ALPHA".ToCharArray());
        var left = PersistentHashSet<string>.CreateRange([leftRepresentative, "left"], comparer);
        var right = PersistentHashSet<string>.CreateRange([rightRepresentative, "right"], comparer);

        Assert.Same(leftRepresentative, left.Union(right).Single(item => comparer.Equals(item, "alpha")));
        Assert.Same(leftRepresentative, left.Intersect(right).Single());

        var foreign = PersistentHashSet<string>.CreateRange(["alpha"], StringComparer.InvariantCultureIgnoreCase);
        Assert.Throws<ArgumentException>(() => left.Union(foreign));
        Assert.Throws<ArgumentException>(() => left.Intersect(foreign));
        Assert.Throws<ArgumentException>(() => left.Except(foreign));
        Assert.Throws<ArgumentException>(() => left.SymmetricExcept(foreign));
        Assert.False(left.IsSubsetOf(foreign));
        Assert.True(left.Overlaps(foreign));
        Assert.False(left.SetEquals(foreign));
    }

    /// <summary>Checks every operation against collision-heavy randomized mathematical models.</summary>
    [Fact]
    public void StructuralSetAndMapAlgebraMatchModelsAcrossCollisionsAndPrefixes()
    {
        var random = new Random(20260712);
        var comparer = new CountingComparer();
        for (var trial = 0; trial < 250; trial++)
        {
            var leftValues = Enumerable.Range(0, random.Next(0, 180))
                .Select(_ => random.Next(-250, 251)).ToHashSet();
            var rightValues = Enumerable.Range(0, random.Next(0, 180))
                .Select(_ => random.Next(-250, 251)).ToHashSet();
            var left = PersistentHashSet<Key>.CreateRange(
                leftValues.Select(value => new Key(value, value % 11)), comparer);
            var right = PersistentHashSet<Key>.CreateRange(
                rightValues.Select(value => new Key(value, value % 11)), comparer);

            Assert.Equal(leftValues.Union(rightValues).Order(), left.Union(right).Select(item => item.Id).Order());
            Assert.Equal(leftValues.Intersect(rightValues).Order(), left.Intersect(right).Select(item => item.Id).Order());
            Assert.Equal(leftValues.Except(rightValues).Order(), left.Except(right).Select(item => item.Id).Order());
            Assert.Equal(
                leftValues.SymmetricExceptModel(rightValues).Order(),
                left.SymmetricExcept(right).Select(item => item.Id).Order());

            Assert.Equal(leftValues.IsSubsetOf(rightValues), left.IsSubsetOf(right));
            Assert.Equal(leftValues.IsProperSubsetOf(rightValues), left.IsProperSubsetOf(right));
            Assert.Equal(leftValues.IsSupersetOf(rightValues), left.IsSupersetOf(right));
            Assert.Equal(leftValues.IsProperSupersetOf(rightValues), left.IsProperSupersetOf(right));
            Assert.Equal(leftValues.Overlaps(rightValues), left.Overlaps(right));
            Assert.Equal(leftValues.SetEquals(rightValues), left.SetEquals(right));

            var leftMap = PersistentHashMap<Key, int>.CreateRange(
                leftValues.Select(value => KeyValuePair.Create(new Key(value, value % 11), value)), comparer);
            var rightMap = PersistentHashMap<Key, int>.CreateRange(
                rightValues.Select(value => KeyValuePair.Create(new Key(value, value % 11), -value)), comparer);
            Assert.Equal(leftValues.Union(rightValues).Count(), leftMap.Union(rightMap).Count);
            Assert.Equal(leftValues.Intersect(rightValues).Count(), leftMap.Intersect(rightMap).Count);
            Assert.Equal(leftValues.Except(rightValues).Count(), leftMap.Except(rightMap).Count);
            Assert.Equal(leftValues.SymmetricExceptModel(rightValues).Count(), leftMap.SymmetricExcept(rightMap).Count);
            foreach (var value in leftValues.Intersect(rightValues))
            {
                Assert.Equal(-value, leftMap.Union(rightMap)[new Key(value, value % 11)]);
                Assert.Equal(value, leftMap.Intersect(rightMap)[new Key(value, value % 11)]);
            }
        }
    }

    private sealed record Key(int Id, int Hash);

    private sealed class CountingComparer : IEqualityComparer<Key>
    {
        /// <summary>
        /// Gets how many times the policy was asked to hash, so a test can assert an operation consulted it only as
        /// often as its bound allows.
        /// </summary>
        public int HashCalls { get; private set; }
        /// <summary>Gets how many times the policy was asked to compare.</summary>
        public int EqualityCalls { get; private set; }

        /// <summary>Determines whether both values hold the same elements.</summary>
        public bool Equals(Key? left, Key? right)
        {
            EqualityCalls++;
            return left?.Id == right?.Id;
        }

        /// <summary>Returns a hash consistent with <see cref="Equals"/>.</summary>
        public int GetHashCode(Key value)
        {
            HashCalls++;
            return value.Hash;
        }

        /// <summary>Returns the value to its initial state.</summary>
        public void Reset() => (HashCalls, EqualityCalls) = (0, 0);
    }

    private static int Scramble(int value)
    {
        var bits = unchecked((uint)value * 0x9e37_79b9u);
        bits ^= bits >> 16;
        return unchecked((int)bits);
    }
}

/// <summary>
/// Helpers turning the persistent set into a plain model the algebra tests can compare against.
/// </summary>
internal static class SetModelExtensions
{
    /// <summary>Returns the elements present in exactly one of the two models.</summary>
    internal static HashSet<T> SymmetricExceptModel<T>(this HashSet<T> left, HashSet<T> right)
    {
        var result = new HashSet<T>(left, left.Comparer);
        result.SymmetricExceptWith(right);
        return result;
    }
}
