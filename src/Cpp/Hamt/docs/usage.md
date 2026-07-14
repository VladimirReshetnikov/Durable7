# HAMT C++ Usage Guide

- Created (UTC): 2026-07-02T20:07:09Z
- Repository HEAD: c58fc1159beb94e985ca66861bdc2ed3767eb2da
- Audience: C++ consumers and maintainers using the HAMT, Patricia, and Merkle map families
- Scope: Public include paths, value semantics, common operations, policy objects, canonical Merkle construction, iteration, and diagnostics

This guide is the practical companion to the [C++ API specification](api-specification.md). It shows
the common usage patterns from the public header-first HAMT port without repeating every contract in
the specification.

## Include And Namespace

```cpp
#include <Tools/DataStructures/Hamt/persistent_hash_map.hpp>
#include <Tools/DataStructures/Hamt/persistent_hash_set.hpp>
#include <Tools/DataStructures/Hamt/persistent_int_map.hpp>
#include <Tools/DataStructures/Hamt/merkle_search_tree.hpp>
#include <Tools/DataStructures/Hamt/merkle_persistence.hpp>
#include <Tools/DataStructures/Hamt/merkle_proofs.hpp>

namespace hamt = tools::data_structures::hamt;
```

Consumers that want the complete workspace surface can replace the individual includes with:

```cpp
#include <Tools/DataStructures/Hamt/hamt.hpp>
```

The workspace builds through `build.ps1`:

```powershell
.\build.ps1 -RunTests
```

## Value Semantics

The collections are persistent values. Update-shaped members return a new map or set and leave the
source value usable:

```cpp
using map_type = hamt::persistent_hash_map<int, std::string>;

auto empty = map_type::empty();
auto one = empty.set_item(1, "one");
auto two = one.set_item(2, "two");
auto replaced = two.set_item(1, "uno");

// empty has no keys.
// one has {1 -> "one"}.
// two has {1 -> "one", 2 -> "two"}.
// replaced has {1 -> "uno", 2 -> "two"}.
```

No-op updates return values that share the same root. `shares_root_with` exists for tests and
diagnostics, not as an ordering or equality substitute.

## Persistent Map

Use `set_item` for add-or-replace:

```cpp
auto map = map_type::empty()
    .set_item(1, "one")
    .set_item(2, "two");

if (const auto* value = map.try_get(1)) {
    // *value == "one"
}

std::string two_value = map.at(2);
```

Use `add` when duplicates are errors, and `try_add` when the caller wants the result without an
exception:

```cpp
auto unique = map_type::empty().add(1, "one");

auto [same, duplicate_added] = unique.try_add(1, "duplicate");
auto [with_two, was_added] = unique.try_add(2, "two");
```

`try_remove` reports the removed value:

```cpp
auto [without_one, removed, value] = with_two.try_remove(1);
if (removed) {
    // value is std::optional<std::string>{"one"}.
}
```

Use `create_range` or `set_items` for bulk add-or-replace with last-wins behavior:

```cpp
auto built = map_type::create_range(std::vector<std::pair<int, std::string>>{
    {1, "one"},
    {2, "two"},
    {1, "uno"},
});
```

## Custom Hash And Equality

Custom policy objects are template arguments and are stored in each map or set value:

```cpp
struct case_insensitive_hash {
    std::size_t operator()(const std::string& value) const noexcept;
};

struct case_insensitive_equal {
    bool operator()(const std::string& left, const std::string& right) const noexcept;
};

using ci_map = hamt::persistent_hash_map<
    std::string,
    int,
    case_insensitive_hash,
    case_insensitive_equal>;

auto map = ci_map::create_range(
    std::vector<std::pair<std::string, int>>{
        {"Alpha", 1},
        {"ALPHA", 3},
        {"beta", 2},
    },
    case_insensitive_hash{},
    case_insensitive_equal{});
```

Equivalent keys must produce equal 32-bit truncated hashes. The implementation uses the low 32 bits
of the hash value to match the C# HAMT hash width.

When an update uses an equivalent key, the map retains the originally stored key object:

```cpp
const std::string* stored = map.try_get_key("alpha");
```

## Iteration And Materialization

Enumeration follows the HAMT bitmap/collision shape. It is stable for an unchanged value, but it is
not insertion order or sorted order.

