// Tests for the range update failure and concurrency.

using Durable7.FingerTree;
using Xunit;

namespace Durable7.FingerTree.Tests;

/// <summary>Serializes tests that arm the static failpoint algebra.</summary>
[CollectionDefinition("Range-update static policy failpoints", DisableParallelization = true)]
public sealed class RangeUpdateFailureCollection
{
    /// <summary>The xUnit collection name shared by failpoint tests.</summary>
    public const string Name = "Range-update static policy failpoints";
}

/// <summary>Callback-failure atomicity, validation precedence, empty-query, and concurrent-read contracts.</summary>
[Collection(RangeUpdateFailureCollection.Name)]
public sealed class RangeUpdateFailureAndConcurrencyTests
{
    private enum StructuralOperation
    {
        SetItem,
        RemoveAt,
        SplitAt,
        Concat,
        GetRange,
        ApplyRange,
    }

    /// <summary>Fails every observed policy callback during a whole-range update and preserves the source.</summary>
    /// <param name="callbackValue">The callback enum value encoded as an integer for xUnit data.</param>
    [Theory]
    [InlineData((int)RangeUpdateCallback.IsIdentity)]
    [InlineData((int)RangeUpdateCallback.ApplyElement)]
    [InlineData((int)RangeUpdateCallback.ApplyMeasure)]
    public void WholeRangeUpdate_EveryObservedCallbackFailureIsAtomic(int callbackValue)
    {
        RangeUpdateThrowingAlgebra.Disable();
        var expected = Enumerable.Range(0, 64).ToArray();
        var source = CreateThrowing(expected);
        AssertEveryInvocationFailsAtomically(
            source,
            expected,
            (RangeUpdateCallback)callbackValue,
            () => source.ApplyRange(0, source.Count, RangeUpdateTag.Add(3)));

        AssertThrowingMatches(expected, source);
        AssertThrowingMatches(expected.Select(value => value + 3).ToArray(),
            source.ApplyRange(0, source.Count, RangeUpdateTag.Add(3)));
    }

    /// <summary>Fails every observed composition callback when stacking a newer tag over a pending older tag.</summary>
    [Fact]
    public void PendingTagComposition_EveryObservedFailureIsAtomic()
    {
        RangeUpdateThrowingAlgebra.Disable();
        var initial = Enumerable.Range(0, 64).ToArray();
        var source = CreateThrowing(initial).ApplyRange(0, initial.Length, RangeUpdateTag.Assign(7));
        var expected = Enumerable.Repeat(7, initial.Length).ToArray();

        AssertEveryInvocationFailsAtomically(
            source,
            expected,
            RangeUpdateCallback.Compose,
            () => source.ApplyRange(0, source.Count, RangeUpdateTag.Add(5)));

        AssertThrowingMatches(expected, source);
        AssertThrowingMatches(Enumerable.Repeat(12, initial.Length).ToArray(),
            source.ApplyRange(0, source.Count, RangeUpdateTag.Add(5)));
    }

    /// <summary>Fails every observed element-measure and combine callback during a middle insertion.</summary>
    /// <param name="callbackValue">The callback enum value encoded as an integer for xUnit data.</param>
    [Theory]
    [InlineData((int)RangeUpdateCallback.Measure)]
    [InlineData((int)RangeUpdateCallback.Combine)]
    [InlineData((int)RangeUpdateCallback.ApplyElement)]
    [InlineData((int)RangeUpdateCallback.ApplyMeasure)]
    [InlineData((int)RangeUpdateCallback.Compose)]
    public void InsertThroughPendingTags_EveryObservedCallbackFailureIsAtomicWhenInvoked(int callbackValue)
    {
        RangeUpdateThrowingAlgebra.Disable();
        var expected = Enumerable.Range(0, 81).Select(value => value + 2).ToArray();
        var source = CreateThrowing(Enumerable.Range(0, 81).ToArray())
            .ApplyRange(0, 81, RangeUpdateTag.Add(2));
        var callback = (RangeUpdateCallback)callbackValue;

        AssertEveryInvocationFailsAtomically(
            source,
            expected,
            callback,
            () => source.Insert(37, 999),
            requireInvocation: callback is RangeUpdateCallback.Measure or RangeUpdateCallback.Combine);

        AssertThrowingMatches(expected, source);
        var result = source.Insert(37, 999);
        AssertThrowingMatches(expected.Take(37).Append(999).Concat(expected.Skip(37)).ToArray(), result);
    }

