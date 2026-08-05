/// Tests for the fixed-length persistent run-delta vector.
///
/// The delicate properties are not the point reads: they are that the run index stays maximal,
/// non-overlapping, and non-adjacent under every edit shape; that a whole-run accept or revert is a
/// structural splice making no value comparison at all; and that a clean position reuses the *exact*
/// checkpoint representative rather than an equal one, which only pointer identity can witness.

#include <durable7/finger_tree/persistent_run_delta_vector.hpp>

#include "test_support/allocation_counter.hpp"
#include "test_support/command_model.hpp"
#include "test_support/test_runner.hpp"

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

using namespace durable7::finger_tree;
using namespace durable7::finger_tree::tests;

namespace {

using int_vector = persistent_run_delta_vector<int>;

/// Counts every value-policy call, so a run splice can be proven comparison-free. The policy is a
/// type rather than an object, so the counter is static state on that type.
struct counting_equality final {
    static inline std::atomic<std::size_t> call_count{0};

    /// Whether two values are equal, recording the call.
    [[nodiscard]] static bool equals(const std::int64_t& left, const std::int64_t& right) noexcept
    {
        call_count.fetch_add(1, std::memory_order_relaxed);
        return left == right;
    }

    /// How many calls have been counted.
    [[nodiscard]] static std::size_t count() noexcept
    {
        return call_count.load(std::memory_order_relaxed);
    }

    /// Clears the count.
    static void reset() noexcept
    {
        call_count.store(0, std::memory_order_relaxed);
    }
};

using counted_vector = persistent_run_delta_vector<std::int64_t, counting_equality>;

/// A coarser-than-natural equivalence relation, so "policy-equal" and "same value" differ.
struct ascii_case_insensitive_equality final {
    /// Whether two strings match ignoring ASCII case.
    [[nodiscard]] static bool equals(const std::string& left, const std::string& right)
    {
        if (left.size() != right.size()) {
            return false;
        }

        for (auto index = std::size_t{0}; index != left.size(); ++index) {
            if (fold(left[index]) != fold(right[index])) {
                return false;
            }
        }

        return true;
    }

private:
    [[nodiscard]] static char fold(const char value) noexcept
    {
        return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
    }
};

using string_vector = persistent_run_delta_vector<std::string, ascii_case_insensitive_equality>;

/// A finer-than-natural equivalence relation: two equal integers in distinct boxes are different.
struct pointer_identity_equality final {
    /// Whether two handles denote the same box.
    [[nodiscard]] static bool equals(
        const std::shared_ptr<int>& left,
        const std::shared_ptr<int>& right) noexcept
    {
        return left == right;
    }
};

using boxed_vector = persistent_run_delta_vector<std::shared_ptr<int>, pointer_identity_equality>;

/// Throws on one armed call, so a failing policy can be shown to publish no partial version.
struct throwing_equality final {
    static inline std::size_t call_count = 0;
    static inline std::size_t armed_call = 0;

    /// Whether two values are equal, throwing on the armed call.
    [[nodiscard]] static bool equals(const int& left, const int& right)
    {
        ++call_count;
        if (call_count == armed_call) {
            armed_call = 0;
            throw std::runtime_error("value policy failure");
        }

        return left == right;
    }

