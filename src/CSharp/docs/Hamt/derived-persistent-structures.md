# Derived Persistent HAMT Structures

- Status: Implemented normative contract
- Created (UTC): 2026-07-17T00:00:00Z
- Repository HEAD: `0bee5b4e50d0a21d43af88efbce5df6d34516bf9`
- Audience: Consumers and maintainers of `Durable7.Hamt`
- Scope: Map patches, simple directed graphs, and single-secondary-index persistent maps

This document specifies three immutable facades built from the repository-general CHAMP family.
They retain policies by object identity, preserve stored representatives, publish complete
successors only after every component update succeeds, and have no Tungsten dependency.

## Persistent Map Patch

`PersistentMapPatch<TKey, TValue>` stores one strict before/after change per key class. A
`MapPatchValue<TValue>` distinguishes absence from a present value, including present null. Both
states are recorded so patches can validate, invert, and compose without consulting an external
base map.

- `Between(source, target, valueComparer)` requires the same key-comparer object and uses structural
  `Diff` to construct the patch.
- `Apply` and `TryApply` first validate every expected state. A conflict publishes no partial result;
  `TryApply` returns the original source and the first conflicting stored patch key.
- `Invert` swaps every before/after state.
- `Compose(next)` requires the same key- and value-comparer objects. Shared changes must agree on
  their intermediate state. Net no-ops are removed.
- `Add` ignores semantic no-ops and rejects an equivalent changed key.

Validation is O(`p * (w + c)`) for `p` changes; successful application adds the same order of path
copies. Inversion and composition are O(`p * (w + c)`) apart from comparer costs. Enumeration order
is stable for one patch version but is neither insertion nor sorted order.

## Persistent Directed Graph

`PersistentDirectedGraph<TVertex>` is a simple directed graph composed from an explicit vertex set
and a `PersistentRelation<TVertex, TVertex>`. The same comparer object governs the vertex set and
both edge domains.

- Isolated vertices are first-class. Adding an edge also adds missing endpoints.
- Edges are unique, self-loops are allowed, and parallel edges are not represented.
- Edge endpoints are normalized to the vertex set's first stored representatives.
- `GetSuccessors` and `GetPredecessors` return persistent adjacency sets. In/out degree is the
  corresponding set count.
- Removing an edge retains both endpoints. Removing a vertex removes every incoming, outgoing, and
  self-loop edge.
- `Reversed` is a cached O(1) view over the relation's inverse; reversing it again returns the
  original graph.

Point membership and edge edits have the composed CHAMP O(`w + c`) expected-width bound. Removing a
vertex is degree-local apart from persistent path copies: O((in-degree + out-degree) * (`w + c`)).
Full edge enumeration is O(`VertexCount + EdgeCount`) through the relation indexes.

## Persistent Indexed Map

`PersistentIndexedMap<TKey, TValue, TIndexKey>` is a primary `IReadOnlyDictionary<TKey, TValue>`
with one automatically maintained nonunique secondary index. It stores each primary value together
with the exact selected index-key representative and maps index keys back to persistent primary-key
sets.

- `Create` requires a retained `Func<TKey, TValue, TIndexKey>` selector plus independent primary-key,
  value, and index-key comparers.
- The selector runs once for a genuinely new or value-changing row. It is not run for duplicate
  `TryAdd`, a value-comparer no-op, removal, lookup, enumeration, or validation.
- `Add` is strict. `SetItem` keeps the primary representative and moves the secondary membership only
  when its selected index class changes.
- Secondary groups are nonempty and globally retain the first live index representative. Removing
  the final row contracts its group.
- Selector or policy failure leaves the source facade and both source indexes unchanged.

Primary lookup and index-group lookup have the CHAMP O(`w + c`) expected-width bound. New/changed
rows perform a bounded number of such operations plus one selector call. Removing a row uses its
stored secondary key and performs no selector call. Enumeration is O(`Count`).

## Principal Public Operations

The complete signatures are documented on the source XML surface. The primary entry points are:

```csharp
PersistentMapPatch<TKey, TValue>.Between(source, target, valueComparer);
patch.Apply(source);
patch.TryApply(source, out result, out conflictingKey);
patch.Invert();
patch.Compose(next);

PersistentDirectedGraph<TVertex>.Create(vertexComparer);
graph.AddVertex(vertex);
graph.AddEdge(source, target);
graph.RemoveEdge(source, target);
graph.RemoveVertex(vertex);
graph.Reversed;

PersistentIndexedMap<TKey, TValue, TIndexKey>.Create(
    indexSelector, keyComparer, valueComparer, indexComparer);
map.Add(key, value);
map.SetItem(key, value);
map.Remove(key);
map.GetKeysByIndex(indexKey);
```

## Validation

`PersistentMapPatchTests.cs`, `PersistentDirectedGraphTests.cs`, and
`PersistentIndexedMapTests.cs` cover presence-safe nulls, conflict atomicity, inverse/composition,
adjacency/reversal/incident removal, selector cardinality and failure, representative retention,
policy preservation, branching snapshots, and recursive coupled-index invariants.
