// Shared support for the range update tests.

using Durable7.FingerTree;
using Xunit;

namespace Durable7.FingerTree.Tests;

/// <summary>
/// An affine tag used by the range-update tests. A tag denotes <c>x => Multiplier * x + Offset</c>.
/// The identity flavor is deliberately semantically irrelevant so tests can supply value-distinct identities.
/// </summary>
internal readonly record struct RangeUpdateTag(int Multiplier, int Offset, int IdentityFlavor = 0)
{
    internal static RangeUpdateTag Identity => new(1, 0);

    internal static RangeUpdateTag DistinctIdentity(int flavor) => new(1, 0, flavor);

    internal static RangeUpdateTag Add(int delta) => new(1, delta);

    internal static RangeUpdateTag Assign(int value) => new(0, value);

    internal static RangeUpdateTag ScaleAndAdd(int multiplier, int offset) => new(multiplier, offset);
}

/// <summary>
/// Ordered sequence measure containing element count, sum, and the zero-based position-weighted sum.
/// The weighted component makes <see cref="RangeUpdateAffineAlgebra.Combine"/> noncommutative.
/// </summary>
internal readonly record struct RangeUpdateMeasure(int Count, long Sum, long PositionWeightedSum);

/// <summary>Affine range action over an ordered, noncommutative measure.</summary>
internal readonly struct RangeUpdateAffineAlgebra
    : IRangeUpdateAlgebra<int, RangeUpdateMeasure, RangeUpdateTag>
{
    public static RangeUpdateMeasure Empty => default;

    public static RangeUpdateMeasure Combine(RangeUpdateMeasure left, RangeUpdateMeasure right) =>
        new(
            checked(left.Count + right.Count),
            checked(left.Sum + right.Sum),
            checked(left.PositionWeightedSum
                + right.PositionWeightedSum
                + ((long)left.Count * right.Sum)));

    public static RangeUpdateMeasure Measure(int element) => new(1, element, 0);

    public static RangeUpdateTag IdentityTag => RangeUpdateTag.Identity;

    public static bool IsIdentity(RangeUpdateTag tag) => tag.Multiplier == 1 && tag.Offset == 0;

    public static RangeUpdateTag Compose(RangeUpdateTag newer, RangeUpdateTag older)
    {
        if (IsIdentity(newer))
            return older;
        if (IsIdentity(older))
            return newer;

        return new RangeUpdateTag(
            checked(newer.Multiplier * older.Multiplier),
            checked((newer.Multiplier * older.Offset) + newer.Offset));
    }

    public static int ApplyElement(RangeUpdateTag tag, int element) =>
        checked((tag.Multiplier * element) + tag.Offset);

    public static RangeUpdateMeasure ApplyMeasure(
        RangeUpdateTag tag,
        RangeUpdateMeasure measure,
        int count)
    {
        if (count == 0)
            return Empty;

        return new RangeUpdateMeasure(
            measure.Count,
            checked(((long)tag.Multiplier * measure.Sum) + ((long)tag.Offset * count)),
            checked(((long)tag.Multiplier * measure.PositionWeightedSum)
                + ((long)tag.Offset * count * (count - 1) / 2)));
    }
}

/// <summary>Nullable replacement tag; <c>HasReplacement == false</c> is the identity.</summary>
internal readonly record struct RangeUpdateNullableTag(bool HasReplacement, string? Replacement)
{
    internal static RangeUpdateNullableTag Identity => default;

    internal static RangeUpdateNullableTag Assign(string? value) => new(true, value);
}

/// <summary>Order-sensitive diagnostic measure for nullable strings.</summary>
internal readonly record struct RangeUpdateNullableMeasure(int Count, int NonNullCount, string Projection);

/// <summary>Replacement action proving that null is an ordinary supported element and tag result.</summary>
internal readonly struct RangeUpdateNullableAlgebra
    : IRangeUpdateAlgebra<string?, RangeUpdateNullableMeasure, RangeUpdateNullableTag>
{
    public static RangeUpdateNullableMeasure Empty => new(0, 0, string.Empty);

    public static RangeUpdateNullableMeasure Combine(
        RangeUpdateNullableMeasure left,
        RangeUpdateNullableMeasure right) =>
        new(
            checked(left.Count + right.Count),
            checked(left.NonNullCount + right.NonNullCount),
            left.Projection + right.Projection);

    public static RangeUpdateNullableMeasure Measure(string? element) =>
        new(1, element is null ? 0 : 1, Token(element));

    public static RangeUpdateNullableTag IdentityTag => RangeUpdateNullableTag.Identity;

    public static bool IsIdentity(RangeUpdateNullableTag tag) => !tag.HasReplacement;

    public static RangeUpdateNullableTag Compose(
        RangeUpdateNullableTag newer,
        RangeUpdateNullableTag older) =>
        newer.HasReplacement ? newer : older;

    public static string? ApplyElement(RangeUpdateNullableTag tag, string? element) =>
        tag.HasReplacement ? tag.Replacement : element;

    public static RangeUpdateNullableMeasure ApplyMeasure(
        RangeUpdateNullableTag tag,
        RangeUpdateNullableMeasure measure,
        int count)
    {
        if (!tag.HasReplacement)
            return measure;

        return new RangeUpdateNullableMeasure(
            count,
            tag.Replacement is null ? 0 : count,
            string.Concat(Enumerable.Repeat(Token(tag.Replacement), count)));
    }

    private static string Token(string? value) => value is null ? "<null>;" : $"<{value.Length}:{value}>;";
}