```cpp
for (const auto& [key, value] : map) {
    // Inspect key and value.
}

auto entries = map.to_vector();
auto keys = map.keys();
auto values = map.values();
```

Iterators keep traversal state inline and do not allocate while walking.

## Persistent Set

`persistent_hash_set<T, Hash, KeyEqual>` is a persistent set wrapper over the map core:

```cpp
using set_type = hamt::persistent_hash_set<int>;

auto empty = set_type::empty();
auto one = empty.add(1);
auto two = one.add(2);
auto removed = two.remove(1);

bool has_two = removed.contains(2);
```

Use try operations when membership changes matter:

```cpp
auto [same, duplicate_added] = one.try_add(1);
auto [with_two, was_added] = one.try_add(2);
auto [without_one, was_removed] = with_two.try_remove(1);
```

Use `try_get_value` to recover the originally stored equivalent value when custom equality is in
play.

## One-Way Edit Sessions

Use the nested move-only `transient` when an operation naturally has an edit phase followed by one
publication point:

```cpp
auto source = map_type::empty().set_item(1, "one");
auto edit = source.to_transient();

edit.set_item(2, "two");
edit.set_item(1, "uno");
bool inserted = edit.try_add(3, "three");
bool removed = edit.remove(2);

auto published = std::move(edit).persist();
```

`source` remains immutable and isolated. A clean session—or one subjected only to equal-value
replacement, duplicate `try_add`, absent removal, or empty clear—publishes a value sharing the
source root. A real edit uses the same persistent CHAMP path-copy operation as calling `set_item`,
`add`, `remove`, or `clear` on a map value; this session is a lifecycle convenience, not an in-place
mutation optimization. It is distinct from the reusable construction-only `bulk_builder`.
If hashing, equality, policy copying, or allocation fails while preparing a point edit, the active
session and its existing iterators remain unchanged.

Publication is deliberately rvalue-only and consumes the session. Every later collection read,
edit, iteration request, or publication attempt on `edit`, and every such operation on a moved-from
session, throws `std::logic_error`. A consumed or moved-from variable may still receive a fresh
session through move assignment. Iterators are bound to the session generation: content changes
invalidate them, logical no-ops do not, and publication invalidates all of them. Moving a session
transfers still-valid iterators to the destination's logical session.

Sets expose the same lifecycle with reporting membership edits:

```cpp
auto edit_set = set_type::create_transient();
bool added_one = edit_set.add(1);
bool duplicate = edit_set.add(1); // false
bool removed_one = edit_set.remove(1);
bool contains_all = edit_set.is_superset_of(std::vector<int>{1, 2});
bool same_members = edit_set.set_equals(std::vector<int>{2, 3});
auto published_set = std::move(edit_set).persist();
```

The set session also mirrors `is_subset_of`, `is_proper_subset_of`, `is_superset_of`,
`is_proper_superset_of`, `overlaps`, and `set_equals` for initializer lists, persistent sets, and
ranges. As on the persistent facade, the receiver's retained hash/equality policy interprets the
argument and collapses equivalent duplicates where cardinality matters.

Publication moves the current map and policy objects before it marks the session consumed. Standard
nothrow-movable policies make that boundary straightforward. A throwing custom policy move can
leave an unconsumed map session with already-moved subobjects, or consume the inner map before set-
wrapper construction fails; there is no retry/content-preservation guarantee for that exceptional
publication path. Use nothrow-movable policies when publication must be retry-independent.

Sessions are unsynchronized single-owner values. Move them between threads only with external
synchronization; continue to share immutable source or published values for concurrent reads.

## Set Algebra

Set algebra methods accept initializer lists or ranges:

```cpp
auto left = set_type::create_range(std::vector{1, 2, 3});
auto right = std::vector{3, 4};

auto unioned = left.union_with(right);
auto intersection = left.intersect_with(right);
auto difference = left.except_with(right);
auto symmetric = left.symmetric_except_with(right);

bool subset = left.is_subset_of(std::vector{1, 2, 3, 4});
bool overlaps = left.overlaps(right);
bool equal = left.set_equals(std::vector{3, 2, 1});
```

Operations that need distinct right-side membership materialize the range into `std::unordered_set`
using the set's hash and equality policy. Superset and overlap checks stream and can exit early.

## Integer Patricia Collections

Choose the Patricia surface for signed integer keys, sorted traversal, or merge-heavy workloads:

