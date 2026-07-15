using Xunit;

namespace Tools.DataStructures.Ordered.Tests;

/// <summary>Receiver-ordered algebra, normalization, representative, relation, and eagerness tests.</summary>
public sealed class PersistentOrderedSetAlgebraAndRelationTests
{
    /// <summary>Verifies all algebra operations use their specified deterministic order and representatives.</summary>
    [Fact]
    public void Algebra_UsesReceiverOrderAndRepresentativePrecedence()
    {
        var comparer = new RepresentativeComparer();
        var receiverA = new Representative(1, "receiver-a");
        var receiverB = new Representative(2, "receiver-b");
        var receiverC = new Representative(3, "receiver-c");
        var argumentA = new Representative(1, "argument-a");
        var argumentD = new Representative(4, "argument-d");
        var receiver = PersistentOrderedSet<Representative>.CreateRange(
            [receiverA, receiverB, receiverC], comparer);
        var argument = PersistentOrderedSet<Representative>.CreateRange([argumentA, argumentD], comparer);

        AssertAlgebraResults(receiver, argument, receiverA, receiverB, receiverC, argumentD);
        AssertAlgebraResults(receiver, (IEnumerable<Representative>)new[] { argumentA, argumentD },
            receiverA, receiverB, receiverC, argumentD);
        OrderedSetAssert.Matches(new[] { receiverA, receiverB, receiverC }, receiver);
        OrderedSetAssert.Matches(new[] { argumentA, argumentD }, argument);
    }

    /// <summary>Verifies foreign-policy arguments collapse in enumeration order under receiver policy.</summary>
    [Fact]
    public void MismatchedComparer_NormalizesAndUsesFirstArgumentRepresentative()
    {
        var receiverComparer = new RepresentativeComparer();
        var foreignComparer = new ReferenceRepresentativeComparer();
        var receiverOnly = new Representative(9, "receiver-only");
        var firstAlpha = new Representative(1, "first-alpha");
        var laterAlpha = new Representative(1, "later-alpha");
        var delta = new Representative(4, "delta");
        var receiver = PersistentOrderedSet<Representative>.Create(receiverComparer).Add(receiverOnly);
        var argument = PersistentOrderedSet<Representative>.CreateRange(
            [firstAlpha, laterAlpha, delta], foreignComparer);

        var union = receiver.Union(argument);
        var symmetric = receiver.SymmetricExcept(argument);
        OrderedSetAssert.Matches(new[] { receiverOnly, firstAlpha, delta }, union);
        OrderedSetAssert.Matches(new[] { receiverOnly, firstAlpha, delta }, symmetric);
        Assert.True(union.TryGetValue(laterAlpha, out var stored));
        Assert.Same(firstAlpha, stored);
        Assert.Same(receiverComparer, union.Comparer);
    }

    /// <summary>Verifies argument membership policy is never consulted after a same-type argument is constructed.</summary>
    [Fact]
    public void SameTypeOperations_DoNotDelegateMembershipToArgumentComparer()
    {
        var receiverComparer = new RepresentativeComparer();
        var foreignComparer = new ReferenceRepresentativeComparer();
        var receiverItem = new Representative(1, "receiver");
        var equalArgument = new Representative(1, "equal-argument");
        var newArgument = new Representative(2, "new-argument");
        var receiver = PersistentOrderedSet<Representative>.Create(receiverComparer).Add(receiverItem);
        var argument = PersistentOrderedSet<Representative>.CreateRange(
            [equalArgument, newArgument], foreignComparer);
        foreignComparer.ThrowFromHash = true;
        foreignComparer.ThrowFromEquals = true;

        OrderedSetAssert.Matches(new[] { receiverItem, newArgument }, receiver.Union(argument));
        OrderedSetAssert.Matches(new[] { receiverItem }, receiver.Intersect(argument));
        OrderedSetAssert.Matches(Array.Empty<Representative>(), receiver.Except(argument));
        OrderedSetAssert.Matches(new[] { newArgument }, receiver.SymmetricExcept(argument));
        Assert.True(receiver.IsSubsetOf(argument));
        Assert.True(receiver.IsProperSubsetOf(argument));
        Assert.False(receiver.IsSupersetOf(argument));
        Assert.False(receiver.IsProperSupersetOf(argument));
        Assert.True(receiver.Overlaps(argument));
        Assert.False(receiver.SetEquals(argument));
    }

