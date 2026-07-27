// Tests for the brodal okasaki heap audit.

using System.Reflection;
using System.Runtime.CompilerServices;
using System.Numerics;
using Xunit;

namespace Durable7.FingerTree.Tests;

/// <summary>
/// Paper-level invariant, persistence, sharing, and operation-bound coverage for
/// <see cref="BrodalOkasakiHeap{T}"/>.
/// </summary>
public sealed class BrodalOkasakiHeapAuditTests
{
    /// <summary>Decodes and validates the fused skew-tree/bootstrapped-forest boundary in adversarial shapes.</summary>
    [Fact]
    public void RepresentationInvariants_HoldAcrossAdversarialShapes()
    {
        const int count = 4_096;
        var ascending = BrodalOkasakiHeap<int>.CreateRange(Enumerable.Range(0, count));
        var descending = BrodalOkasakiHeap<int>.CreateRange(Enumerable.Range(0, count).Reverse());
        var equal = BrodalOkasakiHeap<int>.CreateRange(Enumerable.Repeat(7, count));
        var evens = BrodalOkasakiHeap<int>.CreateRange(Enumerable.Range(0, count / 2).Select(value => value * 2));
        var odds = BrodalOkasakiHeap<int>.CreateRange(Enumerable.Range(0, count / 2).Select(value => value * 2 + 1));
        var melded = evens.Meld(odds);

        HeapInspector.Validate(ascending);
        HeapInspector.Validate(descending);
        HeapInspector.Validate(equal);
        HeapInspector.Validate(melded);

        Assert.Equal(Enumerable.Range(0, count), Drain(ascending));
        Assert.Equal(Enumerable.Range(0, count), Drain(descending));
        Assert.Equal(Enumerable.Repeat(7, count), Drain(equal));
        Assert.Equal(Enumerable.Range(0, count), Drain(melded));
    }

    /// <summary>Runs 30,000 branching insert, meld, and delete-min operations against a retained multiset model.</summary>
    [Fact]
    public void RandomizedPersistentHistory_ThirtyThousandOperationsMatchesRetainedModels()
    {
        const int operationCount = 30_000;
        const int maximumElements = 2_048;
        var random = new Random(0x5B0D_A1);
        var current = Version.Empty;
        var retained = new List<Version> { current };
        var inserts = 0;
        var melds = 0;
        var deletes = 0;

        for (var step = 0; step < operationCount; step++)
        {
            var source = retained.Count > 1 && random.Next(5) == 0
                ? retained[random.Next(retained.Count)]
                : current;
            var choice = random.Next(100);

            if (choice < 52 || source.Model.Count == 0)
            {
                var item = random.Next(-128, 129);
                current = source.Insert(item);
                inserts++;
            }
            else if (choice < 76)
            {
                current = source.DeleteMinimum();
                deletes++;
            }
            else
            {
                Version? partner = null;
                for (var attempt = 0; attempt < 12; attempt++)
                {
                    var candidate = retained[random.Next(retained.Count)];
                    if (source.Model.Count + candidate.Model.Count <= maximumElements)
                    {
                        partner = candidate;
                        break;
                    }
                }

                if (partner is null)
                {
                    current = source.DeleteMinimum();
                    deletes++;
                }
                else
                {
                    current = source.Meld(partner);
                    melds++;
                }
            }

            Assert.Equal(current.Model.Count, current.Heap.Count);
            if (current.Model.Count == 0)
                Assert.True(current.Heap.IsEmpty);
            else
                Assert.Equal(current.Model.Minimum, current.Heap.Minimum);

            if ((step & 127) == 0)
            {
                HeapInspector.Validate(current.Heap);
                Assert.Equal(current.Model.ToSortedArray(), current.Heap.Order());
            }

            if (step % 37 == 0)
            {
                if (retained.Count < 256)
                    retained.Add(current);
                else
                    retained[random.Next(retained.Count)] = current;
            }
        }

        Assert.True(inserts > 10_000);
        Assert.True(melds > 1_000);
        Assert.True(deletes > 3_000);

        foreach (var version in retained.Where((_, index) => index % 7 == 0))
        {
            Assert.Same(version.Root, version.Heap.RootIdentity);
            HeapInspector.Validate(version.Heap);
            Assert.Equal(version.Model.ToSortedArray(), version.Heap.Order());
        }
    }

