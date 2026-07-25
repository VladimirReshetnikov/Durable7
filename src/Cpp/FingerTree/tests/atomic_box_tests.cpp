#include <durable7/finger_tree/detail/atomic_box.hpp>

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

struct wide_value final {
    long long a;
    long long b;
    long long c;
    long long d;

    [[nodiscard]] bool is_intact() const noexcept
    {
        return a == b && b == c && c == d;
    }

    [[nodiscard]] bool operator==(const wide_value&) const = default;
};

void add_atomic_box_tests_impl(suite& tests)
{
    tests.add("atomic box starts empty and computes once in quiescent use", [] {
        detail::atomic_box<int> box;
        auto calls = std::make_shared<std::atomic<int>>(0);

        FT_REQUIRE(!box.has_value());
        const auto first = box.get_or_compute([calls] {
            calls->fetch_add(1);
            return 42;
        });
        const auto second = box.get_or_compute([calls] {
            calls->fetch_add(1);
            return 13;
        });

        FT_REQUIRE_EQUAL(*first, 42);
        FT_REQUIRE(first == second);
        FT_REQUIRE_EQUAL(calls->load(), 1);
        FT_REQUIRE(box.has_value());
    });

    tests.add("atomic box accepts prebuilt shared pointer values", [] {
        detail::atomic_box<int> box;
        auto value = std::make_shared<const int>(7);
        auto published = box.get_or_compute([value] {
            return value;
        });

        FT_REQUIRE(published == value);
        FT_REQUIRE(box.get() == value);
    });

    tests.add("atomic box rejects null factory pointers", [] {
        detail::atomic_box<int> box;
        FT_REQUIRE_THROWS(std::logic_error, box.get_or_compute([] {
            return std::shared_ptr<const int>{};
        }));
        FT_REQUIRE(!box.has_value());
    });

    tests.add("atomic box retries after factory exceptions", [] {
        detail::atomic_box<int> box;
        auto calls = std::make_shared<std::atomic<int>>(0);

        FT_REQUIRE_THROWS(std::runtime_error, box.get_or_compute([calls]() -> int {
            calls->fetch_add(1);
            throw std::runtime_error{"first measure computation failed"};
        }));

        FT_REQUIRE(!box.has_value());
        const auto value = box.get_or_compute([calls] {
            calls->fetch_add(1);
            return 99;
        });

        FT_REQUIRE_EQUAL(*value, 99);
        FT_REQUIRE_EQUAL(calls->load(), 2);
    });

    tests.add("atomic box concurrently publishes intact wide values", [] {
        constexpr auto worker_count = 16;
        detail::atomic_box<wide_value> box;
        auto calls = std::make_shared<std::atomic<int>>(0);
        auto ready = std::make_shared<std::atomic<int>>(0);
        auto start = std::make_shared<std::atomic<bool>>(false);

        std::vector<std::thread> threads;
        std::vector<std::shared_ptr<const wide_value>> results(worker_count);
        threads.reserve(worker_count);

        for (auto index = 0; index < worker_count; ++index) {
            threads.emplace_back([&, index] {
                ready->fetch_add(1);
                while (!start->load()) {
                    std::this_thread::yield();
                }

                results[index] = box.get_or_compute([calls] {
                    calls->fetch_add(1);
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    return wide_value{0x123456789abcdefLL, 0x123456789abcdefLL, 0x123456789abcdefLL, 0x123456789abcdefLL};
                });
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
            constexpr auto expected =
                wide_value{0x123456789abcdefLL, 0x123456789abcdefLL, 0x123456789abcdefLL, 0x123456789abcdefLL};
            FT_REQUIRE(result != nullptr);
            FT_REQUIRE(result->is_intact());
            FT_REQUIRE(*result == expected);
        }

        const auto calls_after_first_wave = calls->load();
        FT_REQUIRE(calls_after_first_wave >= 1);
        FT_REQUIRE(calls_after_first_wave <= worker_count);

        const auto published = box.get_or_compute([] {
            return wide_value{1, 1, 1, 1};
        });
        FT_REQUIRE(published->is_intact());
        FT_REQUIRE_EQUAL(calls->load(), calls_after_first_wave);
    });
}

} // namespace

void add_atomic_box_tests(suite& tests)
{
    add_atomic_box_tests_impl(tests);
}
