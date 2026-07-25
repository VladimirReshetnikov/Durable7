# Persistent Ordered Multimap Contract

- Status: Implemented normative contract
- Created (UTC): 2026-07-17T00:00:00Z
- Repository HEAD: `0bee5b4e50d0a21d43af88efbce5df6d34516bf9`
- Audience: Consumers and maintainers of `Durable7.Ordered`
- Scope: `PersistentOrderedMultimap<TKey, TValue>` semantics, API, and complexity

`PersistentOrderedMultimap<TKey, TValue>` is an immutable set of key/value pairs with two nested
orders. Key groups retain first-insertion order. Within each nonempty key group, comparer-distinct
values retain their own first-insertion order. Enumeration is therefore key-grouped; it does not
preserve one global interleaved pair-arrival history.

The implementation composes `PersistentOrderedMap<TKey, PersistentOrderedSet<TValue>>`. This is a
neutral general-purpose collection and has no Tungsten dependency or semantic baseline.

## Semantic Contract

- Key and value equality policies are independent and retained by object identity.
- The first key representative is retained while its group exists. Each group retains the first
  representative of every value equivalence class.
- A duplicate pair is an identity-preserving no-op. `TryAdd` reports `false` and returns the
  receiver.
- Empty groups are never stored. Removing the final value removes its key group; adding that key
  later appends a new group at the end.
- `KeyCount` counts nonempty groups. `PairCount` is the checked `long` count of distinct pairs.
- `GetValues` for an absent key returns an empty ordered set retaining `ValueComparer`.
- Every successful update publishes one immutable successor after the composed operations succeed;
  the source remains valid if hashing, equality, allocation, or checked arithmetic fails.

## Public Surface

```csharp
public sealed class PersistentOrderedMultimap<TKey, TValue>
    : IEnumerable<KeyValuePair<TKey, TValue>>
{
    public static PersistentOrderedMultimap<TKey, TValue> Empty { get; }
    public static PersistentOrderedMultimap<TKey, TValue> Create(
        IEqualityComparer<TKey>? keyComparer = null,
        IEqualityComparer<TValue>? valueComparer = null);
    public static PersistentOrderedMultimap<TKey, TValue> CreateRange(
        IEnumerable<KeyValuePair<TKey, TValue>> pairs,
        IEqualityComparer<TKey>? keyComparer = null,
        IEqualityComparer<TValue>? valueComparer = null);

    public int KeyCount { get; }
    public long PairCount { get; }
    public bool IsEmpty { get; }
    public IEqualityComparer<TKey> KeyComparer { get; }
    public IEqualityComparer<TValue> ValueComparer { get; }
    public IEnumerable<TKey> Keys { get; }
    public IEnumerable<KeyValuePair<TKey, PersistentOrderedSet<TValue>>> Groups { get; }

    public bool ContainsKey(TKey key);
    public bool Contains(TKey key, TValue value);
    public int CountValues(TKey key);
    public PersistentOrderedSet<TValue> GetValues(TKey key);
    public bool TryGetValues(TKey key, out PersistentOrderedSet<TValue> values);
    public bool TryGetKey(TKey equalKey, out TKey actualKey);
    public bool TryGetValue(TKey key, TValue equalValue, out TValue actualValue);

    public PersistentOrderedMultimap<TKey, TValue> Add(TKey key, TValue value);
    public bool TryAdd(TKey key, TValue value, out PersistentOrderedMultimap<TKey, TValue> result);
    public PersistentOrderedMultimap<TKey, TValue> Remove(TKey key, TValue value);
    public bool TryRemove(TKey key, TValue value, out PersistentOrderedMultimap<TKey, TValue> result);
    public PersistentOrderedMultimap<TKey, TValue> RemoveKey(TKey key);
    public bool TryRemoveKey(TKey key, out PersistentOrderedMultimap<TKey, TValue> result);
    public PersistentOrderedMultimap<TKey, TValue> Clear();
    public KeyValuePair<TKey, TValue>[] ToArray();
}
```

## Complexity

Let `k` be the number of key groups, `v` the size of the affected value group, `w <= 7` the CHAMP
depth, and `c` an equal-hash collision scan. Hashed membership and representative recovery cost
O(`w + c`) at each composed level. A successful pair edit additionally updates the appropriate
ordered sequence and costs O(log `k` + log `v`) in the ordinary case; sparse-label relabeling in
either ordered component has that component's documented linear rebuild bound. Enumeration and
`ToArray` are O(`PairCount`), and `Clear` is O(1).

## Validation

`PersistentOrderedMultimapTests.cs` covers policies, grouped order, representatives, duplicate
identity, group contraction/reappend, counts, range construction, retained branches, and recursive
dual-index invariants.
