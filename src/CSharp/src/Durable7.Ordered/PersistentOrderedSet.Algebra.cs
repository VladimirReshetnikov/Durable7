using Durable7.Hamt;

namespace Durable7.Ordered;

public sealed partial class PersistentOrderedSet<T>
{
    /// <summary>
    /// Returns the receiver-policy union with another ordered set.
    /// </summary>
    /// <param name="other">The argument set.</param>
    /// <returns>
    /// Receiver representatives in receiver order followed by new argument representatives in
    /// argument order.
    /// </returns>
    /// <remarks>
    /// The entire argument is eagerly normalized under this set's comparer, even when the two sets
    /// retain different comparer objects. The first encountered argument representative wins each
    /// collapsed argument class; enumeration/comparer failure occurs before a result is returned.
    /// </remarks>
    public PersistentOrderedSet<T> Union(PersistentOrderedSet<T> other)
    {
        ArgumentNullException.ThrowIfNull(other);
        return UnionCore(other);
    }

    /// <summary>
    /// Returns the receiver-policy union with an enumerable sequence.
    /// </summary>
    /// <param name="other">Argument values in their contribution order.</param>
    /// <returns>
    /// Receiver representatives in receiver order followed by new argument representatives in
    /// argument order.
    /// </returns>
    /// <remarks>
    /// Eager receiver-comparer normalization retains the first argument representative of each
    /// collapsed class and completes before any result-specific shortcut.
    /// </remarks>
    public PersistentOrderedSet<T> Union(IEnumerable<T> other)
    {
        ArgumentNullException.ThrowIfNull(other);
        return UnionCore(other);
    }

    /// <summary>
    /// Returns the receiver-policy intersection with another ordered set.
    /// </summary>
    /// <param name="other">The argument set.</param>
    /// <returns>Retained receiver representatives in receiver order.</returns>
    /// <remarks>
    /// Eager receiver-comparer normalization retains the first argument representative of each
    /// collapsed class; enumeration/comparer failure occurs before a result is returned.
    /// </remarks>
    public PersistentOrderedSet<T> Intersect(PersistentOrderedSet<T> other)
    {
        ArgumentNullException.ThrowIfNull(other);
        return IntersectCore(other);
    }

    /// <summary>
    /// Returns the receiver-policy intersection with an enumerable sequence.
    /// </summary>
    /// <param name="other">The argument values.</param>
    /// <returns>Retained receiver representatives in receiver order.</returns>
    /// <remarks>
    /// Eager receiver-comparer normalization retains the first argument representative of each
    /// collapsed class and completes before any result-specific shortcut.
    /// </remarks>
    public PersistentOrderedSet<T> Intersect(IEnumerable<T> other)
    {
        ArgumentNullException.ThrowIfNull(other);
        return IntersectCore(other);
    }

    /// <summary>
    /// Returns this set without equivalence classes present in another ordered set.
    /// </summary>
    /// <param name="other">The argument set.</param>
    /// <returns>Retained receiver representatives in receiver order.</returns>
    /// <remarks>
    /// Eager receiver-comparer normalization retains the first argument representative of each
    /// collapsed class; enumeration/comparer failure occurs before a result is returned.
    /// </remarks>
    public PersistentOrderedSet<T> Except(PersistentOrderedSet<T> other)
    {
        ArgumentNullException.ThrowIfNull(other);
        return ExceptCore(other);
    }

    /// <summary>
    /// Returns this set without equivalence classes present in an enumerable sequence.
    /// </summary>
    /// <param name="other">The argument values.</param>
    /// <returns>Retained receiver representatives in receiver order.</returns>
    /// <remarks>
    /// Eager receiver-comparer normalization retains the first argument representative of each
    /// collapsed class and completes before any result-specific shortcut.
    /// </remarks>
    public PersistentOrderedSet<T> Except(IEnumerable<T> other)
    {
        ArgumentNullException.ThrowIfNull(other);
        return ExceptCore(other);
    }