    /// <summary>Checks exact reference reuse for constant-time insertion and logarithmic delete-min path copying.</summary>
    [Fact]
    public void PersistentUpdates_ShareExactOffPathObjects()
    {
        var one = BrodalOkasakiHeap<int>.Empty.Insert(0).Insert(10);
        var oldRoot = one.RootIdentity!;
        var oldForest = HeapInspector.Children(oldRoot);
        var inserted = one.Insert(20);
        var insertedRoot = inserted.RootIdentity!;
        var insertedForest = HeapInspector.Children(insertedRoot)!;

        Assert.NotSame(oldRoot, insertedRoot);
        Assert.Same(oldForest, HeapInspector.Tail(insertedForest));
        var insertDelta = HeapInspector.ReferenceDelta(one, inserted);
        Assert.Equal(2, insertDelta.Created);
        Assert.Equal(1, insertDelta.Discarded);

        Assert.Same(one, one.Meld(BrodalOkasakiHeap<int>.Empty));
        Assert.Same(one, BrodalOkasakiHeap<int>.Empty.Meld(one));

        var random = new Random(0x0FF5_1DE);
        var values = Enumerable.Range(0, 8_192).OrderBy(_ => random.Next()).ToArray();
        var large = BrodalOkasakiHeap<int>.CreateRange(values);
        var reduced = large.DeleteMinimum();
        var deleteDelta = HeapInspector.ReferenceDelta(large, reduced);
        var logarithm = BitOperations.Log2((uint)large.Count) + 1;

        Assert.True(deleteDelta.Shared > large.Count - 32 * logarithm);
        Assert.True(deleteDelta.Created <= 32 * logarithm);
        HeapInspector.Validate(large);
        HeapInspector.Validate(reduced);
    }

    /// <summary>Ensures comparer-equivalent elements survive ties and retain their distinct representatives.</summary>
    [Fact]
    public void ComparerEquivalenceAndTies_PreserveEveryRepresentative()
    {
        var comparer = Comparer<Tagged>.Create((left, right) => left.Priority.CompareTo(right.Priority));
        var leftItems = Enumerable.Range(0, 1_024).Select(index => new Tagged(index % 7, $"L{index}"));
        var rightItems = Enumerable.Range(0, 1_024).Select(index => new Tagged(index % 7, $"R{index}"));
        var heap = BrodalOkasakiHeap<Tagged>.CreateRange(leftItems, comparer)
            .Meld(BrodalOkasakiHeap<Tagged>.CreateRange(rightItems, comparer));

        HeapInspector.Validate(heap);
        var drained = Drain(heap);

        Assert.Equal(2_048, drained.Length);
        Assert.Equal(Enumerable.Range(0, 1_024).Select(index => $"L{index}")
            .Concat(Enumerable.Range(0, 1_024).Select(index => $"R{index}"))
            .Order(), drained.Select(item => item.Tag).Order());
        Assert.True(drained.Zip(drained.Skip(1)).All(pair => pair.First.Priority <= pair.Second.Priority));
    }

    /// <summary>Bounds comparison counts for all operations and demonstrates logarithmic delete-min allocation growth.</summary>
    [Fact]
    public void OperationCosts_MatchWorstCaseComparisonAndAllocationBounds()
    {
        var samples = new List<CostSample>();
        foreach (var count in new[] { 1_024, 4_096, 16_384, 65_536 })
        {
            var comparer = new CountingComparer<int>(Comparer<int>.Default);
            var left = BrodalOkasakiHeap<int>.CreateRange(Enumerable.Range(0, count), comparer);
            var right = BrodalOkasakiHeap<int>.CreateRange(Enumerable.Range(count, count), comparer);

            comparer.Reset();
            _ = left.Minimum;
            Assert.Equal(0, comparer.Count);

            comparer.Reset();
            _ = left.Insert(-1);
            Assert.InRange(comparer.Count, 1, 5);

            comparer.Reset();
            _ = left.Meld(right);
            Assert.InRange(comparer.Count, 1, 5);

            comparer.Reset();
            foreach (var _ in left)
            {
            }
            Assert.Equal(0, comparer.Count);

            _ = left.DeleteMinimum();
            comparer.Reset();
            var before = GC.GetAllocatedBytesForCurrentThread();
            BrodalOkasakiHeap<int>? result = null;
            const int repetitions = 64;
            for (var repetition = 0; repetition < repetitions; repetition++)
                result = left.DeleteMinimum();
            var allocated = GC.GetAllocatedBytesForCurrentThread() - before;
            GC.KeepAlive(result);

            var comparisonsPerDelete = comparer.Count / repetitions;
            var bytesPerDelete = allocated / repetitions;
            var logarithm = BitOperations.Log2((uint)count) + 1;
            Assert.True(comparisonsPerDelete <= 32 * logarithm + 8);
            Assert.True(bytesPerDelete <= 4_096L * logarithm + 8_192);
            samples.Add(new CostSample(count, comparisonsPerDelete, bytesPerDelete));
        }

        var smallest = samples[0];
        var largest = samples[^1];
        Assert.True(largest.Comparisons <= smallest.Comparisons + 32 * 6 + 8);
        Assert.True(largest.AllocatedBytes <= smallest.AllocatedBytes * 3 + 4_096);
    }