    /// <summary>
    /// Sweeps every observed ordinal of every policy callback across the split, join, point-edit,
    /// extraction, concatenation, and proper-range update paths.
    /// </summary>
    /// <param name="operationValue">The structural operation encoded as an integer for xUnit data.</param>
    [Theory]
    [InlineData((int)StructuralOperation.SetItem)]
    [InlineData((int)StructuralOperation.RemoveAt)]
    [InlineData((int)StructuralOperation.SplitAt)]
    [InlineData((int)StructuralOperation.Concat)]
    [InlineData((int)StructuralOperation.GetRange)]
    [InlineData((int)StructuralOperation.ApplyRange)]
    public void StructuralOperations_EveryObservedCallbackFailureIsAtomic(int operationValue)
    {
        RangeUpdateThrowingAlgebra.Disable();
        var initial = Enumerable.Range(0, 31).ToArray();
        var expectedSource = initial.Select(static value => value + 2).ToArray();
        var source = CreateThrowing(initial).ApplyRange(0, initial.Length, RangeUpdateTag.Add(2));
        var operationKind = (StructuralOperation)operationValue;

        Action? assertAdditionalInputs = null;
        IReadOnlyList<int> expectedResult;
        Func<RangeUpdateSequence<int, RangeUpdateMeasure, RangeUpdateTag, RangeUpdateThrowingAlgebra>> operation;

        switch (operationKind)
        {
            case StructuralOperation.SetItem:
            {
                var model = expectedSource.ToArray();
                model[15] = 999;
                expectedResult = model;
                operation = () => source.SetItem(15, 999);
                break;
            }

            case StructuralOperation.RemoveAt:
                expectedResult = expectedSource.Where((_, index) => index != 15).ToArray();
                operation = () => source.RemoveAt(15);
                break;

            case StructuralOperation.SplitAt:
                expectedResult = expectedSource.Take(13).ToArray();
                operation = () => source.SplitAt(13).Left;
                break;

            case StructuralOperation.Concat:
            {
                var otherInitial = Enumerable.Range(100, 17).ToArray();
                var concatOther = CreateThrowing(otherInitial)
                    .ApplyRange(0, otherInitial.Length, RangeUpdateTag.Add(-4));
                var otherExpected = otherInitial.Select(static value => value - 4).ToArray();
                expectedResult = expectedSource.Concat(otherExpected).ToArray();
                operation = () => source.Concat(concatOther);
                assertAdditionalInputs = () => AssertThrowingMatches(otherExpected, concatOther);
                break;
            }

            case StructuralOperation.GetRange:
                expectedResult = expectedSource.Skip(7).Take(17).ToArray();
                operation = () => source.GetRange(7, 17);
                break;

            case StructuralOperation.ApplyRange:
            {
                var model = expectedSource.ToArray();
                Array.Fill(model, -8, 6, 19);
                expectedResult = model;
                operation = () => source.ApplyRange(6, 19, RangeUpdateTag.Assign(-8));
                break;
            }

            default:
                throw new InvalidOperationException($"Unknown operation {operationKind}.");
        }

        foreach (var callback in Enum.GetValues<RangeUpdateCallback>())
        {
            if (callback == RangeUpdateCallback.None)
                continue;

            AssertEveryInvocationFailsAtomically(
                source,
                expectedSource,
                callback,
                operation,
                requireInvocation: false,
                assertAdditionalInputs: assertAdditionalInputs);
        }

        AssertThrowingMatches(expectedSource, source);
        AssertThrowingMatches(expectedResult, operation());
    }

