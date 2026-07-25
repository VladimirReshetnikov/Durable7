#include <durable7/finger_tree/detail/lazy_cell.hpp>

#include "test_support/test_runner.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace durable7::finger_tree::tests;
namespace detail = durable7::finger_tree::detail;

namespace {

struct retry_factory {
    std::shared_ptr<std::atomic<int>> calls;

    int operator()() const
    {
        const auto call = calls->fetch_add(1);
        if (call == 0) {
            throw std::runtime_error{"first force fails"};
        }

        return 42;
    }
};

void add_lazy_cell_tests_impl(suite& tests)
{
    tests.add("lazy cell computed value is already forced", [] {
        auto cell = detail::lazy_cell<int>::computed(42);

        FT_REQUIRE(cell.is_forced());
        const auto first = cell.get();
        const auto second = cell.get();
        FT_REQUIRE_EQUAL(*first, 42);
        FT_REQUIRE(first == second);
    });

    tests.add("lazy cell rejects null computed pointers", [] {
        FT_REQUIRE_THROWS(std::invalid_argument, detail::lazy_cell<int>::from_shared(nullptr));
    });

    tests.add("lazy cell computes once in quiescent use", [] {
        auto calls = std::make_shared<std::atomic<int>>(0);
        auto cell = detail::lazy_cell<int>::defer([calls] {
            calls->fetch_add(1);
            return 7;
        });

        FT_REQUIRE(!cell.is_forced());
        const auto first = cell.get();
        const auto second = cell.get();

        FT_REQUIRE_EQUAL(*first, 7);
        FT_REQUIRE(first == second);
        FT_REQUIRE_EQUAL(calls->load(), 1);
        FT_REQUIRE(cell.is_forced());
    });

    tests.add("lazy cell retries after factory exceptions", [] {
        auto calls = std::make_shared<std::atomic<int>>(0);
        auto cell = detail::lazy_cell<int>::defer(retry_factory{calls});

        FT_REQUIRE_THROWS(std::runtime_error, cell.get());
        FT_REQUIRE(!cell.is_forced());

        const auto value = cell.get();
        FT_REQUIRE_EQUAL(*value, 42);
        FT_REQUIRE_EQUAL(calls->load(), 2);
        FT_REQUIRE(cell.is_forced());
    });

    tests.add("lazy cell copies share the same publication cell", [] {
        auto calls = std::make_shared<std::atomic<int>>(0);
        auto first = detail::lazy_cell<int>::defer([calls] {
            calls->fetch_add(1);
            return 99;
        });
        auto second = first;

        const auto first_value = first.get();
        const auto second_value = second.get();

        FT_REQUIRE_EQUAL(*first_value, 99);
        FT_REQUIRE(first_value == second_value);
        FT_REQUIRE_EQUAL(calls->load(), 1);
    });

    tests.add("lazy cell releases captured pending state after publication", [] {
        std::weak_ptr<int> weak_source;
        auto cell = [&weak_source] {
            auto source = std::make_shared<int>(41);
            weak_source = source;
            return detail::lazy_cell<int>::defer([source] {
                return *source + 1;
            });
        }();

        FT_REQUIRE(!weak_source.expired());
        const auto value = cell.get();
        FT_REQUIRE_EQUAL(*value, 42);
        FT_REQUIRE(weak_source.expired());
    });

    tests.add("lazy cell concurrent first forces converge on one published value", [] {
        constexpr auto worker_count = 16;
        auto calls = std::make_shared<std::atomic<int>>(0);
        auto ready = std::make_shared<std::atomic<int>>(0);
        auto start = std::make_shared<std::atomic<bool>>(false);
        auto cell = detail::lazy_cell<int>::defer([calls] {
            calls->fetch_add(1);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            return 1234;
        });

        std::vector<std::thread> threads;
        std::vector<std::shared_ptr<const int>> results(worker_count);
        threads.reserve(worker_count);
        for (auto index = 0; index < worker_count; ++index) {
            threads.emplace_back([&, index] {
                ready->fetch_add(1);
                while (!start->load()) {
                    std::this_thread::yield();
                }

                results[index] = cell.get();
            });
        }

        while (ready->load() != worker_count) {
            std::this_thread::yield();
        }
        start->store(true);

        for (auto& thread : threads) {
            thread.join();
        }

        for (const auto& result : results) {
            FT_REQUIRE(result != nullptr);
            FT_REQUIRE_EQUAL(*result, 1234);
        }

        const auto calls_after_first_wave = calls->load();
        FT_REQUIRE(calls_after_first_wave >= 1);
        FT_REQUIRE(calls_after_first_wave <= worker_count);

        const auto published = cell.get();
        FT_REQUIRE_EQUAL(*published, 1234);
        FT_REQUIRE_EQUAL(calls->load(), calls_after_first_wave);
    });
}

} // namespace

void add_lazy_cell_tests(suite& tests)
{
    add_lazy_cell_tests_impl(tests);
}
