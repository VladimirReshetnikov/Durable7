// Tests for the persistent hash bag algebra.

using Xunit;

namespace Durable7.Hamt.Tests;

/// <summary>Multiset algebra, policy normalization, representative, and failure tests for persistent hash bags.</summary>
public sealed class PersistentHashBagAlgebraTests
{
    /// <summary>Verifies maximum, minimum, saturated-difference, and additive multiplicity tables.</summary>
    [Fact]
    public void Algebra_UsesConventionalMultisetMultiplicityTables()
    {
        var comparer = new OrdinalComparer();
        var left = PersistentHashBag<string>.Create(comparer)
            .AddCopies("a", 2)
            .AddCopies("b", 5)
            .Add("c");
        var right = PersistentHashBag<string>.Create(comparer)
            .AddCopies("a", 4)
            .AddCopies("b", 3)
            .AddCopies("d", 2);

        AssertMultiplicities(left.Union(right), ("a", 4), ("b", 5), ("c", 1), ("d", 2));
        AssertMultiplicities(left.Intersect(right), ("a", 2), ("b", 3));
        AssertMultiplicities(left.Except(right), ("b", 2), ("c", 1));
        AssertMultiplicities(left.Sum(right), ("a", 6), ("b", 8), ("c", 1), ("d", 2));
    }

    /// <summary>Verifies receiver representatives win and argument representatives enter only absent classes.</summary>
    [Fact]
    public void Algebra_UsesReceiverAndArgumentRepresentativePrecedence()
    {
        var comparer = StringComparer.OrdinalIgnoreCase;
        var receiverAlpha = NewString("Alpha");
        var argumentAlpha = NewString("ALPHA");
        var receiverOnly = NewString("Receiver");
        var argumentOnly = NewString("Argument");
        var receiver = PersistentHashBag<string>.Create(comparer)
            .AddCopies(receiverAlpha, 3)
            .Add(receiverOnly);
        var argument = PersistentHashBag<string>.Create(comparer)
            .Add(argumentAlpha)
            .AddCopies(argumentOnly, 2);

        var union = receiver.Union(argument);
        var intersection = receiver.Intersect(argument);
        var difference = receiver.Except(argument);
        var sum = receiver.Sum(argument);

        AssertStoredRepresentative(union, "alpha", receiverAlpha);
        AssertStoredRepresentative(intersection, "alpha", receiverAlpha);
        AssertStoredRepresentative(difference, "alpha", receiverAlpha);
        AssertStoredRepresentative(sum, "alpha", receiverAlpha);
        AssertStoredRepresentative(union, "argument", argumentOnly);
        AssertStoredRepresentative(sum, "argument", argumentOnly);
        Assert.False(intersection.Contains(argumentOnly));
        Assert.False(difference.Contains(argumentOnly));
        Assert.Equal(2, difference.CountOf("ALPHA"));
    }

    /// <summary>Verifies self algebra and every logical no-op preserve the required receiver identity.</summary>
    [Fact]
    public void Algebra_SelfAndLogicalNoOpsHaveSpecifiedIdentity()
    {
        var comparer = new OrdinalComparer();
        var bag = PersistentHashBag<string>.Create(comparer)
            .AddCopies("alpha", 2)
            .Add("beta");
        var empty = PersistentHashBag<string>.Create(comparer);
        var smaller = empty.Add("alpha");
        var superset = bag.Add("gamma").Add("alpha");
        var disjoint = empty.Add("other");

        Assert.Same(bag, bag.Union(bag));
        Assert.Same(bag, bag.Intersect(bag));
        Assert.Same(bag, bag.Union(empty));
        Assert.Same(bag, bag.Sum(empty));
        Assert.Same(bag, bag.Union(smaller));
        Assert.Same(bag, bag.Intersect(superset));
        Assert.Same(bag, bag.Except(disjoint));

        var selfDifference = bag.Except(bag);
        Assert.True(selfDifference.IsEmpty);
        Assert.Same(comparer, selfDifference.Comparer);
        Assert.NotSame(PersistentHashBag<string>.Empty, selfDifference);

        var doubled = bag.Sum(bag);
        Assert.Equal(4, doubled.CountOf("alpha"));
        Assert.Equal(2, doubled.CountOf("beta"));
        Assert.NotSame(bag, doubled);
    }