    /// <summary>Verifies every logical algebra no-op returns the receiver after normalization.</summary>
    [Fact]
    public void Algebra_LogicalNoOpsPreserveReferenceIdentity()
    {
        var comparer = new RepresentativeComparer();
        var alpha = new Representative(1, "alpha");
        var beta = new Representative(2, "beta");
        var set = PersistentOrderedSet<Representative>.CreateRange([alpha, beta], comparer);
        var empty = PersistentOrderedSet<Representative>.Create(comparer);
        var subset = PersistentOrderedSet<Representative>.Create(comparer)
            .Add(new Representative(1, "subset-alpha"));
        var superset = PersistentOrderedSet<Representative>.CreateRange(
            [new Representative(1, "other-alpha"), new Representative(2, "other-beta"), new Representative(3, "gamma")],
            comparer);
        var disjoint = PersistentOrderedSet<Representative>.Create(comparer)
            .Add(new Representative(9, "disjoint"));

        Assert.Same(set, set.Union(set));
        Assert.Same(set, set.Intersect(set));
        Assert.Same(set, set.Union(empty));
        Assert.Same(set, set.SymmetricExcept(empty));
        Assert.Same(set, set.Union(subset));
        Assert.Same(set, set.Intersect(superset));
        Assert.Same(set, set.Except(disjoint));

        var selfDifference = set.Except(set);
        var selfSymmetric = set.SymmetricExcept(set);
        Assert.True(selfDifference.IsEmpty);
        Assert.True(selfSymmetric.IsEmpty);
        Assert.Same(comparer, selfDifference.Comparer);
        Assert.Same(comparer, selfSymmetric.Comparer);
        Assert.NotSame(PersistentOrderedSet<Representative>.Empty, selfDifference);
        Assert.NotSame(PersistentOrderedSet<Representative>.Empty, selfSymmetric);
    }

    /// <summary>Verifies enumerable duplicates collapse once under receiver policy in first-occurrence order.</summary>
    [Fact]
    public void EnumerableAlgebra_CollapsesDuplicatesAndSupportsNull()
    {
        var comparer = new RepresentativeComparer();
        var receiver = new Representative(1, "receiver");
        var firstNew = new Representative(2, "first-new");
        var laterNew = new Representative(2, "later-new");
        var set = PersistentOrderedSet<Representative?>.Create(comparer).Add(receiver);
        Representative?[] argument = [firstNew, laterNew, null, null];

        var union = set.Union(argument);
        OrderedSetAssert.Matches<Representative?>([receiver, firstNew, null], union);
        var symmetric = set.SymmetricExcept(argument);
        OrderedSetAssert.Matches<Representative?>([receiver, firstNew, null], symmetric);
    }

    /// <summary>Verifies all operations eagerly observe receiver comparer failures during normalization.</summary>
    [Fact]
    public void Normalization_ComparerFailuresAreEagerForAlgebraAndRelations()
    {
        var comparer = new SwitchableRepresentativeComparer();
        var sourceItem = new Representative(1, "source");
        var argumentItem = new Representative(2, "argument");
        var source = PersistentOrderedSet<Representative>.Create(comparer).Add(sourceItem);
        var argument = new[] { argumentItem };
        comparer.ThrowFromHash = true;

        foreach (var operation in AllAlgebraAndRelations(source, argument))
        {
            var actual = Assert.Throws<ComparerCallbackException>(operation);
            Assert.Same(comparer.Failure, actual);
        }

        comparer.ThrowFromHash = false;
        OrderedSetAssert.Matches(new[] { sourceItem }, source);
    }