```cpp
auto left = hamt::persistent_int_map<std::string>{}
    .set_item(-10, "left")
    .set_item(20, "twenty");
auto right = hamt::persistent_int_map<std::string>{}
    .set_item(-10, "right")
    .set_item(30, "thirty");

auto right_biased = left.union_with(right);
auto combined = left.union_with(right,
    [] (std::int32_t, const std::string& l, const std::string& r) {
        return l + "+" + r;
    });

auto ordered = combined.to_vector(); // ascending signed keys
```

The 64-bit aliases have the same surface. Integer sets expose `add`, `remove`, structural union,
intersection, difference, and ordered `to_vector()`. No custom hash or comparison policy is needed:
the key bits define both identity and order.

## Merkle Search Tree

Choose `merkle_search_tree<K,V>` when comparator-ordered contents need a canonical SHA-256 address
and exact cross-language `MST2` blocks. Every tree requires an explicit semantic policy with a
comparator and versioned canonical codecs:

```cpp
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using merkle_value = std::optional<std::string>;
using merkle_policy = hamt::merkle_search_tree_policy<std::int32_t, merkle_value>;
using merkle_tree = hamt::merkle_search_tree<std::int32_t, merkle_value>;

auto policy = merkle_policy::natural(
    "golden-int-string-v1",
    std::make_shared<hamt::int32_merkle_codec>(),
    std::make_shared<hamt::nullable_utf8_merkle_codec>());

auto empty = merkle_tree::create(policy);
auto one = empty.set_item(42, merkle_value{"forty-two"});
auto two = one.set_item(7, std::nullopt);

// The source snapshots remain unchanged.
// one.root_hash().to_hex() is the shared one-entry golden digest.
```

The policy ID names comparator semantics as well as application meaning. Reuse it only when every
implementation agrees on key equivalence/order and key/value encodings. Codec IDs are part of the
SHA-256 domain and must be explicit version IDs ending in `-v<digits>`. Use `create` rather than a
default constructor: an implicit host-dependent policy would make content addresses ambiguous.

Bulk construction takes ownership of a vector, sorts it by the policy, retains the first
comparator-equivalent key, and takes the last supplied value:

```cpp
auto built = merkle_tree::create_range(
    std::vector<std::pair<std::int32_t, merkle_value>>{
        {42, merkle_value{"old"}},
        {7, std::nullopt},
        {42, merkle_value{"forty-two"}},
    },
    policy);
```

Point updates are persistent. An encoded-value no-op and an absent removal preserve the root;
actual updates retain untouched block objects:

```cpp
auto unchanged = one.set_item(42, merkle_value{"forty-two"});
bool same_root = unchanged.shares_root_with(one);

auto changed = one.set_item(7, std::nullopt);
std::size_t shared_blocks = one.shared_block_count(changed);
```

Lookup through `get_entry` distinguishes a missing key from a present nullable value. Use the
entry's owning handles when the representative must outlive the source tree:

```cpp
if (const auto* entry = two.get_entry(7)) {
    auto retained_key = entry->key_handle();
    auto retained_value = entry->value_handle();
    // *retained_value is std::nullopt; the entry was present.
}
```

Iteration and inclusive ranges follow comparator order, not hash level or block preorder.
`enumerate_range` materializes and returns an owning vector of entry handles:

```cpp
for (const auto& entry : two) {
    // entry.key(), entry.value(), entry.level()
}

auto selected = two.enumerate_range(7, 42);
```

The forward iterator itself does not retain the tree root. Keep a tree snapshot alive until the
iterator is finished; copy an entry or one of its shared handles when the representative must live
longer.

`content_equals` compares compatible content addresses. `map_equals` additionally applies value
semantics, while `diff` returns owned shared handles for added, removed, and changed records:

```cpp
if (!one.content_equals(two)) {
    for (const auto& change : one.diff(two)) {
        switch (change.kind) {
        case hamt::merkle_map_difference_kind::added:
        case hamt::merkle_map_difference_kind::removed:
        case hamt::merkle_map_difference_kind::changed:
            break;
        }
    }
}
```

Use the diagnostic surface when verifying serialization or structural sharing:

```cpp
auto statistics = two.validate_structure();
auto shape = two.shape();
for (const auto& block : two.blocks_preorder()) {
    const std::string address = block.digest.to_hex();
    const hamt::merkle_bytes& exact_mst2_bytes = *block.bytes;
}
```

