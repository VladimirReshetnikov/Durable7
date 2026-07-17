#include <tools/data_structures/ordered/ordered.hpp>
#include <tools/data_structures/test_support/headless_test_process.h>

#include "../../FingerTree/tests/test_support/test_runner.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <ranges>
#include <source_location>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace ordered = tools::data_structures::ordered;
using namespace tools::data_structures::finger_tree::tests;

void add_persistent_ordered_map_tests(suite& tests);
void add_persistent_ordered_multimap_tests(suite& tests);

namespace {

template <class T>
std::string describe_vector(const std::vector<T>& values)
{
    auto stream = std::ostringstream{};
    stream << '[';
    for (auto index = std::size_t{0}; index != values.size(); ++index) {
        if (index != 0) {
            stream << ", ";
        }
        stream << values[index];
    }
    stream << ']';
    return stream.str();
}

template <class T>
void require_vector_equal(
    const std::vector<T>& actual,
    const std::vector<T>& expected,
    const std::source_location location = std::source_location::current())
{
    if (actual == expected) {
        return;
    }

    auto message = std::ostringstream{};
    message << location.file_name() << ':' << location.line()
            << ": vector mismatch. Actual " << describe_vector(actual)
            << ", expected " << describe_vector(expected);
    throw test_failure(message.str());
}

template <class Set>
void require_set_matches(
    const Set& actual,
    const std::vector<typename Set::value_type>& expected,
    const std::source_location location = std::source_location::current())
{
    require_vector_equal(actual.to_vector(), expected, location);
    FT_REQUIRE_EQUAL(actual.size(), expected.size());
    FT_REQUIRE(actual.debug_validate());
    for (auto index = std::size_t{0}; index != expected.size(); ++index) {
        FT_REQUIRE_EQUAL(actual.at(index), expected[index]);
        FT_REQUIRE_EQUAL(actual.index_of(expected[index]), static_cast<std::ptrdiff_t>(index));
        FT_REQUIRE(actual.contains(expected[index]));
    }
}

struct case_insensitive_hash final {
    [[nodiscard]] std::size_t operator()(const std::string& value) const noexcept
    {
        auto hash = std::size_t{1469598103934665603ULL};
        for (const auto character : value) {
            hash ^= static_cast<unsigned char>(
                std::tolower(static_cast<unsigned char>(character)));
            hash *= std::size_t{1099511628211ULL};
        }
        return hash;
    }
};

struct case_insensitive_equal final {
    [[nodiscard]] bool operator()(
        const std::string& left,
        const std::string& right) const noexcept
    {
        if (left.size() != right.size()) {
            return false;
        }
        for (auto index = std::size_t{0}; index != left.size(); ++index) {
            if (std::tolower(static_cast<unsigned char>(left[index]))
                != std::tolower(static_cast<unsigned char>(right[index]))) {
                return false;
            }
        }
        return true;
    }
};

struct controlled_hash final {
    std::shared_ptr<int> throw_value;

    [[nodiscard]] std::size_t operator()(const int value) const
    {
        if (*throw_value == value) {
            throw std::runtime_error("injected hash failure");
        }
        return std::hash<int>{}(value);
    }
};

struct staged_failure_hash final {
    std::shared_ptr<int> successful_calls_before_throw;

    [[nodiscard]] std::size_t operator()(const int value) const
    {
        if (*successful_calls_before_throw == 0) {
            throw std::runtime_error("injected relabel rebuild failure");
        }
        if (*successful_calls_before_throw > 0) {
            --*successful_calls_before_throw;
        }
        return std::hash<int>{}(value);
    }
};

struct construct_only_value final {
    int value = 0;

    construct_only_value() = default;
    construct_only_value(const construct_only_value&) = default;
    construct_only_value& operator=(const construct_only_value&) = delete;
};

struct construct_only_value_hash final {
    [[nodiscard]] std::size_t operator()(const construct_only_value& value) const noexcept
    {
        return std::hash<int>{}(value.value);
    }
};

struct construct_only_value_equal final {
    [[nodiscard]] bool operator()(
        const construct_only_value& left,
        const construct_only_value& right) const noexcept
    {
        return left.value == right.value;
    }
};

struct construct_only_hash final {
    construct_only_hash() = default;
    construct_only_hash(const construct_only_hash&) = default;
    construct_only_hash& operator=(const construct_only_hash&) = delete;

