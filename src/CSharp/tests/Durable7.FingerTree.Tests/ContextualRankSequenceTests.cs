using Xunit;

namespace Durable7.FingerTree.Tests;

/// <summary>Semantic, exhaustive, branching, policy-failure, and complexity tests.</summary>
public sealed class ContextualRankSequenceTests
{
    /// <summary>Shows that commas inside quotes are not events and that initial context changes rank.</summary>
    [Fact]
    public void QuotedDelimiterMachine_RanksAndSelectsContextDependentEvents()
    {
        var text = ContextualRankSequence<char, QuotedCommaMachine>.Create("\"a,b\",c,d".AsSpan());

        Assert.Equal(new ContextualPrefixSummary(0, 2), text.Evaluate(0));
        Assert.Equal(new ContextualPrefixSummary(1, 1), text.Evaluate(1));
        Assert.Equal(0, text.EventRank(5, 0));
        Assert.Equal(1, text.EventRank(6, 0));

        Assert.True(text.TrySelectEvent(0, 0, out var first));
        Assert.Equal(new ContextualEventLocation(5, 0, 0, 0, 1), first);
        Assert.True(text.TrySelectEvent(1, 0, out var second));
        Assert.Equal(7, second.ElementIndex);
        Assert.False(text.TrySelectEvent(2, 0, out _));
        Assert.False(text.TrySelectEvent(-1, 0, out _));

        Assert.True(text.TrySelectEvent(0, 1, out var initiallyQuoted));
        Assert.Equal(2, initiallyQuoted.ElementIndex);
        Assert.Equal("\"a,b\",c,d".ToCharArray(), text.ToArray());
    }

    /// <summary>Exhausts every short word over quote, comma, and ordinary characters from both states.</summary>
    [Fact]
    public void ExhaustiveSmallWords_MatchIndependentScanner()
    {
        char[] alphabet = ['\"', ',', 'x'];
        for (var length = 0; length <= 7; length++)
        {
            var wordCount = (int)Math.Pow(alphabet.Length, length);
            for (var code = 0; code < wordCount; code++)
            {
                var value = code;
                var word = new char[length];
                for (var index = 0; index < length; index++)
                {
                    word[index] = alphabet[value % alphabet.Length];
                    value /= alphabet.Length;
                }

                AssertMatchesModel(
                    ContextualRankSequence<char, QuotedCommaMachine>.Create(word.AsSpan()),
                    word,
                    static (state, item) => QuotedCommaMachine.Transition(state, item));
            }
        }
    }

    /// <summary>Checks positional operations and retained randomized branches against a list-and-scan model.</summary>
    [Fact]
    public void RandomizedRetainedBranches_MatchListAndMachineModel()
    {
        var random = new Random(0x61C7_2026);
        var versions = new List<(ContextualRankSequence<int, TernaryMachine> Sequence, int[] Model)>
        {
            (ContextualRankSequence<int, TernaryMachine>.Empty, [])
        };

        for (var step = 0; step < 4_000; step++)
        {
            var parent = versions[random.Next(versions.Count)];
            var model = parent.Model.ToList();
            ContextualRankSequence<int, TernaryMachine> next;
            switch (random.Next(8))
            {
                case 0:
                    {
                        var item = random.Next(-9, 10);
                        model.Insert(0, item);
                        next = parent.Sequence.Prepend(item);
                        break;
                    }
                case 1:
                    {
                        var item = random.Next(-9, 10);
                        model.Add(item);
                        next = parent.Sequence.Append(item);
                        break;
                    }
                case 2:
                    {
                        var item = random.Next(-9, 10);
                        var index = random.Next(model.Count + 1);
                        model.Insert(index, item);
                        next = parent.Sequence.Insert(index, item);
                        break;
                    }
                case 3 when model.Count > 0:
                    {
                        var index = random.Next(model.Count);
                        var item = random.Next(-9, 10);
                        model[index] = item;
                        next = parent.Sequence.SetItem(index, item);
                        break;
                    }
                case 4 when model.Count > 0:
                    {
                        var index = random.Next(model.Count);
                        model.RemoveAt(index);
                        next = parent.Sequence.RemoveAt(index);
                        break;
                    }
                case 5:
                    {
                        var index = random.Next(model.Count + 1);
                        var count = random.Next(model.Count - index + 1);
                        model = model.Skip(index).Take(count).ToList();
                        next = parent.Sequence.GetRange(index, count);
                        break;
                    }
                default:
                    {
                        var suffix = versions[random.Next(versions.Count)];
                        if (model.Count > 256 - suffix.Model.Length)
                        {
                            next = parent.Sequence;
                            break;
                        }
                        model.AddRange(suffix.Model);
                        next = parent.Sequence.Concat(suffix.Sequence);
                        break;
                    }
            }

            var snapshot = (next, model.ToArray());
            versions.Add(snapshot);
            AssertMatchesModel(next, snapshot.Item2, static (state, item) => TernaryMachine.Transition(state, item));

            if (versions.Count > 512)
                versions.RemoveAt(random.Next(1, versions.Count - 1));
            if ((step & 127) == 0)
            {
                var retained = versions[random.Next(versions.Count)];
                AssertMatchesModel(
                    retained.Sequence,
                    retained.Model,
                    static (state, item) => TernaryMachine.Transition(state, item));
            }
        }
    }