    /// <summary>Verifies a foreign comparer is normalized under receiver policy with checked count collapse.</summary>
    [Fact]
    public void MismatchedComparer_NormalizesCountsAndUsesFirstObservedArgumentRepresentative()
    {
        var receiverComparer = new ConstantHashIgnoreCaseComparer();
        var firstAlpha = NewString("alpha");
        var secondAlpha = NewString("ALPHA");
        var beta = NewString("beta");
        var argument = PersistentHashBag<string>.Create(StringComparer.Ordinal)
            .AddCopies(firstAlpha, 2)
            .AddCopies(secondAlpha, 3)
            .Add(beta);
        var observedAlpha = argument.Entries
            .First(entry => receiverComparer.Equals(entry.Key, "alpha"))
            .Key;
        var receiver = PersistentHashBag<string>.Create(receiverComparer);

        var union = receiver.Union(argument);
        var sum = receiver.Sum(argument);

        Assert.Same(receiverComparer, union.Comparer);
        AssertMultiplicities(union, ("alpha", 5), ("beta", 1));
        AssertMultiplicities(sum, ("alpha", 5), ("beta", 1));
        AssertStoredRepresentative(union, "alpha", observedAlpha);
        AssertStoredRepresentative(sum, "alpha", observedAlpha);
    }

    /// <summary>Verifies comparer-mismatch normalization is eager even when the answer could be short-circuited.</summary>
    [Fact]
    public void MismatchedComparer_NormalizationOverflowIsEagerForEveryOperation()
    {
        var receiver = PersistentHashBag<string>.Create(new ConstantHashIgnoreCaseComparer());
        var argument = PersistentHashBag<string>.Create(StringComparer.Ordinal)
            .AddCopies("alpha", int.MaxValue)
            .Add("ALPHA");
        var receiverRoot = receiver.RootForTesting;
        var argumentRoot = argument.RootForTesting;

        foreach (var operation in AllOperations)
            Assert.Throws<OverflowException>(() => Apply(operation, receiver, argument));

        Assert.Same(receiverRoot, receiver.RootForTesting);
        Assert.Same(argumentRoot, argument.RootForTesting);
        Assert.True(receiver.IsEmpty);
        Assert.Equal(2L + int.MaxValue - 1L, argument.TotalCount);
        Assert.Equal(int.MaxValue, argument.CountOf("alpha"));
        Assert.Equal(1, argument.CountOf("ALPHA"));
    }

    /// <summary>Verifies receiver comparer exceptions remain observable through eager normalization.</summary>
    [Fact]
    public void MismatchedComparer_ComparerFailuresAreEagerForEveryOperation()
    {
        var comparer = new SwitchableThrowingIgnoreCaseComparer();
        var receiver = PersistentHashBag<string>.Create(comparer);
        var argument = PersistentHashBag<string>.Create(StringComparer.Ordinal).Add("alpha");
        comparer.ThrowFromGetHashCode = true;

        foreach (var operation in AllOperations)
        {
            var failure = Assert.Throws<ComparerCallbackException>(
                () => Apply(operation, receiver, argument));
            Assert.Same(comparer.Failure, failure);
        }

        Assert.True(receiver.IsEmpty);
        Assert.Equal(1, argument.CountOf("alpha"));
    }

    /// <summary>Verifies equality failures during foreign-policy collapse are also eager.</summary>
    [Fact]
    public void MismatchedComparer_EqualityFailuresAreEagerForEveryOperation()
    {
        var comparer = new SwitchableThrowingIgnoreCaseComparer();
        var receiver = PersistentHashBag<string>.Create(comparer);
        var argument = PersistentHashBag<string>.Create(StringComparer.Ordinal)
            .Add("alpha")
            .Add("beta");
        comparer.ThrowFromEquals = true;

        foreach (var operation in AllOperations)
        {
            var failure = Assert.Throws<ComparerCallbackException>(
                () => Apply(operation, receiver, argument));
            Assert.Same(comparer.Failure, failure);
        }

        comparer.ThrowFromEquals = false;
        Assert.True(receiver.IsEmpty);
        Assert.Equal(2, argument.TotalCount);
    }

    /// <summary>Verifies mismatched-policy logical no-ops still return the receiver after normalization.</summary>
    [Fact]
    public void MismatchedComparer_LogicalNoOpsReturnReceiverAfterNormalization()
    {
        var receiverComparer = new ConstantHashIgnoreCaseComparer();
        var receiver = PersistentHashBag<string>.Create(receiverComparer)
            .AddCopies("Alpha", 5)
            .Add("Beta");
        var smaller = PersistentHashBag<string>.Create(StringComparer.Ordinal)
            .AddCopies("ALPHA", 3);
        var equalOrLarger = PersistentHashBag<string>.Create(StringComparer.Ordinal)
            .AddCopies("ALPHA", 5)
            .AddCopies("BETA", 2);
        var disjoint = PersistentHashBag<string>.Create(StringComparer.Ordinal).Add("gamma");
        var foreignEmpty = PersistentHashBag<string>.Create(StringComparer.Ordinal);

        Assert.Same(receiver, receiver.Union(smaller));
        Assert.Same(receiver, receiver.Intersect(equalOrLarger));
        Assert.Same(receiver, receiver.Except(disjoint));
        Assert.Same(receiver, receiver.Sum(foreignEmpty));
    }

