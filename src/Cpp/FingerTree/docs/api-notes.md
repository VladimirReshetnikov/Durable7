# C++ FingerTree API Notes

- Status: Current API notes
- Created (UTC): 2026-06-30T17:10:47Z
- Repository HEAD: bdc938f66eaf22d97a9c0df9fdd547b53319e112
- Updated (UTC): 2026-07-12T05:22:05Z
- Updated against repository HEAD: 2b2f91177a7c90ddfc4769d86f0a928fdede6f03
- Audience: Maintainers implementing and reviewing public C++ APIs
- Scope: C++ naming, contracts, and intentional differences from the C# workspace

The public namespace is `tools::data_structures::finger_tree`. For practical construction,
update, and snapshot examples, start with the [usage guide](usage.md).

## CMake Package

Source-tree and installed consumers use the same namespaced target:

```cmake
find_package(ToolsDataStructuresFingerTree 0.1 CONFIG REQUIRED)
target_link_libraries(my_target PRIVATE tools::data_structures::finger_tree)
```

The target publishes the public include directory and the C++23 language requirement. MSVC-specific dialect
options remain compiler-guarded generator expressions in the export, so an installed package generated on one
compiler does not inject those flags into another compiler. Package versions are compatible within major version
0 only when CMake's `SameMajorVersion` rule accepts them; consumers that require an exact pre-1.0 surface should
request `EXACT`.

The exported target also publishes the canonical-set cryptography dependency: Windows consumers link the system
`bcrypt` library, while non-Windows package configuration resolves `OpenSSL::Crypto` before importing the target.
SHA-256, HMAC-SHA-256, and entropy always come from those vetted providers; the library does not implement crypto.

The C++ port follows the repository's C# semantics, but it uses idiomatic C++ spelling:

- collection observers use `empty`, `size`, `front`, `back`, `at`, and `operator[]`;
- persistent updates return new values and do not mutate existing snapshots;
- `daba_lite` is the one deliberately mutable streaming surface and is segregated from those persistent values;
- absent ranks use `std::optional<std::size_t>` rather than a `-1` sentinel;
- pure-value multi-value returns use named result structs with semantic equality when their public element/value
  components are equality comparable; persistent collection fields compare by logical sequence rather than
  storage identity. The identity-bearing `finger_tree_locate_reference_result` intentionally has no equality;
- container types deliberately do not define pointer-based default equality;
- runtime comparators are stored by the sorted collection wrappers, while priority and interval measures use
  compile-time comparison policy state;
- concurrently published structure, lazy-state, and measure-box pointers use atomic `std::shared_ptr` publication.

The workspace targets MSVC `/std:c++latest`, while keeping the implementation to stable C++20/23-era facilities
where practical. CMake models the target as `CXX_STANDARD 23` and adds `/std:c++latest` explicitly for MSVC
because this bundled CMake rejects `CXX_STANDARD 26` for the installed compiler.

## `persistent_deque<T>`

`persistent_deque<T>` is the C++ port of C# `FingerTreeDeque<T>`. It is immutable: every update returns a new deque
value and existing snapshots remain valid. The implementation is the same tuned simplified finger tree, not a
vector-backed compatibility layer.

Primary C++ spellings:

- observers: `empty`, `size`, `front`, `back`, `try_front`, `try_back`, `at`, `operator[]`, `try_get`;
- endpoint updates: `push_front`, `push_back`, `remove_first`, `remove_last`, `pop_first`, `pop_last`;
- indexed updates: `set_item`, `set_at`, `update_at`, `insert_at`, `insert_range`, `remove_at`, `remove_range`;
- slicing and catenation: `get_range`, `split_at`, `split_item_at`, `split_range`, `concat`, `add_range`;
- sorted-sequence helpers: `sorted_lower_bound`, `sorted_upper_bound`, `sorted_binary_search`, `sorted_contains`,
  `split_at_sorted_lower_bound`, `split_at_sorted_upper_bound`, `split_at_sorted_equal_range`, `insert_sorted`,
  and `remove_all_sorted`;
