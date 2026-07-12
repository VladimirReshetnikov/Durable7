# C++ FingerTree Usage Guide

- Created (UTC): 2026-07-02T20:03:36Z
- Repository HEAD: 17d505f18e9e0a5748058701d408ed6642dcba29
- Updated (UTC): 2026-07-12T04:31:10Z
- Updated against repository HEAD: 8a926e3bdb0cc37da0c8a15c4c32352c2ebcb1f5
- Audience: C++ consumers and maintainers using the public FingerTree headers
- Scope: Public include path, value semantics, canonical ranking, priority cores, and facade quick starts

This guide is the practical entry point for using the C++ FingerTree port. The [API notes](api-notes.md)
document the full public surface and C#-to-C++ mapping; this guide focuses on the common code shapes
that consumers and tests use day to day.

## Include And Namespace

Use the aggregate header unless you are deliberately minimizing include cost:

```cpp
#include <tools/data_structures/finger_tree/finger_tree.hpp>

namespace ft = tools::data_structures::finger_tree;
```

The library is header-first. Build and CTest commands are documented in [validation.md](validation.md).

## Value Semantics

Collection operations are persistent: update-shaped members return a new value and leave the source
snapshot valid.

```cpp
auto empty = ft::persistent_deque<std::string>{};
auto one = empty.push_back("middle");
auto two = one.push_front("left");
auto three = two.push_back("right");

// empty == []
// one   == ["middle"]
// two   == ["left", "middle"]
// three == ["left", "middle", "right"]
```

Copying a collection value is how you retain a snapshot. Internally, unchanged immutable nodes are
shared through reference-counted ownership. The containers are not mutable views over shared state.

Empty or absent operations use the C++ shape documented by each type: nullable pointers for some
try-peek operations, `std::optional` result structs for try-pop/dequeue/search operations, and
standard exceptions for throwing APIs such as `front()` on an empty sequence or `at()` outside the
valid index range.

## Persistent Deque

Use `persistent_deque<T>` for a persistent indexed sequence with efficient endpoint operations,
concatenation, splitting, indexed updates, and sorted-sequence helpers.

```cpp
auto deque = ft::persistent_deque<int>{1, 2, 3};
auto extended = deque.push_front(0).push_back(4);

auto popped = extended.pop_first();
int first = popped.value;                  // 0
auto rest = popped.rest;                   // [1, 2, 3, 4]

auto split = rest.split_at(2);
auto joined = split.left.concat(split.right);

auto values = joined.to_vector();          // [1, 2, 3, 4]
```

Use `try_front`, `try_back`, `try_get`, `try_pop_first`, and `try_pop_last` when empty results are
expected and should not throw.

The deque also exposes sorted helpers over an already sorted deque:

```cpp
auto sorted = ft::persistent_deque<int>{1, 3, 3, 7, 9};
std::size_t lower = sorted.sorted_lower_bound(3); // 1
std::size_t upper = sorted.sorted_upper_bound(3); // 3
auto without_threes = sorted.remove_all_sorted(3);
```

## RRB Vector

Use `rrb_vector<T>` when uniform low-depth indexing and persistent concatenation matter more than the deque's
amortized-O(1) endpoints. Regular 32-way branches use radix indexing without size tables; splits and concatenation
introduce cumulative tables only along irregular boundary spines.

```cpp
auto vector = ft::rrb_vector<int>::from_range(std::vector{10, 20, 30, 40});
auto changed = vector.set_item(2, 300);     // vector still contains 30 at index 2

auto split = changed.split_at(2);
auto joined = split.left.concat(split.right);
auto inserted = joined.insert_range(1, std::array{11, 12});
auto popped = inserted.pop_last();          // { value = 40, rest = [10, 11, 12, 20, 300] }
```

For append-heavy construction, stage values in the builder. `to_immutable()` caches the clean snapshot; adding
more values afterwards cannot mutate any snapshot already returned.

```cpp
auto builder = ft::rrb_vector<int>::create_builder();
builder.add_range(std::views::iota(0, 1'000));
auto first = builder.to_immutable();

builder.add(1'000);
auto second = builder.to_immutable();       // first remains [0, 1000); second is [0, 1000]
```