    /// <summary>Verifies additive overflow is failure-atomic for both immutable operands.</summary>
    [Fact]
    public void Sum_PerClassOverflowLeavesBothOperandsUnchanged()
    {
        var comparer = new OrdinalComparer();
        var left = PersistentHashBag<string>.Create(comparer).AddCopies("alpha", int.MaxValue);
        var right = PersistentHashBag<string>.Create(comparer).Add("alpha");
        var leftRoot = left.RootForTesting;
        var rightRoot = right.RootForTesting;

        Assert.Throws<OverflowException>(() => left.Sum(right));

        Assert.Same(leftRoot, left.RootForTesting);
        Assert.Same(rightRoot, right.RootForTesting);
        Assert.Equal(int.MaxValue, left.CountOf("alpha"));
        Assert.Equal(1, right.CountOf("alpha"));
        left.ValidateCanonicalityForDiagnostics();
        right.ValidateCanonicalityForDiagnostics();
    }

    /// <summary>Verifies empty algebra results retain the receiver comparer and singleton rule.</summary>
    [Fact]
    public void EmptyAlgebraResults_PreserveReceiverComparerIdentity()
    {
        var comparer = new OrdinalComparer();
        var custom = PersistentHashBag<string>.Create(comparer).Add("alpha");
        var customEmpty = PersistentHashBag<string>.Create(comparer);

        foreach (var result in new[] { custom.Except(custom), custom.Intersect(customEmpty) })
        {
            Assert.True(result.IsEmpty);
            Assert.Same(comparer, result.Comparer);
            Assert.NotSame(PersistentHashBag<string>.Empty, result);
        }

        Assert.Same(
            PersistentHashBag<string>.Empty,
            PersistentHashBag<string>.Empty.Add("alpha").Except(PersistentHashBag<string>.Empty.Add("alpha")));
    }

    /// <summary>Verifies every algebra member rejects a null operand before doing any work.</summary>
    [Fact]
    public void Algebra_RejectsNullOperands()
    {
        var bag = PersistentHashBag<string>.Empty.Add("alpha");

        Assert.Throws<ArgumentNullException>(() => bag.Union(null!));
        Assert.Throws<ArgumentNullException>(() => bag.Intersect(null!));
        Assert.Throws<ArgumentNullException>(() => bag.Except(null!));
        Assert.Throws<ArgumentNullException>(() => bag.Sum(null!));
    }

    private static readonly BagOperation[] AllOperations =
    [
        BagOperation.Union,
        BagOperation.Intersect,
        BagOperation.Except,
        BagOperation.Sum,
    ];

    private static PersistentHashBag<string> Apply(
        BagOperation operation,
        PersistentHashBag<string> left,
        PersistentHashBag<string> right) =>
        operation switch
        {
            BagOperation.Union => left.Union(right),
            BagOperation.Intersect => left.Intersect(right),
            BagOperation.Except => left.Except(right),
            BagOperation.Sum => left.Sum(right),
            _ => throw new ArgumentOutOfRangeException(nameof(operation)),
        };

    private static void AssertMultiplicities(
        PersistentHashBag<string> bag,
        params (string Item, int Count)[] expected)
    {
        Assert.Equal(expected.Length, bag.DistinctCount);
        Assert.Equal(expected.Sum(pair => (long)pair.Count), bag.TotalCount);
        foreach (var (item, count) in expected)
            Assert.Equal(count, bag.CountOf(item));
        bag.ValidateCanonicalityForDiagnostics();
    }

    private static void AssertStoredRepresentative(
        PersistentHashBag<string> bag,
        string lookup,
        string expected)
    {
        Assert.True(bag.TryGetValue(lookup, out var actual));
        Assert.Same(expected, actual);
    }

    private static string NewString(string value) => new(value.ToCharArray());

    private enum BagOperation
    {
        Union,
        Intersect,
        Except,
        Sum,
    }

    private sealed class OrdinalComparer : IEqualityComparer<string>
    {
        public bool Equals(string? left, string? right) => StringComparer.Ordinal.Equals(left, right);

        public int GetHashCode(string value) => StringComparer.Ordinal.GetHashCode(value);
    }

    private sealed class ConstantHashIgnoreCaseComparer : IEqualityComparer<string>
    {
        public bool Equals(string? left, string? right) =>
            StringComparer.OrdinalIgnoreCase.Equals(left, right);

        public int GetHashCode(string value) => 0;
    }

    private sealed class SwitchableThrowingIgnoreCaseComparer : IEqualityComparer<string>
    {
        internal ComparerCallbackException Failure { get; } = new();

        internal bool ThrowFromGetHashCode { get; set; }

        internal bool ThrowFromEquals { get; set; }

        public bool Equals(string? left, string? right)
        {
            if (ThrowFromEquals)
                throw Failure;
            return StringComparer.OrdinalIgnoreCase.Equals(left, right);
        }

        public int GetHashCode(string value)
        {
            if (ThrowFromGetHashCode)
                throw Failure;
            return 0;
        }
    }

    private sealed class ComparerCallbackException : Exception
    {
    }
}