    /// <summary>Verifies equality failures during collision-heavy normalization are eager too.</summary>
    [Fact]
    public void Normalization_EqualityFailuresAreEagerForAlgebraAndRelations()
    {
        var comparer = new SwitchableRepresentativeComparer();
        var source = PersistentOrderedSet<Representative>.Create(comparer);
        var argument = new[]
        {
            new Representative(1, "first"),
            new Representative(2, "second"),
        };
        comparer.ThrowFromEquals = true;

        foreach (var operation in AllAlgebraAndRelations(source, argument))
        {
            var actual = Assert.Throws<ComparerCallbackException>(operation);
            Assert.Same(comparer.Failure, actual);
        }

        comparer.ThrowFromEquals = false;
        OrderedSetAssert.Matches(Array.Empty<Representative>(), source);
    }

    /// <summary>Verifies apparent short-circuits do not suppress later enumerable failures.</summary>
    [Fact]
    public void Normalization_EnumeratesTheWholeArgumentBeforeAlgebraOrRelations()
    {
        var source = PersistentOrderedSet<int>.CreateRange([1, 2]);
        var failure = new EnumerationCallbackException();
        foreach (var operation in AllAlgebraAndRelations(source, ThrowAfterOverlap(failure)))
        {
            var actual = Assert.Throws<EnumerationCallbackException>(operation);
            Assert.Same(failure, actual);
        }
        OrderedSetAssert.Matches(new[] { 1, 2 }, source);
    }

    /// <summary>Exhaustively verifies all six relations over small normalized sets.</summary>
    [Fact]
    public void Relations_ExhaustiveSmallTruthTableMatchesMathematicalSets()
    {
        const int universe = 5;
        for (var receiverMask = 0; receiverMask < 1 << universe; receiverMask++)
        {
            var receiverItems = Items(receiverMask, universe);
            var receiver = PersistentOrderedSet<int>.CreateRange(receiverItems);
            for (var argumentMask = 0; argumentMask < 1 << universe; argumentMask++)
            {
                var distinctArgument = Items(argumentMask, universe);
                var argumentWithDuplicates = distinctArgument.SelectMany(item => new[] { item, item }).ToArray();
                var receiverOnly = receiverMask & ~argumentMask;
                var argumentOnly = argumentMask & ~receiverMask;

                Assert.Equal(receiverOnly == 0, receiver.IsSubsetOf(argumentWithDuplicates));
                Assert.Equal(receiverOnly == 0 && argumentOnly != 0, receiver.IsProperSubsetOf(argumentWithDuplicates));
                Assert.Equal(argumentOnly == 0, receiver.IsSupersetOf(argumentWithDuplicates));
                Assert.Equal(argumentOnly == 0 && receiverOnly != 0, receiver.IsProperSupersetOf(argumentWithDuplicates));
                Assert.Equal((receiverMask & argumentMask) != 0, receiver.Overlaps(argumentWithDuplicates));
                Assert.Equal(receiverMask == argumentMask, receiver.SetEquals(argumentWithDuplicates));
            }
        }
    }

    /// <summary>Verifies receiver-policy duplicate collapse controls proper relation cardinality.</summary>
    [Fact]
    public void Relations_MismatchedArgumentClassesCollapseUnderReceiverComparer()
    {
        var receiverComparer = new RepresentativeComparer();
        var foreignComparer = new ReferenceRepresentativeComparer();
        var receiverRepresentative = new Representative(1, "receiver");
        var firstForeign = new Representative(1, "first-foreign");
        var secondForeign = new Representative(1, "second-foreign");
        var receiver = PersistentOrderedSet<Representative>.Create(receiverComparer).Add(receiverRepresentative);
        var foreign = PersistentOrderedSet<Representative>.CreateRange(
            [firstForeign, secondForeign], foreignComparer);

        Assert.True(receiver.IsSubsetOf(foreign));
        Assert.False(receiver.IsProperSubsetOf(foreign));
        Assert.True(receiver.IsSupersetOf(foreign));
        Assert.False(receiver.IsProperSupersetOf(foreign));
        Assert.True(receiver.Overlaps(foreign));
        Assert.True(receiver.SetEquals(foreign));
    }

