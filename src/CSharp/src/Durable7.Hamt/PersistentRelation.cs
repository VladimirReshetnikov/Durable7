using System.Collections;
using System.Threading;

namespace Durable7.Hamt;

/// <summary>
/// Represents an immutable many-to-many relation backed by mutually inverse persistent hash
/// multimaps.
/// </summary>
/// <typeparam name="TLeft">The left-domain type.</typeparam>
/// <typeparam name="TRight">The right-domain type.</typeparam>
/// <remarks>
/// Each comparer-distinct pair occurs once. The first representative introduced in either domain
/// is reused globally in both indexes, not merely within one adjacency set. Updates publish a new
/// relation only after both successor indexes have been produced; retained versions are unchanged.
/// </remarks>
public sealed class PersistentRelation<TLeft, TRight> :
    IEnumerable<KeyValuePair<TLeft, TRight>>
{
    private readonly PersistentHashMultimap<TLeft, TRight> _forward;
    private readonly PersistentHashMultimap<TRight, TLeft> _reverse;
    private PersistentRelation<TRight, TLeft>? _inverseView;

    private PersistentRelation(
        PersistentHashMultimap<TLeft, TRight> forward,
        PersistentHashMultimap<TRight, TLeft> reverse)
    {
        _forward = forward;
        _reverse = reverse;
    }

    /// <summary>Gets the shared empty relation using default policies in both domains.</summary>
    public static PersistentRelation<TLeft, TRight> Empty { get; } = new(
        PersistentHashMultimap<TLeft, TRight>.Empty,
        PersistentHashMultimap<TRight, TLeft>.Empty);

    /// <summary>Gets the number of represented left equivalence classes.</summary>
    public int LeftCount => _forward.KeyCount;

    /// <summary>Gets the number of represented right equivalence classes.</summary>
    public int RightCount => _reverse.KeyCount;

    /// <summary>Gets the number of distinct relation pairs.</summary>
    public long PairCount => _forward.PairCount;

    /// <summary>Gets whether the relation contains no pairs.</summary>
    public bool IsEmpty => _forward.IsEmpty;

    /// <summary>Gets the comparer defining left-domain equivalence.</summary>
    public IEqualityComparer<TLeft> LeftComparer => _forward.KeyComparer;

    /// <summary>Gets the comparer defining right-domain equivalence.</summary>
    public IEqualityComparer<TRight> RightComparer => _reverse.KeyComparer;

    /// <summary>Gets stored left representatives in stable-for-one-version HAMT order.</summary>
    public IEnumerable<TLeft> Lefts => _forward.Keys;

    /// <summary>Gets stored right representatives in stable-for-one-version HAMT order.</summary>
    public IEnumerable<TRight> Rights => _reverse.Keys;

    /// <summary>Gets left representatives and their nonempty persistent right sets.</summary>
    public IEnumerable<KeyValuePair<TLeft, PersistentHashSet<TRight>>> Groups => _forward.Groups;

    /// <summary>Gets the cached inverse relation without rebuilding either index.</summary>
    public PersistentRelation<TRight, TLeft> Inverse
    {
        get
        {
            var current = Volatile.Read(ref _inverseView);
            if (current is not null)
                return current;

            var candidate = new PersistentRelation<TRight, TLeft>(_reverse, _forward)
            {
                _inverseView = this,
            };
            return Interlocked.CompareExchange(ref _inverseView, candidate, comparand: null) ?? candidate;
        }
    }

    /// <summary>Creates an empty relation with independent domain comparers.</summary>
    public static PersistentRelation<TLeft, TRight> Create(
        IEqualityComparer<TLeft>? leftComparer = null,
        IEqualityComparer<TRight>? rightComparer = null)
    {
        leftComparer ??= EqualityComparer<TLeft>.Default;
        rightComparer ??= EqualityComparer<TRight>.Default;
        if (ReferenceEquals(leftComparer, EqualityComparer<TLeft>.Default)
            && ReferenceEquals(rightComparer, EqualityComparer<TRight>.Default))
        {
            return Empty;
        }

        return new(
            PersistentHashMultimap<TLeft, TRight>.Create(leftComparer, rightComparer),
            PersistentHashMultimap<TRight, TLeft>.Create(rightComparer, leftComparer));
    }

    /// <summary>Creates a relation from pair contributions, collapsing equivalent duplicates.</summary>
    public static PersistentRelation<TLeft, TRight> CreateRange(
        IEnumerable<KeyValuePair<TLeft, TRight>> pairs,
        IEqualityComparer<TLeft>? leftComparer = null,
        IEqualityComparer<TRight>? rightComparer = null)
    {
        ArgumentNullException.ThrowIfNull(pairs);
        var result = Create(leftComparer, rightComparer);
        foreach (var (left, right) in pairs)
            result = result.Add(left, right);
        return result;
    }

    /// <summary>Determines whether the relation contains an equivalent pair.</summary>
    public bool Contains(TLeft left, TRight right) => _forward.Contains(left, right);

    /// <summary>Determines whether a represented left class has at least one related right.</summary>
    public bool ContainsLeft(TLeft left) => _forward.ContainsKey(left);

    /// <summary>Determines whether a represented right class has at least one related left.</summary>
    public bool ContainsRight(TRight right) => _reverse.ContainsKey(right);

    /// <summary>Gets a persistent set of rights related to an equivalent left.</summary>
    public PersistentHashSet<TRight> GetRights(TLeft left) => _forward.GetValues(left);

    /// <summary>Gets the number of rights related to an equivalent left.</summary>
    public int CountRights(TLeft left) => _forward.CountValues(left);

    /// <summary>Tries to retrieve the nonempty persistent right set for an equivalent left.</summary>
    public bool TryGetRights(TLeft left, out PersistentHashSet<TRight> rights) =>
        _forward.TryGetValues(left, out rights);

    /// <summary>Gets a persistent set of lefts related to an equivalent right.</summary>
    public PersistentHashSet<TLeft> GetLefts(TRight right) => _reverse.GetValues(right);

    /// <summary>Gets the number of lefts related to an equivalent right.</summary>
    public int CountLefts(TRight right) => _reverse.CountValues(right);

    /// <summary>Tries to retrieve the nonempty persistent left set for an equivalent right.</summary>
    public bool TryGetLefts(TRight right, out PersistentHashSet<TLeft> lefts) =>
        _reverse.TryGetValues(right, out lefts);

    /// <summary>Tries to retrieve the globally retained left representative.</summary>
    public bool TryGetLeft(TLeft equalLeft, out TLeft actualLeft) =>
        _forward.TryGetKey(equalLeft, out actualLeft);

    /// <summary>Tries to retrieve the globally retained right representative.</summary>
    public bool TryGetRight(TRight equalRight, out TRight actualRight) =>
        _reverse.TryGetKey(equalRight, out actualRight);

    /// <summary>Adds a pair, returning the receiver when an equivalent pair already exists.</summary>
    public PersistentRelation<TLeft, TRight> Add(TLeft left, TRight right)
    {
        // Normalize through the outer keys before touching either index. Without this step, the
        // nested multimaps could retain a different representative for the same right class in
        // different left groups (and symmetrically for left classes in reverse groups).
        var actualLeft = _forward.TryGetKey(left, out var storedLeft) ? storedLeft : left;
        var actualRight = _reverse.TryGetKey(right, out var storedRight) ? storedRight : right;

        var forward = _forward.Add(actualLeft, actualRight);
        if (ReferenceEquals(forward, _forward))
            return this;

        var reverse = _reverse.Add(actualRight, actualLeft);
        return new(forward, reverse);
    }

    /// <summary>Attempts to add an absent pair.</summary>
    public bool TryAdd(
        TLeft left,
        TRight right,
        out PersistentRelation<TLeft, TRight> result)
    {
        result = Add(left, right);
        return !ReferenceEquals(result, this);
    }

    /// <summary>Removes one pair, returning the receiver when it is absent.</summary>
    public PersistentRelation<TLeft, TRight> Remove(TLeft left, TRight right) =>
        TryRemove(left, right, out var result) ? result : this;

    /// <summary>Attempts to remove one pair from both indexes.</summary>
    public bool TryRemove(
        TLeft left,
        TRight right,
        out PersistentRelation<TLeft, TRight> result)
    {
        if (!_forward.TryGetKey(left, out var actualLeft)
            || !_reverse.TryGetKey(right, out var actualRight)
            || !_forward.Contains(actualLeft, actualRight))
        {
            result = this;
            return false;
        }

        var forward = _forward.Remove(actualLeft, actualRight);
        var reverse = _reverse.Remove(actualRight, actualLeft);
        result = Wrap(forward, reverse);
        return true;
    }

    /// <summary>Removes every pair containing an equivalent left.</summary>
    public PersistentRelation<TLeft, TRight> RemoveLeft(TLeft left) =>
        TryRemoveLeft(left, out var result, out _) ? result : this;

    /// <summary>Attempts to remove a left class and returns its persistent right set.</summary>
    public bool TryRemoveLeft(
        TLeft left,
        out PersistentRelation<TLeft, TRight> result,
        out PersistentHashSet<TRight> rights)
    {
        if (!_forward.TryGetKey(left, out var actualLeft)
            || !_forward.TryRemoveKey(actualLeft, out var forward, out rights))
        {
            result = this;
            rights = PersistentHashSet<TRight>.Create(RightComparer);
            return false;
        }

        var reverse = _reverse;
        foreach (var right in rights)
            reverse = reverse.Remove(right, actualLeft);
        result = Wrap(forward, reverse);
        return true;
    }

    /// <summary>Removes every pair containing an equivalent right.</summary>
    public PersistentRelation<TLeft, TRight> RemoveRight(TRight right) =>
        TryRemoveRight(right, out var result, out _) ? result : this;

    /// <summary>Attempts to remove a right class and returns its persistent left set.</summary>
    public bool TryRemoveRight(
        TRight right,
        out PersistentRelation<TLeft, TRight> result,
        out PersistentHashSet<TLeft> lefts)
    {
        if (!_reverse.TryGetKey(right, out var actualRight)
            || !_reverse.TryRemoveKey(actualRight, out var reverse, out lefts))
        {
            result = this;
            lefts = PersistentHashSet<TLeft>.Create(LeftComparer);
            return false;
        }

        var forward = _forward;
        foreach (var left in lefts)
            forward = forward.Remove(left, actualRight);
        result = Wrap(forward, reverse);
        return true;
    }

    /// <summary>Returns a comparer-preserving empty relation.</summary>
    public PersistentRelation<TLeft, TRight> Clear() =>
        IsEmpty ? this : Create(LeftComparer, RightComparer);

    /// <summary>Copies all relation pairs in forward nested-HAMT order.</summary>
    public KeyValuePair<TLeft, TRight>[] ToArray() => _forward.ToArray();

    /// <summary>Returns an enumerator over relation pairs in forward nested-HAMT order.</summary>
    public IEnumerator<KeyValuePair<TLeft, TRight>> GetEnumerator() => _forward.GetEnumerator();

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

    internal void ValidateInvariants()
    {
        _forward.ValidateInvariants();
        _reverse.ValidateInvariants();
        if (_forward.PairCount != _reverse.PairCount)
            throw new InvalidOperationException("The relation indexes disagree on pair count.");
        if (!ReferenceEquals(_forward.KeyComparer, _reverse.ValueComparer)
            || !ReferenceEquals(_forward.ValueComparer, _reverse.KeyComparer))
            throw new InvalidOperationException("The relation indexes do not retain swapped policies.");

        foreach (var (left, right) in _forward)
        {
            if (!_reverse.Contains(right, left))
                throw new InvalidOperationException("A forward relation pair is absent from the reverse index.");
        }

        foreach (var (right, left) in _reverse)
        {
            if (!_forward.Contains(left, right))
                throw new InvalidOperationException("A reverse relation pair is absent from the forward index.");
        }
    }

    private static PersistentRelation<TLeft, TRight> Wrap(
        PersistentHashMultimap<TLeft, TRight> forward,
        PersistentHashMultimap<TRight, TLeft> reverse) =>
        forward.IsEmpty ? Create(forward.KeyComparer, forward.ValueComparer) : new(forward, reverse);
}