- traversal/materialization: `begin`, `end`, `copy_to`, `to_vector`.

Notable C++ differences from C#:

- index and count types are `std::size_t`; `sorted_binary_search` returns `std::ptrdiff_t` to retain the C#
  bitwise-complement insertion-index convention;
- try-peek/get operations return nullable pointers instead of using out parameters;
- construction uses initializer-list, iterator, and range APIs rather than C# `params ReadOnlySpan<T>` and
  `IEnumerable<T>` overloads;
- runtime sorted-search comparers are `std::less`-style callables where `compare(a, b)` means `a < b`;
- `const_iterator` is a multipass forward iterator. It retains the immutable root, so its references remain valid
  after the facade object that created it is destroyed. Construction owns O(log n) traversal state; increment
  reuses that state instead of growing a sequence-sized buffer, and `copy_to` streams through the same traversal.

## `rrb_vector<T>`

`rrb_vector<T>` is the C++ port of C# `RrbVector<T>`. It is a persistent 32-way relaxed radix-balanced vector:
leaves contain at most 32 elements, regular branches omit cumulative-size tables and use five-bit radix descent,
and relaxed branches carry cumulative sizes only where split or concatenation makes child spans irregular.
Every stored node is reached through `std::shared_ptr<const node>`; updates allocate replacement boundary paths
and cannot mutate retained snapshots.

Primary operations:

- observers and traversal: `empty`, `size`, `height`, `front`, `back`, `at`, `operator[]`, `try_get`, retained
  forward iteration, and `to_vector`;
- point and endpoint updates: `set_item`, `add_first`, `add_last`, `push_front`, `push_back`, `pop_last`, and
  `try_pop_last`;
- structural edits: `concat`, `split_at`, `insert_range`, and `remove_range`;
- bulk staging: `create_builder`, `to_builder`, builder `add`, `add_range`, `clear`, and `to_immutable`;
- representation diagnostics: `root_identity`, `shares_root_with`, `leaf_identities`, `structure_statistics`,
  and `validate_invariants`.

Indexing and point update descend O(log32 n) levels. Split, range edits, endpoint operations, and concatenation
rebuild only boundary spines; concatenation is O(log32(n + m)). Exact root and leaf boundaries preserve the
corresponding node identities, and equality-comparable no-op `set_item` plus empty insert/remove operations return
the original root. The append builder freezes full 32-element leaves, copies a partial tail when publishing, and
caches a clean immutable snapshot; subsequent staging is isolated from every previously returned snapshot.

Notable C++ differences and limits:

- counts and indices use `std::size_t`; range and index violations throw `std::out_of_range`;
- the element type is constrained to `std::copy_constructible`, matching path-copying and snapshot publication;
- `rrb_vector_split<T>` and `rrb_vector_pop<T>` replace C# tuple/out-result shapes;
- the builder is a mutable construction aid but does not expose editable node ownership or transient tokens;
- there is deliberately no persistent tail buffer, so immutable endpoint append is a boundary-spine operation
  rather than a worst-case O(1) tail write.

## `zip_tree_rank_policy<T>` And `canonical_sorted_set<T>`

`canonical_sorted_set<T>` is the C++ port of C# `CanonicalSortedSet<T>`, with the retained
`zip_tree_rank_policy<T>` carrying both comparison semantics and an identity-bearing rank space. Copying a policy
handle preserves compatibility; independently constructing the same seed or key reproduces ranks and topology but
does not make the handles algebra-compatible.

Policy factories are:

- `random()`, which obtains a fresh unexposed 32-byte key from CNG or OpenSSL for each call;
- `seeded(seed)`, which derives the HMAC key as `SHA256("ZZT2" || seed_be64)` and records the public seed; and
- `keyed(rank_key)`, which takes an owned copy of at least 32 caller-retained bytes and exposes no key material.