    /// <summary>Checks split/concat identity boundaries, multi-event transitions, and argument validation.</summary>
    [Fact]
    public void BoundariesMultiEventsAndInvalidArguments_AreExact()
    {
        var sequence = ContextualRankSequence<int, TernaryMachine>.Create(
            new int[] { 2, -3, 4, 0 }.AsSpan());
        for (var index = 0; index <= sequence.Count; index++)
        {
            var split = sequence.SplitAt(index);
            Assert.Equal(sequence.ToArray(), split.Left.Concat(split.Right).ToArray());
        }

        Assert.Same(sequence, sequence.Concat(ContextualRankSequence<int, TernaryMachine>.Empty));
        Assert.Same(sequence, ContextualRankSequence<int, TernaryMachine>.Empty.Concat(sequence));
        Assert.Same(sequence, sequence.GetRange(0, sequence.Count));
        Assert.Same(sequence, sequence.SplitAt(0).Right);
        Assert.Same(sequence, sequence.SplitAt(sequence.Count).Left);
        Assert.True(sequence.TrySelectEvent(0, 0, out var location));
        Assert.InRange(location.EventIndexInElement, 0, location.ElementEventCount - 1);

        Assert.Throws<ArgumentOutOfRangeException>(() => sequence.Evaluate(-1));
        Assert.Throws<ArgumentOutOfRangeException>(() => sequence.Evaluate(TernaryMachine.StateCount));
        Assert.Throws<ArgumentOutOfRangeException>(() => sequence.EvaluatePrefix(-1, 0));
        Assert.Throws<ArgumentOutOfRangeException>(() => sequence.EvaluatePrefix(sequence.Count + 1, 0));
        Assert.Throws<ArgumentOutOfRangeException>(() => _ = sequence[-1]);
        Assert.Throws<ArgumentOutOfRangeException>(() => sequence.Insert(sequence.Count + 1, 0));
        Assert.Throws<ArgumentOutOfRangeException>(() => sequence.RemoveAt(sequence.Count));
        Assert.Throws<ArgumentNullException>(() => sequence.Concat(null!));
        Assert.Throws<ArgumentNullException>(() =>
            ContextualRankSequence<int, TernaryMachine>.CreateRange(null!));
    }

    /// <summary>Uses transition-call counts to prove cached queries do not rescan elements.</summary>
    [Fact]
    public void CachedRankAndSelect_DoNotRescanTheInput()
    {
        var sequence = ContextualRankSequence<int, CountingMachine>.Create(
            Enumerable.Range(0, 8_192).ToArray().AsSpan());
        _ = sequence.Evaluate(0); // force every deferred summary before the counter gate
        CountingMachine.Reset();

        for (var index = 0; index <= sequence.Count; index += 17)
            _ = sequence.EventRank(index, index & 1);
        Assert.Equal(0, CountingMachine.CallCount);

        var successful = 0;
        var total = sequence.Evaluate(0).EventCount;
        for (long eventIndex = 0; eventIndex < total; eventIndex += 31)
        {
            Assert.True(sequence.TrySelectEvent(eventIndex, 0, out _));
            successful++;
        }
        Assert.Equal(successful, CountingMachine.CallCount);
    }

    /// <summary>Rejects invalid policies and overflowing composed event counts without changing a source version.</summary>
    [Fact]
    public void InvalidTransitionsAndOverflow_AreFailureAtomic()
    {
        Assert.Throws<InvalidOperationException>(() =>
            ContextualRankSequence<int, InvalidStateMachine>.Empty.Append(1));
        Assert.Throws<InvalidOperationException>(() =>
            ContextualRankSequence<int, NegativeEventMachine>.Empty.Append(1));

        var source = ContextualRankSequence<int, HugeEventMachine>.Empty.Append(1);
        Assert.Equal(long.MaxValue, source.Evaluate(0).EventCount);
        Assert.Throws<OverflowException>(() => source.Append(2));
        Assert.Equal([1], source.ToArray());
        Assert.Equal(long.MaxValue, source.Evaluate(0).EventCount);
    }