    private static T[] Drain<T>(BrodalOkasakiHeap<T> heap)
    {
        var result = new T[heap.Count];
        for (var index = 0; index < result.Length; index++)
        {
            Assert.True(heap.TryDeleteMinimum(out var minimum, out heap));
            result[index] = minimum!;
            if ((index & 255) == 0)
                HeapInspector.Validate(heap);
        }
        Assert.True(heap.IsEmpty);
        return result;
    }

    private sealed record Version(BrodalOkasakiHeap<int> Heap, MultisetModel Model, object? Root)
    {
        /// <summary>Gets the empty collection.</summary>
        internal static Version Empty { get; } = new(BrodalOkasakiHeap<int>.Empty, MultisetModel.Empty, null);

        /// <summary>Returns a collection with the element inserted.</summary>
        internal Version Insert(int item)
        {
            var heap = Heap.Insert(item);
            return new Version(heap, Model.Insert(item), heap.RootIdentity);
        }

        /// <summary>Returns the collection without its minimum-priority entry.</summary>
        internal Version DeleteMinimum()
        {
            var expected = Model.Minimum;
            Assert.True(Heap.TryDeleteMinimum(out var actual, out var heap));
            Assert.Equal(expected, actual);
            return new Version(heap, Model.DeleteMinimum(), heap.RootIdentity);
        }

        /// <summary>Returns the collection holding both operands' elements.</summary>
        internal Version Meld(Version other)
        {
            var heap = Heap.Meld(other.Heap);
            return new Version(heap, Model.Meld(other.Model), heap.RootIdentity);
        }
    }

    private sealed class MultisetModel(Dictionary<int, int> counts, int count)
    {
        private readonly Dictionary<int, int> _counts = counts;

        /// <summary>Gets the empty collection.</summary>
        internal static MultisetModel Empty { get; } = new([], 0);

        /// <summary>Gets the number of elements in the collection.</summary>
        internal int Count { get; } = count;

        /// <summary>Returns the smallest element.</summary>
        internal int Minimum => _counts.Keys.Min();

        /// <summary>Returns a collection with the element inserted.</summary>
        internal MultisetModel Insert(int item)
        {
            var copy = new Dictionary<int, int>(_counts);
            copy[item] = copy.GetValueOrDefault(item) + 1;
            return new MultisetModel(copy, checked(Count + 1));
        }

        /// <summary>Returns the collection without its minimum-priority entry.</summary>
        internal MultisetModel DeleteMinimum()
        {
            var minimum = Minimum;
            var copy = new Dictionary<int, int>(_counts);
            if (copy[minimum] == 1)
                copy.Remove(minimum);
            else
                copy[minimum]--;
            return new MultisetModel(copy, Count - 1);
        }

        /// <summary>Returns the collection holding both operands' elements.</summary>
        internal MultisetModel Meld(MultisetModel other)
        {
            var copy = new Dictionary<int, int>(_counts);
            foreach (var (item, multiplicity) in other._counts)
                copy[item] = checked(copy.GetValueOrDefault(item) + multiplicity);
            return new MultisetModel(copy, checked(Count + other.Count));
        }

        /// <summary>Returns the elements in ascending order.</summary>
        internal int[] ToSortedArray() => _counts.OrderBy(pair => pair.Key)
            .SelectMany(pair => Enumerable.Repeat(pair.Key, pair.Value))
            .ToArray();
    }

    private readonly record struct Tagged(int Priority, string Tag);

    private readonly record struct CostSample(int Count, long Comparisons, long AllocatedBytes);

    private sealed class CountingComparer<T>(IComparer<T> inner) : IComparer<T>
    {
        /// <summary>Gets the number of elements in the collection.</summary>
        internal long Count { get; private set; }

        /// <summary>Orders two values.</summary>
        public int Compare(T? left, T? right)
        {
            Count++;
            return inner.Compare(left!, right!);
        }

        /// <summary>Returns the value to its initial state.</summary>
        internal void Reset() => Count = 0;
    }

    private static class HeapInspector
    {
        private const BindingFlags InstanceFlags = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic;
        private static readonly Dictionary<Type, Accessors> AccessorsByType = [];