The immutable facade deliberately has no persistent tail buffer. `push_back` and `pop_last` therefore remain
boundary-spine operations; use the builder for bulk append staging.

## Canonical Sorted Set

Use `canonical_sorted_set<T>` when reproducible same-policy topology, persistent sharing, and a memoized content
digest matter more than the sorted finger-tree facade's worst-case bounds. Retain one policy handle across every
version that must participate in structural algebra:

```cpp
auto policy = ft::zip_tree_rank_policy<int>::seeded(0x1234'5678'9abc'def0ULL);
auto first = ft::canonical_sorted_set<int>::from_range(std::array{8, 2, 5, 3, 2}, policy);
auto second = first.add(13).remove(3);

bool has_five = second.contains(5);
std::uint64_t digest = second.content_hash();
auto statistics = second.validate_structure();
```

`seeded` is reproducible but public. Use `random` for an unexposed fresh rank key, or `keyed` with at least 32
protected caller-retained bytes when predictable ranks are a threat. A custom ordering always travels with an
equivalence-class-coherent rank hash:

```cpp
struct item { std::uint32_t key; std::string payload; };
auto less = [] (const item& left, const item& right) { return left.key < right.key; };
auto rank_hash = [] (const item& value) { return std::uint64_t{value.key}; };
auto policy = ft::zip_tree_rank_policy<item>::seeded(7, less, rank_hash);
```

Pin and test the actual application encoding-to-hash mapping. The bulk factory retains the first
comparison-equivalent input object. `try_get` recovers that representative. `union_with`,
`intersect`, and `except` require policy identity, while `set_equals` deliberately compares semantic contents under
the receiver's comparer even when rank policies differ.

## DABA Lite Sliding-Window Aggregation

Use `daba_lite<T, MonoidPolicy>` for one mutable FIFO window whose aggregate must remain available with bounded
worst-case work per slide. The policy's `measure_type` is the stored value type itself. It supplies only identity
and combination; no inverse or commutativity is required.

The value type must be copyable with nonthrowing move construction and assignment. This lets the implementation
perform callbacks, allocation, and possibly throwing copies while preparing a private plan, then publish the
entire mutation through nonthrowing moves. An exception from any preparation step leaves the exact window intact.

```cpp
struct sum_monoid {
    using measure_type = long long;

    static constexpr measure_type empty() noexcept { return 0; }
    static constexpr measure_type combine(measure_type left, measure_type right) noexcept
    {
        return left + right;
    }
};

ft::daba_lite<long long, sum_monoid> window;
window.insert(10);
window.insert(20);
window.insert(30);

auto first_sum = window.aggregate(); // 60
window.evict();
window.insert(40);
auto next_sum = window.aggregate();  // 90, for [20, 30, 40]
```

Prefer `try_evict()` when an empty window is ordinary control flow; `evict()` throws `std::out_of_range` when
empty. `validate_structure()` returns cursor-region and block-capacity statistics without invoking the policy.
It is suitable for invariant gates and diagnostics, not the hot query path.

This is the deliberately ephemeral member of an otherwise persistence-first workspace. Do not copy it, publish
it as a snapshot, enumerate it, or race one instance between threads. Successful eviction releases the retired
slot and crossed chunk immediately. `clear()` also releases every owned value and chunk before returning; that
deterministic native reclamation is O(n + c), although subsequent reuse starts from one empty 64-slot block.

## Generic Measured Tree

Use `finger_tree<T, MeasurePolicy>` directly when your algorithm is driven by a monoid measure.
`MeasurePolicy` names a `measure_type` and provides `empty`, `measure`, and `combine`.

```cpp
auto tree = ft::finger_tree<int, ft::size_measure<int>>::from_range(std::vector{10, 20, 30, 40});

auto split = tree.split([] (std::size_t count) {
    return count > 2;
});

auto located = tree.try_locate([] (std::size_t count) {
    return count > 2;
});

// split.left  == [10, 20]
// split.right == [30, 40]
// located.item == 30, located.measure_before == 2
```

