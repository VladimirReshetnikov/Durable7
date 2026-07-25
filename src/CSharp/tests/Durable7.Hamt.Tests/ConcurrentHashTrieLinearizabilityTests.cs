using Xunit;

namespace Durable7.Hamt.Tests;

/// <summary>Small-history linearizability checking for the mutable Ctrie surface.</summary>
public sealed class ConcurrentHashTrieLinearizabilityTests
{
    /// <summary>Exhaustively serializes each observed concurrent history against a dictionary model.</summary>
    [Fact]
    public async Task MixedShortHistories_HaveAValidLinearization()
    {
        var random = new Random(0x43545249);
        for (var round = 0; round < 400; round++)
        {
            var comparer = new ScenarioComparer(round % 3);
            var trie = new ConcurrentHashTrie<int, int>(comparer);
            var initial = new Dictionary<int, int>();
            for (var key = 0; key < random.Next(3); key++)
            {
                var value = random.Next(8);
                initial[key] = value;
                trie.SetItem(key, value);
            }

            var operations = Enumerable.Range(0, 5)
                .Select(_ => HistoryOperation.Create(random))
                .ToArray();
            using var start = new ManualResetEventSlim();
            var clock = 0;
            var tasks = operations.Select(operation => Task.Run(() =>
            {
                start.Wait();
                operation.Start = Interlocked.Increment(ref clock);
                operation.Execute(trie);
                operation.End = Interlocked.Increment(ref clock);
            })).ToArray();

            start.Set();
            await Task.WhenAll(tasks).WaitAsync(TimeSpan.FromSeconds(30));
            var final = trie.Snapshot().ToDictionary(entry => entry.Key, entry => entry.Value);
            var linearizable = HasLinearization(
                operations,
                initial,
                final,
                completedMask: 0);

            Assert.True(linearizable, FormatFailure(round, initial, operations, final));
            var statistics = trie.ValidateStructureForTesting();
            Assert.Equal(final.Count, statistics.EntryCount);
            Assert.Equal(0, statistics.TombNodeCount);
        }
    }

    private static bool HasLinearization(
        HistoryOperation[] operations,
        Dictionary<int, int> state,
        Dictionary<int, int> final,
        int completedMask)
    {
        var allCompleted = (1 << operations.Length) - 1;
        if (completedMask == allCompleted)
            return DictionariesEqual(state, final);

        for (var candidate = 0; candidate < operations.Length; candidate++)
        {
            var candidateBit = 1 << candidate;
            if ((completedMask & candidateBit) != 0
                || HasUncompletedRealTimePredecessor(operations, candidate, completedMask))
            {
                continue;
            }

            var next = new Dictionary<int, int>(state);
            if (!TryApply(operations[candidate], next))
                continue;
            if (HasLinearization(operations, next, final, completedMask | candidateBit))
                return true;
        }

        return false;
    }

    private static bool HasUncompletedRealTimePredecessor(
        HistoryOperation[] operations,
        int candidate,
        int completedMask)
    {
        for (var index = 0; index < operations.Length; index++)
        {
            if ((completedMask & (1 << index)) == 0
                && operations[index].End < operations[candidate].Start)
            {
                return true;
            }
        }

        return false;
    }

    private static bool TryApply(HistoryOperation operation, Dictionary<int, int> state)
    {
        switch (operation.Kind)
        {
            case OperationKind.Set:
                state[operation.Key] = operation.Argument;
                return true;
            case OperationKind.TryAdd:
            {
                var expected = !state.ContainsKey(operation.Key);
                if (expected)
                    state.Add(operation.Key, operation.Argument);
                return operation.BooleanResult == expected;
            }
            case OperationKind.TryUpdate:
            {
                var expected = state.TryGetValue(operation.Key, out var current)
                    && current == operation.Comparison;
                if (expected)
                    state[operation.Key] = operation.Argument;
                return operation.BooleanResult == expected;
            }
            case OperationKind.TryRemove:
            {
                var expected = state.Remove(operation.Key, out var removed);
                return operation.BooleanResult == expected
                    && (!expected || operation.ValueResult == removed);
            }
            case OperationKind.Read:
            {
                var expected = state.TryGetValue(operation.Key, out var value);
                return operation.BooleanResult == expected
                    && (!expected || operation.ValueResult == value);
            }
            case OperationKind.GetOrAdd:
            {
                if (!state.TryGetValue(operation.Key, out var value))
                    state[operation.Key] = value = operation.Argument;
                return operation.ValueResult == value;
            }
            case OperationKind.AddOrUpdate:
            {
                var value = state.TryGetValue(operation.Key, out var current)
                    ? current + operation.Comparison
                    : operation.Argument;
                state[operation.Key] = value;
                return operation.ValueResult == value;
            }
            case OperationKind.Snapshot:
                return operation.SnapshotResult is not null
                    && DictionariesEqual(state, operation.SnapshotResult);
            case OperationKind.Clear:
                state.Clear();
                return true;
            default:
                throw new InvalidOperationException($"Unknown operation {operation.Kind}.");
        }
    }

