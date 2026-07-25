#include <durable7/finger_tree/persistent_chunked_bit_set.hpp>

#include "test_support/test_runner.hpp"

#include <cstdint>
#include <limits>
#include <random>
#include <set>
#include <stdexcept>
#include <vector>

namespace ft = durable7::finger_tree;
using namespace durable7::finger_tree::tests;

void add_persistent_chunked_bit_set_tests(suite& tests)
{
    tests.add("chunked bit set point edits respect domain and identity", [] {
        const auto empty = ft::persistent_chunked_bit_set{};
        FT_REQUIRE(empty.is_empty());
        FT_REQUIRE(!empty.contains(-1));
        FT_REQUIRE_THROWS(std::out_of_range, empty.add(-1));
        const auto set = empty.add(0).add((std::numeric_limits<std::int32_t>::max)());
        FT_REQUIRE_EQUAL(set.count(), std::uint64_t{2});
        FT_REQUIRE_EQUAL(set.chunk_count(), std::size_t{2});
        FT_REQUIRE(set.add(0).to_vector() == set.to_vector());
        FT_REQUIRE(empty.is_empty());
    });

    tests.add("chunked bit set range construction orders and deduplicates", [] {
        const auto source = std::vector<std::int32_t>{65, 0, 64, 63, 1, 130, 65};
        const auto set = ft::persistent_chunked_bit_set::create_range(source);
        FT_REQUIRE((set.to_vector() == std::vector<std::int32_t>{0, 1, 63, 64, 65, 130}));
        FT_REQUIRE_EQUAL(set.chunk_count(), std::size_t{3});
        FT_REQUIRE(set.debug_validate());
    });

    tests.add("chunked bit set rank is inclusive", [] {
        const auto set = ft::persistent_chunked_bit_set::create_range(
            std::vector<std::int32_t>{0, 2, 63, 64, 100, 130});
        FT_REQUIRE_EQUAL(set.rank(-1), std::uint64_t{0});
        FT_REQUIRE_EQUAL(set.rank(0), std::uint64_t{1});
        FT_REQUIRE_EQUAL(set.rank(1), std::uint64_t{1});
        FT_REQUIRE_EQUAL(set.rank(63), std::uint64_t{3});
        FT_REQUIRE_EQUAL(set.rank(99), std::uint64_t{4});
        FT_REQUIRE_EQUAL(set.rank(130), std::uint64_t{6});
    });

    tests.add("chunked bit set select returns population order", [] {
        const auto expected = std::vector<std::int32_t>{0, 1, 63, 64, 65, 511, 4096};
        const auto set = ft::persistent_chunked_bit_set::create_range(expected);
        for (auto rank = std::size_t{0}; rank != expected.size(); ++rank) {
            FT_REQUIRE_EQUAL(set.select(rank), expected[rank]);
            FT_REQUIRE_EQUAL(*set.try_select(rank), expected[rank]);
        }
        FT_REQUIRE(!set.try_select(expected.size()).has_value());
        FT_REQUIRE_THROWS(std::out_of_range, set.select(expected.size()));
    });

    tests.add("chunked bit set removal contracts empty chunks", [] {
        const auto source = ft::persistent_chunked_bit_set::create_range(
            std::vector<std::int32_t>{1, 64, 65});
        const auto reduced = source.remove(64).remove(65);
        FT_REQUIRE((reduced.to_vector() == std::vector<std::int32_t>{1}));
        FT_REQUIRE_EQUAL(reduced.chunk_count(), std::size_t{1});
        FT_REQUIRE((source.to_vector() == std::vector<std::int32_t>{1, 64, 65}));
    });

    tests.add("chunked bit set algebra matches mathematical sets", [] {
        const auto left = ft::persistent_chunked_bit_set::create_range(
            std::vector<std::int32_t>{0, 1, 64, 130});
        const auto right = ft::persistent_chunked_bit_set::create_range(
            std::vector<std::int32_t>{1, 2, 64, 65});
        FT_REQUIRE((left.union_with(right).to_vector()
            == std::vector<std::int32_t>{0, 1, 2, 64, 65, 130}));
        FT_REQUIRE((left.intersect_with(right).to_vector()
            == std::vector<std::int32_t>{1, 64}));
        FT_REQUIRE((left.except_with(right).to_vector()
            == std::vector<std::int32_t>{0, 130}));
        FT_REQUIRE((left.symmetric_except_with(right).to_vector()
            == std::vector<std::int32_t>{0, 2, 65, 130}));
        FT_REQUIRE(left.except_with(left).is_empty());
    });

    tests.add("chunked bit set randomized edits match sorted set", [] {
        auto random = std::mt19937{1729};
        auto model = std::set<std::int32_t>{};
        auto set = ft::persistent_chunked_bit_set{};
        for (auto operation = 0; operation != 400; ++operation) {
            const auto bit = static_cast<std::int32_t>(random() % 2048u);
            if ((random() & 1u) == 0u) {
                model.insert(bit);
                set = set.add(bit);
            } else {
                model.erase(bit);
                set = set.remove(bit);
            }
            FT_REQUIRE(set.to_vector() == std::vector<std::int32_t>(model.begin(), model.end()));
            FT_REQUIRE_EQUAL(set.count(), static_cast<std::uint64_t>(model.size()));
            FT_REQUIRE(set.debug_validate());
        }
    });
}