    /// <summary>
    /// Confirms that failpoint sweeps reach both single- and double-rotation insertion paths under
    /// pending tags, including every callback ordinal observed inside tag-safe rotations.
    /// </summary>
    [Fact]
    public void TaggedInsertions_SweepSingleAndDoubleRotationCallbackPaths()
    {
        foreach (var desiredRotationCount in new[] { 1L, 2L })
        {
            var scenario = FindTaggedInsertionWithRotationCount(desiredRotationCount);
            var successful = RangeUpdateDiagnosticsAdapter.Observe(scenario.Operation);
            Assert.Equal(desiredRotationCount, successful.Diagnostics.Rotations);
            AssertThrowingMatches(scenario.ExpectedResult, successful.Result);

            foreach (var callback in Enum.GetValues<RangeUpdateCallback>())
            {
                if (callback == RangeUpdateCallback.None)
                    continue;

                AssertEveryInvocationFailsAtomically(
                    scenario.Source,
                    scenario.ExpectedSource,
                    callback,
                    scenario.Operation,
                    requireInvocation: false);
            }

            AssertThrowingMatches(scenario.ExpectedSource, scenario.Source);
        }
    }

    /// <summary>Fails logical read callbacks under a pending tag and proves reads cannot mutate the version.</summary>
    /// <param name="measureQuery">When true, tests a covered-subtree measure query; otherwise tests indexing.</param>
    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void LogicalRead_CallbackFailuresLeaveTaggedVersionUsable(bool measureQuery)
    {
        RangeUpdateThrowingAlgebra.Disable();
        var initial = Enumerable.Range(0, 96).ToArray();
        var source = CreateThrowing(initial).ApplyRange(0, initial.Length, RangeUpdateTag.Add(11));
        var expected = initial.Select(value => value + 11).ToArray();
        var callback = measureQuery ? RangeUpdateCallback.ApplyMeasure : RangeUpdateCallback.ApplyElement;

        AssertEveryInvocationFailsAtomically(
            source,
            expected,
            callback,
            measureQuery
                ? () =>
                {
                    _ = source.MeasureRange(13, 57);
                    return source;
                }
                : () =>
                {
                    _ = source[41];
                    return source;
                });

        AssertThrowingMatches(expected, source);
    }

    /// <summary>Locks validation before tag-policy calls, including mixed-invalid parameter precedence.</summary>
    [Fact]
    public void InvalidRangeValidation_PrecedesEveryTagCallback()
    {
        RangeUpdateThrowingAlgebra.Disable();
        var source = CreateThrowing(1, 2, 3);

        foreach (var callback in new[]
        {
            RangeUpdateCallback.IsIdentity,
            RangeUpdateCallback.Compose,
            RangeUpdateCallback.ApplyElement,
            RangeUpdateCallback.ApplyMeasure,
        })
        {
            RangeUpdateThrowingAlgebra.Arm(callback);
            try
            {
                Assert.Equal("index", Assert.Throws<ArgumentOutOfRangeException>(() =>
                    source.ApplyRange(-1, -1, RangeUpdateTag.Add(1))).ParamName);
                Assert.Equal("count", Assert.Throws<ArgumentOutOfRangeException>(() =>
                    source.ApplyRange(source.Count + 1, -1, RangeUpdateTag.Add(1))).ParamName);
                Assert.Equal("index", Assert.Throws<ArgumentOutOfRangeException>(() =>
                    source.ApplyRange(source.Count + 1, 0, RangeUpdateTag.Add(1))).ParamName);
                Assert.Equal("count", Assert.Throws<ArgumentOutOfRangeException>(() =>
                    source.ApplyRange(1, int.MaxValue, RangeUpdateTag.Add(1))).ParamName);
                Assert.Equal(0, RangeUpdateThrowingAlgebra.MatchingInvocationCount);
            }
            finally
            {
                RangeUpdateThrowingAlgebra.Disable();
            }
        }

        AssertThrowingMatches([1, 2, 3], source);
    }