    private static bool DictionariesEqual(
        IReadOnlyDictionary<int, int> left,
        IReadOnlyDictionary<int, int> right) =>
        left.Count == right.Count
        && left.All(entry => right.TryGetValue(entry.Key, out var value) && value == entry.Value);

    private static string FormatFailure(
        int round,
        IReadOnlyDictionary<int, int> initial,
        IEnumerable<HistoryOperation> operations,
        IReadOnlyDictionary<int, int> final) =>
        $"Non-linearizable Ctrie history in round {round}.{Environment.NewLine}"
        + $"Initial: {FormatMap(initial)}{Environment.NewLine}"
        + string.Join(Environment.NewLine, operations.Select(operation => operation.ToString()))
        + $"{Environment.NewLine}Final: {FormatMap(final)}";

    private static string FormatMap(IReadOnlyDictionary<int, int> map) =>
        "{" + string.Join(", ", map.OrderBy(entry => entry.Key).Select(entry => $"{entry.Key}:{entry.Value}")) + "}";

    private enum OperationKind
    {
        Set,
        TryAdd,
        TryUpdate,
        TryRemove,
        Read,
        GetOrAdd,
        AddOrUpdate,
        Snapshot,
        Clear,
    }

    private sealed class HistoryOperation(
        OperationKind kind,
        int key,
        int argument,
        int comparison)
    {
        internal OperationKind Kind { get; } = kind;
        internal int Key { get; } = key;
        internal int Argument { get; } = argument;
        internal int Comparison { get; } = comparison;
        internal int Start { get; set; }
        internal int End { get; set; }
        internal bool BooleanResult { get; private set; }
        internal int ValueResult { get; private set; }
        internal Dictionary<int, int>? SnapshotResult { get; private set; }

        internal static HistoryOperation Create(Random random) => new(
            (OperationKind)random.Next(Enum.GetValues<OperationKind>().Length),
            random.Next(4),
            random.Next(8),
            random.Next(1, 4));

        internal void Execute(ConcurrentHashTrie<int, int> trie)
        {
            switch (Kind)
            {
                case OperationKind.Set:
                    trie.SetItem(Key, Argument);
                    break;
                case OperationKind.TryAdd:
                    BooleanResult = trie.TryAdd(Key, Argument);
                    break;
                case OperationKind.TryUpdate:
                    BooleanResult = trie.TryUpdate(Key, Argument, Comparison);
                    break;
                case OperationKind.TryRemove:
                    BooleanResult = trie.TryRemove(Key, out var removed);
                    ValueResult = removed;
                    break;
                case OperationKind.Read:
                    BooleanResult = trie.TryGetValue(Key, out var value);
                    ValueResult = value;
                    break;
                case OperationKind.GetOrAdd:
                    ValueResult = trie.GetOrAdd(Key, _ => Argument);
                    break;
                case OperationKind.AddOrUpdate:
                    ValueResult = trie.AddOrUpdate(Key, _ => Argument, (_, current) => current + Comparison);
                    break;
                case OperationKind.Snapshot:
                    SnapshotResult = trie.Snapshot().ToDictionary(entry => entry.Key, entry => entry.Value);
                    break;
                case OperationKind.Clear:
                    trie.Clear();
                    break;
                default:
                    throw new InvalidOperationException($"Unknown operation {Kind}.");
            }
        }

        public override string ToString()
        {
            var result = Kind switch
            {
                OperationKind.TryAdd or OperationKind.TryUpdate or OperationKind.TryRemove or OperationKind.Read =>
                    $"success={BooleanResult}, value={ValueResult}",
                OperationKind.GetOrAdd or OperationKind.AddOrUpdate => $"value={ValueResult}",
                OperationKind.Snapshot => $"snapshot={FormatMap(SnapshotResult!)}",
                _ => "void",
            };
            return $"[{Start},{End}] {Kind}(key={Key}, arg={Argument}, cmp={Comparison}) => {result}";
        }
    }

    private sealed class ScenarioComparer(int scenario) : IEqualityComparer<int>
    {
        public bool Equals(int x, int y) => x == y;

        public int GetHashCode(int obj) => scenario switch
        {
            0 => obj,
            1 => obj << 5,
            _ => 0,
        };
    }
}