    [[nodiscard]] std::size_t operator()(const int value) const noexcept
    {
        return std::hash<int>{}(value);
    }
};

struct construct_only_equal final {
    construct_only_equal() = default;
    construct_only_equal(const construct_only_equal&) = default;
    construct_only_equal& operator=(const construct_only_equal&) = delete;

    [[nodiscard]] bool operator()(const int left, const int right) const noexcept
    {
        return left == right;
    }
};

template <class T, class Hash, class KeyEqual>
concept ordered_set_type_available = requires {
    typename ordered::persistent_ordered_set<T, Hash, KeyEqual>;
};

static_assert(ordered_set_type_available<int, std::hash<int>, std::equal_to<int>>);
static_assert(!ordered_set_type_available<
    construct_only_value,
    construct_only_value_hash,
    construct_only_value_equal>);
static_assert(!ordered_set_type_available<
    int,
    construct_only_hash,
    std::equal_to<int>>);
static_assert(!ordered_set_type_available<
    int,
    std::hash<int>,
    construct_only_equal>);

struct constant_int_hash final {
    [[nodiscard]] std::size_t operator()(const int) const noexcept
    {
        return 0x5A;
    }
};

class deterministic_rng final {
public:
    explicit deterministic_rng(const std::uint64_t seed)
        : state_(seed)
    {
    }

    [[nodiscard]] std::uint32_t next()
    {
        state_ = state_ * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<std::uint32_t>(state_ >> 32);
    }

    [[nodiscard]] std::size_t next_index(const std::size_t exclusive)
    {
        return exclusive == 0 ? 0 : static_cast<std::size_t>(next()) % exclusive;
    }