    /// <summary>
    /// Returns the receiver-policy symmetric difference with another ordered set.
    /// </summary>
    /// <param name="other">The argument set.</param>
    /// <returns>
    /// Receiver-only representatives in receiver order followed by argument-only representatives in
    /// argument order.
    /// </returns>
    /// <remarks>
    /// Eager receiver-comparer normalization retains the first argument representative of each
    /// collapsed class; enumeration/comparer failure occurs before a result is returned.
    /// </remarks>
    public PersistentOrderedSet<T> SymmetricExcept(PersistentOrderedSet<T> other)
    {
        ArgumentNullException.ThrowIfNull(other);
        return SymmetricExceptCore(other);
    }

    /// <summary>
    /// Returns the receiver-policy symmetric difference with an enumerable sequence.
    /// </summary>
    /// <param name="other">The argument values.</param>
    /// <returns>
    /// Receiver-only representatives in receiver order followed by argument-only representatives in
    /// first-occurrence argument order.
    /// </returns>
    /// <remarks>
    /// Eager receiver-comparer normalization completes before the empty-argument identity shortcut;
    /// enumeration/comparer failure occurs before a result is returned.
    /// </remarks>
    public PersistentOrderedSet<T> SymmetricExcept(IEnumerable<T> other)
    {
        ArgumentNullException.ThrowIfNull(other);
        return SymmetricExceptCore(other);
    }

    /// <summary>
    /// Determines whether every receiver class occurs in an enumerable argument after receiver-policy
    /// normalization.
    /// </summary>
    /// <param name="other">The argument values.</param>
    /// <remarks>
    /// The entire argument is eagerly collapsed under the receiver comparer in enumeration order;
    /// the first representative wins and late argument failures are not hidden by a count shortcut.
    /// </remarks>
    public bool IsSubsetOf(IEnumerable<T> other)
    {
        ArgumentNullException.ThrowIfNull(other);
        var argument = Normalize(other);
        return Count <= argument.Items.Length && EveryReceiverOccursIn(argument.Membership);
    }

    /// <summary>
    /// Determines whether the receiver is a strict receiver-policy subset of an enumerable argument.
    /// </summary>
    /// <param name="other">The argument values.</param>
    /// <remarks>
    /// The entire argument is eagerly collapsed under the receiver comparer in enumeration order;
    /// the first representative wins and late argument failures are not hidden by a count shortcut.
    /// </remarks>
    public bool IsProperSubsetOf(IEnumerable<T> other)
    {
        ArgumentNullException.ThrowIfNull(other);
        var argument = Normalize(other);
        return Count < argument.Items.Length && EveryReceiverOccursIn(argument.Membership);
    }

    /// <summary>
    /// Determines whether every receiver-policy argument class occurs in this set.
    /// </summary>
    /// <param name="other">The argument values.</param>
    /// <remarks>
    /// The entire argument is eagerly collapsed under the receiver comparer before membership checks;
    /// the first representative of each argument class is retained.
    /// </remarks>
    public bool IsSupersetOf(IEnumerable<T> other)
    {
        ArgumentNullException.ThrowIfNull(other);
        var argument = Normalize(other);
        return Count >= argument.Items.Length && EveryArgumentOccursHere(argument.Items);
    }

    /// <summary>
    /// Determines whether the receiver is a strict receiver-policy superset of an enumerable argument.
    /// </summary>
    /// <param name="other">The argument values.</param>
    /// <remarks>
    /// The entire argument is eagerly collapsed under the receiver comparer before membership checks;
    /// late enumeration/comparer failures are not hidden by a count shortcut.
    /// </remarks>
    public bool IsProperSupersetOf(IEnumerable<T> other)
    {
        ArgumentNullException.ThrowIfNull(other);
        var argument = Normalize(other);
        return Count > argument.Items.Length && EveryArgumentOccursHere(argument.Items);
    }