Each factory has an overload accepting a `std::less`-style strict ordering and a 64-bit rank hash. That hash must
be constant on the order's equivalence classes; bulk and incremental duplicate paths check the contract and throw
`std::logic_error` on a mismatch. Natural factories use `stable_zip_tree_rank_hash<T>` for integral, Boolean,
`std::string`, and `std::string_view` values. Integrals map through their width-specific unsigned bit pattern;
strings use pinned FNV-1a over bytes. FNV is only the deterministic input mapping—the HMAC layer supplies the
pseudorandom rank. Custom types and cross-language wire contracts should always pass an explicit pinned mapping.

Rank derivation exactly matches the C# ZZT2 policy: HMAC-SHA-256 receives the rank hash as one big-endian 64-bit
message, the leading-zero count of its first word is the geometric coordinate, and its second and third
big-endian words are the unsigned secondary coordinate and digest content word. The secondary comparison is
unsigned. A comparer-smaller key breaks a complete rank tie, giving one Cartesian topology for one coherent
policy and set of stored comparison representatives.

Primary set operations are:

- construction: an empty value from a retained policy and `from_range(values, policy)`;
- lookup and diagnostics: `contains`, `try_get`, `size`, `height`, `content_hash`, `validate_structure`,
  `topology_signature`, `root_identity`, `node_identities`, and `shared_node_count_with`;
- persistent update: `add`, `remove`, and `clear`; and
- set behavior: `union_with`, `intersect`, `except`, `set_equals`, subset/superset relations, and `overlaps`.

Sorted bulk construction is O(n log n) plus a linear Cartesian pass and retains the first input representative
from every equivalence class. Incremental updates copy O(h) nodes and share every off-path node. Nodes hold the
representative through `std::shared_ptr<const T>`, so a moved range and rvalue `add` support move-only element
types; bulk construction from an lvalue range requires copying its elements. Removal, lookup, iteration, set
algebra, validation, and semantic equality do not copy stored elements.

Algebra requires the exact same retained policy handle and throws `std::invalid_argument` otherwise. Semantic
`set_equals` is intentionally different: it uses the receiver's comparer across policy families, including when
the other family has a different equivalence relation. This makes equality potentially asymmetric in the same
way as comparer-bearing set APIs. Same-policy equality first checks count and the memoized tree digest, then walks
canonical nodes in lockstep; digest equality is never treated as proof by itself.

All searches, build/freeze, updates, merge/split, traversal, validation, digest computation, equality, and unique
node reclamation use explicit stacks. Expected height and update cost are O(log n), but a colliding or adversarial
rank hash can force height and work to O(n). The digest uses atomic acquire/release publication and supports cold
concurrent readers. Concurrent access additionally requires the caller-supplied comparer and rank hash to be safe
for overlapping calls. The immutable set is otherwise ordinary value state; mutating one does not exist.

## `daba_lite<T, MonoidPolicy>`

`daba_lite<T, MonoidPolicy>` ports C# `DabaLite<T, TMonoid>` and the VLDB Journal 2021 DABA Lite schedule. The
policy satisfies `daba_lite_monoid_policy`: its `measure_type` is exactly `T`, and static `empty()` and
`combine(left, right)` members define the associative monoid. The implementation is an ephemeral FIFO window,
not a persistent collection. It is noncopyable and nonmovable so no accidental operation duplicates or
invalidates its six internal cursors.

`T` satisfies `daba_lite_value`: it is copyable, nothrow move-constructible, and nothrow move-assignable. Copies
may throw because they occur while building an unpublished operation plan. Nonthrowing moves make slot rewrites,
aggregate replacement, and cursor publication one nonthrowing commit. A type with a potentially throwing move is
rejected at constraint checking rather than admitted into a representation that could be torn mid-commit.

Primary operations:

- observers: `empty`, `size`, and FIFO-ordered `aggregate`;
- mutation: `insert`, `evict`, `try_evict`, and `clear`;
- representation diagnostics: `validate_structure`, returning `daba_lite_statistics`.

The six cursors preserve `F <= L <= R <= A <= B <= E` over linked 64-slot blocks. Each insertion or eviction
performs one scheduled fixup and never runs an unbounded reversal loop. `insert`, `evict`/`try_evict`, and a
nonempty `aggregate` invoke `combine` at most three, two, and exactly one times respectively; an empty aggregate
invokes `empty` and no `combine`. These are unconditional invocation bounds. Complete operations are worst-case
O(1) when policy calls and value copies are O(1).

