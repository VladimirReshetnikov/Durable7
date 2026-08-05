/// Tests for the checkpoint-differential persistent sorted map.
///
/// The load-bearing rules are the ones a plain persistent map does not have: an equivalent write is
/// a semantic no-op, repeated writes coalesce, a return to the checkpoint state cancels a record,
/// and a wholly cancelled epoch reuses the exact checkpoint root. Sharing assertions therefore
/// always compare two distinct values and are paired with a negative control, so a predicate that
/// could never fail is visible as a broken test rather than a passing one.

#include <durable7/finger_tree/persistent_delta_map.hpp>

#include "test_support/command_model.hpp"
#include "test_support/operation_counter.hpp"
#include "test_support/test_runner.hpp"

#include <algorithm>
#include <cctype>
#include <concepts>
#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace durable7::finger_tree;
using namespace durable7::finger_tree::tests;

namespace {

using text_map = persistent_delta_map<int, std::string>;
using entry_list = std::vector<std::pair<int, std::string>>;

[[nodiscard]] char lower(const char value)
{
    return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
}

/// A key order whose equivalence classes are wider than object identity, so representative
/// retention is observable.
struct case_insensitive_less final {
    [[nodiscard]] bool operator()(const std::string& left, const std::string& right) const
    {
        return std::lexicographical_compare(
            left.begin(),
            left.end(),
            right.begin(),
            right.end(),
            [](const char l, const char r) { return lower(l) < lower(r); });
    }
};

/// A value equivalence that is coarser than `operator==`, so a differing write can still be a
/// semantic no-op.
struct case_insensitive_equal final {
    [[nodiscard]] bool operator()(const std::string& left, const std::string& right) const
    {
        return left.size() == right.size()
            && std::equal(left.begin(), left.end(), right.begin(), [](const char l, const char r) {
                   return lower(l) == lower(r);
               });
    }
};

/// A value equivalence that counts its invocations, so a test can assert an operation performed no
/// value comparison at all.
struct counting_equal final {
    operation_counter* counter = nullptr;

    [[nodiscard]] bool operator()(const std::string& left, const std::string& right) const
    {
        counter->record_invocation();
        return left == right;
    }
};

using counted_map = persistent_delta_map<int, std::string, counting_compare<>, counting_equal>;

/// A map with `count` changes over `count` checkpointed keys, built through the counting policies.
[[nodiscard]] counted_map counted_changes(
    const int count,
    operation_counter& keys,
    operation_counter& values)
{
    auto map = counted_map::create(counting_compare<>{keys}, counting_equal{&values});
    for (auto index = 0; index != count; ++index) {
        map = map.set_item(index, "before");
    }

    map = map.checkpoint();
    for (auto index = 0; index != count; ++index) {
        map = map.set_item(index, "after");
    }

    return map;
}

[[nodiscard]] entry_list contents(const text_map& map)
{
    auto entries = entry_list{};
    for (const auto& entry : map) {
        entries.push_back(entry);
    }

    return entries;
}

[[nodiscard]] std::vector<int> change_keys(const text_map& map)
{
    auto keys = std::vector<int>{};
    map.for_each_change([&keys](const text_map::change_type& change) { keys.push_back(change.key); });
    return keys;
}

[[nodiscard]] text_map sample()
{
    return text_map::create_range(entry_list{{1, "one"}, {2, "two"}, {3, "three"}});
}

} // namespace

