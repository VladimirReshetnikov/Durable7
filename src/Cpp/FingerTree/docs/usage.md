# C++ FingerTree Usage Guide

- Created (UTC): 2026-07-02T20:03:36Z
- Repository HEAD: 17d505f18e9e0a5748058701d408ed6642dcba29
- Audience: C++ consumers and maintainers using the public FingerTree headers
- Scope: Public include path, value semantics, common construction/update patterns, and facade quick starts

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
| Custom monoid measure, measure-guided locate, or split | `finger_tree<T, MeasurePolicy>` |
| O(1) logical reverse | `reversible_deque<T>` |
| Sorted values with duplicates | `sorted_bag<T, Less>` |
| Unique sorted values and set algebra | `sorted_set<T, Less>` |
| Sorted key/value lookup and rank access | `sorted_map<Key, T, Less>` |
| Minimum-priority draining and meld | `priority_queue<T, Priority, Comparison>` |
| Closed-interval overlap and containment queries | `interval_tree<T, Comparison>` |
| Chunked persistent positional sequence | `rope<T>` |
| Chunked sequence with cumulative measure navigation | `measured_rope<T, MeasurePolicy>` |
| Newline-aware text content | `text_rope`, `rope_builder`, and text helper functions |

For validation scope, see [validation.md](validation.md). For cross-language contract alignment, see
the repository [porting and semantic parity guide](../../../../docs/guides/porting-and-semantic-parity.md).