All potentially throwing work needed by a mutation—monoid callbacks, allocation, and value copies—finishes
before its cursor/aggregate publication. Any such exception leaves the window, chunk chain, statistics, and
aggregate unchanged; callback-owned side effects are outside that guarantee. Successful eviction resets the
retired `std::optional<T>` immediately and
severs a predecessor block on the crossing operation. The active chain has `n` occupied positions plus 1 through
127 slack slots. `validate_structure` is callback-free and checks link direction, reachability, slot ownership,
cursor order, region equations, active-block count, and slack bounds.

`clear` is the intentional native ownership divergence. It obtains the identity and replacement block before
publication, then deterministically destroys every retired value and block before returning. That prompt release
costs O(n + c) for n occupied positions in c blocks; claiming the C# tracing collector's O(1) root-swap time in a
generic owning C++ container would be false. Calling `clear` on an already empty window does no policy work.

There is no oldest-value or iteration API because the Lite schedule overwrites raw values with partial products.
One instance is mutable and not thread-safe; do not overlap any access without external synchronization.

## `finger_tree<T, MeasurePolicy>`

`finger_tree<T, MeasurePolicy>` is the C++ port of the public C# general measured
`FingerTree<TElement, TMeasure, TMeasureOps>`. `MeasurePolicy` supplies the monoid identity, monoid combine, and
element measure through the same static-policy shape used by the measure infrastructure.

Primary operations:

- observers: `empty`, `measure`, `front`, `back`;
- endpoint updates/views: `prepend`, `append`, `try_view_left`, `try_view_right`;
- catenation: `concat`;
- measure-guided search: `split`, `try_split_find`, `try_locate`, `try_locate_reference`;
- streaming traversal/copy: `begin`, `end`, `cbegin`, `cend`, `for_each`, `copy_to`;
- explicit materialization: `to_vector`.

Notable C++ differences from C#:

- the measure policy is a single C++ type parameter whose nested `measure_type` names the measure, rather than
  separate `TMeasure` and `TMeasureOps` generic parameters;
- split and locate predicates are ordinary C++ callables; non-capturing predicate objects and lambdas both
  instantiate the same templated descent path;
- absent views and split-find searches use `std::optional` instead of C# `bool` plus out parameters;
- `try_locate` is a total result: `item` is optional, while `measure_before` is the boundary prefix measure when
  found, the whole-tree measure on a miss, and the identity for an empty tree;
- `try_locate_reference` has the same total-result boundary semantics but returns a pointer to the stored element
  instead of copying it. The pointer remains valid while the source tree, or another persistent snapshot sharing
  the located node, remains alive. Its result carrier deliberately has no `operator==`: raw pointer equality would
  expose sharing identity, while pointee equality would misrepresent an API whose purpose is canonical-reference
  access and whose pointer validity is tied to a sharing snapshot lifetime;
- `const_iterator` is a multipass forward iterator that retains the immutable root. It descends tree and node
  blocks directly, does not flatten the sequence, and returns references to stored elements. Construction owns
  O(log n) traversal state; prefix increment reuses its reserved stack. `for_each` and `copy_to` stream directly
  without an intermediate sequence container.

## Named Measure Operations

The C++ measure layer includes free functions corresponding to the C# named extension methods:

- max/min trees: `try_peek_max`, `try_extract_max`, `try_peek_min`, and `try_extract_min`;
- key and order-statistic trees: `split_by_lower_bound`, `split_by_upper_bound`, and `split_at_index`;
- product trees: `split_by_first`, `split_by_second`, `try_split_find_by_first`, `try_split_find_by_second`, plus
  size+max/size+min peek/extract helpers;
- sum and size+sum trees: `split_by_cumulative_weight` and `try_select_by_cumulative_weight`.

## `reversible_deque<T>`