    [[nodiscard]] int next_int(const int low, const int high)
    {
        return low + static_cast<int>(
            next_index(static_cast<std::size_t>(high - low + 1)));
    }

private:
    std::uint64_t state_;
};

[[nodiscard]] std::ptrdiff_t find_value(const std::vector<int>& values, const int value)
{
    const auto found = std::ranges::find(values, value);
    return found == values.end()
        ? std::ptrdiff_t{-1}
        : static_cast<std::ptrdiff_t>(found - values.begin());
}

void add_construction_and_lookup_tests(suite& tests)
{
    tests.add("construction retains first representative and first position", [] {
        using set_type = ordered::persistent_ordered_set<
            std::string,
            case_insensitive_hash,
            case_insensitive_equal>;
        const auto source = std::vector<std::string>{
            "Alpha", "beta", "ALPHA", "Gamma", "BETA"};
        const auto set = set_type::create_range(
            source, case_insensitive_hash{}, case_insensitive_equal{});

        require_set_matches(
            set,
            std::vector<std::string>{"Alpha", "beta", "Gamma"});
        FT_REQUIRE(set.contains("alpha"));
        const auto* actual = set.try_get_value("ALPHA");
        FT_REQUIRE(actual != nullptr);
        FT_REQUIRE_EQUAL(*actual, std::string{"Alpha"});
        FT_REQUIRE(set.try_get_value("missing") == nullptr);
        FT_REQUIRE_EQUAL(set.front(), std::string{"Alpha"});
        FT_REQUIRE_EQUAL(set.back(), std::string{"Gamma"});
    });

    tests.add("empty and invalid positional operations report native errors", [] {
        const auto empty = ordered::persistent_ordered_set<int>::empty_set();
        FT_REQUIRE(empty.empty());
        FT_REQUIRE_EQUAL(empty.index_of(1), std::ptrdiff_t{-1});
        FT_REQUIRE_THROWS(std::logic_error, empty.front());
        FT_REQUIRE_THROWS(std::logic_error, empty.back());
        FT_REQUIRE_THROWS(std::logic_error, empty.remove_first());
        FT_REQUIRE_THROWS(std::logic_error, empty.remove_last());
        FT_REQUIRE_THROWS(std::out_of_range, empty[0]);
        FT_REQUIRE_THROWS(std::out_of_range, empty.at(0));
        FT_REQUIRE_THROWS(std::out_of_range, empty.remove_at(0));
        FT_REQUIRE_THROWS(std::out_of_range, empty.move_to(0, 1));
        FT_REQUIRE_THROWS(std::out_of_range, empty.move_to_first(1));

        const auto set = ordered::persistent_ordered_set<int>::create_range({1, 2, 3});
        FT_REQUIRE_THROWS(std::out_of_range, set.insert(4, 4));
        FT_REQUIRE_THROWS(std::out_of_range, set.get_range(2, 2));
        FT_REQUIRE_THROWS(std::out_of_range, set.take(4));
        FT_REQUIRE_THROWS(std::out_of_range, set.drop(4));
    });

    tests.add("iteration projects representatives in order", [] {
        const auto set = ordered::persistent_ordered_set<int>::create_range({4, 1, 3});
        auto enumerated = std::vector<int>{};
        for (const auto value : set) {
            enumerated.push_back(value);
        }
        require_vector_equal(enumerated, std::vector<int>{4, 1, 3});
        FT_REQUIRE(std::ranges::forward_range<decltype(set)>);
    });

    tests.add("iterator owns its snapshot after facade destruction", [] {
        using set_type = ordered::persistent_ordered_set<int>;
        auto iterator = [] {
            const auto local = set_type::create_range({4, 1, 3});
            return local.begin();
        }();

        auto values = std::vector<int>{};
        const auto end = set_type::const_iterator{};
        while (iterator != end) {
            values.push_back(*iterator);
            ++iterator;
        }
        require_vector_equal(values, std::vector<int>{4, 1, 3});
    });

    tests.add("equal-hash collision classes retain order through edits", [] {
        using set_type = ordered::persistent_ordered_set<int, constant_int_hash>;
        auto set = set_type::create(constant_int_hash{});
        for (auto value = 0; value != 64; ++value) {
            set = set.add(value);
        }
        for (auto value = 0; value < 64; value += 3) {
            set = set.remove(value);
        }

        auto expected = std::vector<int>{};
        for (auto value = 0; value != 64; ++value) {
            if (value % 3 != 0) {
                expected.push_back(value);
            }
        }
        require_set_matches(set, expected);
    });
}

void add_edit_and_order_tests(suite& tests)
{
    tests.add("addition is distinct from explicit movement", [] {
        using set_type = ordered::persistent_ordered_set<
            std::string,
            case_insensitive_hash,
            case_insensitive_equal>;
        const auto source = set_type::create_range(
            std::vector<std::string>{"Alpha", "Beta", "Gamma"},
            case_insensitive_hash{},
            case_insensitive_equal{});

        const auto duplicate = source.add("ALPHA");
        FT_REQUIRE(duplicate.shares_index_with(source));
        require_set_matches(
            duplicate,
            std::vector<std::string>{"Alpha", "Beta", "Gamma"});

        require_set_matches(
            source.add_first("Delta"),
            std::vector<std::string>{"Delta", "Alpha", "Beta", "Gamma"});
        require_set_matches(
            source.insert(2, "Delta"),
            std::vector<std::string>{"Alpha", "Beta", "Delta", "Gamma"});
        require_set_matches(
            source.move_to_last("ALPHA"),
            std::vector<std::string>{"Beta", "Gamma", "Alpha"});
        require_set_matches(
            source.move_to_first("gamma"),
            std::vector<std::string>{"Gamma", "Alpha", "Beta"});
        require_set_matches(
            source.move_to(2, "alpha"),
            std::vector<std::string>{"Beta", "Gamma", "Alpha"});
        FT_REQUIRE(source.move_to(1, "BETA").shares_index_with(source));
        FT_REQUIRE_THROWS(std::out_of_range, source.move_to_first("missing"));
    });

    tests.add("remove variants clear and prior versions stay independent", [] {
        const auto source = ordered::persistent_ordered_set<int>::create_range({1, 2, 3, 4});
        const auto removed = source.remove(2);
        const auto [tried, did_remove] = removed.try_remove(3);
        const auto [same, missing] = tried.try_remove(99);

        require_set_matches(source, std::vector<int>{1, 2, 3, 4});
        require_set_matches(removed, std::vector<int>{1, 3, 4});
        FT_REQUIRE(did_remove);
        require_set_matches(tried, std::vector<int>{1, 4});
        FT_REQUIRE(!missing);
        FT_REQUIRE(same.shares_index_with(tried));
        require_set_matches(source.remove_at(1), std::vector<int>{1, 3, 4});
        require_set_matches(source.remove_first(), std::vector<int>{2, 3, 4});
        require_set_matches(source.remove_last(), std::vector<int>{1, 2, 3});
        FT_REQUIRE(source.clear().empty());
        FT_REQUIRE(source.clear().clear().empty());
    });

    tests.add("range reverse and stable sort preserve ordered-set semantics", [] {
        const auto source = ordered::persistent_ordered_set<std::string>::create_range(
            {"ccc", "a", "bb", "d", "ee"});

        require_set_matches(
            source.get_range(1, 3),
            std::vector<std::string>{"a", "bb", "d"});
        require_set_matches(
            source.take(2),
            std::vector<std::string>{"ccc", "a"});
        require_set_matches(
            source.drop(3),
            std::vector<std::string>{"d", "ee"});
        require_set_matches(
            source.reverse(),
            std::vector<std::string>{"ee", "d", "bb", "a", "ccc"});

        const auto by_length = source.sort([](const std::string& left, const std::string& right) {
            return left.size() < right.size();
        });
        require_set_matches(
            by_length,
            std::vector<std::string>{"a", "d", "bb", "ee", "ccc"});
        require_set_matches(
            by_length.add("ffff"),
            std::vector<std::string>{"a", "d", "bb", "ee", "ccc", "ffff"});

        const auto already = by_length.sort([](const std::string& left, const std::string& right) {
            return left.size() < right.size();
        });
        FT_REQUIRE(already.shares_index_with(by_length));
    });

    tests.add("repeated middle insertion exhausts gaps and relabels deterministically", [] {
        auto set = ordered::persistent_ordered_set<std::string>::create_range({"start", "end"});
        for (auto index = 0; index != 100; ++index) {
            set = set.insert(1, "k" + std::to_string(index));
        }

        FT_REQUIRE_EQUAL(set.size(), std::size_t{102});
        FT_REQUIRE_EQUAL(set.front(), std::string{"start"});
        FT_REQUIRE_EQUAL(set.back(), std::string{"end"});
        for (auto index = 0; index != 100; ++index) {
            const auto item = "k" + std::to_string(index);
            FT_REQUIRE_EQUAL(
                set.index_of(item),
                static_cast<std::ptrdiff_t>(1 + (99 - index)));
        }
        set.validate_invariants();
    });
}

void add_algebra_and_relation_tests(suite& tests)
{
    tests.add("algebra eagerly normalizes the argument under receiver policy", [] {
        using receiver_type = ordered::persistent_ordered_set<
            std::string,
            case_insensitive_hash,
            case_insensitive_equal>;
        const auto receiver = receiver_type::create_range(
            std::vector<std::string>{"Alpha", "Gamma"},
            case_insensitive_hash{},
            case_insensitive_equal{});
        const auto argument = ordered::persistent_ordered_set<std::string>::create_range(
            {"ALPHA", "Beta", "beta"});

        require_set_matches(
            receiver.union_with(argument),
            std::vector<std::string>{"Alpha", "Gamma", "Beta"});
        require_set_matches(
            receiver.intersect_with(argument),
            std::vector<std::string>{"Alpha"});
        require_set_matches(
            receiver.except_with(argument),
            std::vector<std::string>{"Gamma"});
        require_set_matches(
            receiver.symmetric_except_with(argument),
            std::vector<std::string>{"Gamma", "Beta"});

        FT_REQUIRE(receiver.union_with(std::vector<std::string>{"ALPHA"})
            .shares_index_with(receiver));
        FT_REQUIRE(receiver.intersect_with(
            std::vector<std::string>{"alpha", "gamma"})
            .shares_index_with(receiver));
        FT_REQUIRE(receiver.except_with(std::vector<std::string>{"missing"})
            .shares_index_with(receiver));
        FT_REQUIRE(receiver.symmetric_except_with(std::vector<std::string>{})
            .shares_index_with(receiver));
    });

    tests.add("relations count receiver-policy classes rather than duplicates", [] {
        const auto set = ordered::persistent_ordered_set<int>::create_range({1, 2, 3});
        FT_REQUIRE(set.is_subset_of(std::vector<int>{3, 2, 1, 1}));
        FT_REQUIRE(set.is_proper_subset_of(std::vector<int>{1, 2, 3, 4, 4}));
        FT_REQUIRE(set.is_superset_of(std::vector<int>{1, 1, 3}));
        FT_REQUIRE(set.is_proper_superset_of(std::vector<int>{1, 1}));
        FT_REQUIRE(set.overlaps(std::vector<int>{9, 2, 9}));
        FT_REQUIRE(set.set_equals(std::vector<int>{3, 2, 1, 2}));
        FT_REQUIRE(!set.set_equals(std::vector<int>{1, 2}));
    });

    tests.add("normalization finishes before relation shortcuts", [] {
        const auto throw_value = std::make_shared<int>(-1);
        using set_type = ordered::persistent_ordered_set<int, controlled_hash>;
        const auto set = set_type::create(controlled_hash{throw_value}).add(1);

        *throw_value = 99;
        FT_REQUIRE_THROWS(
            std::runtime_error,
            set.overlaps(std::vector<int>{1, 99}));
        *throw_value = -1;
        require_set_matches(set, std::vector<int>{1});
    });
}

void add_failure_model_and_concurrency_tests(suite& tests)
{
    tests.add("relabel rebuild failures leave the source root unchanged", [] {
        const auto successful_calls_before_throw = std::make_shared<int>(-1);
        using set_type = ordered::persistent_ordered_set<int, staged_failure_hash>;
        auto source = set_type::create(staged_failure_hash{successful_calls_before_throw})
                          .add(-2)
                          .add(-1);
        for (auto value = 0; value != 20; ++value) {
            source = source.insert(1, value);
        }
        const auto snapshot = source;
        const auto expected = source.to_vector();

        // The provisional membership add consumes the one permitted hash.
        // The first existing item visited by the unpublished relabel rebuild
        // then throws, before either facade root can be published.
        *successful_calls_before_throw = 1;
        FT_REQUIRE_THROWS(std::runtime_error, source.insert(1, 1000));
        *successful_calls_before_throw = -1;

        FT_REQUIRE(source.shares_index_with(snapshot));
        require_set_matches(source, expected);
        require_set_matches(snapshot, expected);
    });

    tests.add("callback failures leave every published version unchanged", [] {
        const auto throw_value = std::make_shared<int>(-1);
        using set_type = ordered::persistent_ordered_set<int, controlled_hash>;
        const auto source = set_type::create(controlled_hash{throw_value})
                                .add(1)
                                .add(2)
                                .add(3);

        *throw_value = 99;
        FT_REQUIRE_THROWS(std::runtime_error, source.add(99));
        *throw_value = -1;
        FT_REQUIRE_THROWS(
            std::runtime_error,
            source.sort([](const int, const int) -> bool {
                throw std::runtime_error("injected ordering failure");
            }));
        require_set_matches(source, std::vector<int>{1, 2, 3});
    });

    tests.add("generated histories match an ordered unique-list model", [] {
        auto rng = deterministic_rng{0x0D3E3D5E7ULL};
        auto set = ordered::persistent_ordered_set<int>{};
        auto model = std::vector<int>{};
        auto snapshots = std::vector<
            std::pair<ordered::persistent_ordered_set<int>, std::vector<int>>>{};

        for (auto step = 0; step != 900; ++step) {
            const auto value = rng.next_int(-20, 20);
            switch (rng.next_index(11)) {
            case 0:
                set = set.add(value);
                if (find_value(model, value) < 0) {
                    model.push_back(value);
                }
                break;
            case 1:
                set = set.add_first(value);
                if (find_value(model, value) < 0) {
                    model.insert(model.begin(), value);
                }
                break;
            case 2: {
                const auto position = rng.next_index(model.size() + 1);
                set = set.insert(position, value);
                if (find_value(model, value) < 0) {
                    model.insert(model.begin() + static_cast<std::ptrdiff_t>(position), value);
                }
                break;
            }
            case 3:
                set = set.remove(value);
                if (const auto found = find_value(model, value); found >= 0) {
                    model.erase(model.begin() + found);
                }
                break;
            case 4:
                if (!model.empty()) {
                    const auto old_position = rng.next_index(model.size());
                    const auto final_position = rng.next_index(model.size());
                    const auto moved = model[old_position];
                    set = set.move_to(final_position, moved);
                    model.erase(model.begin() + static_cast<std::ptrdiff_t>(old_position));
                    model.insert(
                        model.begin() + static_cast<std::ptrdiff_t>(final_position), moved);
                }
                break;
            case 5:
                if (!model.empty()) {
                    const auto position = rng.next_index(model.size());
                    set = set.remove_at(position);
                    model.erase(model.begin() + static_cast<std::ptrdiff_t>(position));
                }
                break;
            case 6: {
                const auto index = rng.next_index(model.size() + 1);
                const auto count = rng.next_index(model.size() - index + 1);
                set = set.get_range(index, count);
                model = std::vector<int>{
                    model.begin() + static_cast<std::ptrdiff_t>(index),
                    model.begin() + static_cast<std::ptrdiff_t>(index + count)};
                break;
            }
            case 7:
                set = set.reverse();
                std::ranges::reverse(model);
                break;
            case 8:
                set = set.union_with(std::vector<int>{value, value + 1, value});
                if (find_value(model, value) < 0) {
                    model.push_back(value);
                }
                if (find_value(model, value + 1) < 0) {
                    model.push_back(value + 1);
                }
                break;
            case 9:
                set = set.sort();
                std::ranges::sort(model);
                break;
            default:
                snapshots.push_back({set, model});
                if (snapshots.size() > 32) {
                    snapshots.erase(snapshots.begin());
                }
                break;
            }

            require_set_matches(set, model);
        }

        for (const auto& [snapshot, expected] : snapshots) {
            require_set_matches(snapshot, expected);
        }
    });

    tests.add("published snapshots tolerate concurrent readers", [] {
        auto expected = std::vector<int>{};
        expected.reserve(256);
        for (auto value = 0; value != 256; ++value) {
            expected.push_back(value);
        }
        const auto set = ordered::persistent_ordered_set<int>::create_range(expected);

        auto failures = std::atomic<int>{0};
        auto readers = std::vector<std::thread>{};
        readers.reserve(4);
        for (auto worker = 0; worker != 4; ++worker) {
            readers.emplace_back([&] {
                try {
                    for (auto pass = 0; pass != 64; ++pass) {
                        require_vector_equal(set.to_vector(), expected);
                        FT_REQUIRE_EQUAL(set.front(), 0);
                        FT_REQUIRE_EQUAL(set.back(), 255);
                        FT_REQUIRE_EQUAL(set.at(127), 127);
                    }
                } catch (...) {
                    failures.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }
        for (auto& reader : readers) {
            reader.join();
        }
        FT_REQUIRE_EQUAL(failures.load(std::memory_order_relaxed), 0);
    });
}

} // namespace

int main(const int argument_count, const char* const* arguments)
{
    if (!tds_enter_headless_test_process()) {
        return EXIT_FAILURE;
    }

    suite tests;
    tests.set_group("metadata");
    tests.add("aggregate header exposes version metadata", [] {
        FT_REQUIRE_EQUAL(
            ordered::library_name,
            std::string_view{"Tools.DataStructures.Ordered.Cpp"});
        FT_REQUIRE_EQUAL(ordered::version_major, 0U);
        FT_REQUIRE_EQUAL(ordered::version_minor, 1U);
        FT_REQUIRE_EQUAL(ordered::version_patch, 0U);
    });

    tests.set_group("construction-and-lookup");
    add_construction_and_lookup_tests(tests);
    tests.set_group("ordered-map");
    add_persistent_ordered_map_tests(tests);
    tests.set_group("ordered-multimap");
    add_persistent_ordered_multimap_tests(tests);
    tests.set_group("edits-and-order");
    add_edit_and_order_tests(tests);
    tests.set_group("algebra-and-relations");
    add_algebra_and_relation_tests(tests);
    tests.set_group("failure-model-and-concurrency");
    add_failure_model_and_concurrency_tests(tests);
    return tests.run(argument_count, arguments);
}