    /// <summary>Verifies empty range measurement invokes no element, combine, or tag-action callbacks.</summary>
    [Fact]
    public void EmptyRangeMeasure_ReturnsEmptyWithoutCallbacks()
    {
        RangeUpdateThrowingAlgebra.Disable();
        var source = CreateThrowing(1, 2, 3).ApplyRange(0, 3, RangeUpdateTag.Add(9));

        foreach (var callback in new[]
        {
            RangeUpdateCallback.Combine,
            RangeUpdateCallback.Measure,
            RangeUpdateCallback.IsIdentity,
            RangeUpdateCallback.Compose,
            RangeUpdateCallback.ApplyElement,
            RangeUpdateCallback.ApplyMeasure,
        })
        {
            RangeUpdateThrowingAlgebra.Arm(callback);
            try
            {
                Assert.Equal(RangeUpdateAffineAlgebra.Empty, source.MeasureRange(2, 0));
                Assert.Equal(RangeUpdateAffineAlgebra.Empty, source.MeasureRange(source.Count, 0));
                Assert.Equal(0, RangeUpdateThrowingAlgebra.MatchingInvocationCount);
            }
            finally
            {
                RangeUpdateThrowingAlgebra.Disable();
            }
        }
    }

    /// <summary>Reads several shared tagged versions concurrently through all immutable observation paths.</summary>
    [Fact]
    public void SharedVersions_AreSafeForConcurrentReads()
    {
        var baseModel = Enumerable.Range(-100, 201).ToList();
        var baseVersion = RangeUpdateAssert.Create(baseModel.ToArray());

        var addModel = baseModel.ToList();
        var addVersion = baseVersion.ApplyRange(0, baseVersion.Count, RangeUpdateTag.Add(7));
        RangeUpdateAssert.Apply(addModel, 0, addModel.Count, RangeUpdateTag.Add(7));

        var assignModel = addModel.ToList();
        var assignVersion = addVersion.ApplyRange(37, 121, RangeUpdateTag.Assign(4));
        RangeUpdateAssert.Apply(assignModel, 37, 121, RangeUpdateTag.Assign(4));

        var branchModel = addModel.ToList();
        var branchVersion = addVersion.ApplyRange(11, 173, RangeUpdateTag.Add(-9));
        RangeUpdateAssert.Apply(branchModel, 11, 173, RangeUpdateTag.Add(-9));
        branchVersion = branchVersion.Insert(100, 999).RemoveAt(17);
        branchModel.Insert(100, 999);
        branchModel.RemoveAt(17);

        var versions = new[]
        {
            (Sequence: baseVersion, Model: (IReadOnlyList<int>)baseModel),
            (Sequence: addVersion, Model: (IReadOnlyList<int>)addModel),
            (Sequence: assignVersion, Model: (IReadOnlyList<int>)assignModel),
            (Sequence: branchVersion, Model: (IReadOnlyList<int>)branchModel),
        };

        Parallel.For(
            0,
            8,
            new ParallelOptions { MaxDegreeOfParallelism = 4 },
            worker =>
            {
                for (var iteration = 0; iteration < 80; iteration++)
                {
                    var version = versions[(worker + iteration) % versions.Length];
                    Assert.Equal(version.Model.Count, version.Sequence.Count);
                    Assert.Equal(version.Model, version.Sequence.ToArray());
                    Assert.Equal(RangeUpdateAssert.Fold(version.Model), version.Sequence.Measure);

                    var index = (worker * 17 + iteration * 13) % version.Model.Count;
                    Assert.Equal(version.Model[index], version.Sequence[index]);
                    var count = Math.Min(31, version.Model.Count - index);
                    Assert.Equal(
                        RangeUpdateAssert.Fold(version.Model.Skip(index).Take(count)),
                        version.Sequence.MeasureRange(index, count));
                }
            });

        foreach (var version in versions)
            RangeUpdateAssert.Matches(version.Model, version.Sequence);
    }

    private static RangeUpdateSequence<
        int,
        RangeUpdateMeasure,
        RangeUpdateTag,
        RangeUpdateThrowingAlgebra> CreateThrowing(params int[] values) =>
        RangeUpdateSequence<int, RangeUpdateMeasure, RangeUpdateTag, RangeUpdateThrowingAlgebra>
            .Create(values.AsSpan());