`reversible_deque<T>` is the C++ port of C# `ReversibleDeque<T>`. It is the strict, reversal-aware sibling of
`persistent_deque<T>`: every update returns a new immutable snapshot, and `reverse()` flips an orientation bit in
O(1) instead of rebuilding the sequence.

Primary operations:

- observers: `empty`, `size`, `front`, `back`, `try_front`, `try_back`, `at`, and `operator[]`;
- endpoint updates: `push_front`, `push_back`, `remove_first`, `remove_last`, `try_pop_front`, and `try_pop_back`;
- indexed updates: `set_item`, `set_at`, `insert_at`, and `remove_at`;
- slicing and catenation: `split_at`, `concat`, and `reverse`;
- streaming traversal/copy: `begin`, `end`, `cbegin`, `cend`, and `copy_to`;
- explicit materialization and test support: `to_vector`, `validate_invariants`, and `tree_depth`.

Notable C++ differences from C#:

- indexed reads return by value. This matches the C# indexer semantics and avoids dangling references because
  reversed logical descent can materialize temporary mirrored node/digit views;
- try-pop operations return `std::optional` result structs instead of C# `bool` plus out parameters;
- counts and indices use `std::size_t`, continuing the port-wide count policy;
- construction uses initializer-list, iterator, and range APIs rather than C# `params ReadOnlySpan<T>` and
  `IEnumerable<T>` overloads;
- `const_iterator` is a retained multipass forward iterator. Its O(log n) task stack carries one orientation bit
  per tree/node frame and reaches physical leaves directly, so references remain stable, prefix increment performs
  no allocation, and a complete traversal is O(n). `copy_to` streams through the same cursor without indexed
  descent or mirrored-node materialization.

## `rope<T>`

`rope<T>` is the C++ port of C# `Rope<T>`. It is a persistent chunked positional sequence backed by
`finger_tree<rope_chunk<T>, rope_chunk_length_measure<T>>`, with bounded chunks for locality and O(log n) editing.

Primary operations:

- observers: `empty`, `size`, `front`, `back`, `at`, `operator[]`, and `try_get`;
- endpoint updates: `push_front`, `push_back`, `add_first`, `add_last`, `remove_first`, and `remove_last`;
- indexed and range updates: `set_item`, `set_at`, `insert_at`, `insert_range`, `remove_at`, and `remove_range`;
- slicing and catenation: `slice`, `split_at`, and `concat`;
- streaming traversal/copy: `begin`, `end`, `cbegin`, `cend`, and indexed-span `copy_to`;
- explicit materialization: `to_vector`, `get_range`, and `compact`;
- test/diagnostic support: `validate_invariants` and `chunk_count`.

Notable C++ differences from C#:

- counts and indices use `std::size_t`, continuing the port-wide count policy;
- copying construction is available through initializer-list, iterator, range, and `std::span<const T>` entry
  points;
- zero-copy `from_chunks` accepts `std::shared_ptr<const std::vector<T>>` storage, making retained ownership and
  immutability expectations visible at the type boundary. It does not accept raw spans or pointers for retained
  storage;
- chunks store `shared_ptr<const std::vector<T>>` plus offset/length instead of `ReadOnlyMemory<T>`. Slices share
  backing vectors, and `compact()` rebuilds fresh chunks to release oversized retained backing storage;
- `const_iterator` is a multipass, chunk-aware forward iterator. Its underlying measured-tree cursor retains the
  chunk tree and backing storage, so references survive destruction of the facade that produced the iterator.

## `measured_rope<T, MeasurePolicy>`

`measured_rope<T, MeasurePolicy>` is the C++ port of C#
`MeasuredRope<T, TMeasure, TMeasureOps>`. It is the measured sibling of `rope<T>`: a persistent chunked sequence
whose tree measure is `{count, user_measure}`.

Primary operations:

- positional observers and edits: the same `empty`, `size`, `front`, `back`, `at`, `try_get`, endpoint, indexed,
  range, split, slice, concat, copy, materialization, and `compact` operations as `rope<T>`;