        /// <summary>Checks the heap's structural invariants. For tests and diagnostics.</summary>
        internal static void Validate<T>(BrodalOkasakiHeap<T> heap)
        {
            var statistics = heap.ValidateStructure();
            Assert.Equal(heap.Count, statistics.Count);
            var root = heap.RootIdentity;
            if (root is null)
            {
                Assert.Equal(0, heap.Count);
                Assert.Equal(0, statistics.RootForestLength);
                Assert.Equal(0, statistics.MaximumRank);
                Assert.Equal(0, statistics.MaximumDepth);
                return;
            }

            Assert.Equal(0, Rank(root));
            ValidateForestRanks(Children(root));
            Assert.Equal(ForestLength(Children(root)), statistics.RootForestLength);

            var logicalCount = 0;
            var maximumRank = 0;
            var maximumDepth = 0;
            var logicalPending = new Stack<(object Tree, int Depth)>();
            logicalPending.Push((root, 1));
            while (logicalPending.TryPop(out var frame))
            {
                var tree = frame.Tree;
                logicalCount++;
                Assert.True(logicalCount <= heap.Count, "The fused tree graph contains more logical occurrences than Count.");
                Assert.True(Rank(tree) >= 0);
                maximumRank = Math.Max(maximumRank, Rank(tree));
                maximumDepth = Math.Max(maximumDepth, frame.Depth);
                var parent = (T)Value(tree);
                for (var forest = Children(tree); forest is not null; forest = Tail(forest))
                {
                    var child = Head(forest);
                    Assert.True(heap.Comparer.Compare(parent, (T)Value(child)) <= 0, "Heap order is violated.");
                    logicalPending.Push((child, checked(frame.Depth + 1)));
                }
            }
            Assert.Equal(heap.Count, logicalCount);
            Assert.Equal(maximumRank, statistics.MaximumRank);
            Assert.Equal(maximumDepth, statistics.MaximumDepth);

            var inspected = new HashSet<object>(ReferenceIdentityComparer.Instance);
            var shapes = new Dictionary<object, Shape>(ReferenceIdentityComparer.Instance);
            var pending = new Stack<object>();
            pending.Push(root);
            while (pending.TryPop(out var tree))
            {
                if (!inspected.Add(tree))
                    continue;

                var decomposition = Decompose(tree);
                Assert.All(decomposition.ZeroChildren, child => Assert.Equal(0, Rank(child)));
                ValidateTreeRanks(decomposition.RankedChildren);
                ValidateForestRanks(decomposition.EmbeddedForest);
                _ = ShapeOf(tree, shapes);

                foreach (var child in decomposition.ZeroChildren)
                    pending.Push(child);
                foreach (var child in decomposition.RankedChildren)
                    pending.Push(child);
                for (var forest = decomposition.EmbeddedForest; forest is not null; forest = Tail(forest))
                    pending.Push(Head(forest));
            }
        }

        /// <summary>Gets how many references were added and released over the measured region.</summary>
        internal static ReferenceChanges ReferenceDelta<T>(BrodalOkasakiHeap<T> before, BrodalOkasakiHeap<T> after)
        {
            var oldNodes = UniqueNodes(before.RootIdentity);
            var newNodes = UniqueNodes(after.RootIdentity);
            var shared = newNodes.Count(node => oldNodes.Contains(node));
            return new ReferenceChanges(shared, newNodes.Count - shared, oldNodes.Count - shared);
        }

        /// <summary>Gets this node's children.</summary>
        internal static object? Children(object tree) => For(tree).Children.GetValue(tree);

        /// <summary>Gets the trailing part.</summary>
        internal static object? Tail(object forest) => For(forest).Tail.GetValue(forest);

        private static object Head(object forest) => For(forest).Head.GetValue(forest)!;

        private static int Rank(object tree) => (int)For(tree).Rank.GetValue(tree)!;

        private static object Value(object tree) => For(tree).Value.GetValue(tree)!;

        private static Shape ShapeOf(object tree, Dictionary<object, Shape> memo)
        {
            if (memo.TryGetValue(tree, out var known))
                return known;

            var rank = Rank(tree);
            var decomposition = Decompose(tree);
            long size = 1;
            var height = 0;
            foreach (var child in decomposition.ZeroChildren.Concat(decomposition.RankedChildren))
            {
                Assert.True(Rank(child) < rank);
                var childShape = ShapeOf(child, memo);
                size = checked(size + childShape.Size);
                height = Math.Max(height, checked(childShape.Height + 1));
            }

            Assert.Equal(rank, height);
            if (rank < 31)
            {
                var lower = 1L << rank;
                var upper = (1L << (rank + 1)) - 1;
                Assert.InRange(size, lower, upper);
            }

            var result = new Shape(size, height);
            memo.Add(tree, result);
            return result;
        }

