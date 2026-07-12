using System.Diagnostics.CodeAnalysis;
using System.Numerics;

namespace Tools.DataStructures.Hamt.Internal;

internal interface IPatriciaKey<TKey>
{
    static abstract ulong Encode(TKey key);

    static abstract TKey Decode(ulong bits);
}

internal readonly struct Int32PatriciaKey : IPatriciaKey<int>
{
    public static ulong Encode(int key) => (uint)key ^ 0x80000000u;

    public static int Decode(ulong bits) => unchecked((int)((uint)bits ^ 0x80000000u));
}

internal readonly struct Int64PatriciaKey : IPatriciaKey<long>
{
    public static ulong Encode(long key) => (ulong)key ^ 0x8000000000000000UL;

    public static long Decode(ulong bits) => unchecked((long)(bits ^ 0x8000000000000000UL));
}

internal sealed class PatriciaMapCore<TKey, TValue, TKeyPolicy>
    where TKeyPolicy : struct, IPatriciaKey<TKey>
{
    internal static PatriciaMapCore<TKey, TValue, TKeyPolicy> Empty { get; } = new(root: null, count: 0);

    private readonly Node? _root;

    private PatriciaMapCore(Node? root, int count)
    {
        _root = root;
        Count = count;
    }

    internal int Count { get; }

    internal object? RootIdentity => _root;

    internal bool TryGetValue(TKey key, [MaybeNullWhen(false)] out TValue value)
    {
        var bits = TKeyPolicy.Encode(key);
        var node = _root;
        while (node is Branch branch)
        {
            if (!Matches(bits, branch.Prefix, branch.Mask))
            {
                value = default;
                return false;
            }

            node = GoesLeft(bits, branch.Mask) ? branch.Left : branch.Right;
        }

        if (node is Leaf leaf && leaf.Key == bits)
        {
            value = leaf.Value;
            return true;
        }

        value = default;
        return false;
    }

    internal PatriciaMapCore<TKey, TValue, TKeyPolicy> SetItem(TKey key, TValue value, bool overwrite, out bool added)
    {
        var bits = TKeyPolicy.Encode(key);
        if (_root is null)
        {
            added = true;
            return new PatriciaMapCore<TKey, TValue, TKeyPolicy>(new Leaf(bits, value), 1);
        }

        var root = Set(_root, bits, value, overwrite, out added);
        if (ReferenceEquals(root, _root))
            return this;
        return new PatriciaMapCore<TKey, TValue, TKeyPolicy>(root, checked(Count + (added ? 1 : 0)));
    }

    internal PatriciaMapCore<TKey, TValue, TKeyPolicy> Remove(
        TKey key,
        out bool removed,
        [MaybeNullWhen(false)] out TValue value)
    {
        if (_root is null)
        {
            removed = false;
            value = default!;
            return this;
        }

        var root = Remove(_root, TKeyPolicy.Encode(key), out removed, out value);
        if (!removed)
            return this;
        return root is null
            ? Empty
            : new PatriciaMapCore<TKey, TValue, TKeyPolicy>(root, checked(Count - 1));
    }

    internal IEnumerable<KeyValuePair<TKey, TValue>> Enumerate()
    {
        if (_root is null)
            yield break;

        var stack = new Stack<Node>();
        stack.Push(_root);
        while (stack.TryPop(out var node))
        {
            if (node is Leaf leaf)
            {
                yield return KeyValuePair.Create(TKeyPolicy.Decode(leaf.Key), leaf.Value);
                continue;
            }

            var branch = (Branch)node;
            stack.Push(branch.Right);
            stack.Push(branch.Left);
        }
    }

    internal PatriciaMapCore<TKey, TValue, TKeyPolicy> UnionRight(
        PatriciaMapCore<TKey, TValue, TKeyPolicy> other)
    {
        if (_root is null)
            return other;
        if (other._root is null || ReferenceEquals(_root, other._root))
            return this;
        var root = UnionRight(_root, other._root);
        if (ReferenceEquals(root, _root))
            return this;
        if (ReferenceEquals(root, other._root))
            return other;
        return new PatriciaMapCore<TKey, TValue, TKeyPolicy>(root, root.Count);
    }

    internal PatriciaMapCore<TKey, TValue, TKeyPolicy> Union(
        PatriciaMapCore<TKey, TValue, TKeyPolicy> other,
        Func<TKey, TValue, TValue, TValue> combine)
    {
        if (_root is null)
            return other;
        if (other._root is null)
            return this;
        var root = Union(_root, other._root, combine);
        if (ReferenceEquals(root, _root))
            return this;
        if (ReferenceEquals(root, other._root))
            return other;
        return new PatriciaMapCore<TKey, TValue, TKeyPolicy>(root, root.Count);
    }

    internal PatriciaMapCore<TKey, TValue, TKeyPolicy> IntersectLeft(
        PatriciaMapCore<TKey, TValue, TKeyPolicy> other)
    {
        if (_root is null || other._root is null)
            return Empty;
        if (ReferenceEquals(_root, other._root))
            return this;
        var root = IntersectLeft(_root, other._root);
        if (root is null)
            return Empty;
        if (ReferenceEquals(root, _root))
            return this;
        return new PatriciaMapCore<TKey, TValue, TKeyPolicy>(root, root.Count);
    }

    internal PatriciaMapCore<TKey, TValue, TKeyPolicy> Intersect(
        PatriciaMapCore<TKey, TValue, TKeyPolicy> other,
        Func<TKey, TValue, TValue, TValue> combine)
    {
        if (_root is null || other._root is null)
            return Empty;
        var root = Intersect(_root, other._root, combine);
        if (root is null)
            return Empty;
        if (ReferenceEquals(root, _root))
            return this;
        if (ReferenceEquals(root, other._root))
            return other;
        return new PatriciaMapCore<TKey, TValue, TKeyPolicy>(root, root.Count);
    }

    internal PatriciaMapCore<TKey, TValue, TKeyPolicy> Except(
        PatriciaMapCore<TKey, TValue, TKeyPolicy> other)
    {
        if (_root is null || other._root is null)
            return this;
        if (ReferenceEquals(_root, other._root))
            return Empty;
        var root = Except(_root, other._root);
        if (root is null)
            return Empty;
        if (ReferenceEquals(root, _root))
            return this;
        return new PatriciaMapCore<TKey, TValue, TKeyPolicy>(root, root.Count);
    }

    private static Node Set(Node node, ulong key, TValue value, bool overwrite, out bool added)
    {
        if (node is Leaf leaf)
        {
            if (leaf.Key == key)
            {
                added = false;
                if (!overwrite || ValuesEqual(leaf.Value, value))
                    return leaf;
                return new Leaf(key, value);
            }

            added = true;
            return Join(key, new Leaf(key, value), leaf.Key, leaf);
        }

        var branch = (Branch)node;
        if (!Matches(key, branch.Prefix, branch.Mask))
        {
            added = true;
            return Join(key, new Leaf(key, value), branch.Prefix, branch);
        }

        if (GoesLeft(key, branch.Mask))
        {
            var left = Set(branch.Left, key, value, overwrite, out added);
            return ReferenceEquals(left, branch.Left)
                ? branch
                : new Branch(branch.Prefix, branch.Mask, left, branch.Right);
        }

        var right = Set(branch.Right, key, value, overwrite, out added);
        return ReferenceEquals(right, branch.Right)
            ? branch
            : new Branch(branch.Prefix, branch.Mask, branch.Left, right);
    }

    private static Node? Remove(Node node, ulong key, out bool removed, out TValue value)
    {
        if (node is Leaf leaf)
        {
            if (leaf.Key == key)
            {
                removed = true;
                value = leaf.Value;
                return null;
            }

            removed = false;
            value = default!;
            return leaf;
        }

        var branch = (Branch)node;
        if (!Matches(key, branch.Prefix, branch.Mask))
        {
            removed = false;
            value = default!;
            return branch;
        }

        if (GoesLeft(key, branch.Mask))
        {
            var left = Remove(branch.Left, key, out removed, out value);
            if (!removed)
                return branch;
            return left is null ? branch.Right : new Branch(branch.Prefix, branch.Mask, left, branch.Right);
        }

        var right = Remove(branch.Right, key, out removed, out value);
        if (!removed)
            return branch;
        return right is null ? branch.Left : new Branch(branch.Prefix, branch.Mask, branch.Left, right);
    }

    private static Node UnionRight(Node left, Node right)
    {
        if (ReferenceEquals(left, right))
            return left;
        if (left is Leaf leftLeaf)
            return Set(right, leftLeaf.Key, leftLeaf.Value, overwrite: false, out _);
        if (right is Leaf rightLeaf)
            return Set(left, rightLeaf.Key, rightLeaf.Value, overwrite: true, out _);

        var l = (Branch)left;
        var r = (Branch)right;
        if (l.Mask == r.Mask && l.Prefix == r.Prefix)
            return Rebuild(l, UnionRight(l.Left, r.Left), UnionRight(l.Right, r.Right));

        if (l.Mask > r.Mask && Matches(r.Prefix, l.Prefix, l.Mask))
        {
            return GoesLeft(r.Prefix, l.Mask)
                ? Rebuild(l, UnionRight(l.Left, right), l.Right)
                : Rebuild(l, l.Left, UnionRight(l.Right, right));
        }

        if (r.Mask > l.Mask && Matches(l.Prefix, r.Prefix, r.Mask))
        {
            return GoesLeft(l.Prefix, r.Mask)
                ? Rebuild(r, UnionRight(left, r.Left), r.Right)
                : Rebuild(r, r.Left, UnionRight(left, r.Right));
        }

        return Join(l.Prefix, l, r.Prefix, r);
    }

    private static Node Union(Node left, Node right, Func<TKey, TValue, TValue, TValue> combine)
    {
        if (left is Leaf leftOnly && right is Leaf rightOnly)
        {
            return leftOnly.Key == rightOnly.Key
                ? CombinedLeaf(leftOnly, rightOnly, combine)
                : Join(leftOnly.Key, leftOnly, rightOnly.Key, rightOnly);
        }
        if (left is Leaf leftLeaf)
        {
            var matchingRight = FindLeaf(right, leftLeaf.Key);
            var value = matchingRight is null
                ? leftLeaf.Value
                : combine(TKeyPolicy.Decode(leftLeaf.Key), leftLeaf.Value, matchingRight.Value);
            return Set(right, leftLeaf.Key, value, overwrite: true, out _);
        }
        if (right is Leaf rightLeaf)
        {
            var matchingLeft = FindLeaf(left, rightLeaf.Key);
            var value = matchingLeft is null
                ? rightLeaf.Value
                : combine(TKeyPolicy.Decode(rightLeaf.Key), matchingLeft.Value, rightLeaf.Value);
            return Set(left, rightLeaf.Key, value, overwrite: true, out _);
        }

        var l = (Branch)left;
        var r = (Branch)right;
        if (l.Mask == r.Mask && l.Prefix == r.Prefix)
            return Rebuild(l, Union(l.Left, r.Left, combine), Union(l.Right, r.Right, combine));
        if (l.Mask > r.Mask && Matches(r.Prefix, l.Prefix, l.Mask))
        {
            return GoesLeft(r.Prefix, l.Mask)
                ? Rebuild(l, Union(l.Left, right, combine), l.Right)
                : Rebuild(l, l.Left, Union(l.Right, right, combine));
        }
        if (r.Mask > l.Mask && Matches(l.Prefix, r.Prefix, r.Mask))
        {
            return GoesLeft(l.Prefix, r.Mask)
                ? Rebuild(r, Union(left, r.Left, combine), r.Right)
                : Rebuild(r, r.Left, Union(left, r.Right, combine));
        }
        return Join(l.Prefix, l, r.Prefix, r);
    }

    private static Node? IntersectLeft(Node left, Node right)
    {
        if (ReferenceEquals(left, right))
            return left;
        if (left is Leaf leftLeaf)
            return Contains(right, leftLeaf.Key) ? leftLeaf : null;
        if (right is Leaf rightLeaf)
            return FindLeaf(left, rightLeaf.Key);

        var l = (Branch)left;
        var r = (Branch)right;
        if (l.Mask == r.Mask && l.Prefix == r.Prefix)
            return Collapse(l, IntersectLeft(l.Left, r.Left), IntersectLeft(l.Right, r.Right));
        if (l.Mask > r.Mask && Matches(r.Prefix, l.Prefix, l.Mask))
            return IntersectLeft(GoesLeft(r.Prefix, l.Mask) ? l.Left : l.Right, right);
        if (r.Mask > l.Mask && Matches(l.Prefix, r.Prefix, r.Mask))
            return IntersectLeft(left, GoesLeft(l.Prefix, r.Mask) ? r.Left : r.Right);
        return null;
    }

    private static Node? Intersect(Node left, Node right, Func<TKey, TValue, TValue, TValue> combine)
    {
        if (left is Leaf leftLeaf)
        {
            var matchingRight = FindLeaf(right, leftLeaf.Key);
            return matchingRight is null ? null : CombinedLeaf(leftLeaf, matchingRight, combine);
        }
        if (right is Leaf rightLeaf)
        {
            var matchingLeft = FindLeaf(left, rightLeaf.Key);
            return matchingLeft is null ? null : CombinedLeaf(matchingLeft, rightLeaf, combine);
        }

        var l = (Branch)left;
        var r = (Branch)right;
        if (l.Mask == r.Mask && l.Prefix == r.Prefix)
            return Collapse(l, Intersect(l.Left, r.Left, combine), Intersect(l.Right, r.Right, combine));
        if (l.Mask > r.Mask && Matches(r.Prefix, l.Prefix, l.Mask))
            return Intersect(GoesLeft(r.Prefix, l.Mask) ? l.Left : l.Right, right, combine);
        if (r.Mask > l.Mask && Matches(l.Prefix, r.Prefix, r.Mask))
            return Intersect(left, GoesLeft(l.Prefix, r.Mask) ? r.Left : r.Right, combine);
        return null;
    }

    private static Leaf CombinedLeaf(
        Leaf left,
        Leaf right,
        Func<TKey, TValue, TValue, TValue> combine)
    {
        var value = combine(TKeyPolicy.Decode(left.Key), left.Value, right.Value);
        if (ValuesEqual(value, left.Value))
            return left;
        if (ValuesEqual(value, right.Value))
            return right;
        return new Leaf(left.Key, value);
    }

    private static Node? Except(Node left, Node right)
    {
        if (ReferenceEquals(left, right))
            return null;
        if (left is Leaf leftLeaf)
            return Contains(right, leftLeaf.Key) ? null : leftLeaf;
        if (right is Leaf rightLeaf)
            return Remove(left, rightLeaf.Key, out _, out _);

        var l = (Branch)left;
        var r = (Branch)right;
        if (l.Mask == r.Mask && l.Prefix == r.Prefix)
            return Collapse(l, Except(l.Left, r.Left), Except(l.Right, r.Right));
        if (l.Mask > r.Mask && Matches(r.Prefix, l.Prefix, l.Mask))
        {
            return GoesLeft(r.Prefix, l.Mask)
                ? Collapse(l, Except(l.Left, right), l.Right)
                : Collapse(l, l.Left, Except(l.Right, right));
        }
        if (r.Mask > l.Mask && Matches(l.Prefix, r.Prefix, r.Mask))
            return Except(left, GoesLeft(l.Prefix, r.Mask) ? r.Left : r.Right);
        return left;
    }

    private static bool Contains(Node node, ulong key) => FindLeaf(node, key) is not null;

    private static Leaf? FindLeaf(Node node, ulong key)
    {
        while (node is Branch branch)
        {
            if (!Matches(key, branch.Prefix, branch.Mask))
                return null;
            node = GoesLeft(key, branch.Mask) ? branch.Left : branch.Right;
        }
        var leaf = (Leaf)node;
        return leaf.Key == key ? leaf : null;
    }

    private static Node Rebuild(Branch original, Node left, Node right) =>
        ReferenceEquals(left, original.Left) && ReferenceEquals(right, original.Right)
            ? original
            : new Branch(original.Prefix, original.Mask, left, right);

    private static Node? Collapse(Branch original, Node? left, Node? right)
    {
        if (left is null)
            return right;
        if (right is null)
            return left;
        return Rebuild(original, left, right);
    }

    private static Node Join(ulong leftKey, Node left, ulong rightKey, Node right)
    {
        var mask = HighestBit(leftKey ^ rightKey);
        var prefix = PrefixOf(leftKey, mask);
        return GoesLeft(leftKey, mask)
            ? new Branch(prefix, mask, left, right)
            : new Branch(prefix, mask, right, left);
    }

    private static ulong HighestBit(ulong value) => 1UL << (63 - BitOperations.LeadingZeroCount(value));

    private static ulong PrefixOf(ulong key, ulong mask) => key & ~((mask - 1) | mask);

    private static bool Matches(ulong key, ulong prefix, ulong mask) => PrefixOf(key, mask) == prefix;

    private static bool GoesLeft(ulong key, ulong mask) => (key & mask) == 0;

    private static bool ValuesEqual(TValue? left, TValue? right) =>
        (!typeof(TValue).IsValueType && ReferenceEquals(left, right)) ||
        EqualityComparer<TValue>.Default.Equals(left!, right!);

    private abstract class Node
    {
        internal abstract int Count { get; }
    }

    private sealed class Leaf(ulong key, TValue value) : Node
    {
        internal override int Count => 1;

        internal ulong Key { get; } = key;

        internal TValue Value { get; } = value;
    }

    private sealed class Branch(ulong prefix, ulong mask, Node left, Node right) : Node
    {
        internal override int Count { get; } = checked(left.Count + right.Count);

        internal ulong Prefix { get; } = prefix;

        internal ulong Mask { get; } = mask;

        internal Node Left { get; } = left;

        internal Node Right { get; } = right;
    }
}