- measure observers/navigation: `measure`, `prefix_measure`, `split_by_measure`, and `try_locate_by_measure`;
- test/diagnostic support: `validate_invariants` and `chunk_count`.

Notable C++ differences from C#:

- `MeasurePolicy` is the single C++ policy parameter and supplies the nested `measure_type`, matching the rest of
  the C++ measure layer;
- measure predicates are ordinary copyable callables. Function objects and lambdas use the same templated path, so
  the value-type-predicate overload distinction in C# is not needed;
- `try_locate_by_measure` is a total result: `value` is optional, `index` is the boundary index when found and
  `size()` on a miss, and `measure_before` is the boundary prefix measure when found and the whole user measure on
  a miss;
- measure navigation descends by the second component of the tree measure, then scans within the isolated chunk to
  find the exact boundary element and measure-before value. The scan is bounded by `max_chunk_size`;
- traversal uses the same retained, chunk-aware forward-iterator contract as `rope<T>`; `to_vector` remains the
  explicit owning materialization operation.

## Text Rope Helpers

The in-scope text layer mirrors the non-editor C# `RopeText` and `RopeBuilder` surface:

- `newline_measure`, a `char -> std::size_t` measure that counts `'\n'`;
- `text_rope`, an alias for `measured_rope<char, newline_measure>`;
- string interop: `to_char_rope`, `to_text_rope`, and `as_string`;
- line helpers: `line_count`, `line_of_offset`, `line_start_offset`, `line_column_of`, `offset_of`, `get_line`,
  and `lines`;
- `rope_builder`, a fluent append-only character builder with `append`, `append_line`, `clear`, `to_rope`, and
  `to_text_rope`.

Out of scope for the first C++ port are the editor-grade extensions from C# `RopeTextExtras`: Unicode scalar and
grapheme indexing, newline-style detection, CR-stripping line helpers, and `TextReader` adapters.

## `sorted_bag<T, Less>`, `sorted_set<T, Less>`, And `sorted_map<Key, T, Less>`

The sorted collection wrappers are the C++ ports of C# `SortedBag<T>`, `SortedSet<T>`, and
`SortedDictionary<TKey, TValue>`. They are persistent wrappers over the general measured tree with
order-statistic measures.

Primary bag operations:

- observers: `empty`, `size`, `comparison`, `min`, `max`, `at`, `operator[]`;
- updates: `add`, `add_range`, `try_remove`, `remove`, `remove_all`;
- queries: `contains`, `count_less_than`, `count_at_most`, `count_of`, `get_range`;
- streaming traversal/copy: `begin`, `end`, `cbegin`, `cend`, `copy_to`;
- explicit materialization: `to_vector`.

Primary set operations:

- observers and rank access: `empty`, `size`, `comparison`, `min`, `max`, `at`, `operator[]`, `index_of`;
- updates: `add`, `add_range`, `try_remove`, `remove`;
- navigation: `try_floor`, `try_ceiling`, `try_lower`, `try_higher`, `get_range`;
- algebra and relations: `union_with`, `intersect`, `except`, `symmetric_except`, subset/superset predicates,
  `overlaps`, and `set_equals`;
- streaming traversal/copy: `begin`, `end`, `cbegin`, `cend`, `copy_to`;
- explicit materialization: `to_vector`.

Primary map operations:

- observers and key/rank access: `empty`, `size`, `comparison`, `min_entry`, `max_entry`, `at`, `entry_at`,
  `index_of_key`;
- lookup and updates: `contains_key`, `try_get`, `set_item`, `insert`, `try_insert`, `try_remove`, `remove`;
- navigation: `try_floor_entry`, `try_ceiling_entry`, `try_lower_entry`, `try_higher_entry`, `get_range`;
- streaming traversal/copy: `begin`, `end`, `cbegin`, `cend`, `copy_to`;
- explicit materialization: `to_vector`, `keys_to_vector`, `values_to_vector`.

Notable C++ differences from C#:

- sorted wrappers store a runtime `Less` object, defaulting to `std::less<>`, because their order-statistic
  measures are comparison-independent just like the C# sorted wrappers' measures;
