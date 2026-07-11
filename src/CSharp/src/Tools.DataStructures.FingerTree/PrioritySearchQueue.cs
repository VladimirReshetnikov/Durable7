using System.Collections;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;

namespace Tools.DataStructures.FingerTree;

/// <summary>Stores one value/priority per ordered key with both search-tree and priority-queue queries.</summary>
/// <typeparam name="TKey">The ordered key type.</typeparam>
/// <typeparam name="TPriority">The priority type; lower values are selected first.</typeparam>
/// <typeparam name="TValue">The payload type.</typeparam>
[DebuggerDisplay("Count = {Count}, Height = {Height}")]
public sealed class PrioritySearchQueue<TKey, TPriority, TValue> :
    IEnumerable<PrioritySearchEntry<TKey, TPriority, TValue>>
{
    private readonly Node? _root;

    private PrioritySearchQueue(
        Node? root,
        IComparer<TKey> keyComparer,
        IComparer<TPriority> priorityComparer)
    {
        _root = root;
        KeyComparer = keyComparer;
        PriorityComparer = priorityComparer;
    }

    /// <summary>Gets the shared empty queue using default comparers.</summary>
    public static PrioritySearchQueue<TKey, TPriority, TValue> Empty { get; } =
        new(null, Comparer<TKey>.Default, Comparer<TPriority>.Default);

    /// <summary>Gets the number of keyed entries.</summary>
    public int Count => _root?.Count ?? 0;

    /// <summary>Gets whether the queue is empty.</summary>
    public bool IsEmpty => _root is null;

    /// <summary>Gets the AVL height for diagnostics.</summary>
    public int Height => _root?.Height ?? 0;

    /// <summary>Gets the retained key comparer.</summary>
    public IComparer<TKey> KeyComparer { get; }

    /// <summary>Gets the retained priority comparer.</summary>
    public IComparer<TPriority> PriorityComparer { get; }

    internal object? RootIdentity => _root;

    /// <summary>Creates an empty queue with retained comparers.</summary>
    /// <param name="keyComparer">The key comparer, or <see langword="null"/> for the default.</param>
    /// <param name="priorityComparer">The priority comparer, or <see langword="null"/> for the default.</param>
    /// <returns>An empty queue using the supplied policies.</returns>
    public static PrioritySearchQueue<TKey, TPriority, TValue> Create(
        IComparer<TKey>? keyComparer = null,
        IComparer<TPriority>? priorityComparer = null)
    {
        keyComparer ??= Comparer<TKey>.Default;
        priorityComparer ??= Comparer<TPriority>.Default;
        return ReferenceEquals(keyComparer, Comparer<TKey>.Default)
            && ReferenceEquals(priorityComparer, Comparer<TPriority>.Default)
            ? Empty
            : new PrioritySearchQueue<TKey, TPriority, TValue>(null, keyComparer, priorityComparer);
    }

    /// <summary>Creates a queue from entries with last-wins duplicate-key semantics.</summary>
    /// <param name="entries">The entries to add in enumeration order.</param>
    /// <param name="keyComparer">The key comparer, or <see langword="null"/> for the default.</param>
    /// <param name="priorityComparer">The priority comparer, or <see langword="null"/> for the default.</param>
    /// <returns>A queue containing the entries.</returns>
    public static PrioritySearchQueue<TKey, TPriority, TValue> CreateRange(
        IEnumerable<PrioritySearchEntry<TKey, TPriority, TValue>> entries,
        IComparer<TKey>? keyComparer = null,
        IComparer<TPriority>? priorityComparer = null)
    {
        ArgumentNullException.ThrowIfNull(entries);
        var result = Create(keyComparer, priorityComparer);
        foreach (var entry in entries)
            result = result.SetItem(entry.Key, entry.Priority, entry.Value);
        return result;
    }

    /// <summary>Determines whether a key is present.</summary>
    /// <param name="key">The key.</param>
    /// <returns><see langword="true"/> when present.</returns>
    public bool ContainsKey(TKey key) => TryGetEntry(key, out _);

    /// <summary>Tries to retrieve an entry by key.</summary>
    /// <param name="key">The key.</param>
    /// <param name="entry">The stored entry on success.</param>
    /// <returns><see langword="true"/> when present.</returns>
    public bool TryGetEntry(
        TKey key,
        [MaybeNullWhen(false)] out PrioritySearchEntry<TKey, TPriority, TValue> entry)
    {
        var node = _root;
        while (node is not null)
        {
            var comparison = KeyComparer.Compare(key, node.Entry.Key);
            if (comparison == 0)
            {
                entry = node.Entry;
                return true;
            }
            node = comparison < 0 ? node.Left : node.Right;
        }
        entry = default;
        return false;
    }

    /// <summary>Adds or replaces a keyed entry.</summary>
    /// <param name="key">The key.</param>
    /// <param name="priority">The new priority.</param>
    /// <param name="value">The new value.</param>
    /// <returns>The updated queue, or this queue for an equal entry.</returns>
    public PrioritySearchQueue<TKey, TPriority, TValue> SetItem(TKey key, TPriority priority, TValue value)
    {
        var entry = new PrioritySearchEntry<TKey, TPriority, TValue>(key, priority, value);
        var root = Set(_root, entry, overwrite: true, out _);
        return ReferenceEquals(root, _root) ? this : new(root, KeyComparer, PriorityComparer);
    }

    /// <summary>Attempts to add a key and rejects an existing equivalent key.</summary>
    /// <param name="key">The key.</param>
    /// <param name="priority">The priority.</param>
    /// <param name="value">The value.</param>
    /// <param name="result">The updated queue on success; otherwise, this queue.</param>
    /// <returns><see langword="true"/> when added.</returns>
    public bool TryAdd(
        TKey key,
        TPriority priority,
        TValue value,
        out PrioritySearchQueue<TKey, TPriority, TValue> result)
    {
        var root = Set(_root, new(key, priority, value), overwrite: false, out var added);
        result = added ? new(root, KeyComparer, PriorityComparer) : this;
        return added;
    }

    /// <summary>Removes a key when present.</summary>
    /// <param name="key">The key.</param>
    /// <returns>The updated queue, or this queue when absent.</returns>
    public PrioritySearchQueue<TKey, TPriority, TValue> Remove(TKey key)
    {
        var root = Remove(_root, key, out var removed, out _);
        if (!removed)
            return this;
        return root is null ? Create(KeyComparer, PriorityComparer) : new(root, KeyComparer, PriorityComparer);
    }

    /// <summary>Attempts to remove an entry by key.</summary>
    /// <param name="key">The key.</param>
    /// <param name="entry">The removed entry on success.</param>
    /// <param name="result">The updated queue on success; otherwise, this queue.</param>
    /// <returns><see langword="true"/> when removed.</returns>
    public bool TryRemove(
        TKey key,
        [MaybeNullWhen(false)] out PrioritySearchEntry<TKey, TPriority, TValue> entry,
        out PrioritySearchQueue<TKey, TPriority, TValue> result)
    {
        var root = Remove(_root, key, out var removed, out entry);
        if (!removed)
        {
            result = this;
            return false;
        }
        result = root is null ? Create(KeyComparer, PriorityComparer) : new(root, KeyComparer, PriorityComparer);
        return true;
    }

    /// <summary>Gets the globally minimum-priority entry in O(1).</summary>
    /// <exception cref="InvalidOperationException">The queue is empty.</exception>
    public PrioritySearchEntry<TKey, TPriority, TValue> Minimum =>
        _root?.Winner ?? throw new InvalidOperationException("The priority search queue is empty.");

    /// <summary>Tries to get the globally minimum-priority entry.</summary>
    /// <param name="entry">The minimum entry on success.</param>
    /// <returns><see langword="true"/> when nonempty.</returns>
    public bool TryGetMinimum(
        [MaybeNullWhen(false)] out PrioritySearchEntry<TKey, TPriority, TValue> entry)
    {
        if (_root is null)
        {
            entry = default;
            return false;
        }
        entry = _root.Winner;
        return true;
    }

    /// <summary>Removes the globally minimum-priority entry in O(log n).</summary>
    /// <param name="entry">The removed entry.</param>
    /// <returns>The remaining queue.</returns>
    /// <exception cref="InvalidOperationException">The queue is empty.</exception>
    public PrioritySearchQueue<TKey, TPriority, TValue> DeleteMinimum(
        out PrioritySearchEntry<TKey, TPriority, TValue> entry)
    {
        if (_root is null)
            throw new InvalidOperationException("The priority search queue is empty.");
        entry = _root.Winner;
        return Remove(entry.Key);
    }

    /// <summary>
    /// Enumerates entries whose keys are in an inclusive range and priorities are at most a threshold.
    /// </summary>
    /// <param name="minimumKey">The inclusive lower key bound.</param>
    /// <param name="maximumKey">The inclusive upper key bound.</param>
    /// <param name="maximumPriority">The inclusive priority threshold.</param>
    /// <returns>Qualifying entries in key order.</returns>
    /// <exception cref="ArgumentException">The key range is inverted.</exception>
    public IEnumerable<PrioritySearchEntry<TKey, TPriority, TValue>> EnumerateAtMost(
        TKey minimumKey,
        TKey maximumKey,
        TPriority maximumPriority)
    {
        if (KeyComparer.Compare(minimumKey, maximumKey) > 0)
            throw new ArgumentException("The minimum key must not follow the maximum key.", nameof(minimumKey));
        return EnumerateAtMostCore(_root, minimumKey, maximumKey, maximumPriority);
    }

    /// <summary>Returns an enumerator over all entries in key order.</summary>
    /// <returns>An enumerator.</returns>
    public IEnumerator<PrioritySearchEntry<TKey, TPriority, TValue>> GetEnumerator() =>
        Enumerate().GetEnumerator();

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

    private IEnumerable<PrioritySearchEntry<TKey, TPriority, TValue>> Enumerate()
    {
        var stack = new Stack<Node>();
        var node = _root;
        while (node is not null || stack.Count != 0)
        {
            while (node is not null)
            {
                stack.Push(node);
                node = node.Left;
            }
            node = stack.Pop();
            yield return node.Entry;
            node = node.Right;
        }
    }

    private IEnumerable<PrioritySearchEntry<TKey, TPriority, TValue>> EnumerateAtMostCore(
        Node? node,
        TKey minimumKey,
        TKey maximumKey,
        TPriority maximumPriority)
    {
        if (node is null || PriorityComparer.Compare(node.Winner.Priority, maximumPriority) > 0)
            yield break;
        var lower = KeyComparer.Compare(node.Entry.Key, minimumKey);
        var upper = KeyComparer.Compare(node.Entry.Key, maximumKey);
        if (lower > 0)
        {
            foreach (var entry in EnumerateAtMostCore(node.Left, minimumKey, maximumKey, maximumPriority))
                yield return entry;
        }
        if (lower >= 0 && upper <= 0 && PriorityComparer.Compare(node.Entry.Priority, maximumPriority) <= 0)
            yield return node.Entry;
        if (upper < 0)
        {
            foreach (var entry in EnumerateAtMostCore(node.Right, minimumKey, maximumKey, maximumPriority))
                yield return entry;
        }
    }

    private Node Set(
        Node? node,
        PrioritySearchEntry<TKey, TPriority, TValue> entry,
        bool overwrite,
        out bool added)
    {
        if (node is null)
        {
            added = true;
            return NewNode(entry, null, null);
        }
        var comparison = KeyComparer.Compare(entry.Key, node.Entry.Key);
        if (comparison == 0)
        {
            added = false;
            if (!overwrite
                || (EqualityComparer<TPriority>.Default.Equals(entry.Priority, node.Entry.Priority)
                    && EqualityComparer<TValue>.Default.Equals(entry.Value, node.Entry.Value)))
                return node;
            return NewNode(new(node.Entry.Key, entry.Priority, entry.Value), node.Left, node.Right);
        }
        if (comparison < 0)
        {
            var left = Set(node.Left, entry, overwrite, out added);
            return ReferenceEquals(left, node.Left) ? node : Balance(NewNode(node.Entry, left, node.Right));
        }
        var right = Set(node.Right, entry, overwrite, out added);
        return ReferenceEquals(right, node.Right) ? node : Balance(NewNode(node.Entry, node.Left, right));
    }

    private Node? Remove(
        Node? node,
        TKey key,
        out bool removed,
        [MaybeNullWhen(false)] out PrioritySearchEntry<TKey, TPriority, TValue> entry)
    {
        if (node is null)
        {
            removed = false;
            entry = default;
            return null;
        }
        var comparison = KeyComparer.Compare(key, node.Entry.Key);
        if (comparison == 0)
        {
            removed = true;
            entry = node.Entry;
            if (node.Left is null)
                return node.Right;
            if (node.Right is null)
                return node.Left;
            var successor = MinimumKeyNode(node.Right);
            var right = Remove(node.Right, successor.Entry.Key, out _, out _);
            return Balance(NewNode(successor.Entry, node.Left, right));
        }
        if (comparison < 0)
        {
            var left = Remove(node.Left, key, out removed, out entry);
            return removed ? Balance(NewNode(node.Entry, left, node.Right)) : node;
        }
        var newRight = Remove(node.Right, key, out removed, out entry);
        return removed ? Balance(NewNode(node.Entry, node.Left, newRight)) : node;
    }

    private Node Balance(Node node)
    {
        var factor = HeightOf(node.Left) - HeightOf(node.Right);
        if (factor > 1)
        {
            if (HeightOf(node.Left!.Left) < HeightOf(node.Left.Right))
                return RotateRight(NewNode(node.Entry, RotateLeft(node.Left), node.Right));
            return RotateRight(node);
        }
        if (factor < -1)
        {
            if (HeightOf(node.Right!.Right) < HeightOf(node.Right.Left))
                return RotateLeft(NewNode(node.Entry, node.Left, RotateRight(node.Right)));
            return RotateLeft(node);
        }
        return node;
    }

    private Node RotateLeft(Node node)
    {
        var pivot = node.Right!;
        return NewNode(pivot.Entry, NewNode(node.Entry, node.Left, pivot.Left), pivot.Right);
    }

    private Node RotateRight(Node node)
    {
        var pivot = node.Left!;
        return NewNode(pivot.Entry, pivot.Left, NewNode(node.Entry, pivot.Right, node.Right));
    }

    private Node NewNode(
        PrioritySearchEntry<TKey, TPriority, TValue> entry,
        Node? left,
        Node? right)
    {
        var winner = entry;
        if (left is not null && IsBefore(left.Winner, winner))
            winner = left.Winner;
        if (right is not null && IsBefore(right.Winner, winner))
            winner = right.Winner;
        return new Node(entry, left, right, winner);
    }

    private bool IsBefore(
        PrioritySearchEntry<TKey, TPriority, TValue> left,
        PrioritySearchEntry<TKey, TPriority, TValue> right)
    {
        var priority = PriorityComparer.Compare(left.Priority, right.Priority);
        return priority < 0 || (priority == 0 && KeyComparer.Compare(left.Key, right.Key) < 0);
    }

    private static int HeightOf(Node? node) => node?.Height ?? 0;

    private static Node MinimumKeyNode(Node node)
    {
        while (node.Left is not null)
            node = node.Left;
        return node;
    }

    private sealed class Node(
        PrioritySearchEntry<TKey, TPriority, TValue> entry,
        Node? left,
        Node? right,
        PrioritySearchEntry<TKey, TPriority, TValue> winner)
    {
        internal PrioritySearchEntry<TKey, TPriority, TValue> Entry { get; } = entry;
        internal Node? Left { get; } = left;
        internal Node? Right { get; } = right;
        internal PrioritySearchEntry<TKey, TPriority, TValue> Winner { get; } = winner;
        internal int Height { get; } = checked(1 + Math.Max(left?.Height ?? 0, right?.Height ?? 0));
        internal int Count { get; } = checked(1 + (left?.Count ?? 0) + (right?.Count ?? 0));
    }
}

/// <summary>Represents one keyed entry in a priority search queue.</summary>
/// <typeparam name="TKey">The key type.</typeparam>
/// <typeparam name="TPriority">The priority type.</typeparam>
/// <typeparam name="TValue">The payload type.</typeparam>
/// <param name="Key">The ordered unique key.</param>
/// <param name="Priority">The priority; lower values are selected first.</param>
/// <param name="Value">The payload.</param>
public readonly record struct PrioritySearchEntry<TKey, TPriority, TValue>(
    TKey Key,
    TPriority Priority,
    TValue Value);