    /// <summary>Verifies every algebra and relation member rejects null before doing any work.</summary>
    [Fact]
    public void AlgebraAndRelations_RejectNullOperands()
    {
        var source = PersistentOrderedSet<int>.Empty.Add(1);
        Assert.Throws<ArgumentNullException>(() => source.Union((PersistentOrderedSet<int>)null!));
        Assert.Throws<ArgumentNullException>(() => source.Intersect((PersistentOrderedSet<int>)null!));
        Assert.Throws<ArgumentNullException>(() => source.Except((PersistentOrderedSet<int>)null!));
        Assert.Throws<ArgumentNullException>(() => source.SymmetricExcept((PersistentOrderedSet<int>)null!));
        Assert.Throws<ArgumentNullException>(() => source.Union((IEnumerable<int>)null!));
        Assert.Throws<ArgumentNullException>(() => source.Intersect((IEnumerable<int>)null!));
        Assert.Throws<ArgumentNullException>(() => source.Except((IEnumerable<int>)null!));
        Assert.Throws<ArgumentNullException>(() => source.SymmetricExcept((IEnumerable<int>)null!));
        Assert.Throws<ArgumentNullException>(() => source.IsSubsetOf(null!));
        Assert.Throws<ArgumentNullException>(() => source.IsProperSubsetOf(null!));
        Assert.Throws<ArgumentNullException>(() => source.IsSupersetOf(null!));
        Assert.Throws<ArgumentNullException>(() => source.IsProperSupersetOf(null!));
        Assert.Throws<ArgumentNullException>(() => source.Overlaps(null!));
        Assert.Throws<ArgumentNullException>(() => source.SetEquals(null!));
    }

    private static void AssertAlgebraResults(
        PersistentOrderedSet<Representative> receiver,
        PersistentOrderedSet<Representative> argument,
        Representative receiverA,
        Representative receiverB,
        Representative receiverC,
        Representative argumentD)
    {
        OrderedSetAssert.Matches(new[] { receiverA, receiverB, receiverC, argumentD }, receiver.Union(argument));
        OrderedSetAssert.Matches(new[] { receiverA }, receiver.Intersect(argument));
        OrderedSetAssert.Matches(new[] { receiverB, receiverC }, receiver.Except(argument));
        OrderedSetAssert.Matches(new[] { receiverB, receiverC, argumentD }, receiver.SymmetricExcept(argument));
    }

    private static void AssertAlgebraResults(
        PersistentOrderedSet<Representative> receiver,
        IEnumerable<Representative> argument,
        Representative receiverA,
        Representative receiverB,
        Representative receiverC,
        Representative argumentD)
    {
        OrderedSetAssert.Matches(new[] { receiverA, receiverB, receiverC, argumentD }, receiver.Union(argument));
        OrderedSetAssert.Matches(new[] { receiverA }, receiver.Intersect(argument));
        OrderedSetAssert.Matches(new[] { receiverB, receiverC }, receiver.Except(argument));
        OrderedSetAssert.Matches(new[] { receiverB, receiverC, argumentD }, receiver.SymmetricExcept(argument));
    }

    private static IEnumerable<Func<object?>> AllAlgebraAndRelations<T>(
        PersistentOrderedSet<T> source,
        IEnumerable<T> argument) =>
        [
            () => source.Union(argument),
            () => source.Intersect(argument),
            () => source.Except(argument),
            () => source.SymmetricExcept(argument),
            () => source.IsSubsetOf(argument),
            () => source.IsProperSubsetOf(argument),
            () => source.IsSupersetOf(argument),
            () => source.IsProperSupersetOf(argument),
            () => source.Overlaps(argument),
            () => source.SetEquals(argument),
        ];

    private static IEnumerable<int> ThrowAfterOverlap(Exception failure)
    {
        yield return 1;
        throw failure;
    }

    private static int[] Items(int mask, int universe) =>
        Enumerable.Range(0, universe).Where(item => (mask & (1 << item)) != 0).ToArray();
}