        private static Decomposition Decompose(object tree)
        {
            var rank = Rank(tree);
            var zeros = new List<object>();
            var ranked = new List<object>();
            var forest = Children(tree);

            while (rank != 0)
            {
                Assert.NotNull(forest);
                var first = Head(forest!);
                var tail = Tail(forest!);
                if (rank == 1 && tail is null)
                {
                    ranked.Insert(0, first);
                    forest = null;
                    break;
                }

                Assert.NotNull(tail);
                var second = Head(tail!);
                var rest = Tail(tail!);
                if (rank == 1)
                {
                    if (Rank(second) == 0)
                    {
                        zeros.Insert(0, first);
                        ranked.Insert(0, second);
                        forest = rest;
                    }
                    else
                    {
                        ranked.Insert(0, first);
                        forest = tail;
                    }
                    break;
                }

                if (Rank(first) == Rank(second))
                {
                    ranked.Insert(0, second);
                    ranked.Insert(0, first);
                    forest = rest;
                    break;
                }

                if (Rank(first) == 0)
                {
                    zeros.Insert(0, first);
                    ranked.Insert(0, second);
                    forest = rest;
                }
                else
                {
                    ranked.Insert(0, first);
                    forest = tail;
                }
                rank--;
            }

            return new Decomposition(zeros, ranked, forest);
        }

        private static void ValidateForestRanks(object? forest)
        {
            var ranks = new List<int>();
            for (; forest is not null; forest = Tail(forest))
                ranks.Add(Rank(Head(forest)));
            ValidateRanks(ranks);
        }

        private static int ForestLength(object? forest)
        {
            var length = 0;
            for (; forest is not null; forest = Tail(forest))
                length++;
            return length;
        }

        private static void ValidateTreeRanks(IEnumerable<object> trees) =>
            ValidateRanks(trees.Select(Rank));

        private static void ValidateRanks(IEnumerable<int> ranks)
        {
            var previous = -1;
            var index = 0;
            var duplicate = false;
            foreach (var rank in ranks)
            {
                Assert.True(rank >= previous, "Skew-forest ranks are not nondecreasing.");
                if (rank == previous)
                {
                    Assert.Equal(1, index);
                    Assert.False(duplicate);
                    duplicate = true;
                }
                previous = rank;
                index++;
            }
        }

        private static HashSet<object> UniqueNodes(object? root)
        {
            var nodes = new HashSet<object>(ReferenceIdentityComparer.Instance);
            if (root is null)
                return nodes;
            var pending = new Stack<object>();
            pending.Push(root);
            while (pending.TryPop(out var tree))
            {
                if (!nodes.Add(tree))
                    continue;
                for (var forest = Children(tree); forest is not null; forest = Tail(forest))
                    pending.Push(Head(forest));
            }
            return nodes;
        }

        private static Accessors For(object value)
        {
            var type = value.GetType();
            if (AccessorsByType.TryGetValue(type, out var accessors))
                return accessors;

            accessors = new Accessors(
                type.GetProperty("Rank", InstanceFlags)!,
                type.GetProperty("Value", InstanceFlags)!,
                type.GetProperty("Children", InstanceFlags)!,
                type.GetProperty("Head", InstanceFlags)!,
                type.GetProperty("Tail", InstanceFlags)!);
            AccessorsByType.Add(type, accessors);
            return accessors;
        }

        /// <summary>
        /// How many references a measured operation added and released, for the retained-memory assertions.
        /// </summary>
        internal readonly record struct ReferenceChanges(int Shared, int Created, int Discarded);

        private readonly record struct Shape(long Size, int Height);

        private sealed record Accessors(
            PropertyInfo Rank,
            PropertyInfo Value,
            PropertyInfo Children,
            PropertyInfo Head,
            PropertyInfo Tail);

        private sealed record Decomposition(
            IReadOnlyList<object> ZeroChildren,
            IReadOnlyList<object> RankedChildren,
            object? EmbeddedForest);

        private sealed class ReferenceIdentityComparer : IEqualityComparer<object>
        {
            /// <summary>
            /// Gets the shared instance. The value carries no state, so one instance serves every caller.
            /// </summary>
            internal static ReferenceIdentityComparer Instance { get; } = new();

            /// <summary>Determines whether both values hold the same elements.</summary>
            public new bool Equals(object? left, object? right) => ReferenceEquals(left, right);

            /// <summary>Returns a hash consistent with <see cref="Equals"/>.</summary>
            public int GetHashCode(object value) => RuntimeHelpers.GetHashCode(value);
        }
    }
}