/// <summary>Callback selected by the failure-atomicity tests.</summary>
internal enum RangeUpdateCallback
{
    None,
    Combine,
    Measure,
    IsIdentity,
    Compose,
    ApplyElement,
    ApplyMeasure,
}

/// <summary>Unique exception thrown by the failpoint range algebra.</summary>
internal sealed class RangeUpdateCallbackException(RangeUpdateCallback callback, int ordinal)
    : Exception($"Range-update callback {callback} failed at invocation {ordinal}.")
{
    internal RangeUpdateCallback Callback { get; } = callback;

    internal int Ordinal { get; } = ordinal;
}

/// <summary>
/// Configurable affine algebra for failure-atomicity and eager-validation tests. Only those tests use this
/// policy, and their xUnit collection disables cross-test parallelism while a failpoint is armed.
/// </summary>
internal readonly struct RangeUpdateThrowingAlgebra
    : IRangeUpdateAlgebra<int, RangeUpdateMeasure, RangeUpdateTag>
{
    private static RangeUpdateCallback _armedCallback;
    private static int _armedOrdinal;
    private static int _matchingInvocationCount;

    internal static int MatchingInvocationCount => _matchingInvocationCount;

    internal static void Disable()
    {
        _armedCallback = RangeUpdateCallback.None;
        _armedOrdinal = 0;
        _matchingInvocationCount = 0;
    }

    internal static void Arm(RangeUpdateCallback callback, int ordinal = 1)
    {
        ArgumentOutOfRangeException.ThrowIfLessThan(ordinal, 1);
        _matchingInvocationCount = 0;
        _armedOrdinal = ordinal;
        _armedCallback = callback;
    }

    public static RangeUpdateMeasure Empty => RangeUpdateAffineAlgebra.Empty;

    public static RangeUpdateMeasure Combine(RangeUpdateMeasure left, RangeUpdateMeasure right)
    {
        Hit(RangeUpdateCallback.Combine);
        return RangeUpdateAffineAlgebra.Combine(left, right);
    }

    public static RangeUpdateMeasure Measure(int element)
    {
        Hit(RangeUpdateCallback.Measure);
        return RangeUpdateAffineAlgebra.Measure(element);
    }

    public static RangeUpdateTag IdentityTag => RangeUpdateTag.Identity;

    public static bool IsIdentity(RangeUpdateTag tag)
    {
        Hit(RangeUpdateCallback.IsIdentity);
        return RangeUpdateAffineAlgebra.IsIdentity(tag);
    }

    public static RangeUpdateTag Compose(RangeUpdateTag newer, RangeUpdateTag older)
    {
        Hit(RangeUpdateCallback.Compose);
        return RangeUpdateAffineAlgebra.Compose(newer, older);
    }

    public static int ApplyElement(RangeUpdateTag tag, int element)
    {
        Hit(RangeUpdateCallback.ApplyElement);
        return RangeUpdateAffineAlgebra.ApplyElement(tag, element);
    }

    public static RangeUpdateMeasure ApplyMeasure(
        RangeUpdateTag tag,
        RangeUpdateMeasure measure,
        int count)
    {
        Hit(RangeUpdateCallback.ApplyMeasure);
        return RangeUpdateAffineAlgebra.ApplyMeasure(tag, measure, count);
    }

    private static void Hit(RangeUpdateCallback callback)
    {
        if (_armedCallback != callback)
            return;

        var ordinal = ++_matchingInvocationCount;
        if (ordinal == _armedOrdinal)
            throw new RangeUpdateCallbackException(callback, ordinal);
    }
}

/// <summary>Shared model and assertion helpers for range-update tests.</summary>
internal static class RangeUpdateAssert
{
    internal static RangeUpdateSequence<int, RangeUpdateMeasure, RangeUpdateTag, RangeUpdateAffineAlgebra>
        Create(params int[] items) =>
        RangeUpdateSequence<int, RangeUpdateMeasure, RangeUpdateTag, RangeUpdateAffineAlgebra>
            .Create(items.AsSpan());

    internal static RangeUpdateMeasure Fold(IEnumerable<int> items)
    {
        var result = RangeUpdateAffineAlgebra.Empty;
        foreach (var item in items)
            result = RangeUpdateAffineAlgebra.Combine(result, RangeUpdateAffineAlgebra.Measure(item));
        return result;
    }

    internal static void Apply(List<int> model, int index, int count, RangeUpdateTag tag)
    {
        for (var offset = 0; offset < count; offset++)
            model[index + offset] = RangeUpdateAffineAlgebra.ApplyElement(tag, model[index + offset]);
    }

    internal static RangeUpdateStructureSnapshot Matches(
        IReadOnlyList<int> expected,
        RangeUpdateSequence<int, RangeUpdateMeasure, RangeUpdateTag, RangeUpdateAffineAlgebra> actual)
    {
        Assert.Equal(expected.Count, actual.Count);
        Assert.Equal(expected.Count == 0, actual.IsEmpty);
        Assert.Equal(expected, actual.ToArray());
        Assert.Equal(Fold(expected), actual.Measure);
        for (var index = 0; index < expected.Count; index++)
            Assert.Equal(expected[index], actual[index]);

        return RangeUpdateDiagnosticsAdapter.Validate(actual, static (left, right) => left == right);
    }

    internal static void AllRangesMatch(
        IReadOnlyList<int> expected,
        RangeUpdateSequence<int, RangeUpdateMeasure, RangeUpdateTag, RangeUpdateAffineAlgebra> actual)
    {
        for (var index = 0; index <= expected.Count; index++)
        {
            for (var count = 0; count <= expected.Count - index; count++)
                Assert.Equal(Fold(expected.Skip(index).Take(count)), actual.MeasureRange(index, count));
        }
    }
}