    /// Arms the given call ordinal.
    static void arm(const std::size_t call) noexcept
    {
        call_count = 0;
        armed_call = call;
    }
};

using armed_vector = persistent_run_delta_vector<int, throwing_equality>;

[[nodiscard]] int_vector natural_vector(const std::vector<int>& values)
{
    return int_vector::from_range(values);
}

[[nodiscard]] std::vector<int> repeated(const std::size_t count, const int value)
{
    return std::vector<int>(count, value);
}

/// The maximal differing runs of two equal-length models, recomputed from scratch.
[[nodiscard]] std::vector<persistent_dirty_run> expected_runs(
    const std::vector<int>& current,
    const std::vector<int>& checkpoint)
{
    auto runs = std::vector<persistent_dirty_run>{};
    auto index = std::size_t{0};
    while (index != current.size()) {
        if (current[index] == checkpoint[index]) {
            ++index;
            continue;
        }

        const auto start = index;
        while (index != current.size() && current[index] != checkpoint[index]) {
            ++index;
        }

        runs.push_back(persistent_dirty_run{start, index - start});
    }

    return runs;
}

void require_runs(
    const std::vector<persistent_dirty_run>& actual,
    const std::vector<persistent_dirty_run>& expected)
{
    FT_REQUIRE_EQUAL(actual.size(), expected.size());
    for (auto rank = std::size_t{0}; rank != expected.size(); ++rank) {
        FT_REQUIRE_EQUAL(actual[rank].start, expected[rank].start);
        FT_REQUIRE_EQUAL(actual[rank].length, expected[rank].length);
    }
}

[[nodiscard]] std::size_t total_length(const std::vector<persistent_dirty_run>& runs)
{
    auto total = std::size_t{0};
    for (const auto& run : runs) {
        total += run.length;
    }

    return total;
}

/// Checks one version against an independently maintained pair of plain models.
void require_matches_model(
    const int_vector& vector,
    const std::vector<int>& current,
    const std::vector<int>& checkpoint)
{
    FT_REQUIRE_EQUAL(vector.size(), current.size());
    FT_REQUIRE(vector.to_vector() == current);
    FT_REQUIRE(vector.checkpoint_to_vector() == checkpoint);

    const auto runs = expected_runs(current, checkpoint);
    const auto dirty_total = total_length(runs);
    require_runs(vector.dirty_runs(), runs);
    FT_REQUIRE_EQUAL(vector.dirty_count(), dirty_total);
    FT_REQUIRE_EQUAL(vector.dirty_run_count(), runs.size());
    FT_REQUIRE_EQUAL(vector.has_changes(), dirty_total != 0);

    for (auto index = std::size_t{0}; index != current.size(); ++index) {
        FT_REQUIRE_EQUAL(vector.at(index), current[index]);
        FT_REQUIRE_EQUAL(vector.checkpoint_at(index), checkpoint[index]);

        auto containing = std::optional<persistent_dirty_run>{};
        for (const auto& run : runs) {
            if (run.contains(index)) {
                containing = run;
            }
        }

        FT_REQUIRE_EQUAL(vector.is_dirty(index), containing.has_value());
        FT_REQUIRE(vector.dirty_run_containing(index) == containing);
        // The cleanliness invariant in its observable form: a clean position holds the *same* box as
        // the checkpoint, a dirty one a different box.
        FT_REQUIRE_EQUAL(vector.shares_representative_at(index), !containing.has_value());
    }

    for (auto rank = std::size_t{0}; rank != runs.size(); ++rank) {
        FT_REQUIRE(vector.dirty_run_at(rank) == runs[rank]);
        FT_REQUIRE(vector.try_dirty_run_at(rank) == std::optional{runs[rank]});
    }
    FT_REQUIRE(!vector.try_dirty_run_at(runs.size()).has_value());

    const auto statistics = vector.validate_structure();
    FT_REQUIRE_EQUAL(statistics.count, current.size());
    FT_REQUIRE_EQUAL(statistics.dirty_count, dirty_total);
    FT_REQUIRE_EQUAL(statistics.dirty_run_count, runs.size());
    FT_REQUIRE_EQUAL(statistics.shared_representative_count, current.size() - dirty_total);
}

/// A counted vector of 0..length with two dirty runs: a long leading one and a trailing singleton.
[[nodiscard]] counted_vector clustered_vector(const std::size_t length, const std::size_t leading)
{
    auto values = std::vector<std::int64_t>{};
    values.reserve(length);
    for (auto index = std::size_t{0}; index != length; ++index) {
        values.push_back(static_cast<std::int64_t>(index));
    }

    auto vector = counted_vector::from_range(values);
    for (auto index = std::size_t{0}; index != leading; ++index) {
        vector = vector.set_item(index, 1'000'000 + static_cast<std::int64_t>(index));
    }

    return vector.set_item(length - 1, 1'000'000 + static_cast<std::int64_t>(length - 1));
}

} // namespace