    private static void AssertEveryInvocationFailsAtomically(
        RangeUpdateSequence<int, RangeUpdateMeasure, RangeUpdateTag, RangeUpdateThrowingAlgebra> source,
        IReadOnlyList<int> expected,
        RangeUpdateCallback callback,
        Func<RangeUpdateSequence<int, RangeUpdateMeasure, RangeUpdateTag, RangeUpdateThrowingAlgebra>> operation,
        bool requireInvocation = true,
        Action? assertAdditionalInputs = null)
    {
        RangeUpdateThrowingAlgebra.Disable();
        RangeUpdateThrowingAlgebra.Arm(callback, int.MaxValue);
        int invocationCount;
        try
        {
            _ = operation();
        }
        finally
        {
            invocationCount = RangeUpdateThrowingAlgebra.MatchingInvocationCount;
            RangeUpdateThrowingAlgebra.Disable();
        }

        if (!requireInvocation && invocationCount == 0)
            return;
        Assert.True(invocationCount > 0, $"Expected {callback} to be called by the selected operation.");

        for (var ordinal = 1; ordinal <= invocationCount; ordinal++)
        {
            RangeUpdateThrowingAlgebra.Arm(callback, ordinal);
            try
            {
                var exception = Assert.Throws<RangeUpdateCallbackException>(() =>
                {
                    _ = operation();
                });
                Assert.Equal(callback, exception.Callback);
                Assert.Equal(ordinal, exception.Ordinal);
            }
            finally
            {
                RangeUpdateThrowingAlgebra.Disable();
            }

            AssertThrowingMatches(expected, source);
            assertAdditionalInputs?.Invoke();
        }
    }

    private static (
        RangeUpdateSequence<int, RangeUpdateMeasure, RangeUpdateTag, RangeUpdateThrowingAlgebra> Source,
        int[] ExpectedSource,
        int[] ExpectedResult,
        Func<RangeUpdateSequence<int, RangeUpdateMeasure, RangeUpdateTag, RangeUpdateThrowingAlgebra>> Operation)
        FindTaggedInsertionWithRotationCount(long desiredRotationCount)
    {
        RangeUpdateThrowingAlgebra.Disable();
        for (var count = 2; count <= 96; count++)
        {
            var initial = Enumerable.Range(0, count).ToArray();
            var expectedSource = initial.Select(static value => value + 2).ToArray();
            var source = CreateThrowing(initial)
                .ApplyRange(0, count, RangeUpdateTag.Add(2));

            for (var index = 0; index <= count; index++)
            {
                var capturedIndex = index;
                Func<RangeUpdateSequence<
                    int,
                    RangeUpdateMeasure,
                    RangeUpdateTag,
                    RangeUpdateThrowingAlgebra>> operation =
                    () => source.Insert(capturedIndex, -777);
                var observed = RangeUpdateDiagnosticsAdapter.Observe(operation);
                if (observed.Diagnostics.Rotations != desiredRotationCount)
                    continue;

                var expectedResult = expectedSource.ToList();
                expectedResult.Insert(index, -777);
                return (source, expectedSource, expectedResult.ToArray(), operation);
            }
        }

        throw new InvalidOperationException(
            $"No deterministic tagged insertion with {desiredRotationCount} rotations was found.");
    }

    private static void AssertThrowingMatches(
        IReadOnlyList<int> expected,
        RangeUpdateSequence<int, RangeUpdateMeasure, RangeUpdateTag, RangeUpdateThrowingAlgebra> actual)
    {
        RangeUpdateThrowingAlgebra.Disable();
        Assert.Equal(expected.Count, actual.Count);
        Assert.Equal(expected, actual.ToArray());
        Assert.Equal(RangeUpdateAssert.Fold(expected), actual.Measure);
        RangeUpdateDiagnosticsAdapter.Validate(actual, static (left, right) => left == right);
    }
}
