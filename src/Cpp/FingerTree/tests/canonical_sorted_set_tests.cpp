#include <durable7/finger_tree/finger_tree.hpp>

#include "test_support/command_model.hpp"
#include "test_support/allocation_counter.hpp"
#include "test_support/test_runner.hpp"

#include <algorithm>
#include <array>
#include <barrier>
#include <cctype>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace ft = durable7::finger_tree;
using namespace durable7::finger_tree::tests;

namespace {

static_assert(std::forward_iterator<ft::canonical_sorted_set<int>::const_iterator>);
static_assert(std::ranges::forward_range<const ft::canonical_sorted_set<int>>);

[[nodiscard]] std::vector<int> values_of(const ft::canonical_sorted_set<int>& set)
{
    return {set.begin(), set.end()};
}

void require_valid(const ft::canonical_sorted_set<int>& set)
{
    const auto statistics = set.validate_structure();
    FT_REQUIRE_EQUAL(statistics.count, set.size());
    FT_REQUIRE_EQUAL(statistics.height, set.height());
    FT_REQUIRE(std::ranges::is_sorted(set));
}

[[nodiscard]] std::vector<std::byte> bytes_from_hex(const std::string_view text)
{
    const auto nibble = [](const char value) -> unsigned char {
        if (value >= '0' && value <= '9') {
            return static_cast<unsigned char>(value - '0');
        }
        if (value >= 'a' && value <= 'f') {
            return static_cast<unsigned char>(value - 'a' + 10);
        }
        throw std::invalid_argument("non-hex test vector");
    };
    auto result = std::vector<std::byte>{};
    result.reserve(text.size() / 2);
    for (auto index = std::size_t{0}; index < text.size(); index += 2) {
        result.push_back(static_cast<std::byte>((nibble(text[index]) << 4) | nibble(text[index + 1])));
    }
    return result;
}

struct representative final {
    int key = 0;
    std::shared_ptr<int> identity;
};

struct move_only_value final {
    int key = 0;

    explicit move_only_value(const int value) noexcept
        : key(value)
    {
    }