- absent ranks use `std::optional<std::size_t>` instead of C#'s `-1` sentinel;
- absent lookup, navigation, insertion, and removal results use `std::optional`;
- `sorted_map` is the C++ name for C# `SortedDictionary`;
- `sorted_bag` preserves comparer-equal insertion order, `sorted_set` keeps the first comparer-equal value during
  range construction, and `sorted_map` keeps the last duplicate-key entry;
- bag/set `at` and `operator[]`, and map `entry_at`, return `const` references to the canonical stored objects;
  their references follow the owning snapshot's lifetime;
- set algebra reuses the receiver, empty, or disjoint tree directly when comparator state is compatible. When two
  values have the same `Less` type but incompatible runtime state, the right operand is normalized under the
  receiver's comparator before algebra or relation evaluation;
- all three wrappers expose the measured tree's retained multipass forward iterator and streaming `copy_to`.
  Set-algebra merge walks stream comparator-compatible operands without temporary operand vectors. Incompatible
  runtime comparator state still requires rebuilding and sorting the right operand under the receiver's order
  before that streaming merge.

## `brodal_okasaki_heap<T, Less>`

`brodal_okasaki_heap<T, Less>` is the C++23 port of C# `BrodalOkasakiHeap<T>`. It retains the fused
bootstrapped skew-binomial representation rather than substituting a conventional binomial heap:

- `insert`, `minimum`, `try_minimum`, and compatible `meld` are worst-case O(1); `delete_minimum` and
  `try_delete_minimum` are worst-case O(log n);
- insert and nonempty meld perform at most five `Less` invocations. Minimum lookup and traversal do not invoke
  the comparator;
- comparator-object identity is representation policy. Default-constructed heaps share one policy object, and
  copies/updates retain it; independently supplied comparator objects are deliberately incompatible even when
  their runtime state compares equal. Compatibility is checked before empty-heap identities;
- comparer-equivalent values are never coalesced. C# `Compare(left, right) <= 0` is represented by one reverse
  strict-less call, so ties retain the same root-choice semantics and every distinct stored representative;
- values live behind `std::shared_ptr<const T>`, permitting rvalue insertion, moved bulk construction, and meld
  for move-only `T`. `minimum_handle()` and the first component of `try_delete_minimum()` retain the exact
  representative independently of later remainder destruction;
- empty-side meld and empty `clear` preserve exact versions. Validators report logical count, root-forest
  length, maximum rank, and maximum depth, and deliberately count repeated logical occurrences in self-melded
  shared DAGs;
- tree/forest control blocks use an allocation-free deferred deleter. This preserves the C#-faithful chain that
  arises under decreasing or tied root insertion while making deterministic C++ reclamation iterative and stack
  safe. Nodes remain immutable and concurrent reads of independently retained snapshots are safe.

`try_delete_minimum()` returns
`std::optional<std::pair<std::shared_ptr<const T>, brodal_okasaki_heap>>`; the pair is the removed representative
handle followed by the persistent remainder. The source snapshot is never modified. Comparator exceptions during
insert, meld, deletion, or validation cannot publish a partial version.

## `priority_search_queue<Key, Priority, Value, KeyLess, PriorityLess>`

This is the C++23 port of C# `PrioritySearchQueue<TKey, TPriority, TValue>`. It is one immutable AVL tree ordered
by key; every node caches its entry, children, count, height, and the full priority-then-key winner of its subtree.
There is no auxiliary heap or second key index.

Primary operations and bounds:

- `contains_key`, `try_get_entry`, `try_get_entry_handle`, `set_item`, `try_add`, `remove`, and `try_remove` are
  O(log n); absent removal, duplicate `try_add`, and exact replacement no-ops retain the original root;
- `minimum` and `try_minimum` read the root's cached winner in O(1). `delete_minimum` returns a named
  `{ entry, remainder }` view and removes the winner by retained key in O(log n);
- the retained key comparator defines one equivalence class. The first stored key handle is never replaced;
  priority and payload are last-wins. A replacement is an exact no-op only when both priority-comparator
  directions report equivalence and ordinary `operator==` agrees for priority and payload;