`try_locate` is total: on a miss, the optional item is empty and `measure_before` is the whole-tree
measure. This matters for non-group measures where the caller cannot subtract a suffix to recover
the prefix.

Named measure helpers live beside the generic tree. For example:

```cpp
auto weights = ft::finger_tree<int, ft::sum_measure<int>>{5, 1, 4};
auto selected = ft::try_select_by_cumulative_weight(weights, 5);
auto split_by_weight = ft::split_by_cumulative_weight(weights, 5);
```

## Reversible Deque

Use `reversible_deque<T>` when logical reversal is a primary operation. `reverse()` is O(1) and
returns a new orientation over shared immutable storage.

```cpp
auto deque = ft::reversible_deque<int>::from_range(std::vector{1, 2, 3});
auto reversed = deque.reverse();

int front = reversed.front();              // 3
auto restored = reversed.reverse();        // [1, 2, 3]
```

Endpoint and index operations respect the current logical orientation. Keep using
`persistent_deque<T>` when reversal is not part of the contract and you want the tuned deque's
normal endpoint profile.

## Sorted Collections

Use the sorted wrappers for persistent ordered collections with rank access and range queries:

```cpp
auto bag = ft::sorted_bag<int>::from_range(std::vector{5, 1, 3, 3, 1});
std::size_t threes = bag.count_of(3);       // 2

auto set = ft::sorted_set<int>::from_range(std::vector{5, 1, 3, 3});
auto with_four = set.add(4);
auto rank = with_four.index_of(4);         // std::optional<std::size_t>

using map_type = ft::sorted_map<int, std::string>;
auto map = map_type{}
    .set_item(2, "two")
    .set_item(1, "one");
auto maybe_value = map.try_get(2);         // std::optional<std::string>
```

`sorted_bag` preserves duplicates. `sorted_set` keeps one comparer-equal value. `sorted_map`
inserts or replaces with `set_item`, rejects duplicate keys with `insert`, and reports duplicate
insertions through `try_insert`.

Pass a runtime comparator object when the ordering is not the default:

```cpp
auto descending = ft::sorted_set<int, std::greater<>>::from_range(
    std::vector{5, 1, 9, 3},
    std::greater<>{});
```

## Brodal-Okasaki Heap

Use `brodal_okasaki_heap<T, Less>` when worst-case constant-time persistent insertion and melding matter. Values
that compare equivalent remain distinct, and heaps can meld only when they retain the same comparator object.
Derive related heaps from one empty value (or pass its `comparer_policy()` handle) when melding is planned:

```cpp
auto empty = ft::brodal_okasaki_heap<int>{};
auto left = empty.insert(8).insert(2).insert(5);
auto right = empty.insert(7).insert(1);
auto merged = left.meld(right);

auto removed = merged.try_delete_minimum();
// *removed->first == 1; removed->second.minimum() == 2
```

`minimum()` returns a snapshot-owned reference. `minimum_handle()` and the removed handle returned by
`try_delete_minimum()` can outlive the heap snapshot and are the move-only-friendly representative surfaces.
`delete_minimum()` throws `std::logic_error` on an empty heap; the `try_` form returns `std::nullopt`.

## Priority Queue

`priority_queue<Element, Priority, Comparison>` is a persistent minimum-priority queue. Equal
priorities drain in insertion order.

```cpp
auto queue = ft::priority_queue<std::string, int>{}
    .enqueue("slow", 10)
    .enqueue("fast", 1)
    .enqueue("normal", 5);

auto peek = queue.try_peek();
// peek->element == "fast", peek->priority == 1

while (auto next = queue.try_dequeue()) {
    // process next->element and next->priority
    queue = next->rest;
}
```

Use `reverse_comparison<Priority>` to make a max-priority queue:

```cpp
using max_queue = ft::priority_queue<std::string, int, ft::reverse_comparison<int>>;
```

## Interval Tree

`interval_tree<T, Comparison>` stores closed intervals and supports overlap and containment queries.