    /// <summary>Exercises concurrent cached reads over a shared version while independent branches are built.</summary>
    [Fact]
    public void ConcurrentReadersAndIndependentWriters_ObserveStableVersions()
    {
        var source = ContextualRankSequence<int, TernaryMachine>.Create(
            Enumerable.Range(-256, 513).ToArray().AsSpan());
        var expected = source.ToArray();
        var expectedSummaries = Enumerable.Range(0, TernaryMachine.StateCount)
            .Select(source.Evaluate)
            .ToArray();

        Parallel.For(0, 8, worker =>
        {
            var branch = source;
            for (var iteration = 0; iteration < 500; iteration++)
            {
                var state = (worker + iteration) % TernaryMachine.StateCount;
                Assert.Equal(expectedSummaries[state], source.Evaluate(state));
                var boundary = (worker * 31 + iteration * 17) % (source.Count + 1);
                _ = source.EventRank(boundary, state);
                var total = expectedSummaries[state].EventCount;
                if (total > 0)
                    Assert.True(source.TrySelectEvent((worker + iteration) % total, state, out _));

                branch = branch.SetItem(iteration % branch.Count, worker - iteration);
            }
            Assert.Equal(expected, source.ToArray());
            Assert.Equal(source.Count, branch.Count);
        });
    }

    private static void AssertMatchesModel<TElement, TMachine>(
        ContextualRankSequence<TElement, TMachine> sequence,
        IReadOnlyList<TElement> model,
        Func<int, TElement, ContextualEventTransition> transition)
        where TMachine : IContextualEventMachine<TElement>
    {
        Assert.Equal(model, sequence.ToArray());
        Assert.Equal(model.Count, sequence.Count);
        for (var initialState = 0; initialState < ContextualRankSequence<TElement, TMachine>.StateCount; initialState++)
        {
            var state = initialState;
            long events = 0;
            var expectedLocations = new List<ContextualEventLocation>();
            Assert.Equal(new ContextualPrefixSummary(state, events), sequence.EvaluatePrefix(0, initialState));
            for (var index = 0; index < model.Count; index++)
            {
                var step = transition(state, model[index]);
                for (long ordinal = 0; ordinal < step.EventCount; ordinal++)
                    expectedLocations.Add(new(index, ordinal, state, step.NextState, step.EventCount));
                state = step.NextState;
                events += step.EventCount;
                Assert.Equal(new ContextualPrefixSummary(state, events), sequence.EvaluatePrefix(index + 1, initialState));
                Assert.Equal(events, sequence.EventRank(index + 1, initialState));
            }

            Assert.Equal(new ContextualPrefixSummary(state, events), sequence.Evaluate(initialState));
            for (var eventIndex = 0; eventIndex < expectedLocations.Count; eventIndex++)
            {
                Assert.True(sequence.TrySelectEvent(eventIndex, initialState, out var actual));
                Assert.Equal(expectedLocations[eventIndex], actual);
            }
            Assert.False(sequence.TrySelectEvent(expectedLocations.Count, initialState, out _));
        }
    }

    private readonly struct QuotedCommaMachine : IContextualEventMachine<char>
    {
        public static int StateCount => 2;

        public static ContextualEventTransition Transition(int state, char element) => element switch
        {
            '\"' => new(1 - state, 0),
            ',' when state == 0 => new(state, 1),
            _ => new(state, 0)
        };
    }

    private readonly struct TernaryMachine : IContextualEventMachine<int>
    {
        public static int StateCount => 3;

        public static ContextualEventTransition Transition(int state, int element)
        {
            var magnitude = Math.Abs((long)element);
            return new((int)((state + magnitude) % StateCount), (state + magnitude) % 3);
        }
    }

    private readonly struct CountingMachine : IContextualEventMachine<int>
    {
        private static long _callCount;

        public static int StateCount => 2;
        public static long CallCount => Interlocked.Read(ref _callCount);
        public static void Reset() => Interlocked.Exchange(ref _callCount, 0);

        public static ContextualEventTransition Transition(int state, int element)
        {
            Interlocked.Increment(ref _callCount);
            return new((state + (element & 1)) & 1, ((element ^ state) & 3) == 0 ? 1 : 0);
        }
    }

    private readonly struct InvalidStateMachine : IContextualEventMachine<int>
    {
        public static int StateCount => 1;
        public static ContextualEventTransition Transition(int state, int element) => new(1, 0);
    }

    private readonly struct NegativeEventMachine : IContextualEventMachine<int>
    {
        public static int StateCount => 1;
        public static ContextualEventTransition Transition(int state, int element) => new(0, -1);
    }

    private readonly struct HugeEventMachine : IContextualEventMachine<int>
    {
        public static int StateCount => 1;
        public static ContextualEventTransition Transition(int state, int element) => new(0, long.MaxValue);
    }
}