    move_only_value(const move_only_value&) = delete;
    move_only_value& operator=(const move_only_value&) = delete;
    move_only_value(move_only_value&&) noexcept = default;
    move_only_value& operator=(move_only_value&&) noexcept = default;
};

static_assert(!std::copy_constructible<move_only_value>);

void add_canonical_sorted_set_tests_impl(suite& tests)
{
    tests.add("ZZT2 keyed and public-seed vectors match the C# reference exactly", [] {
        auto key = std::array<std::byte, 32>{};
        for (auto index = std::size_t{0}; index < key.size(); ++index) {
            key[index] = static_cast<std::byte>(index);
        }
        const auto keyed = ft::zip_tree_rank_policy<int>::keyed(
            key,
            std::less<int>{},
            [](const int) { return 0x0102030405060708ULL; });
        const auto keyed_rank = keyed.rank_for(0);
        FT_REQUIRE_EQUAL(keyed_rank.geometric, std::uint32_t{1});
        FT_REQUIRE_EQUAL(keyed_rank.secondary, 0x197520638af1f7a6ULL);
        FT_REQUIRE_EQUAL(keyed_rank.content, 0xf31ebe0ab983ff0fULL);

        constexpr auto seed = std::uint64_t{0x0123456789abcdefULL};
        const auto seeded = ft::zip_tree_rank_policy<int>::seeded(
            seed,
            std::less<int>{},
            [](const int) { return 0xfedcba9876543210ULL; });
        const auto seeded_rank = seeded.rank_for(0);
        FT_REQUIRE_EQUAL(seeded_rank.geometric, std::uint32_t{0});
        FT_REQUIRE_EQUAL(seeded_rank.secondary, 0x9efdaef03f68c6bdULL);
        FT_REQUIRE_EQUAL(seeded_rank.content, 0x4da75484837a7798ULL);
        FT_REQUIRE(seeded.public_seed() == std::optional{seed});

        const auto public_key = bytes_from_hex(
            "0a2b912916749411e2b58555bfff0d8481d2aceefd74fc13d8a685e889649516");
        const auto equivalent_keyed = ft::zip_tree_rank_policy<int>::keyed(
            public_key,
            std::less<int>{},
            [](const int) { return 0xfedcba9876543210ULL; });
        FT_REQUIRE(equivalent_keyed.rank_for(0) == seeded_rank);

        auto owned_key = key;
        const auto retained_key = owned_key;
        const auto first_owned = ft::zip_tree_rank_policy<int>::keyed(owned_key);
        owned_key.fill(std::byte{0xff});
        const auto reproduced_owned = ft::zip_tree_rank_policy<int>::keyed(retained_key);
        FT_REQUIRE(first_owned.rank_for(137) == reproduced_owned.rank_for(137));

        FT_REQUIRE_EQUAL(ft::stable_zip_tree_rank_hash<std::int32_t>{}(-1), 0xffffffffULL);
        FT_REQUIRE_EQUAL(ft::stable_zip_tree_rank_hash<std::int64_t>{}(-1), 0xffffffffffffffffULL);
        FT_REQUIRE_EQUAL(
            ft::stable_zip_tree_rank_hash<std::string>{}("hello"),
            0xa430d84680aabd0bULL);
        FT_REQUIRE_THROWS(
            std::invalid_argument,
            ft::zip_tree_rank_policy<int>::keyed(std::span<const std::byte>{key}.first(31)));

        const auto unsigned_policy = ft::zip_tree_rank_policy<int>::seeded(0x771122);
        auto ranks = std::vector<std::pair<int, ft::zip_tree_rank>>{};
        ranks.reserve(512);
        for (auto value = 0; value != 512; ++value) {
            ranks.emplace_back(value, unsigned_policy.rank_for(value));
        }
        auto high = std::optional<std::pair<int, ft::zip_tree_rank>>{};
        auto low = std::optional<std::pair<int, ft::zip_tree_rank>>{};
        for (auto index = std::size_t{1}; index != ranks.size() && !high.has_value(); ++index) {
            const auto& [value, rank] = ranks[index];
            for (auto other_index = std::size_t{0}; other_index != index; ++other_index) {
                const auto& [other, other_rank] = ranks[other_index];
                if (rank.geometric == other_rank.geometric
                    && ((rank.secondary >> 63) != (other_rank.secondary >> 63))) {
                    high = (rank.secondary >> 63) != 0
                        ? std::pair{value, rank}
                        : std::pair{other, other_rank};
                    low = (rank.secondary >> 63) == 0
                        ? std::pair{value, rank}
                        : std::pair{other, other_rank};
                    break;
                }
            }
        }
        FT_REQUIRE(high.has_value());
        FT_REQUIRE(low.has_value());
        FT_REQUIRE(high->second.secondary > low->second.secondary);
        const auto unsigned_tree = ft::canonical_sorted_set<int>::from_range(
            std::array{low->first, high->first},
            unsigned_policy);
        FT_REQUIRE_EQUAL(unsigned_tree.topology_signature().front().item, high->first);
    });

    tests.add("fresh random keys separate policies while copied handles retain identity", [] {
        const auto first = ft::zip_tree_rank_policy<int>::random();
        const auto retained = first;
        const auto second = ft::zip_tree_rank_policy<int>::random();
        FT_REQUIRE(first.is_same_policy(retained));
        FT_REQUIRE(!first.is_same_policy(second));
        FT_REQUIRE(!first.public_seed().has_value());
        FT_REQUIRE(!second.public_seed().has_value());
        // The only probabilistic assertion in this suite: collision of the retained 128 rank bits
        // for independent CSPRNG keys has probability at most 2^-128.
        FT_REQUIRE(first.rank_for(0x5eed) != second.rank_for(0x5eed));
    });

    tests.add("bulk and incremental permutations converge on one canonical topology", [] {
        const auto policy = ft::zip_tree_rank_policy<int>::seeded(0x123456789abcdef0ULL);
        auto values = std::vector<int>{};
        for (auto value = -500; value < 500; ++value) {
            values.push_back(value);
        }
        const auto baseline = ft::canonical_sorted_set<int>::from_range(values, policy);
        deterministic_rng random{0x7a69705f7065726dULL};
        for (auto trial = 0; trial != 20; ++trial) {
            auto permutation = values;
            for (auto index = permutation.size(); index > 1; --index) {
                std::swap(permutation[index - 1], permutation[random.next_index(index)]);
            }
            const auto bulk = ft::canonical_sorted_set<int>::from_range(permutation, policy);
            auto incremental = ft::canonical_sorted_set<int>{policy};
            for (const auto value : permutation) {
                incremental = incremental.add(value);
            }
            FT_REQUIRE(baseline.topology_signature() == bulk.topology_signature());
            FT_REQUIRE(baseline.topology_signature() == incremental.topology_signature());
            FT_REQUIRE_EQUAL(baseline.content_hash(), bulk.content_hash());
            FT_REQUIRE(baseline.set_equals(incremental));

            auto edited = incremental;
            for (auto index = std::size_t{0}; index != 100; ++index) {
                edited = edited.remove(permutation[index]);
            }
            for (auto index = std::size_t{100}; index != 0; --index) {
                edited = edited.add(permutation[index - 1]);
            }
            FT_REQUIRE(baseline.topology_signature() == edited.topology_signature());
        }
        require_valid(baseline);
    });

    tests.add("bulk construction retains the first equivalent representative", [] {
        const auto less = [](const representative& left, const representative& right) {
            return left.key < right.key;
        };
        const auto hash = [](const representative& value) {
            return static_cast<std::uint64_t>(static_cast<std::uint32_t>(value.key));
        };
        const auto policy = ft::zip_tree_rank_policy<representative>::seeded(7, less, hash);
        const auto first_identity = std::make_shared<int>(1);
        const auto later_identity = std::make_shared<int>(2);
        const auto values = std::vector<representative>{
            representative{5, first_identity}, representative{1, std::make_shared<int>(3)},
            representative{5, later_identity}, representative{5, std::make_shared<int>(4)}};
        const auto set = ft::canonical_sorted_set<representative>::from_range(values, policy);
        const auto* actual = set.try_get(representative{5, {}});
        FT_REQUIRE(actual != nullptr);
        FT_REQUIRE(actual->identity == first_identity);
        FT_REQUIRE(actual->identity != later_identity);
        FT_REQUIRE_EQUAL(set.size(), std::size_t{2});
        (void)set.validate_structure();
    });

    tests.add("equivalent representatives dynamically reject an incoherent rank hash", [] {
        const auto case_less = [](const std::string& left, const std::string& right) {
            const auto lower = [](std::string value) {
                std::ranges::transform(value, value.begin(), [](const unsigned char character) {
                    return static_cast<char>(std::tolower(character));
                });
                return value;
            };
            return lower(left) < lower(right);
        };
        const auto incoherent = ft::zip_tree_rank_policy<std::string>::seeded(
            17,
            case_less,
            [](const std::string& value) { return static_cast<std::uint64_t>(value.front()); });
        const auto one = ft::canonical_sorted_set<std::string>{incoherent}.add("alpha");
        FT_REQUIRE_THROWS(std::logic_error, one.add("ALPHA"));
        FT_REQUIRE_THROWS(
            std::logic_error,
            ft::canonical_sorted_set<std::string>::from_range(
                std::array{std::string{"alpha"}, std::string{"ALPHA"}},
                incoherent));
    });

    tests.add("fully colliding ranks remain deterministic deep and stack safe", [] {
        constexpr auto count = 4'096;
        const auto policy = ft::zip_tree_rank_policy<int>::seeded(
            1,
            std::less<int>{},
            [](const int) { return std::uint64_t{0}; });
        auto forward_values = std::vector<int>{};
        for (auto value = 0; value != count; ++value) {
            forward_values.push_back(value);
        }
        auto reverse_values = forward_values;
        std::ranges::reverse(reverse_values);
        const auto forward = ft::canonical_sorted_set<int>::from_range(forward_values, policy);
        const auto reverse = ft::canonical_sorted_set<int>::from_range(reverse_values, policy);
        FT_REQUIRE_EQUAL(forward.height(), std::size_t{count});
        FT_REQUIRE(forward.topology_signature() == reverse.topology_signature());
        FT_REQUIRE_EQUAL(forward.content_hash(), reverse.content_hash());
        FT_REQUIRE_EQUAL(
            forward.validate_structure().priority_collision_count,
            std::size_t{count - 1});

        const auto without_last = forward.remove(count - 1);
        const auto restored = without_last.add(count - 1);
        FT_REQUIRE_EQUAL(without_last.height(), std::size_t{count - 1});
        FT_REQUIRE_EQUAL(restored.content_hash(), forward.content_hash());
        FT_REQUIRE(restored.set_equals(forward));
    });

    tests.add("linear-tree destruction is allocation free and stack safe", [] {
        constexpr auto count = 16'384;
        const auto policy = ft::zip_tree_rank_policy<int>::seeded(
            1,
            std::less<int>{},
            [](const int) { return std::uint64_t{0}; });
        auto values = std::vector<int>{};
        values.reserve(count);
        for (auto value = 0; value != count; ++value) {
            values.push_back(value);
        }
        auto set = ft::canonical_sorted_set<int>::from_range(values, policy);
        FT_REQUIRE_EQUAL(set.height(), std::size_t{count});
        allocation_counting_scope allocations;
        set = ft::canonical_sorted_set<int>{policy};
        FT_REQUIRE_EQUAL(allocations.allocations(), std::size_t{0});
#ifndef FINGERTREE_DISABLE_ALLOCATION_TRACKING
        FT_REQUIRE(allocations.deallocations() >= std::size_t{count * 2});
#endif
        FT_REQUIRE(set.empty());
    });

    tests.add("randomized persistent history matches a sorted-set model and snapshots", [] {
        const auto policy = ft::zip_tree_rank_policy<int>::seeded(42);
        auto actual = ft::canonical_sorted_set<int>{policy};
        auto model = std::set<int>{};
        auto snapshots = std::vector<std::pair<ft::canonical_sorted_set<int>, std::vector<int>>>{};
        deterministic_rng random{0x63616e6f6e5f6d6fULL};
        for (auto operation = 0; operation != 20'000; ++operation) {
            const auto value = static_cast<int>(random.next_index(10'001)) - 5'000;
            if (random.next_index(3) == 0) {
                actual = actual.remove(value);
                model.erase(value);
            } else {
                actual = actual.add(value);
                model.insert(value);
            }
            if (operation % 997 == 0) {
                snapshots.emplace_back(actual, std::vector<int>{model.begin(), model.end()});
            }
        }
        FT_REQUIRE(values_of(actual) == std::vector<int>(model.begin(), model.end()));
        require_valid(actual);
        for (const auto& [snapshot, expected] : snapshots) {
            FT_REQUIRE(values_of(snapshot) == expected);
            require_valid(snapshot);
        }
    });

    tests.add("algebra is policy-identity gated and preserves no-op versions", [] {
        const auto policy = ft::zip_tree_rank_policy<int>::seeded(99);
        const auto left = ft::canonical_sorted_set<int>::from_range(std::array{1, 2, 4, 8}, policy);
        const auto right = ft::canonical_sorted_set<int>::from_range(std::array{2, 3, 4, 5}, policy);
        const auto empty = ft::canonical_sorted_set<int>{policy};
        FT_REQUIRE(values_of(left.union_with(right)) == std::vector<int>({1, 2, 3, 4, 5, 8}));
        FT_REQUIRE(values_of(left.intersect(right)) == std::vector<int>({2, 4}));
        FT_REQUIRE(values_of(left.except(right)) == std::vector<int>({1, 8}));
        FT_REQUIRE(left.is_same_version(left.add(2)));
        FT_REQUIRE(left.is_same_version(left.remove(99)));
        FT_REQUIRE(left.is_same_version(left.union_with(left)));
        FT_REQUIRE(left.is_same_version(left.intersect(left)));
        FT_REQUIRE(left.is_same_version(left.except(empty)));
        FT_REQUIRE(empty.is_same_version(empty.clear()));

        const auto incompatible = ft::canonical_sorted_set<int>::from_range(
            std::array{1, 2, 4, 8},
            ft::zip_tree_rank_policy<int>::seeded(99));
        FT_REQUIRE(left.set_equals(incompatible));
        FT_REQUIRE(left.topology_signature() == incompatible.topology_signature());
        FT_REQUIRE_EQUAL(left.content_hash(), incompatible.content_hash());
        FT_REQUIRE(left.validate_structure() == incompatible.validate_structure());
        FT_REQUIRE_THROWS(std::invalid_argument, left.union_with(incompatible));
    });

    tests.add("semantic equality and relations use the receiver comparer", [] {
        const auto insensitive_less = [](const std::string& left, const std::string& right) {
            const auto fold = [](const std::string& text) {
                auto result = text;
                std::ranges::transform(result, result.begin(), [](const unsigned char character) {
                    return static_cast<char>(std::tolower(character));
                });
                return result;
            };
            return fold(left) < fold(right);
        };
        const auto insensitive_hash = [](const std::string& value) {
            auto folded = value;
            std::ranges::transform(folded, folded.begin(), [](const unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
            return ft::stable_zip_tree_rank_hash<std::string>{}(folded);
        };
        const auto insensitive_policy = ft::zip_tree_rank_policy<std::string>::seeded(
            0x1a51,
            insensitive_less,
            insensitive_hash);
        const auto sensitive_policy = ft::zip_tree_rank_policy<std::string>::seeded(0x5e51);
        const auto insensitive = ft::canonical_sorted_set<std::string>::from_range(
            std::array{std::string{"alpha"}},
            insensitive_policy);
        const auto sensitive = ft::canonical_sorted_set<std::string>::from_range(
            std::array{std::string{"alpha"}, std::string{"ALPHA"}},
            sensitive_policy);
        FT_REQUIRE(insensitive.set_equals(sensitive));
        FT_REQUIRE(!sensitive.set_equals(insensitive));
        FT_REQUIRE(insensitive.is_subset_of(sensitive));
        FT_REQUIRE(insensitive.is_superset_of(sensitive));
        FT_REQUIRE(!insensitive.is_proper_subset_of(sensitive));
        FT_REQUIRE(!insensitive.is_proper_superset_of(sensitive));
        FT_REQUIRE(insensitive.overlaps(sensitive));

        const auto proper = ft::canonical_sorted_set<std::string>::from_range(
            std::array{std::string{"alpha"}, std::string{"bravo"}},
            insensitive_policy);
        FT_REQUIRE(insensitive.is_proper_subset_of(proper));
        FT_REQUIRE(proper.is_proper_superset_of(insensitive));
    });

    tests.add("localized add and remove share almost every off-path node", [] {
        const auto policy = ft::zip_tree_rank_policy<int>::seeded(0x51a71c);
        auto values = std::vector<int>{};
        for (auto value = 0; value != 4'096; ++value) {
            values.push_back(value);
        }
        const auto original = ft::canonical_sorted_set<int>::from_range(values, policy);
        const auto added = original.add(-1);
        const auto removed = original.remove(2'048);
        FT_REQUIRE(original.shared_node_count_with(added) * 10 >= original.size() * 9);
        FT_REQUIRE(original.shared_node_count_with(removed) * 10 >= (original.size() - 1) * 9);
        FT_REQUIRE(!original.shares_root_with(added));
        FT_REQUIRE(!original.shares_root_with(removed));
        require_valid(original);
        require_valid(added);
        require_valid(removed);
    });

    tests.add("cold digest publication is stable across concurrent readers", [] {
        auto key = std::array<std::byte, 32>{};
        for (auto index = std::size_t{0}; index != key.size(); ++index) {
            key[index] = static_cast<std::byte>(index * 7 + 3);
        }
        const auto expected_policy = ft::zip_tree_rank_policy<int>::keyed(key);
        const auto actual_policy = ft::zip_tree_rank_policy<int>::keyed(key);
        auto values = std::vector<int>{};
        for (auto value = 0; value != 20'000; ++value) {
            values.push_back(value);
        }
        const auto expected = ft::canonical_sorted_set<int>::from_range(values, expected_policy).content_hash();
        const auto actual = ft::canonical_sorted_set<int>::from_range(values, actual_policy);

        constexpr auto reader_count = std::size_t{8};
        auto start = std::barrier{static_cast<std::ptrdiff_t>(reader_count)};
        auto results = std::array<std::uint64_t, reader_count>{};
        auto threads = std::vector<std::thread>{};
        for (auto index = std::size_t{0}; index != reader_count; ++index) {
            threads.emplace_back([&, index] {
                start.arrive_and_wait();
                for (auto repetition = 0; repetition != 128; ++repetition) {
                    results[index] = actual.content_hash();
                }
            });
        }
        for (auto& thread : threads) {
            thread.join();
        }
        FT_REQUIRE(std::ranges::all_of(results, [expected](const auto value) { return value == expected; }));
    });

    tests.add("move-only representatives support persistent incremental updates and algebra", [] {
        const auto less = [](const move_only_value& left, const move_only_value& right) {
            return left.key < right.key;
        };
        const auto hash = [](const move_only_value& value) {
            return static_cast<std::uint64_t>(static_cast<std::uint32_t>(value.key));
        };
        const auto policy = ft::zip_tree_rank_policy<move_only_value>::seeded(73, less, hash);
        const auto empty = ft::canonical_sorted_set<move_only_value>{policy};
        const auto one = empty.add(move_only_value{2});
        const auto three = one.add(move_only_value{1}).add(move_only_value{3});
        FT_REQUIRE_EQUAL(three.size(), std::size_t{3});
        FT_REQUIRE_EQUAL(three.try_get(move_only_value{2})->key, 2);
        FT_REQUIRE_EQUAL(three.begin()->key, 1);
        FT_REQUIRE_EQUAL(three.remove(move_only_value{2}).size(), std::size_t{2});
        FT_REQUIRE_EQUAL(three.union_with(one).size(), std::size_t{3});
        FT_REQUIRE_EQUAL(three.intersect(one).size(), std::size_t{1});
        FT_REQUIRE_EQUAL(three.except(one).size(), std::size_t{2});
        FT_REQUIRE_EQUAL(one.size(), std::size_t{1});
        (void)three.validate_structure();

        auto movable = std::vector<move_only_value>{};
        movable.emplace_back(4);
        movable.emplace_back(2);
        movable.emplace_back(4);
        const auto bulk = ft::canonical_sorted_set<move_only_value>::from_range(
            std::move(movable),
            policy);
        FT_REQUIRE_EQUAL(bulk.size(), std::size_t{2});
        FT_REQUIRE_EQUAL(bulk.begin()->key, 2);
        (void)bulk.validate_structure();
    });

    tests.add("throwing policy callbacks cannot mutate published snapshots", [] {
        const auto less = [](const int left, const int right) {
            if (left == 777 || right == 777) {
                throw std::runtime_error("injected comparator failure");
            }
            return left < right;
        };
        const auto hash = [](const int value) {
            if (value == 888) {
                throw std::runtime_error("injected hash failure");
            }
            return static_cast<std::uint64_t>(static_cast<std::uint32_t>(value));
        };
        const auto policy = ft::zip_tree_rank_policy<int>::seeded(3, less, hash);
        const auto set = ft::canonical_sorted_set<int>::from_range(std::array{1, 2, 3}, policy);
        const auto identity = set.root_identity();
        FT_REQUIRE_THROWS(std::runtime_error, set.add(777));
        FT_REQUIRE_THROWS(std::runtime_error, set.add(888));
        FT_REQUIRE_EQUAL(set.root_identity(), identity);
        FT_REQUIRE(values_of(set) == std::vector<int>({1, 2, 3}));
        (void)set.validate_structure();
    });
}

} // namespace

void add_canonical_sorted_set_tests(suite& tests)
{
    add_canonical_sorted_set_tests_impl(tests);
}
