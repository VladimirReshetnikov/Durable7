#include <durable7/finger_tree/finger_tree.hpp>

#include "test_support/test_runner.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <memory>
#include <random>
#include <thread>
#include <vector>

namespace ft = durable7::finger_tree;
using namespace durable7::finger_tree::tests;

namespace {

struct tearable final {
    long long a = 0;
    long long b = 0;
    long long c = 0;
    long long d = 0;

    explicit constexpr tearable(const long long value = 0) noexcept
        : a(value)
        , b(value)
        , c(value)
        , d(value)
    {
    }

    [[nodiscard]] constexpr long long value() const noexcept
    {
        return a;
    }

    [[nodiscard]] constexpr bool is_intact() const noexcept
    {
        return a == b && b == c && c == d;
    }

    [[nodiscard]] bool operator==(const tearable&) const = default;
};

struct tearable_sum_measure final {
    using element_type = tearable;
    using measure_type = tearable;

    [[nodiscard]] static constexpr measure_type empty() noexcept
    {
        return tearable{};
    }

    [[nodiscard]] static constexpr measure_type measure(const element_type& value) noexcept
    {
        return value;
    }

    [[nodiscard]] static constexpr measure_type combine(const measure_type& left, const measure_type& right) noexcept
    {
        return tearable{left.value() + right.value()};
    }
};

[[nodiscard]] std::chrono::steady_clock::time_point stress_deadline()
{
    auto seconds = 0.25;
#ifdef _MSC_VER
    char* raw = nullptr;
    std::size_t raw_size = 0;
    if (_dupenv_s(&raw, &raw_size, "FINGERTREE_STRESS_SECONDS") == 0 && raw != nullptr) {
#else
    if (const char* raw = std::getenv("FINGERTREE_STRESS_SECONDS"); raw != nullptr) {
#endif
        char* end = nullptr;
        const auto parsed = std::strtod(raw, &end);
        if (end != raw && parsed > 0 && std::isfinite(parsed)) {
            seconds = parsed;
        }
#ifdef _MSC_VER
        std::free(raw);
#endif
    }

    return std::chrono::steady_clock::now()
        + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>{seconds});
}

template <class Worker>
void run_racing_workers(const int worker_count, Worker worker)
{
    auto ready = std::make_shared<std::atomic<int>>(0);
    auto start = std::make_shared<std::atomic<bool>>(false);
    auto failures = std::make_shared<std::atomic<int>>(0);
    auto threads = std::vector<std::thread>{};
    threads.reserve(static_cast<std::size_t>(worker_count));

    for (auto worker_id = 0; worker_id != worker_count; ++worker_id) {
        threads.emplace_back([&, worker_id] {
            ready->fetch_add(1);
            while (!start->load()) {
                std::this_thread::yield();
            }

            try {
                worker(worker_id, *failures);
            } catch (...) {
                failures->fetch_add(1);
            }
        });
    }

    while (ready->load() != worker_count) {
        std::this_thread::yield();
    }
    start->store(true);

    for (auto& thread : threads) {
        thread.join();
    }

    FT_REQUIRE_EQUAL(failures->load(), 0);
}

[[nodiscard]] std::vector<tearable> tearable_range(const int count, const long long first = 0)
{
    auto values = std::vector<tearable>{};
    values.reserve(static_cast<std::size_t>(count));
    for (auto value = 0; value != count; ++value) {
        values.emplace_back(first + value);
    }

    return values;
}

void validate_contiguous(const std::vector<tearable>& values, std::atomic<int>& failures)
{
    for (auto index = std::size_t{0}; index < values.size(); ++index) {
        if (!values[index].is_intact()) {
            failures.fetch_add(1);
            return;
        }

        if (index > 0 && values[index].value() != values[index - 1].value() + 1) {
            failures.fetch_add(1);
            return;
        }
    }
}

void add_tearable_concurrency_tests_impl(suite& tests)
{
    tests.add("tearable stress concurrent first reads of measured finger tree stay intact", [] {
        constexpr auto item_count = 4096;
        constexpr auto worker_count = 16;
        const auto values = tearable_range(item_count);
        const auto expected_sum = static_cast<long long>(item_count) * (item_count - 1) / 2;
        const auto tree = ft::finger_tree<tearable, tearable_sum_measure>::from_range(values);

        run_racing_workers(worker_count, [&](const int, std::atomic<int>& failures) {
            const auto measure = tree.measure();
            if (!measure.is_intact() || measure.value() != expected_sum) {
                failures.fetch_add(1);
            }

            const auto front = tree.front();
            const auto back = tree.back();
            if (!front.is_intact() || front.value() != 0 || !back.is_intact() || back.value() != item_count - 1) {
                failures.fetch_add(1);
            }

            const auto located = tree.try_locate([](const tearable& current) {
                return current.value() >= 2048;
            });
            if (!located.item.has_value() || !located.item->is_intact() || !located.measure_before.is_intact()) {
                failures.fetch_add(1);
            }
        });
    });

    tests.add("tearable stress concurrent first reads of measured rope stay intact", [] {
        constexpr auto item_count = 4096;
        constexpr auto worker_count = 16;
        const auto values = tearable_range(item_count);
        const auto expected_sum = static_cast<long long>(item_count) * (item_count - 1) / 2;
        const auto rope = ft::measured_rope<tearable, tearable_sum_measure>::from_range(values);
        const auto cursor = rope.get_cursor(2048);
        const auto expected_before = 2048LL * 2047 / 2;
        const auto expected_through_next = 2048LL * 2049 / 2;

        run_racing_workers(worker_count, [&](const int, std::atomic<int>& failures) {
            const auto measure = rope.measure();
            if (!measure.is_intact() || measure.value() != expected_sum) {
                failures.fetch_add(1);
            }

            const auto materialized = rope.to_vector();
            if (materialized.size() != static_cast<std::size_t>(item_count)) {
                failures.fetch_add(1);
                return;
            }

            for (auto index = std::size_t{0}; index < materialized.size(); ++index) {
                if (!materialized[index].is_intact() || materialized[index].value() != static_cast<long long>(index)) {
                    failures.fetch_add(1);
                    return;
                }
            }

            const auto before = cursor.measure_before();
            const auto after = cursor.measure_after();
            const auto* next = cursor.try_peek_next();
            if (!before.is_intact() || before.value() != expected_before
                || !after.is_intact() || after.value() != expected_sum - expected_before
                || next == nullptr || !next->is_intact() || next->value() != 2048) {
                failures.fetch_add(1);
                return;
            }

            const auto located = cursor.seek_by_measure([](const tearable& sum) {
                return sum.value() >= expected_through_next;
            });
            if (!located.found || located.cursor.position() != 2048
                || located.cursor.try_peek_next() == nullptr
                || !located.cursor.try_peek_next()->is_intact()) {
                failures.fetch_add(1);
            }
        });
    });

    tests.add("tearable stress atomic rope publication snapshots stay contiguous", [] {
        auto cell = std::atomic<std::shared_ptr<const ft::rope<tearable>>>{};
        cell.store(std::make_shared<const ft::rope<tearable>>());
        auto stop = std::atomic<bool>{false};
        auto failures = std::atomic<int>{0};
        const auto deadline = stress_deadline();

        auto writer = std::thread{[&] {
            auto current = ft::rope<tearable>{};
            auto next = 0LL;
            while (std::chrono::steady_clock::now() < deadline) {
                current = current.push_back(tearable{next++});
                if (current.size() > 512) {
                    current = current.remove_first();
                }

                cell.store(std::make_shared<const ft::rope<tearable>>(current));
            }

            stop.store(true);
        }};

        auto readers = std::vector<std::thread>{};
        for (auto index = 0; index != 4; ++index) {
            readers.emplace_back([&] {
                while (!stop.load()) {
                    const auto snapshot = cell.load();
                    validate_contiguous(snapshot->to_vector(), failures);
                }
            });
        }

        writer.join();
        for (auto& reader : readers) {
            reader.join();
        }

        FT_REQUIRE_EQUAL(failures.load(), 0);
    });

    tests.add("tearable stress branching histories off retained rope stay consistent", [] {
        constexpr auto base_size = 256;
        const auto base_values = tearable_range(base_size);
        const auto base_rope = ft::rope<tearable>::from_range(base_values);
        const auto deadline = stress_deadline();
        const auto replay_seed = effective_replay_seed(17);
        capture_replay_seed(replay_seed);

        run_racing_workers(8, [&, replay_seed](const int worker_id, std::atomic<int>& failures) {
            auto rng = std::mt19937_64{
                static_cast<std::mt19937_64::result_type>(worker_id * 7919) + replay_seed};
            auto next = static_cast<long long>(worker_id + 1) * 1'000'000'000LL;

            while (std::chrono::steady_clock::now() < deadline) {
                for (auto probe = 0; probe != 8; ++probe) {
                    const auto index = static_cast<std::size_t>(rng() % base_size);
                    const auto value = base_rope[index];
                    if (!value.is_intact() || value.value() != static_cast<long long>(index)) {
                        failures.fetch_add(1);
                    }
                }

                auto rope = base_rope;
                auto model = base_values;
                const auto operation_count = 3 + static_cast<int>(rng() % 8);
                for (auto step = 0; step != operation_count; ++step) {
                    const auto size = model.size();
                    switch (rng() % 8) {
                    case 0:
                        rope = rope.push_front(tearable{next});
                        model.insert(model.begin(), tearable{next++});
                        break;
                    case 1:
                        rope = rope.push_back(tearable{next});
                        model.push_back(tearable{next++});
                        break;
                    case 2:
                        if (!model.empty()) {
                            rope = rope.remove_first();
                            model.erase(model.begin());
                        }
                        break;
                    case 3:
                        if (!model.empty()) {
                            rope = rope.remove_last();
                            model.pop_back();
                        }
                        break;
                    case 4: {
                        const auto insert_index = static_cast<std::size_t>(rng() % (size + 1));
                        rope = rope.insert_at(insert_index, tearable{next});
                        model.insert(model.begin() + static_cast<std::ptrdiff_t>(insert_index), tearable{next++});
                        break;
                    }
                    case 5:
                        if (!model.empty()) {
                            const auto remove_index = static_cast<std::size_t>(rng() % size);
                            rope = rope.remove_at(remove_index);
                            model.erase(model.begin() + static_cast<std::ptrdiff_t>(remove_index));
                        }
                        break;
                    case 6:
                        if (!model.empty()) {
                            const auto set_index = static_cast<std::size_t>(rng() % size);
                            rope = rope.set_item(set_index, tearable{next});
                            model[set_index] = tearable{next++};
                        }
                        break;
                    default: {
                        const auto split_index = static_cast<std::size_t>(rng() % (size + 1));
                        const auto split = rope.split_at(split_index);
                        rope = split.left.concat(split.right);
                        break;
                    }
                    }
                }

                rope.validate_invariants();
                if (rope.to_vector() != model) {
                    failures.fetch_add(1);
                }
            }
        });

        FT_REQUIRE(base_rope.to_vector() == base_values);
    });
}

} // namespace

void add_tearable_concurrency_tests(suite& tests)
{
    add_tearable_concurrency_tests_impl(tests);
}