void add_persistent_delta_map_tests(suite& tests)
{
    static_assert(value_equivalence_for<std::equal_to<std::string>, std::string>);
    static_assert(value_equivalence_for<case_insensitive_equal, std::string>);
    static_assert(std::copyable<text_map>);

    tests.add("Delta map classifies added, removed, and updated keys in ascending key order", [] {
        const auto base = sample();
        FT_REQUIRE(base.is_clean());
        FT_REQUIRE_EQUAL(base.change_count(), std::size_t{0});

        const auto edited = base.set_item(2, "TWO").remove(3).set_item(9, "nine");
        FT_REQUIRE_EQUAL(edited.size(), std::size_t{3});
        FT_REQUIRE_EQUAL(edited.change_count(), std::size_t{3});
        FT_REQUIRE(edited.has_changes());

        const auto changes = edited.changes_to_vector();
        FT_REQUIRE_EQUAL(changes.size(), std::size_t{3});
        FT_REQUIRE_EQUAL(changes[0].key, 2);
        FT_REQUIRE(changes[0].kind() == persistent_map_change_kind::updated);
        FT_REQUIRE(changes[0].before == std::optional<std::string>{"two"});
        FT_REQUIRE(changes[0].after == std::optional<std::string>{"TWO"});
        FT_REQUIRE_EQUAL(changes[1].key, 3);
        FT_REQUIRE(changes[1].kind() == persistent_map_change_kind::removed);
        FT_REQUIRE(changes[1].before == std::optional<std::string>{"three"});
        FT_REQUIRE(!changes[1].after.has_value());
        FT_REQUIRE_EQUAL(changes[2].key, 9);
        FT_REQUIRE(changes[2].kind() == persistent_map_change_kind::added);
        FT_REQUIRE(!changes[2].before.has_value());
        FT_REQUIRE(changes[2].after == std::optional<std::string>{"nine"});

        const auto statistics = edited.validate_structure();
        FT_REQUIRE_EQUAL(statistics.added_count, std::size_t{1});
        FT_REQUIRE_EQUAL(statistics.removed_count, std::size_t{1});
        FT_REQUIRE_EQUAL(statistics.updated_count, std::size_t{1});
        FT_REQUIRE_EQUAL(statistics.change_count, std::size_t{3});
        FT_REQUIRE_EQUAL(statistics.checkpoint_size, std::size_t{3});
        FT_REQUIRE(!statistics.is_clean);

        // The checkpoint snapshot still answers for the pre-edit state.
        FT_REQUIRE_EQUAL(edited.checkpoint_snapshot().at(3), std::string{"three"});
        FT_REQUIRE(!edited.contains_key(3));
    });

    tests.add("Delta map treats an equivalent write as a semantic no-op sharing every root", [] {
        const auto base = sample();
        const auto same = base.set_item(2, "two");
        FT_REQUIRE(&same != &base);
        FT_REQUIRE(same.shares_roots_with(base));
        FT_REQUIRE_EQUAL(same.change_count(), std::size_t{0});

        // Negative control: an effective write must replace the current and change roots while
        // still sharing the checkpoint, so the sharing predicate above is not vacuous.
        const auto changed = base.set_item(2, "TWO");
        FT_REQUIRE(!changed.shares_roots_with(base));
        FT_REQUIRE(!changed.shares_current_root_with(base));
        FT_REQUIRE(!changed.shares_change_root_with(base));
        FT_REQUIRE(changed.shares_checkpoint_root_with(base));

        // The retained equivalence, not `operator==`, decides what a no-op is.
        using folded_map = persistent_delta_map<int, std::string, std::less<int>, case_insensitive_equal>;
        const auto folded = folded_map::create_range(entry_list{{1, "one"}});
        const auto folded_same = folded.set_item(1, "ONE");
        FT_REQUIRE(folded_same.shares_roots_with(folded));
        FT_REQUIRE_EQUAL(folded_same.at(1), std::string{"one"});
        FT_REQUIRE(!folded.set_item(1, "other").shares_current_root_with(folded));
    });

    tests.add("Delta map coalesces repeated writes into one record capturing the first before", [] {
        const auto base = sample();
        const auto once = base.set_item(2, "a");
        const auto twice = once.set_item(2, "b");
        const auto thrice = twice.set_item(2, "c");
        FT_REQUIRE_EQUAL(thrice.change_count(), std::size_t{1});

        const auto change = thrice.try_get_change(2);
        FT_REQUIRE(change.has_value());
        FT_REQUIRE(change->before == std::optional<std::string>{"two"});
        FT_REQUIRE(change->after == std::optional<std::string>{"c"});
        FT_REQUIRE(!thrice.try_get_change(1).has_value());

        // Every intermediate version keeps its own coalesced answer.
        FT_REQUIRE(once.try_get_change(2)->after == std::optional<std::string>{"a"});
        FT_REQUIRE(twice.try_get_change(2)->after == std::optional<std::string>{"b"});
        FT_REQUIRE(once.try_get_change(2)->before == std::optional<std::string>{"two"});
        (void)thrice.validate_structure();
    });

    tests.add("Delta map cancels a record when a key returns to its checkpoint state", [] {
        const auto base = sample();

        const auto restored = base.set_item(2, "a").set_item(2, "two");
        FT_REQUIRE_EQUAL(restored.change_count(), std::size_t{0});
        FT_REQUIRE(!restored.has_changes());
        FT_REQUIRE(restored.is_clean());

        const auto round_tripped = base.set_item(7, "seven").remove(7);
        FT_REQUIRE_EQUAL(round_tripped.change_count(), std::size_t{0});
        FT_REQUIRE(round_tripped.is_clean());

        const auto readded = base.remove(1).set_item(1, "one");
        FT_REQUIRE_EQUAL(readded.change_count(), std::size_t{0});
        FT_REQUIRE(readded.is_clean());

        // A partial return leaves exactly the keys that still differ.
        const auto partial = base.set_item(1, "x").set_item(2, "y").set_item(1, "one");
        FT_REQUIRE_EQUAL(partial.change_count(), std::size_t{1});
        FT_REQUIRE(change_keys(partial) == std::vector<int>{2});
        (void)partial.validate_structure();
    });

    tests.add("Delta map snaps the current root back to the checkpoint when the index empties", [] {
        const auto base = sample();
        const auto dirty = base.set_item(1, "x").set_item(9, "y").remove(3);
        FT_REQUIRE(!dirty.shares_current_root_with(base));
        FT_REQUIRE(dirty.shares_checkpoint_root_with(base));
        FT_REQUIRE(!dirty.is_clean());

        const auto cancelled = dirty.set_item(1, "one").remove(9).set_item(3, "three");
        FT_REQUIRE(cancelled.is_clean());
        FT_REQUIRE(cancelled.shares_current_root_with(base));
        FT_REQUIRE(cancelled.shares_checkpoint_root_with(base));
        FT_REQUIRE(cancelled.current_root_identity() == cancelled.checkpoint_root_identity());
        FT_REQUIRE(contents(cancelled) == contents(base));

        const auto statistics = cancelled.validate_structure();
        FT_REQUIRE(statistics.is_clean);
        FT_REQUIRE_EQUAL(statistics.change_count, std::size_t{0});

        // A batch that ends clean behaves exactly like the equivalent point writes.
        const auto folded = base.set_items(entry_list{{1, "x"}, {2, "y"}, {1, "one"}, {2, "two"}});
        FT_REQUIRE(folded.is_clean());
        FT_REQUIRE(folded.shares_current_root_with(base));
    });

    tests.add("Delta map retains the baseline key representative across updates and re-adds", [] {
        using folded_map = persistent_delta_map<std::string, std::string, case_insensitive_less>;
        const auto base = folded_map::create_range(
            std::vector<std::pair<std::string, std::string>>{{"Key", "v0"}});
        FT_REQUIRE_EQUAL(base.try_get_entry("KEY")->first, std::string{"Key"});

        const auto updated = base.set_item("KEY", "v1");
        FT_REQUIRE_EQUAL(updated.try_get_entry("key")->first, std::string{"Key"});
        FT_REQUIRE_EQUAL(updated.try_get_change("kEy")->key, std::string{"Key"});

        const auto removed = updated.remove("kEy");
        FT_REQUIRE_EQUAL(removed.try_get_change("KEY")->key, std::string{"Key"});
        FT_REQUIRE(removed.try_get_change("KEY")->before == std::optional<std::string>{"v0"});

        const auto readded = removed.set_item("KEY", "v2");
        FT_REQUIRE_EQUAL(readded.try_get_entry("key")->first, std::string{"Key"});
        FT_REQUIRE_EQUAL(readded.try_get_change("key")->key, std::string{"Key"});
        FT_REQUIRE(readded.try_get_change("key")->before == std::optional<std::string>{"v0"});
        (void)readded.validate_structure();
    });

    tests.add("Delta map begins a new representative episode after complete cancellation", [] {
        using folded_map = persistent_delta_map<std::string, std::string, case_insensitive_less>;
        const auto base = folded_map::create();

        const auto added = base.set_item("New", "x");
        FT_REQUIRE_EQUAL(added.try_get_change("NEW")->key, std::string{"New"});

        // A still-active addition episode keeps its first representative.
        const auto rewritten = added.set_item("NEW", "y");
        FT_REQUIRE_EQUAL(rewritten.try_get_entry("new")->first, std::string{"New"});
        FT_REQUIRE_EQUAL(rewritten.try_get_change("new")->key, std::string{"New"});

        const auto cancelled = rewritten.remove("new");
        FT_REQUIRE(cancelled.is_clean());
        FT_REQUIRE(!cancelled.try_get_change("New").has_value());

        // After complete cancellation the class has no baseline, so a later addition supplies one.
        const auto reopened = cancelled.set_item("nEw", "z");
        FT_REQUIRE_EQUAL(reopened.try_get_entry("NEW")->first, std::string{"nEw"});
        FT_REQUIRE_EQUAL(reopened.try_get_change("NEW")->key, std::string{"nEw"});
        (void)reopened.validate_structure();
    });

    tests.add("Delta map removal of an absent key returns a version sharing every root", [] {
        const auto base = sample();
        const auto missing = base.remove(42);
        FT_REQUIRE(&missing != &base);
        FT_REQUIRE(missing.shares_roots_with(base));

        const auto dirty = base.set_item(5, "five");
        const auto still_missing = dirty.remove(42);
        FT_REQUIRE(still_missing.shares_roots_with(dirty));

        // Negative control: removing a present key must replace the current and change roots.
        const auto present = base.remove(1);
        FT_REQUIRE(!present.shares_current_root_with(base));
        FT_REQUIRE(!present.shares_change_root_with(base));
        FT_REQUIRE_EQUAL(present.change_count(), std::size_t{1});
    });

    tests.add("Delta map checkpoint and rollback are O(1) root swaps", [] {
        const auto base = sample();
        const auto same_checkpoint = base.checkpoint();
        const auto same_rollback = base.rollback();
        FT_REQUIRE(&same_checkpoint != &base);
        FT_REQUIRE(same_checkpoint.shares_roots_with(base));
        FT_REQUIRE(same_rollback.shares_roots_with(base));
        FT_REQUIRE(contents(same_rollback) == contents(base));

        const auto dirty = base.set_item(1, "x").set_item(4, "four");
        const auto promoted = dirty.checkpoint();
        FT_REQUIRE(promoted.is_clean());
        FT_REQUIRE(promoted.shares_current_root_with(dirty));
        FT_REQUIRE(promoted.checkpoint_root_identity() == dirty.current_root_identity());
        // Negative control: promotion genuinely moved the checkpoint root.
        FT_REQUIRE(!promoted.shares_checkpoint_root_with(dirty));
        FT_REQUIRE(contents(promoted) == contents(dirty));

        const auto reverted = dirty.rollback();
        FT_REQUIRE(reverted.is_clean());
        FT_REQUIRE(reverted.shares_current_root_with(base));
        FT_REQUIRE(reverted.shares_checkpoint_root_with(base));
        FT_REQUIRE(!reverted.shares_current_root_with(dirty));
        FT_REQUIRE(contents(reverted) == contents(base));

        // The dirty version is untouched by either derived value.
        FT_REQUIRE_EQUAL(dirty.change_count(), std::size_t{2});
        FT_REQUIRE_EQUAL(dirty.at(1), std::string{"x"});
        (void)promoted.validate_structure();
        (void)reverted.validate_structure();
    });

    tests.add("Delta map change enumeration is output-optimal and policy-callback free", [] {
        auto keys = operation_counter{};
        auto values = operation_counter{};
        constexpr auto count = 256;
        const auto map = counted_changes(count, keys, values);
        FT_REQUIRE_EQUAL(map.change_count(), std::size_t{count});

        keys.reset();
        values.reset();
        const auto changes = map.changes_to_vector();
        FT_REQUIRE_EQUAL(changes.size(), std::size_t{count});
        FT_REQUIRE_EQUAL(keys.comparisons(), std::size_t{0});
        FT_REQUIRE_EQUAL(values.invocations(), std::size_t{0});
        FT_REQUIRE_EQUAL(changes.front().key, 0);
        FT_REQUIRE_EQUAL(changes.back().key, count - 1);

        auto visited = std::size_t{0};
        keys.reset();
        values.reset();
        map.for_each_change([&visited](const counted_map::change_type& change) {
            FT_REQUIRE(change.kind() == persistent_map_change_kind::updated);
            ++visited;
        });
        FT_REQUIRE_EQUAL(visited, std::size_t{count});
        FT_REQUIRE_EQUAL(keys.comparisons(), std::size_t{0});
        FT_REQUIRE_EQUAL(values.invocations(), std::size_t{0});

        // Checkpoint and rollback invoke no policy callback either.
        keys.reset();
        values.reset();
        const auto reverted = map.checkpoint().rollback();
        FT_REQUIRE(reverted.is_clean());
        FT_REQUIRE_EQUAL(keys.comparisons(), std::size_t{0});
        FT_REQUIRE_EQUAL(values.invocations(), std::size_t{0});
    });

    tests.add("Delta map range-restricted enumeration seeks boundaries instead of filtering", [] {
        auto keys = operation_counter{};
        auto values = operation_counter{};
        const auto small = counted_changes(64, keys, values);
        const auto large = counted_changes(1024, keys, values);

        keys.reset();
        values.reset();
        const auto small_point = small.changes_in_range(32, 32);
        const auto small_comparisons = keys.comparisons();
        FT_REQUIRE_EQUAL(small_point.size(), std::size_t{1});
        FT_REQUIRE_EQUAL(values.invocations(), std::size_t{0});

        keys.reset();
        values.reset();
        const auto large_point = large.changes_in_range(512, 512);
        const auto large_comparisons = keys.comparisons();
        FT_REQUIRE_EQUAL(large_point.size(), std::size_t{1});
        FT_REQUIRE_EQUAL(large_point.front().key, 512);
        // Zero value comparisons: the restriction touches only the key order.
        FT_REQUIRE_EQUAL(values.invocations(), std::size_t{0});

        // A filter over all k records would need at least one key comparison per record, so a
        // sixteenfold larger change set would cost about sixteen times as much. A boundary seek
        // grows logarithmically instead.
        FT_REQUIRE(large_comparisons * 4 < large.change_count());
        FT_REQUIRE(large_comparisons <= small_comparisons + 32);

        keys.reset();
        values.reset();
        const auto window = large.changes_in_range(100, 103);
        FT_REQUIRE_EQUAL(window.size(), std::size_t{4});
        FT_REQUIRE_EQUAL(window.front().key, 100);
        FT_REQUIRE_EQUAL(window.back().key, 103);
        FT_REQUIRE_EQUAL(values.invocations(), std::size_t{0});
        FT_REQUIRE(keys.comparisons() * 4 < large.change_count());

        auto visited = std::size_t{0};
        large.for_each_change_in_range(
            100,
            103,
            [&visited](const counted_map::change_type&) { ++visited; });
        FT_REQUIRE_EQUAL(visited, std::size_t{4});
    });

    tests.add("Delta map inverted ranges are decided by the retained ordering", [] {
        const auto base = sample().set_item(1, "x").set_item(2, "y").set_item(3, "z");
        FT_REQUIRE_EQUAL(base.changes_in_range(1, 3).size(), std::size_t{3});
        FT_REQUIRE(base.changes_in_range(3, 1).empty());
        FT_REQUIRE(base.changes_in_range(2, 2).size() == std::size_t{1});
        FT_REQUIRE(base.get_range(3, 1).empty());

        using descending_map = persistent_delta_map<int, std::string, std::greater<int>>;
        const auto descending = descending_map::create_range(
            entry_list{{1, "one"}, {2, "two"}, {3, "three"}},
            std::greater<int>{});
        FT_REQUIRE_EQUAL(descending.keys_to_vector().front(), 3);

        const auto edited = descending.set_item(1, "x").set_item(2, "y").set_item(3, "z");
        auto keys = std::vector<int>{};
        edited.for_each_change([&keys](const descending_map::change_type& change) {
            keys.push_back(change.key);
        });
        FT_REQUIRE((keys == std::vector<int>{3, 2, 1}));

        // Under a descending order the low endpoint is the numerically greater key.
        auto in_range = std::vector<int>{};
        for (const auto& change : edited.changes_in_range(3, 1)) {
            in_range.push_back(change.key);
        }
        FT_REQUIRE((in_range == std::vector<int>{3, 2, 1}));
        FT_REQUIRE(edited.changes_in_range(1, 3).empty());
        FT_REQUIRE_EQUAL(edited.changes_in_range(2, 1).size(), std::size_t{2});
        (void)edited.validate_structure();
    });

    tests.add("Delta map lookups, ranks, and neighbours read the current state", [] {
        const auto base = sample().set_item(9, "nine").remove(2);
        FT_REQUIRE_EQUAL(base.size(), std::size_t{3});
        FT_REQUIRE_EQUAL(base.checkpoint_size(), std::size_t{3});
        FT_REQUIRE(!base.empty());
        FT_REQUIRE(base.contains_key(9));
        FT_REQUIRE(!base.contains_key(2));
        FT_REQUIRE(base.try_get(1) == std::optional<std::string>{"one"});
        FT_REQUIRE(!base.try_get(2).has_value());
        FT_REQUIRE_EQUAL(base.at(3), std::string{"three"});
        FT_REQUIRE_EQUAL(base.entry_at(0).first, 1);
        FT_REQUIRE_EQUAL(base.entry_at(2).first, 9);
        FT_REQUIRE(base.index_of_key(3) == std::optional<std::size_t>{1});
        FT_REQUIRE(!base.index_of_key(2).has_value());
        FT_REQUIRE_EQUAL(base.min_entry().first, 1);
        FT_REQUIRE_EQUAL(base.max_entry().first, 9);
        FT_REQUIRE(base.try_min_entry()->second == std::string{"one"});
        FT_REQUIRE(base.try_max_entry()->second == std::string{"nine"});
        FT_REQUIRE_EQUAL(base.try_floor_entry(5)->first, 3);
        FT_REQUIRE_EQUAL(base.try_ceiling_entry(5)->first, 9);
        FT_REQUIRE_EQUAL(base.try_lower_entry(3)->first, 1);
        FT_REQUIRE_EQUAL(base.try_higher_entry(3)->first, 9);
        FT_REQUIRE(!base.try_lower_entry(1).has_value());
        FT_REQUIRE(!base.try_higher_entry(9).has_value());
        FT_REQUIRE_EQUAL(base.get_range(1, 3).size(), std::size_t{2});
        FT_REQUIRE((base.keys_to_vector() == std::vector<int>{1, 3, 9}));
        FT_REQUIRE_EQUAL(base.values_to_vector().at(2), std::string{"nine"});
        FT_REQUIRE_EQUAL(base.to_vector().size(), std::size_t{3});
        FT_REQUIRE_EQUAL(contents(base).size(), std::size_t{3});
        FT_REQUIRE_EQUAL(base.current_snapshot().size(), std::size_t{3});
        FT_REQUIRE_EQUAL(base.checkpoint_snapshot().size(), std::size_t{3});

        const auto empty = text_map{};
        FT_REQUIRE(empty.empty());
        FT_REQUIRE(empty.is_clean());
        FT_REQUIRE(!empty.try_min_entry().has_value());
        FT_REQUIRE(!empty.try_max_entry().has_value());
        FT_REQUIRE(empty.changes_to_vector().empty());
    });

    tests.add("Delta map set_items is the fold over set_item", [] {
        const auto base = sample();
        const auto folded = base.set_items(entry_list{{1, "a"}, {4, "b"}, {1, "c"}});
        const auto stepwise = base.set_item(1, "a").set_item(4, "b").set_item(1, "c");
        FT_REQUIRE(contents(folded) == contents(stepwise));
        FT_REQUIRE(change_keys(folded) == change_keys(stepwise));
        FT_REQUIRE(folded.try_get_change(1)->before == std::optional<std::string>{"one"});
        FT_REQUIRE(folded.try_get_change(1)->after == std::optional<std::string>{"c"});

        const auto nothing = base.set_items(entry_list{});
        FT_REQUIRE(nothing.shares_roots_with(base));
        const auto no_ops = base.set_items(entry_list{{1, "one"}, {2, "two"}});
        FT_REQUIRE(no_ops.shares_roots_with(base));
        // Negative control: one effective entry is enough to publish a successor.
        FT_REQUIRE(!base.set_items(entry_list{{1, "one"}, {2, "z"}}).shares_current_root_with(base));
    });

    tests.add("Delta map retains every published version independently", [] {
        const auto base = sample();
        const auto first = base.set_item(1, "x");
        const auto second = first.remove(2);
        const auto third = second.checkpoint().set_item(3, "z");

        FT_REQUIRE_EQUAL(base.at(1), std::string{"one"});
        FT_REQUIRE_EQUAL(base.change_count(), std::size_t{0});
        FT_REQUIRE_EQUAL(first.at(1), std::string{"x"});
        FT_REQUIRE_EQUAL(first.change_count(), std::size_t{1});
        FT_REQUIRE_EQUAL(second.change_count(), std::size_t{2});
        FT_REQUIRE(second.contains_key(3));
        FT_REQUIRE_EQUAL(third.change_count(), std::size_t{1});
        FT_REQUIRE(third.try_get_change(3)->before == std::optional<std::string>{"three"});
        FT_REQUIRE_EQUAL(third.checkpoint_snapshot().at(1), std::string{"x"});

        (void)base.validate_structure();
        (void)first.validate_structure();
        (void)second.validate_structure();
        (void)third.validate_structure();
    });

    tests.add("Delta map endpoint presence stays distinct from a present empty optional", [] {
        using optional_map = persistent_delta_map<int, std::optional<int>>;
        const auto base = optional_map::create_range(
            std::vector<std::pair<int, std::optional<int>>>{{1, std::optional<int>{}}});
        FT_REQUIRE(base.contains_key(1));
        FT_REQUIRE(base.at(1) == std::optional<int>{});

        const auto removed = base.remove(1);
        const auto change = removed.try_get_change(1);
        FT_REQUIRE(change.has_value());
        FT_REQUIRE(change->kind() == persistent_map_change_kind::removed);
        // The endpoint is present; the value it carries is an empty optional.
        FT_REQUIRE(change->before.has_value());
        FT_REQUIRE(!change->before->has_value());
        FT_REQUIRE(!change->after.has_value());

        const auto assigned = base.set_item(1, std::optional<int>{7});
        FT_REQUIRE(assigned.try_get_change(1)->kind() == persistent_map_change_kind::updated);
        FT_REQUIRE(assigned.set_item(1, std::optional<int>{}).is_clean());
        (void)removed.validate_structure();
    });

    tests.add("Delta map errors use the workspace exception channels", [] {
        const auto base = sample();
        FT_REQUIRE_THROWS(std::out_of_range, base.at(42));
        FT_REQUIRE_THROWS(std::out_of_range, base.entry_at(3));
        FT_REQUIRE_THROWS(std::out_of_range, base.entry_at(base.size()));
        FT_REQUIRE_THROWS(std::logic_error, text_map{}.min_entry());
        FT_REQUIRE_THROWS(std::logic_error, text_map{}.max_entry());

        const auto impossible = persistent_map_change<int, std::string>{1, std::nullopt, std::nullopt};
        FT_REQUIRE_THROWS(std::logic_error, impossible.kind());
    });

    tests.add("Delta map reproduces a two-model reference over a randomized history", [] {
        auto rng = deterministic_rng{0xde'17'a0'0d'0f'f1ULL};
        auto map = persistent_delta_map<int, int>{};
        auto current_model = std::map<int, int>{};
        auto checkpoint_model = std::map<int, int>{};
        auto retained = std::vector<persistent_delta_map<int, int>>{};

        for (auto step = 0; step != 600; ++step) {
            const auto key = rng.next_int(0, 31);
            switch (rng.next_index(10)) {
            case 0:
            case 1:
            case 2:
            case 3:
            case 4: {
                const auto value = rng.next_int(0, 5);
                map = map.set_item(key, value);
                current_model[key] = value;
                break;
            }
            case 5:
            case 6:
            case 7:
                map = map.remove(key);
                current_model.erase(key);
                break;
            case 8:
                map = map.checkpoint();
                checkpoint_model = current_model;
                break;
            default:
                map = map.rollback();
                current_model = checkpoint_model;
                break;
            }

            auto expected_entries = std::vector<std::pair<int, int>>{};
            for (const auto& entry : current_model) {
                expected_entries.emplace_back(entry.first, entry.second);
            }
            FT_REQUIRE(map.to_vector() == expected_entries);

            auto expected_changes = std::vector<persistent_map_change<int, int>>{};
            for (const auto& entry : checkpoint_model) {
                const auto found = current_model.find(entry.first);
                if (found == current_model.end()) {
                    expected_changes.push_back({entry.first, entry.second, std::nullopt});
                } else if (found->second != entry.second) {
                    expected_changes.push_back({entry.first, entry.second, found->second});
                }
            }
            for (const auto& entry : current_model) {
                if (!checkpoint_model.contains(entry.first)) {
                    expected_changes.push_back({entry.first, std::nullopt, entry.second});
                }
            }
            std::sort(
                expected_changes.begin(),
                expected_changes.end(),
                [](const auto& left, const auto& right) { return left.key < right.key; });
            FT_REQUIRE(map.changes_to_vector() == expected_changes);

            const auto statistics = map.validate_structure();
            FT_REQUIRE_EQUAL(statistics.change_count, expected_changes.size());
            FT_REQUIRE_EQUAL(statistics.size, current_model.size());
            FT_REQUIRE_EQUAL(statistics.checkpoint_size, checkpoint_model.size());
            FT_REQUIRE_EQUAL(statistics.is_clean, expected_changes.empty());

            if (step % 97 == 0) {
                retained.push_back(map);
            }
        }

        for (const auto& version : retained) {
            (void)version.validate_structure();
        }
    });
}