void add_persistent_run_delta_vector_tests(suite& tests)
{
    tests.add("Run-delta empty and factory contracts are exact", [] {
        const auto empty = int_vector{};
        FT_REQUIRE(empty.empty());
        FT_REQUIRE_EQUAL(empty.size(), std::size_t{0});
        FT_REQUIRE(!empty.has_changes());
        FT_REQUIRE_EQUAL(empty.dirty_count(), std::size_t{0});
        FT_REQUIRE_EQUAL(empty.dirty_run_count(), std::size_t{0});
        FT_REQUIRE(empty.dirty_runs().empty());
        FT_REQUIRE(empty.to_vector().empty());
        FT_REQUIRE(empty.begin() == empty.end());
        FT_REQUIRE(empty.try_get(0) == nullptr);
        FT_REQUIRE(empty.try_get_checkpoint(0) == nullptr);
        FT_REQUIRE(!empty.try_dirty_run_at(0).has_value());
        FT_REQUIRE(int_vector::empty_vector().empty());

        // Every position-addressed and rank-addressed entry point rejects an out-of-range argument
        // rather than reporting absence.
        FT_REQUIRE_THROWS(std::out_of_range, empty.at(0));
        FT_REQUIRE_THROWS(std::out_of_range, empty[0]);
        FT_REQUIRE_THROWS(std::out_of_range, empty.checkpoint_at(0));
        FT_REQUIRE_THROWS(std::out_of_range, empty.is_dirty(0));
        FT_REQUIRE_THROWS(std::out_of_range, empty.dirty_run_containing(0));
        FT_REQUIRE_THROWS(std::out_of_range, empty.dirty_run_at(0));
        FT_REQUIRE_THROWS(std::out_of_range, empty.set_item(0, 1));
        FT_REQUIRE_THROWS(std::out_of_range, empty.reset_item(0));
        FT_REQUIRE_THROWS(std::out_of_range, empty.accept_dirty_run_at(0));
        FT_REQUIRE_THROWS(std::out_of_range, empty.revert_dirty_run_at(0));
        FT_REQUIRE_THROWS(std::out_of_range, empty.accept_dirty_run_containing(0));
        FT_REQUIRE_THROWS(std::out_of_range, empty.revert_dirty_run_containing(0));
        FT_REQUIRE_THROWS(std::out_of_range, empty.current_representative_at(0));
        FT_REQUIRE_THROWS(std::out_of_range, empty.checkpoint_representative_at(0));

        const auto listed = int_vector{4, 5, 6};
        const auto ranged = natural_vector(std::vector<int>{4, 5, 6});
        const auto source = std::vector<int>{4, 5, 6};
        const auto paired = int_vector{source.begin(), source.end()};
        for (const auto& built : {listed, ranged, paired}) {
            FT_REQUIRE(built.to_vector() == source);
            FT_REQUIRE(built.checkpoint_to_vector() == source);
            FT_REQUIRE(!built.has_changes());
            require_matches_model(built, source, source);
        }
        FT_REQUIRE_EQUAL(*listed.try_get(1), 5);
        FT_REQUIRE_EQUAL(*listed.try_get_checkpoint(1), 5);
        FT_REQUIRE(listed.try_get(3) == nullptr);

        constexpr auto run = persistent_dirty_run{3, 2};
        FT_REQUIRE_EQUAL(run.end_exclusive(), std::size_t{5});
        FT_REQUIRE(run.contains(3));
        FT_REQUIRE(run.contains(4));
        FT_REQUIRE(!run.contains(2));
        FT_REQUIRE(!run.contains(5));
        FT_REQUIRE(!persistent_dirty_run{}.contains(0));
        FT_REQUIRE((persistent_dirty_run{1, 1} < run));
        const auto overflowing = persistent_dirty_run{(std::numeric_limits<std::size_t>::max)(), 1};
        FT_REQUIRE_THROWS(std::overflow_error, overflowing.end_exclusive());

        const auto statistics = empty.validate_structure();
        FT_REQUIRE_EQUAL(statistics.count, std::size_t{0});
        FT_REQUIRE_EQUAL(statistics.shared_representative_count, std::size_t{0});
    });

    tests.add("Run-delta policy-equal writes are no-ops sharing every root", [] {
        const auto source = string_vector{"Alpha", "Beta", "Gamma"};

        // A policy-equal write changes nothing: same roots, same representative, same accounting.
        const auto no_op = source.set_item(0, "ALPHA");
        FT_REQUIRE(!no_op.has_changes());
        FT_REQUIRE_EQUAL(no_op.dirty_count(), std::size_t{0});
        FT_REQUIRE_EQUAL(no_op.dirty_run_count(), std::size_t{0});
        FT_REQUIRE_EQUAL(no_op.at(0), std::string{"Alpha"});
        FT_REQUIRE(no_op.shares_current_root_with(source));
        FT_REQUIRE(no_op.shares_checkpoint_root_with(source));
        FT_REQUIRE(no_op.current_representative_at(0) == source.current_representative_at(0));
        (void)no_op.validate_structure();

        // Negative control: the sharing predicates are not vacuously true. A genuine edit shares the
        // checkpoint root but not the current root.
        const auto dirty = source.set_item(0, "Delta");
        FT_REQUIRE(!dirty.shares_current_root_with(source));
        FT_REQUIRE(dirty.shares_checkpoint_root_with(source));
        FT_REQUIRE(dirty.current_representative_at(0) != source.current_representative_at(0));
        FT_REQUIRE_EQUAL(dirty.dirty_count(), std::size_t{1});
        require_runs(dirty.dirty_runs(), {persistent_dirty_run{0, 1}});
        FT_REQUIRE_EQUAL(dirty.at(0), std::string{"Delta"});
        FT_REQUIRE_EQUAL(dirty.checkpoint_at(0), std::string{"Alpha"});
        (void)dirty.validate_structure();

        // Returning to the checkpoint's equality class cancels the delta and restores the exact
        // checkpoint box, not a fresh box holding an equal string.
        const auto cancelled = dirty.set_item(0, "alpha");
        FT_REQUIRE(!cancelled.has_changes());
        FT_REQUIRE_EQUAL(cancelled.at(0), std::string{"Alpha"});
        FT_REQUIRE(cancelled.current_representative_at(0) == source.checkpoint_representative_at(0));
        FT_REQUIRE(cancelled.shares_checkpoint_root_with(source));
        // The last dirty position clearing canonicalizes the current root onto the checkpoint root.
        FT_REQUIRE(cancelled.shares_current_root_with(source));
        (void)cancelled.validate_structure();

        // Every retained intermediate version is untouched.
        FT_REQUIRE_EQUAL(dirty.at(0), std::string{"Delta"});
        FT_REQUIRE_EQUAL(source.at(0), std::string{"Alpha"});
        FT_REQUIRE(!source.has_changes());
    });

    tests.add("Run-delta cancellation restores the exact checkpoint representative", [] {
        // A pointer-identity policy proves restoration is by identity, never merely by value: the
        // two boxes below are equal integers stored in distinct allocations.
        const auto checkpoint_box = std::make_shared<int>(7);
        const auto replacement_box = std::make_shared<int>(7);
        const auto neighbour_box = std::make_shared<int>(8);
        const auto neighbour_replacement = std::make_shared<int>(9);
        FT_REQUIRE_EQUAL(*checkpoint_box, *replacement_box);
        FT_REQUIRE(checkpoint_box != replacement_box);

        // A second dirty position is deliberate: with only one, clearing it would empty the run
        // index and the clean-version canonicalization would restore the whole checkpoint root,
        // masking a cancellation that had actually installed the wrong representative.
        const auto source = boxed_vector{checkpoint_box, neighbour_box};
        const auto dirty = source.set_item(0, replacement_box).set_item(1, neighbour_replacement);
        FT_REQUIRE(dirty.is_dirty(0));
        FT_REQUIRE(dirty.at(0) == replacement_box);
        FT_REQUIRE(dirty.checkpoint_at(0) == checkpoint_box);
        FT_REQUIRE(!dirty.shares_representative_at(0));
        (void)dirty.validate_structure();

        const auto reset = dirty.reset_item(0);
        FT_REQUIRE_EQUAL(reset.dirty_count(), std::size_t{1});
        FT_REQUIRE(reset.at(0) == checkpoint_box);
        FT_REQUIRE(reset.shares_representative_at(0));
        FT_REQUIRE(reset.current_representative_at(0) == source.checkpoint_representative_at(0));
        FT_REQUIRE(!reset.shares_representative_at(1));
        (void)reset.validate_structure();

        // The same guarantee through set_item under a value policy, where the substrate's
        // equal-replacement short circuit would otherwise be free to keep the wrong object: writing
        // a string in the checkpoint's equality class must reinstate the checkpoint's own box while
        // the neighbouring run record keeps the version off the canonicalization path.
        const auto text = string_vector{"one", "two", "three"};
        const auto checkpoint_identity = text.checkpoint_representative_at(1);
        const auto edited = text.set_item(1, "eleven").set_item(2, "thirteen");
        FT_REQUIRE(edited.current_representative_at(1) != checkpoint_identity);
        require_runs(edited.dirty_runs(), {persistent_dirty_run{1, 2}});

        const auto restored = edited.set_item(1, std::string{"two"});
        FT_REQUIRE_EQUAL(restored.dirty_count(), std::size_t{1});
        FT_REQUIRE(restored.current_representative_at(1) == checkpoint_identity);
        FT_REQUIRE(restored.shares_representative_at(1));
        (void)restored.validate_structure();

        // A case-only rewrite lands in the checkpoint's class under this policy, so it must restore
        // the checkpoint's own box rather than store the equal-but-differently-cased replacement.
        const auto folded = edited.set_item(1, std::string{"TWO"});
        FT_REQUIRE(folded.current_representative_at(1) == checkpoint_identity);
        FT_REQUIRE_EQUAL(folded.at(1), std::string{"two"});
        (void)folded.validate_structure();

        // reset_item on an already clean position is a no-op returning the receiver's contents.
        const auto clean = restored.reset_item(2);
        FT_REQUIRE(!clean.has_changes());
        const auto unchanged = clean.reset_item(1);
        FT_REQUIRE(unchanged.shares_current_root_with(clean));
        FT_REQUIRE(unchanged.shares_checkpoint_root_with(clean));
        // Negative control for the same predicate against a genuinely different version.
        FT_REQUIRE(!unchanged.shares_current_root_with(edited));
        (void)unchanged.validate_structure();
    });

    tests.add("Run-delta point edits keep the run index maximal and non-adjacent", [] {
        auto model = repeated(12, 0);
        const auto checkpoint = model;
        auto vector = natural_vector(model);

        const auto write = [&](const std::size_t index, const int value) {
            vector = vector.set_item(index, value);
            model[index] = value;
            require_matches_model(vector, model, checkpoint);
        };
        const auto clear = [&](const std::size_t index) {
            vector = vector.reset_item(index);
            model[index] = checkpoint[index];
            require_matches_model(vector, model, checkpoint);
        };

        // Two detached singletons.
        write(2, 20);
        write(4, 40);
        require_runs(vector.dirty_runs(), {persistent_dirty_run{2, 1}, persistent_dirty_run{4, 1}});

        // Both-merge: the gap between two singletons fuses them into one run.
        write(3, 30);
        require_runs(vector.dirty_runs(), {persistent_dirty_run{2, 3}});

        // Right-merge then left-merge: a detached position, then the gap joining it leftwards.
        write(6, 60);
        require_runs(vector.dirty_runs(), {persistent_dirty_run{2, 3}, persistent_dirty_run{6, 1}});
        write(5, 50);
        require_runs(vector.dirty_runs(), {persistent_dirty_run{2, 5}});

        // Left-extension at the low edge keeps one maximal run.
        write(1, 10);
        require_runs(vector.dirty_runs(), {persistent_dirty_run{1, 6}});

        // The four-way remove split: interior, left edge, right edge, and sole position.
        clear(4);
        require_runs(vector.dirty_runs(), {persistent_dirty_run{1, 3}, persistent_dirty_run{5, 2}});
        clear(1);
        require_runs(vector.dirty_runs(), {persistent_dirty_run{2, 2}, persistent_dirty_run{5, 2}});
        clear(6);
        require_runs(vector.dirty_runs(), {persistent_dirty_run{2, 2}, persistent_dirty_run{5, 1}});
        clear(5);
        require_runs(vector.dirty_runs(), {persistent_dirty_run{2, 2}});
        clear(3);
        require_runs(vector.dirty_runs(), {persistent_dirty_run{2, 1}});

        // Clearing the last dirty position canonicalizes the current root onto the checkpoint root.
        const auto before_last = vector;
        clear(2);
        FT_REQUIRE(!vector.has_changes());
        FT_REQUIRE(vector.dirty_runs().empty());
        const auto rolled_back = vector.rollback();
        FT_REQUIRE(vector.shares_current_root_with(rolled_back));
        FT_REQUIRE(vector.shares_checkpoint_root_with(rolled_back));
        // Negative control: the retained one-run predecessor is genuinely a different version.
        FT_REQUIRE(!vector.shares_current_root_with(before_last));
        FT_REQUIRE_EQUAL(before_last.dirty_count(), std::size_t{1});
    });

    tests.add("Run-delta checkpoint and rollback are constant-time root swaps", [] {
        const auto source = natural_vector(std::vector<int>{0, 1, 2, 3, 4, 5});
        const auto edited = source.set_item(1, 10).set_item(4, 40);
        FT_REQUIRE_EQUAL(edited.dirty_count(), std::size_t{2});

        const auto accepted = edited.checkpoint();
        FT_REQUIRE(!accepted.has_changes());
        FT_REQUIRE(accepted.to_vector() == edited.to_vector());
        FT_REQUIRE(accepted.checkpoint_to_vector() == edited.to_vector());
        FT_REQUIRE(accepted.shares_current_root_with(edited));
        FT_REQUIRE(!accepted.shares_checkpoint_root_with(edited));
        (void)accepted.validate_structure();

        const auto reverted = edited.rollback();
        FT_REQUIRE(!reverted.has_changes());
        FT_REQUIRE(reverted.to_vector() == source.to_vector());
        FT_REQUIRE(reverted.shares_checkpoint_root_with(edited));
        FT_REQUIRE(!reverted.shares_current_root_with(edited));
        (void)reverted.validate_structure();

        // Already clean: both operations return an equal value sharing storage. The comparison is
        // between two genuinely distinct handles, not a version against itself.
        const auto again = accepted.checkpoint();
        const auto rolled = accepted.rollback();
        FT_REQUIRE(again.shares_current_root_with(accepted));
        FT_REQUIRE(again.shares_checkpoint_root_with(accepted));
        FT_REQUIRE(rolled.shares_current_root_with(accepted));
        FT_REQUIRE(rolled.shares_checkpoint_root_with(accepted));
        FT_REQUIRE(again.to_vector() == accepted.to_vector());
        // Negative control on the same predicate: an unrelated same-length version shares nothing.
        FT_REQUIRE(!again.shares_current_root_with(source));
        FT_REQUIRE(!again.shares_checkpoint_root_with(source));

        // The retained source and edited versions are untouched by either derivation.
        FT_REQUIRE(!source.has_changes());
        FT_REQUIRE_EQUAL(edited.dirty_count(), std::size_t{2});
    });

    tests.add("Run-delta run splices touch only the selected run and compare no values", [] {
        auto values = std::vector<std::int64_t>{};
        for (auto value = std::int64_t{0}; value != 20; ++value) {
            values.push_back(value);
        }
        const auto original = counted_vector::from_range(values);
        auto edited = original;
        for (const auto index : {2u, 3u, 4u, 5u, 9u, 10u, 11u, 12u, 13u, 17u}) {
            edited = edited.set_item(index, 1'000 + static_cast<std::int64_t>(index));
        }
        const auto expected = std::vector<persistent_dirty_run>{
            persistent_dirty_run{2, 4},
            persistent_dirty_run{9, 5},
            persistent_dirty_run{17, 1}};
        require_runs(edited.dirty_runs(), expected);
        FT_REQUIRE_EQUAL(edited.dirty_count(), std::size_t{10});

        // Positive control: the counter is live, so a zero delta below means something.
        counting_equality::reset();
        const auto probe = edited.set_item(0, -1);
        FT_REQUIRE(counting_equality::count() > 0);
        FT_REQUIRE_EQUAL(probe.dirty_count(), std::size_t{11});

        // Splicing a whole run must not consult the value policy at all.
        auto before = counting_equality::count();
        const auto accepted_middle = edited.accept_dirty_run_at(1);
        FT_REQUIRE_EQUAL(counting_equality::count(), before);
        require_runs(
            accepted_middle.dirty_runs(),
            {persistent_dirty_run{2, 4}, persistent_dirty_run{17, 1}});
        FT_REQUIRE_EQUAL(accepted_middle.dirty_count(), std::size_t{5});
        for (auto index = std::size_t{9}; index != 14; ++index) {
            FT_REQUIRE_EQUAL(accepted_middle.at(index), 1'000 + static_cast<std::int64_t>(index));
            FT_REQUIRE_EQUAL(
                accepted_middle.checkpoint_at(index),
                1'000 + static_cast<std::int64_t>(index));
            // A splice moves subtrees: the checkpoint now holds the current version's own boxes.
            FT_REQUIRE(
                accepted_middle.checkpoint_representative_at(index)
                == edited.current_representative_at(index));
        }
        // Accepting leaves the current root untouched by identity.
        FT_REQUIRE(accepted_middle.shares_current_root_with(edited));
        FT_REQUIRE(!accepted_middle.shares_checkpoint_root_with(edited));
        (void)accepted_middle.validate_structure();

        before = counting_equality::count();
        const auto reverted_first = accepted_middle.revert_dirty_run_at(0);
        FT_REQUIRE_EQUAL(counting_equality::count(), before);
        require_runs(reverted_first.dirty_runs(), {persistent_dirty_run{17, 1}});
        for (auto index = std::size_t{2}; index != 6; ++index) {
            FT_REQUIRE_EQUAL(reverted_first.at(index), static_cast<std::int64_t>(index));
            FT_REQUIRE(reverted_first.shares_representative_at(index));
        }
        FT_REQUIRE(reverted_first.shares_checkpoint_root_with(accepted_middle));
        FT_REQUIRE(!reverted_first.shares_current_root_with(accepted_middle));
        (void)reverted_first.validate_structure();

        // The sole-run case collapses onto the whole-checkpoint path, still without comparisons.
        before = counting_equality::count();
        const auto clean = reverted_first.accept_dirty_run_at(0);
        FT_REQUIRE_EQUAL(counting_equality::count(), before);
        FT_REQUIRE(!clean.has_changes());
        FT_REQUIRE_EQUAL(clean.checkpoint_at(17), std::int64_t{1'017});
        (void)clean.validate_structure();

        // Every retained source version is unchanged.
        require_runs(edited.dirty_runs(), expected);
        FT_REQUIRE(!original.has_changes());
        FT_REQUIRE(original.to_vector() == values);

        FT_REQUIRE_THROWS(std::out_of_range, edited.accept_dirty_run_at(3));
        FT_REQUIRE_THROWS(std::out_of_range, edited.revert_dirty_run_at(3));
        FT_REQUIRE_THROWS(std::out_of_range, edited.accept_dirty_run_containing(20));
        FT_REQUIRE_THROWS(std::out_of_range, edited.revert_dirty_run_containing(20));
    });

    tests.add("Run-delta runs are addressable by rank and by any contained position", [] {
        auto values = std::vector<std::int64_t>{};
        for (auto value = std::int64_t{0}; value != 16; ++value) {
            values.push_back(value);
        }
        const auto original = counted_vector::from_range(values);
        auto edited = original;
        for (const auto index : {2u, 3u, 4u, 5u, 9u, 10u, 11u}) {
            edited = edited.set_item(index, 1'000 + static_cast<std::int64_t>(index));
        }
        const auto expected =
            std::vector<persistent_dirty_run>{persistent_dirty_run{2, 4}, persistent_dirty_run{9, 3}};
        require_runs(edited.dirty_runs(), expected);

        // A clean position is vacuous, not an error: the receiver's contents come back sharing every
        // root, and consult no comparer.
        for (const auto index : {std::size_t{0}, std::size_t{6}, std::size_t{8}, std::size_t{15}}) {
            FT_REQUIRE(!edited.is_dirty(index));
            const auto before = counting_equality::count();
            const auto accepted = edited.accept_dirty_run_containing(index);
            const auto reverted = edited.revert_dirty_run_containing(index);
            FT_REQUIRE_EQUAL(counting_equality::count(), before);
            for (const auto& unchanged : {accepted, reverted}) {
                FT_REQUIRE(unchanged.shares_current_root_with(edited));
                FT_REQUIRE(unchanged.shares_checkpoint_root_with(edited));
                FT_REQUIRE_EQUAL(unchanged.dirty_count(), edited.dirty_count());
                require_runs(unchanged.dirty_runs(), expected);
            }
        }

        // Every position inside a run acts exactly as the rank-addressed operation on that run.
        for (auto rank = std::size_t{0}; rank != expected.size(); ++rank) {
            const auto& run = expected[rank];
            const auto accepted_by_rank = edited.accept_dirty_run_at(rank);
            const auto reverted_by_rank = edited.revert_dirty_run_at(rank);
            for (auto index = run.start; index != run.end_exclusive(); ++index) {
                FT_REQUIRE(edited.dirty_run_containing(index) == std::optional{run});
                const auto before = counting_equality::count();
                const auto accepted_by_position = edited.accept_dirty_run_containing(index);
                const auto reverted_by_position = edited.revert_dirty_run_containing(index);
                FT_REQUIRE_EQUAL(counting_equality::count(), before);

                FT_REQUIRE(accepted_by_position.to_vector() == accepted_by_rank.to_vector());
                FT_REQUIRE(
                    accepted_by_position.checkpoint_to_vector()
                    == accepted_by_rank.checkpoint_to_vector());
                FT_REQUIRE(reverted_by_position.to_vector() == reverted_by_rank.to_vector());
                FT_REQUIRE(
                    reverted_by_position.checkpoint_to_vector()
                    == reverted_by_rank.checkpoint_to_vector());
                require_runs(
                    accepted_by_position.dirty_runs(),
                    accepted_by_rank.dirty_runs());
                require_runs(
                    reverted_by_position.dirty_runs(),
                    reverted_by_rank.dirty_runs());
                (void)accepted_by_position.validate_structure();
                (void)reverted_by_position.validate_structure();
            }
        }

        // Negative control for the sharing predicate used above: a genuine splice does not share the
        // root it rewrote.
        const auto spliced = edited.accept_dirty_run_containing(3);
        FT_REQUIRE(!spliced.shares_checkpoint_root_with(edited));
        FT_REQUIRE(spliced.shares_current_root_with(edited));
        require_runs(edited.dirty_runs(), expected);
    });

    tests.add("Run-delta descriptors stay output-optimal for a clustered delta", [] {
        // The k-versus-r witness: thousands of dirty positions, exactly two descriptors.
        constexpr auto length = std::size_t{4'096};
        const auto edited = clustered_vector(length, length - 2);
        FT_REQUIRE_EQUAL(edited.dirty_count(), length - 1);
        FT_REQUIRE_EQUAL(edited.dirty_run_count(), std::size_t{2});

        counting_equality::reset();
        const auto runs = edited.dirty_runs();
        // Enumeration is driven by the run index alone: no value is inspected.
        FT_REQUIRE_EQUAL(counting_equality::count(), std::size_t{0});
        require_runs(
            runs,
            {persistent_dirty_run{0, length - 2}, persistent_dirty_run{length - 1, 1}});

        // Accepting the huge run is a splice, not a walk over its positions, and costs exactly the
        // same number of comparisons as accepting a single-position run: none.
        const auto before_long = counting_equality::count();
        const auto accepted_long = edited.accept_dirty_run_at(0);
        const auto long_calls = counting_equality::count() - before_long;

        const auto narrow = clustered_vector(length, 1);
        FT_REQUIRE_EQUAL(narrow.dirty_run_count(), std::size_t{2});
        const auto before_short = counting_equality::count();
        const auto accepted_short = narrow.accept_dirty_run_at(0);
        const auto short_calls = counting_equality::count() - before_short;
        FT_REQUIRE_EQUAL(long_calls, std::size_t{0});
        FT_REQUIRE_EQUAL(short_calls, long_calls);

        require_runs(accepted_long.dirty_runs(), {persistent_dirty_run{length - 1, 1}});
        FT_REQUIRE_EQUAL(accepted_long.dirty_count(), std::size_t{1});
        FT_REQUIRE_EQUAL(accepted_long.checkpoint_at(0), std::int64_t{1'000'000});
        FT_REQUIRE_EQUAL(accepted_long.checkpoint_at(length - 1), static_cast<std::int64_t>(length - 1));
        require_runs(accepted_short.dirty_runs(), {persistent_dirty_run{length - 1, 1}});
        (void)accepted_long.validate_structure();
        (void)accepted_short.validate_structure();
        require_runs(
            edited.dirty_runs(),
            {persistent_dirty_run{0, length - 2}, persistent_dirty_run{length - 1, 1}});

#ifndef FINGERTREE_DISABLE_ALLOCATION_TRACKING
        // With allocation tracking on, length independence is directly observable: splicing a
        // 4094-position run allocates no more than splicing a 1-position run does, up to the
        // boundary-spine slack, and nowhere near the run's length.
        constexpr auto splice_ceiling = std::size_t{512};
        {
            allocation_counting_scope scope;
            const auto long_splice = edited.accept_dirty_run_at(0);
            FT_REQUIRE(scope.allocations() < splice_ceiling);
            FT_REQUIRE_EQUAL(long_splice.dirty_count(), std::size_t{1});
        }
        {
            allocation_counting_scope scope;
            const auto short_splice = narrow.accept_dirty_run_at(0);
            FT_REQUIRE(scope.allocations() < splice_ceiling);
            FT_REQUIRE_EQUAL(short_splice.dirty_count(), std::size_t{1});
        }
#endif
    });

    tests.add("Run-delta reflexive IEEE equality keeps a NaN round trip a no-op", [] {
        // The default policy for a raw floating payload is the reflexive IEEE one, because the naive
        // relation over operator== is irreflexive on NaN and would leave a phantom run that no
        // invariant check could detect.
        static_assert(
            std::is_same_v<default_value_equality<double>, reflexive_ieee_equality<double>>);
        static_assert(std::is_same_v<default_value_equality<int>, natural_value_equality<int>>);
        FT_REQUIRE(reflexive_ieee_equality<double>::equals(
            std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::quiet_NaN()));
        FT_REQUIRE(reflexive_ieee_equality<double>::equals(0.0, -0.0));
        FT_REQUIRE(!reflexive_ieee_equality<double>::equals(
            std::numeric_limits<double>::quiet_NaN(),
            1.0));

        const auto nan = std::numeric_limits<double>::quiet_NaN();
        using double_vector = persistent_run_delta_vector<double>;
        const auto source = double_vector{nan, 1.0, 0.0};

        const auto rewritten = source.set_item(0, std::numeric_limits<double>::quiet_NaN());
        FT_REQUIRE(!rewritten.has_changes());
        FT_REQUIRE_EQUAL(rewritten.dirty_count(), std::size_t{0});
        FT_REQUIRE_EQUAL(rewritten.dirty_run_count(), std::size_t{0});
        FT_REQUIRE(rewritten.shares_current_root_with(source));
        FT_REQUIRE(std::isnan(rewritten.at(0)));
        (void)rewritten.validate_structure();

        // Signed zeros stay one class, matching the .NET default comparer the C# original uses.
        const auto signed_zero = source.set_item(2, -0.0);
        FT_REQUIRE(!signed_zero.has_changes());
        FT_REQUIRE(signed_zero.shares_current_root_with(source));

        // A genuine change is still recorded, and returning NaN cancels it against the checkpoint.
        const auto dirty = source.set_item(0, 2.0);
        require_runs(dirty.dirty_runs(), {persistent_dirty_run{0, 1}});
        FT_REQUIRE(!dirty.shares_current_root_with(source));
        const auto cancelled = dirty.set_item(0, std::numeric_limits<double>::quiet_NaN());
        FT_REQUIRE(!cancelled.has_changes());
        FT_REQUIRE(std::isnan(cancelled.at(0)));
        FT_REQUIRE(cancelled.current_representative_at(0) == source.checkpoint_representative_at(0));
        (void)cancelled.validate_structure();
    });

    tests.add("Run-delta policy failure publishes no partial version", [] {
        const auto source = armed_vector{1, 2, 3};

        // The second policy call is the checkpoint comparison, immediately before a successor would
        // be constructed.
        throwing_equality::arm(2);
        FT_REQUIRE_THROWS(std::runtime_error, source.set_item(1, 99));

        FT_REQUIRE(source.to_vector() == std::vector<int>({1, 2, 3}));
        FT_REQUIRE(!source.has_changes());
        FT_REQUIRE_EQUAL(source.dirty_count(), std::size_t{0});
        (void)source.validate_structure();

        // The first policy call is the current-value comparison, before anything is read.
        throwing_equality::arm(1);
        FT_REQUIRE_THROWS(std::runtime_error, source.reset_item(1));
        FT_REQUIRE(!source.has_changes());

        throwing_equality::arm(0);
        const auto edited = source.set_item(1, 99);
        FT_REQUIRE(edited.to_vector() == std::vector<int>({1, 99, 3}));
        require_runs(edited.dirty_runs(), {persistent_dirty_run{1, 1}});
        FT_REQUIRE(source.to_vector() == std::vector<int>({1, 2, 3}));
        (void)edited.validate_structure();
    });

    tests.add("Run-delta retained versions match an independent model under random edits", [] {
        constexpr auto length = std::size_t{40};
        auto rng = deterministic_rng{0xD17E2026ULL};

        struct version final {
            int_vector vector;
            std::vector<int> current;
            std::vector<int> checkpoint;
        };

        auto initial = std::vector<int>{};
        initial.reserve(length);
        for (auto index = std::size_t{0}; index != length; ++index) {
            initial.push_back(static_cast<int>(index % 7));
        }

        auto versions = std::vector<version>{};
        versions.push_back(version{natural_vector(initial), initial, initial});

        for (auto step = 0; step != 1'200; ++step) {
            const auto parent_index = rng.next_index(versions.size());
            const auto parent = versions[parent_index];
            auto result = parent.vector;
            auto current = parent.current;
            auto checkpoint = parent.checkpoint;

            switch (rng.next_index(7)) {
            case 0:
            case 1: {
                const auto index = rng.next_index(length);
                const auto value = static_cast<int>(rng.next_index(9)) - 4;
                result = result.set_item(index, value);
                current[index] = value;
                break;
            }
            case 2: {
                const auto index = rng.next_index(length);
                result = result.reset_item(index);
                current[index] = checkpoint[index];
                break;
            }
            case 3:
                result = result.checkpoint();
                checkpoint = current;
                break;
            case 4:
                result = result.rollback();
                current = checkpoint;
                break;
            case 5: {
                const auto runs = expected_runs(current, checkpoint);
                if (runs.empty()) {
                    break;
                }

                const auto rank = rng.next_index(runs.size());
                const auto run = runs[rank];
                if (rng.next_index(2) == 0) {
                    result = result.accept_dirty_run_at(rank);
                    for (auto index = run.start; index != run.end_exclusive(); ++index) {
                        checkpoint[index] = current[index];
                    }
                } else {
                    result = result.revert_dirty_run_at(rank);
                    for (auto index = run.start; index != run.end_exclusive(); ++index) {
                        current[index] = checkpoint[index];
                    }
                }
                break;
            }
            default: {
                // Position-addressed run operations, including the vacuous clean-position case.
                const auto index = rng.next_index(length);
                const auto runs = expected_runs(current, checkpoint);
                auto containing = std::optional<persistent_dirty_run>{};
                for (const auto& run : runs) {
                    if (run.contains(index)) {
                        containing = run;
                    }
                }

                if (rng.next_index(2) == 0) {
                    result = result.accept_dirty_run_containing(index);
                    if (containing.has_value()) {
                        for (auto position = containing->start;
                             position != containing->end_exclusive();
                             ++position) {
                            checkpoint[position] = current[position];
                        }
                    }
                } else {
                    result = result.revert_dirty_run_containing(index);
                    if (containing.has_value()) {
                        for (auto position = containing->start;
                             position != containing->end_exclusive();
                             ++position) {
                            current[position] = checkpoint[position];
                        }
                    }
                }
                break;
            }
            }

            require_matches_model(result, current, checkpoint);
            // Deriving a branch leaves the parent version exactly as it was.
            require_matches_model(parent.vector, parent.current, parent.checkpoint);
            versions.push_back(version{std::move(result), std::move(current), std::move(checkpoint)});
            if (versions.size() > 24) {
                versions.erase(versions.begin() + 1 + static_cast<std::ptrdiff_t>(
                                                          rng.next_index(versions.size() - 2)));
            }
        }

        // Every surviving retained version still reports its own state.
        for (const auto& retained : versions) {
            require_matches_model(retained.vector, retained.current, retained.checkpoint);
        }
    });

    tests.add("Run-delta independent readers observe retained snapshots concurrently", [] {
        constexpr auto length = std::size_t{512};
        auto initial = std::vector<int>{};
        initial.reserve(length);
        for (auto index = std::size_t{0}; index != length; ++index) {
            initial.push_back(static_cast<int>(index % 17));
        }

        auto source = natural_vector(initial);
        auto expected_current = initial;
        for (auto index = std::size_t{100}; index != 200; ++index) {
            const auto value = -static_cast<int>(index);
            source = source.set_item(index, value);
            expected_current[index] = value;
        }

        auto failed = std::atomic<bool>{false};
        {
            auto readers = std::vector<std::jthread>{};
            for (auto worker = std::size_t{0}; worker != 4; ++worker) {
                readers.emplace_back([&, worker] {
                    try {
                        auto branch = source;
                        for (auto iteration = std::size_t{0}; iteration != 128; ++iteration) {
                            const auto index = (worker * 97 + iteration * 31) % length;
                            if (source.at(index) != expected_current[index]
                                || source.dirty_count() != 100
                                || source.dirty_run_count() != 1) {
                                throw std::logic_error("a retained snapshot was observed torn");
                            }

                            branch = branch.set_item(
                                index,
                                static_cast<int>(worker) * 10'000 + static_cast<int>(iteration));
                        }
                        (void)branch.validate_structure();
                    } catch (...) {
                        failed.store(true, std::memory_order_relaxed);
                    }
                });
            }
        }

        FT_REQUIRE(!failed.load(std::memory_order_relaxed));
        require_matches_model(source, expected_current, initial);
    });
}