`validate_structure` performs a deep canonical audit; it is not required before ordinary trusted
reads. The persistence layer saves and loads exact closures, verifies untrusted packs under finite
budgets, creates and verifies exact proofs, synchronizes block frontiers, and merges divergent
roots. See the [Merkle core](merkle-search-tree.md) and
[persistence specification](merkle-persistence.md) for the complete contracts.

### Merkle Persistence And Proofs

Export/save and verified load use immutable content-addressed blocks:

```cpp
auto store = hamt::in_memory_merkle_block_store{};
auto pack = hamt::export_merkle_pack(two);
std::size_t added = hamt::save_merkle_tree(two, store);

auto loaded = hamt::load_merkle_tree(two.root_hash(), policy, store);
auto imported = hamt::import_merkle_pack(pack, policy);
```

Pass a `merkle_verification_budget` to load, import, or proof verification when the defaults are not
appropriate. Limits are immutable; use a `with_max_*` member to create a modified copy.

```cpp
auto budget = hamt::merkle_verification_budget{}
    .with_max_block_count(10000)
    .with_max_depth(80);

auto bounded = hamt::load_merkle_tree(two.root_hash(), policy, store, budget);
```

Point and inclusive-range proofs carry exact `MSP2` query descriptors and authenticated `MST2`
blocks:

```cpp
auto membership = hamt::create_merkle_proof(two, std::int32_t{42});
auto range = hamt::create_merkle_range_proof(two, std::int32_t{7}, std::int32_t{42});

auto checked = hamt::verify_merkle_proof(membership, policy, budget);
if (!checked.valid()) {
    // Inspect failure_kind(), failure_message(), and verified byte/block counts.
}
```

Synchronization is iterative. Transfer the addresses in `requested_blocks()`, store them, and plan
again until the frontier is empty; then load and publish the target root. A store used to prune
known subtrees must contain verified closures.

Three-way merge returns a complete tree or conflicts, never partial output:

```cpp
auto result = hamt::merge_merkle_trees(base, left, right);
if (result.success()) {
    const auto& merged = *result.merged_tree();
} else {
    for (const auto& conflict : result.unresolved_conflicts()) {
        // base/left/right distinguish absence from a present nullable value.
    }
}
```

## Concurrency And Lifetime

Map and set values are immutable after construction. Independent snapshots can be read concurrently,
and update-shaped operations create new values without mutating retained snapshots. Ordinary C++
object lifetime rules still apply: do not race on the same local variable while another thread
reassigns it. CHAMP edit sessions are unsynchronized and support one logical owner; do not overlap
session operations, and use caller synchronization for sequential transfer between threads. Merkle
policies additionally call their shared comparator and codecs during reads, updates, equality,
diff, and validation; custom implementations must support the concurrency the caller permits.

## Choosing A Surface

| Need | Start with |
| --- | --- |
| Immutable unordered key/value collection | `persistent_hash_map<Key, T>` |
| Duplicate-rejecting insert | `add` or `try_add` |
| Stored equivalent key recovery | `try_get_key` |
| Immutable unordered value set | `persistent_hash_set<T>` |
| Stored equivalent item recovery | `try_get_value` |
| One-way map/set edit phase | `to_transient()` / `create_transient()`, then `std::move(session).persist()` |
| Union/intersection/difference | `union_with`, `intersect_with`, `except_with`, `symmetric_except_with` |
| Custom value semantics | Template hash/equality policy objects plus `create(...)` or `create_range(...)` |
| Canonical ordered map with a SHA-256 root | `merkle_search_tree<K,V>` |
| Exact C#/Rust-compatible `MST2` blocks | `blocks_preorder()` under an explicit compatible policy |
| Verified block-store load/import | `load_merkle_tree` / `import_merkle_pack` |
| Membership, absence, or range proof | `create_merkle_proof` / `create_merkle_range_proof` |
| Iterative block synchronization | `plan_merkle_sync` / `create_merkle_sync_pack` |
| Typed three-way merge | `merge_merkle_trees` |
| Deep Merkle invariant audit | `validate_structure()` |

For cross-language contract alignment, see the repository
[porting and semantic parity guide](../../../../docs/guides/porting-and-semantic-parity.md).