- comparator-equivalent priorities break ties by retained key order, including custom descending key policies;
- `enumerate_at_most(minimum_key, maximum_key, maximum_priority)` eagerly rejects an inverted inclusive key
  range, prunes a subtree when its cached winner exceeds the inclusive threshold, and materializes a
  `std::vector<entry_type>` in key order. The vector copies only shared entry handles, never `Key`, `Priority`, or
  `Value`; its cost is O(log n + v) for the visited nodes and may be O(n) when unselective;
- the forward iterator is an explicit-stack in-order traversal retaining the root. Validators independently check
  strict key bounds, AVL balance, cached count/height, and exact winner-handle identity. Node identity and shared
  node-count diagnostics quantify path copying.

`priority_search_entry<Key, Priority, Value>` owns independent `std::shared_ptr<const ...>` component handles.
Lookup, removal, minimum, range-query, and deletion results therefore preserve exact representatives and support
move-only components. An outer `std::optional<entry_type>` remains distinct from an entry whose key, priority, or
payload is itself an empty `std::optional`. Comparator/equality exceptions and component construction failures can
discard unpublished path copies but never mutate an existing queue. AVL height bounds make recursive updates and
ordinary `shared_ptr` reclamation stack-safe; unlike the potentially linear Brodal root chain, this core needs no
deferred deleter.

## `priority_queue<T, Priority, Comparison>`

`priority_queue<T, Priority, Comparison>` is the C++ port of C# `PriorityQueue<TElement, TPriority>`. It is a
persistent meldable minimum-priority queue backed by `finger_tree<priority_entry<T, Priority>, priority_measure<...>>`.

Primary operations:

- observers: `empty`, `size`, `try_peek_priority`, `try_peek`;
- updates: `enqueue`, `try_dequeue`, `meld`;
- streaming traversal/copy: `begin`, `end`, `cbegin`, `cend`, `copy_to`;
- explicit materialization: `to_vector`.

Notable C++ differences from C#:

- priority ordering is a compile-time static comparison policy, defaulting to `default_comparison<Priority>`. This
  matches the measure layer's compile-time comparison regime and still supports max-queue behavior through
  `reverse_comparison<Priority>`;
- absent peek/dequeue results use `std::optional`;
- iteration, `copy_to`, and `to_vector` expose insertion/tree order, matching the C# queue's unspecified
  enumeration order.

## `interval_tree<T, Comparison>`

`interval_tree<T, Comparison>` is the C++ port of C# `IntervalTree<T>`. It stores closed `interval<T>` values in
nondecreasing low-endpoint order and is backed by
`finger_tree<interval<T>, interval_measure<T, Comparison>>`.

Primary operations:

- observers: `empty`, `size`;
- construction: default construction, initializer-list construction, iterator construction, and `from_range`;
- updates: `insert`, `try_remove`, `remove`, `coalesce`;
- queries: `try_find_overlap`, `try_find_containing`, `find_overlaps`, `count_overlaps`, `contains`;
- streaming traversal/copy: `begin`, `end`, `cbegin`, `cend`, `copy_to`;
- explicit materialization: `to_vector`.

Notable C++ differences from C#:

- endpoint ordering is a compile-time static comparison policy, defaulting to `default_comparison<T>`. C# uses
  `Comparer<T>.Default`; the C++ policy shape avoids per-node comparer storage and also supports custom endpoint
  orderings such as projections;
- absent query/remove results use `std::optional` result values rather than C# `bool` plus out parameters;
- `size()` returns `std::size_t`;
- `find_overlaps` returns `std::vector<interval<T>>` in nondecreasing low-endpoint order;
- `contains` and `try_remove` match endpoints by the configured comparison policy, not by `operator==`, matching
  the C# comparer-equality contract;
- iteration and `copy_to` stream nondecreasing low-endpoint order. `count_overlaps` counts directly, and
  `coalesce` sweeps the iterator into a rebuilt tree without first materializing all source intervals.