```cpp
auto intervals = ft::interval_tree<int>{}
    .insert(1, 5)
    .insert(10, 15)
    .insert(3, 8);

auto first_overlap = intervals.try_find_overlap({4, 4});
auto all_overlaps = intervals.find_overlaps({4, 11});
std::size_t count = intervals.count_overlaps({4, 11});

auto coalesced = intervals.coalesce();
```

Endpoint equality follows the configured comparison policy, not necessarily `operator==`.

## Ropes And Text

Use `rope<T>` for persistent chunked positional sequences. Use `measured_rope<T, MeasurePolicy>` when
you also need cumulative-measure navigation.

```cpp
auto rope = ft::rope<int>::from_range(std::vector{1, 2, 3, 4});
auto edited = rope.insert_at(2, 99).remove_at(0);
auto slice = edited.slice(1, 2);

auto measured = ft::measured_rope<int, ft::sum_measure<int>>::from_range(std::vector{5, 1, 4});
auto located = measured.try_locate_by_measure(ft::sum_above_predicate<int>{5});
```

`rope<T>::from_chunks` accepts immutable shared vector storage when retaining chunk backing storage
is intentional. `compact()` rebuilds fresh chunks to release oversized retained backing storage.

For text:

```cpp
auto text = ft::to_text_rope("hello\nworld\n");

std::size_t lines = ft::line_count(text);  // 3, including the trailing empty line
auto position = ft::line_column_of(text, 8);
std::string second = ft::get_line(text, 1);

auto inserted = text.insert_range(5, std::string{",\nthere"});
std::string materialized = ft::as_string(inserted);
```

For incremental construction:

```cpp
auto builder = ft::rope_builder{};
builder.append("hello").append(' ').append("world").append_line().append_line("tail");

auto char_rope = builder.to_rope();
auto text_rope = builder.to_text_rope();
```

## Concurrency And Publication

Collection values are immutable after construction. Independent snapshots can be read concurrently,
and update-shaped operations return new values without mutating existing ones.

`daba_lite` is the explicit exception: it is mutable streaming state, has no snapshot operation, and requires
external synchronization around every overlapping access.

If one thread publishes snapshots to another, publish the owning pointer atomically. Include `<atomic>`
and `<memory>` for this pattern:

```cpp
auto published = std::atomic<std::shared_ptr<const ft::rope<int>>>{};
published.store(std::make_shared<const ft::rope<int>>(ft::rope<int>{}));

auto next = std::make_shared<const ft::rope<int>>(
    published.load()->push_back(42));
published.store(next);
```

Do not race on the same non-atomic local variable while another thread reassigns it. The data
structure nodes are immutable and internally publication-safe, but ordinary C++ object lifetime and
data-race rules still apply to your variables.

## Choosing A Surface

| Need | Start with |
| --- | --- |
| Persistent indexed sequence with endpoint edits | `persistent_deque<T>` |
| Mutable FIFO aggregate with worst-case bounded slides | `daba_lite<T, MonoidPolicy>` |
| Custom monoid measure, measure-guided locate, or split | `finger_tree<T, MeasurePolicy>` |
| O(1) logical reverse | `reversible_deque<T>` |
| Sorted values with duplicates | `sorted_bag<T, Less>` |
| Unique sorted values and set algebra | `sorted_set<T, Less>` |
| Sorted key/value lookup and rank access | `sorted_map<Key, T, Less>` |
| Worst-case O(1) persistent insertion and meld | `brodal_okasaki_heap<T, Less>` |
| Minimum-priority draining and meld | `priority_queue<T, Priority, Comparison>` |
| Closed-interval overlap and containment queries | `interval_tree<T, Comparison>` |
| Chunked persistent positional sequence | `rope<T>` |
| Chunked sequence with cumulative measure navigation | `measured_rope<T, MeasurePolicy>` |
| Newline-aware text content | `text_rope`, `rope_builder`, and text helper functions |

For validation scope, see [validation.md](validation.md). For cross-language contract alignment, see
the repository [porting and semantic parity guide](../../../../docs/guides/porting-and-semantic-parity.md).
