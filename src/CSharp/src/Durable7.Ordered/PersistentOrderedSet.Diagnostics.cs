// The structural diagnostics for the persistent ordered set, used by the tests to assert on shape
// and sharing.

using System.Diagnostics;

namespace Durable7.Ordered;

public sealed partial class PersistentOrderedSet<T>
{
    /// <summary>
    /// Recomputes the independently owned dual-index invariants. Test-only; O(n log n) in the
    /// conservative public-substrate model.
    /// </summary>
    internal void ValidateInvariants()
    {
        if (_order.Count != _stamps.Count)
            throw new InvalidOperationException("The order and membership indexes have different counts.");

        var hasPrevious = false;
        var previous = 0L;
        foreach (var entry in _order)
        {
            if (hasPrevious && entry.Stamp <= previous)
                throw new InvalidOperationException("Order-maintenance stamps are not strictly ascending.");
            hasPrevious = true;
            previous = entry.Stamp;

            if (!_stamps.TryGetValue(entry.Item, out var indexedStamp) || indexedStamp != entry.Stamp)
                throw new InvalidOperationException("An ordered representative has no matching membership stamp.");
            if (!_stamps.TryGetKey(entry.Item, out var indexedRepresentative) ||
                !SameRepresentative(entry.Item, indexedRepresentative))
            {
                throw new InvalidOperationException("The order and membership indexes retain different representatives.");
            }
        }

        foreach (var pair in _stamps)
        {
            var position = IndexOfStamp(pair.Value);
            var ordered = _order[position];
            if (!Comparer.Equals(pair.Key, ordered.Item) || !SameRepresentative(pair.Key, ordered.Item))
                throw new InvalidOperationException("A membership entry has no matching ordered representative.");
        }
    }

    private static bool SameRepresentative(T left, T right) =>
        typeof(T).IsValueType
            ? EqualityComparer<T>.Default.Equals(left, right)
            : ReferenceEquals(left, right);
}

/// <summary>
/// The debugger's view of an insertion-ordered set: its elements in order, rather than its two
/// indexes.
/// </summary>
internal sealed class PersistentOrderedSetDebugView<T>(PersistentOrderedSet<T> set)
{
    /// <summary>Gets the elements.</summary>
    [DebuggerBrowsable(DebuggerBrowsableState.RootHidden)]
    public T[] Items => set.ToArray();
}
