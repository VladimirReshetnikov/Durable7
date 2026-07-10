#include <tools/data_structures/finger_tree/detail/measured_lazy_cell.hpp>

#include "test_support/allocation_counter.hpp"
#include "test_support/test_runner.hpp"

#include <atomic>
#include <memory>
#include <optional>
#include <stdexcept>

using namespace tools::data_structures::finger_tree::tests;
namespace detail = tools::data_structures::finger_tree::detail;

namespace {

struct measured_tree_stub final {
    using measure_type = int;

    int value;

    [[nodiscard]] int measure() const noexcept
    {
        return value;
    }
};

void add_measured_lazy_cell_tests_impl(suite& tests)
{
    tests.add("measured lazy cell computed tree answers measure directly", [] {
        auto tree = std::make_shared<const measured_tree_stub>(measured_tree_stub{42});
        auto cell = detail::measured_lazy_cell<measured_tree_stub>::computed(tree);

        FT_REQUIRE(cell.is_forced());
        FT_REQUIRE(cell.force() == tree);
        FT_REQUIRE_EQUAL(cell.measure(), 42);
    });

#ifndef FINGERTREE_DISABLE_ALLOCATION_TRACKING
    tests.add("measured lazy cell computed state needs one control allocation", [] {
        auto tree = std::make_shared<const measured_tree_stub>(measured_tree_stub{42});

        allocation_counting_scope allocations;
        auto cell = detail::measured_lazy_cell<measured_tree_stub>::computed(tree);

        FT_REQUIRE(cell.force() == tree);
        FT_REQUIRE_EQUAL(allocations.allocations(), static_cast<std::size_t>(1));
    });
#endif

    tests.add("measured lazy cell rejects null computed trees", [] {
        FT_REQUIRE_THROWS(
            std::invalid_argument,
            detail::measured_lazy_cell<measured_tree_stub>::computed(nullptr));
    });

    tests.add("measured lazy cell can answer push-like measure without forcing", [] {
        auto factory_calls = std::make_shared<std::atomic<int>>(0);
        auto probe_calls = std::make_shared<std::atomic<int>>(0);
        auto cell = detail::measured_lazy_cell<measured_tree_stub>::defer(
            [factory_calls] {
                factory_calls->fetch_add(1);
                return measured_tree_stub{100};
            },
            [probe_calls] -> std::optional<int> {
                probe_calls->fetch_add(1);
                return 75;
            });

        FT_REQUIRE(!cell.is_forced());
        FT_REQUIRE_EQUAL(cell.measure(), 75);
        FT_REQUIRE_EQUAL(factory_calls->load(), 0);
        FT_REQUIRE_EQUAL(probe_calls->load(), 1);
        FT_REQUIRE(!cell.is_forced());

        const auto forced = cell.force();
        FT_REQUIRE_EQUAL(forced->measure(), 100);
        FT_REQUIRE_EQUAL(factory_calls->load(), 1);
        FT_REQUIRE(cell.is_forced());
        FT_REQUIRE_EQUAL(cell.measure(), 100);
        FT_REQUIRE_EQUAL(probe_calls->load(), 1);
    });

    tests.add("measured lazy cell force-only measure path publishes the tree", [] {
        auto factory_calls = std::make_shared<std::atomic<int>>(0);
        auto cell = detail::measured_lazy_cell<measured_tree_stub>::defer_force_only([factory_calls] {
            factory_calls->fetch_add(1);
            return measured_tree_stub{64};
        });

        FT_REQUIRE(!cell.is_forced());
        FT_REQUIRE_EQUAL(cell.measure(), 64);
        FT_REQUIRE(cell.is_forced());
        FT_REQUIRE_EQUAL(factory_calls->load(), 1);
        FT_REQUIRE_EQUAL(cell.force()->measure(), 64);
        FT_REQUIRE_EQUAL(factory_calls->load(), 1);
    });

    tests.add("measured lazy cell retries when force-only measure computation throws", [] {
        auto calls = std::make_shared<std::atomic<int>>(0);
        auto cell = detail::measured_lazy_cell<measured_tree_stub>::defer_force_only([calls] {
            const auto call = calls->fetch_add(1);
            if (call == 0) {
                throw std::runtime_error{"first measure force failed"};
            }

            return measured_tree_stub{11};
        });

        FT_REQUIRE_THROWS(std::runtime_error, cell.measure());
        FT_REQUIRE(!cell.is_forced());
        FT_REQUIRE_EQUAL(cell.measure(), 11);
        FT_REQUIRE(cell.is_forced());
        FT_REQUIRE_EQUAL(calls->load(), 2);
    });

    tests.add("measured lazy cell releases captured pending state after force", [] {
        std::weak_ptr<int> weak_source;
        auto cell = [&weak_source] {
            auto source = std::make_shared<int>(5);
            weak_source = source;
            return detail::measured_lazy_cell<measured_tree_stub>::defer(
                [source] {
                    return measured_tree_stub{*source};
                },
                [] -> std::optional<int> {
                    return std::nullopt;
                });
        }();

        FT_REQUIRE(!weak_source.expired());
        FT_REQUIRE_EQUAL(cell.force()->measure(), 5);
        FT_REQUIRE(weak_source.expired());
    });
}

} // namespace

void add_measured_lazy_cell_tests(suite& tests)
{
    add_measured_lazy_cell_tests_impl(tests);
}