    /// <summary>
    /// Determines whether the receiver and argument share a receiver-policy equivalence class.
    /// </summary>
    /// <param name="other">The argument values.</param>
    /// <remarks>
    /// The entire argument is eagerly collapsed under the receiver comparer before overlap checks;
    /// a decisive early element does not hide a later enumeration/comparer failure.
    /// </remarks>
    public bool Overlaps(IEnumerable<T> other)
    {
        ArgumentNullException.ThrowIfNull(other);
        var argument = Normalize(other);
        foreach (var item in argument.Items)
        {
            if (_stamps.ContainsKey(item))
                return true;
        }
        return false;
    }

    /// <summary>
    /// Determines whether the receiver and argument contain the same receiver-policy equivalence classes.
    /// </summary>
    /// <param name="other">The argument values.</param>
    /// <remarks>
    /// The entire argument is eagerly collapsed under the receiver comparer before count or membership
    /// checks; the first representative of each argument class is retained.
    /// </remarks>
    public bool SetEquals(IEnumerable<T> other)
    {
        ArgumentNullException.ThrowIfNull(other);
        var argument = Normalize(other);
        return Count == argument.Items.Length && EveryArgumentOccursHere(argument.Items);
    }

    private PersistentOrderedSet<T> UnionCore(IEnumerable<T> other)
    {
        var argument = Normalize(other);
        List<T>? result = null;
        foreach (var item in argument.Items)
        {
            if (_stamps.ContainsKey(item))
                continue;

            result ??= new List<T>(ToArray());
            AddChecked(result, item);
        }

        return result is null ? this : BuildFromItems(result, Comparer);
    }

    private PersistentOrderedSet<T> IntersectCore(IEnumerable<T> other)
    {
        var argument = Normalize(other);
        List<T> result = new(Math.Min(Count, argument.Items.Length));
        foreach (var entry in _order)
        {
            if (argument.Membership.ContainsKey(entry.Item))
                result.Add(entry.Item);
        }

        return result.Count == Count ? this : BuildFromItems(result, Comparer);
    }

    private PersistentOrderedSet<T> ExceptCore(IEnumerable<T> other)
    {
        var argument = Normalize(other);
        List<T>? result = null;
        var position = 0;
        foreach (var entry in _order)
        {
            if (argument.Membership.ContainsKey(entry.Item))
            {
                result ??= Prefix(position);
            }
            else
            {
                result?.Add(entry.Item);
            }
            position++;
        }

        return result is null ? this : BuildFromItems(result, Comparer);
    }

    private PersistentOrderedSet<T> SymmetricExceptCore(IEnumerable<T> other)
    {
        var argument = Normalize(other);
        if (argument.Items.Length == 0)
            return this;

        List<T> result = [];
        foreach (var entry in _order)
        {
            if (!argument.Membership.ContainsKey(entry.Item))
                result.Add(entry.Item);
        }
        foreach (var item in argument.Items)
        {
            if (!_stamps.ContainsKey(item))
                AddChecked(result, item);
        }

        return BuildFromItems(result, Comparer);
    }

    private NormalizedArgument Normalize(IEnumerable<T> other)
    {
        var membership = PersistentHashMap<T, byte>.Create(Comparer);
        List<T> items = [];
        foreach (var item in other)
        {
            if (!membership.TryAdd(item, 0, out var next))
                continue;
            membership = next;
            items.Add(item);
        }
        return new(items.ToArray(), membership);
    }

    private bool EveryReceiverOccursIn(PersistentHashMap<T, byte> membership)
    {
        foreach (var entry in _order)
        {
            if (!membership.ContainsKey(entry.Item))
                return false;
        }
        return true;
    }

    private bool EveryArgumentOccursHere(IEnumerable<T> items)
    {
        foreach (var item in items)
        {
            if (!_stamps.ContainsKey(item))
                return false;
        }
        return true;
    }

    private List<T> Prefix(int count)
    {
        List<T> result = new(count);
        var index = 0;
        foreach (var entry in _order)
        {
            if (index++ == count)
                break;
            result.Add(entry.Item);
        }
        return result;
    }

    private static void AddChecked(List<T> target, T item)
    {
        if (target.Count == int.MaxValue)
            throw CountOverflowError();
        target.Add(item);
    }

    private readonly record struct NormalizedArgument(
        T[] Items,
        PersistentHashMap<T, byte> Membership);
}
