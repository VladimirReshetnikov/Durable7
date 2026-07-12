#include <Tools/DataStructures/Hamt/merkle_search_tree.hpp>
#include <tools/data_structures/test_support/headless_test_process.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

using tools::data_structures::hamt::int32_merkle_codec;
using tools::data_structures::hamt::int64_merkle_codec;
using tools::data_structures::hamt::merkle_bytes;
using tools::data_structures::hamt::merkle_codec;
using tools::data_structures::hamt::merkle_codec_error;
using tools::data_structures::hamt::merkle_digest;
using tools::data_structures::hamt::merkle_map_difference_kind;
using tools::data_structures::hamt::merkle_policy_mismatch;
using tools::data_structures::hamt::merkle_range_error;
using tools::data_structures::hamt::merkle_search_tree;
using tools::data_structures::hamt::merkle_search_tree_policy;
using tools::data_structures::hamt::merkle_tree_invariant_error;
using tools::data_structures::hamt::nullable_bytes_merkle_codec;
using tools::data_structures::hamt::nullable_utf8_merkle_codec;
using tools::data_structures::hamt::rfc4122_guid;
using tools::data_structures::hamt::rfc4122_guid_merkle_codec;

namespace {

class test_failure final : public std::runtime_error {
public:
    explicit test_failure(const std::string& message)
        : std::runtime_error(message)
    {
    }
};

[[noreturn]] void fail(const char* file, const int line, const char* expression)
{
    auto message = std::ostringstream{};
    message << file << ':' << line << ": check failed: " << expression;
    throw test_failure{message.str()};
}

[[noreturn]] void fail_message(const char* file, const int line, const std::string& message)
{
    auto full_message = std::ostringstream{};
    full_message << file << ':' << line << ": " << message;
    throw test_failure{full_message.str()};
}

#define CHECK(expression) \
    do { \
        if (!(expression)) { \
            fail(__FILE__, __LINE__, #expression); \
        } \
    } while (false)

#define CHECK_EQ(expected, actual) \
    do { \
        if (!((expected) == (actual))) { \
            fail(__FILE__, __LINE__, #expected " == " #actual); \
        } \
    } while (false)

#define CHECK_THROWS_AS(expression, exception_type) \
    do { \
        bool threw_expected_exception = false; \
        try { \
            (void)(expression); \
        } catch (const exception_type&) { \
            threw_expected_exception = true; \
        } catch (const std::exception& ex) { \
            fail_message(__FILE__, __LINE__, std::string{"wrong exception type: "} + ex.what()); \
        } catch (...) { \
            fail_message(__FILE__, __LINE__, "wrong non-standard exception type"); \
        } \
        if (!threw_expected_exception) { \
            fail(__FILE__, __LINE__, #expression " throws " #exception_type); \
        } \
    } while (false)

struct test_case final {
    const char* name;
    void (*run)();
};

std::vector<test_case>& registry()
{
    static auto tests = std::vector<test_case>{};
    return tests;
}

struct registrar final {
    registrar(const char* name, void (*run)())
    {
        registry().push_back(test_case{name, run});
    }
};

#define CONCAT_INNER(left, right) left##right
#define CONCAT(left, right) CONCAT_INNER(left, right)
#define TEST(name) \
    void name(); \
    registrar CONCAT(registrar_, __LINE__)(#name, &name); \
    void name()

[[nodiscard]] merkle_bytes bytes(std::initializer_list<unsigned int> values)
{
    auto result = merkle_bytes{};
    result.reserve(values.size());
    for (const auto value : values) {
        CHECK(value <= (std::numeric_limits<std::uint8_t>::max)());
        result.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(value)));
    }
    return result;
}

[[nodiscard]] merkle_bytes parse_hex(const std::string_view text)
{
    CHECK(text.size() % 2 == 0);
    const auto digit = [](const char value) -> unsigned int {
        if (value >= '0' && value <= '9') {
            return static_cast<unsigned int>(value - '0');
        }
        if (value >= 'a' && value <= 'f') {
            return static_cast<unsigned int>(value - 'a' + 10);
        }
        if (value >= 'A' && value <= 'F') {
            return static_cast<unsigned int>(value - 'A' + 10);
        }
        throw test_failure{"invalid hexadecimal test vector"};
    };

    auto result = merkle_bytes{};
    result.reserve(text.size() / 2);
    for (auto index = std::size_t{0}; index != text.size(); index += 2) {
        const auto value = static_cast<std::uint8_t>(
            (digit(text[index]) << 4) | digit(text[index + 1]));
        result.push_back(static_cast<std::byte>(value));
    }
    return result;
}

using int_tree = merkle_search_tree<std::int32_t, std::int32_t>;
using string_tree = merkle_search_tree<std::int32_t, std::optional<std::string>>;

[[nodiscard]] int_tree::policy_type make_int_policy(
    std::string policy_id = "canonical-i32-v1")
{
    return int_tree::policy_type::natural(
        std::move(policy_id),
        std::make_shared<int32_merkle_codec>(),
        std::make_shared<int32_merkle_codec>());
}

[[nodiscard]] string_tree::policy_type make_string_policy(
    std::string policy_id = "golden-int-string-v1")
{
    return string_tree::policy_type::natural(
        std::move(policy_id),
        std::make_shared<int32_merkle_codec>(),
        std::make_shared<nullable_utf8_merkle_codec>());
}

void assert_matches(const std::map<std::int32_t, std::int32_t>& model, const int_tree& tree)
{
    CHECK_EQ(model.size(), tree.size());
    auto actual = tree.begin();
    for (const auto& [key, value] : model) {
        CHECK(actual != tree.end());
        CHECK_EQ(key, actual->key());
        CHECK_EQ(value, actual->value());
        CHECK_EQ(value, tree.at(key));
        CHECK(tree.try_get_key(key) != nullptr);
        CHECK_EQ(key, *tree.try_get_key(key));
        ++actual;
    }
    CHECK(actual == tree.end());
}

TEST(CanonicalCodecsAndDigestRejectMalformedRepresentations)
{
    const auto int32_codec = int32_merkle_codec{};
    CHECK_EQ(bytes({0x80, 0x00, 0x00, 0x00}), int32_codec.encode((std::numeric_limits<std::int32_t>::min)()));
    CHECK_EQ((std::numeric_limits<std::int32_t>::min)(), int32_codec.decode(bytes({0x80, 0x00, 0x00, 0x00})));
    CHECK_EQ(std::int32_t{-1}, int32_codec.decode(bytes({0xff, 0xff, 0xff, 0xff})));
    CHECK_THROWS_AS(int32_codec.decode(bytes({0, 0, 0})), merkle_codec_error);

    const auto int64_codec = int64_merkle_codec{};
    CHECK_EQ(
        bytes({0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}),
        int64_codec.encode((std::numeric_limits<std::int64_t>::max)()));
    CHECK_EQ(std::int64_t{-2}, int64_codec.decode(bytes({0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe})));
    CHECK_THROWS_AS(int64_codec.decode(bytes({0, 0, 0, 0, 0, 0, 0})), merkle_codec_error);

    const auto utf8_codec = nullable_utf8_merkle_codec{};
    CHECK_EQ(bytes({0}), utf8_codec.encode(std::nullopt));
    CHECK_EQ(bytes({1}), utf8_codec.encode(std::optional<std::string>{""}));
    const auto unicode = std::optional<std::string>{"\xc3\xa9\xf0\x9f\x98\x80"};
    CHECK_EQ(unicode, utf8_codec.decode(utf8_codec.encode(unicode)));
    CHECK_THROWS_AS(utf8_codec.decode({}), merkle_codec_error);
    CHECK_THROWS_AS(utf8_codec.decode(bytes({0, 0})), merkle_codec_error);
    CHECK_THROWS_AS(utf8_codec.decode(bytes({2})), merkle_codec_error);
    CHECK_THROWS_AS(utf8_codec.decode(bytes({1, 0xc0, 0x80})), merkle_codec_error);
    CHECK_THROWS_AS(utf8_codec.decode(bytes({1, 0xed, 0xa0, 0x80})), merkle_codec_error);
    CHECK_THROWS_AS(utf8_codec.decode(bytes({1, 0xf4, 0x90, 0x80, 0x80})), merkle_codec_error);
    CHECK_THROWS_AS(utf8_codec.encode(std::optional<std::string>{std::string{"\xff", 1}}), merkle_codec_error);

    const auto byte_codec = nullable_bytes_merkle_codec{};
    const auto byte_value = std::optional<merkle_bytes>{bytes({0, 1, 0xff})};
    CHECK_EQ(bytes({0}), byte_codec.encode(std::nullopt));
    CHECK_EQ(byte_value, byte_codec.decode(byte_codec.encode(byte_value)));
    CHECK_THROWS_AS(byte_codec.decode({}), merkle_codec_error);
    CHECK_THROWS_AS(byte_codec.decode(bytes({0, 1})), merkle_codec_error);
    CHECK_THROWS_AS(byte_codec.decode(bytes({7})), merkle_codec_error);

    auto guid_bytes = std::array<std::byte, rfc4122_guid::byte_length>{};
    for (auto index = std::size_t{0}; index != guid_bytes.size(); ++index) {
        guid_bytes[index] = static_cast<std::byte>(static_cast<std::uint8_t>(index * 17));
    }
    const auto guid = rfc4122_guid{guid_bytes};
    const auto guid_codec = rfc4122_guid_merkle_codec{};
    CHECK_EQ(guid, guid_codec.decode(guid_codec.encode(guid)));
    CHECK_THROWS_AS(guid_codec.decode(bytes({0})), merkle_codec_error);

    CHECK_EQ(
        std::string{"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
        merkle_digest::hash({}).to_hex());
    const auto digest = merkle_digest::from_hex(
        "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff");
    CHECK_EQ(digest, merkle_digest::from_bytes(digest.as_span()));
    CHECK_EQ(digest, merkle_digest::from_hex(
        "00112233445566778899AABBCCDDEEFF00112233445566778899AABBCCDDEEFF"));
    CHECK(!merkle_digest::try_from_hex("xyz").has_value());
    CHECK(!merkle_digest::try_from_bytes(bytes({0})).has_value());
    CHECK_THROWS_AS(merkle_digest::from_hex("xyz"), std::invalid_argument);
    CHECK_THROWS_AS(merkle_digest::from_bytes(bytes({0})), std::invalid_argument);

    auto too_short = merkle_bytes(31, std::byte{0x5a});
    const auto before = too_short;
    CHECK(!digest.try_write_bytes(too_short));
    CHECK_EQ(before, too_short);
    CHECK_THROWS_AS(digest.write_bytes(too_short), std::length_error);
    CHECK_EQ(before, too_short);
    auto destination = merkle_bytes(40, std::byte{0x5a});
    CHECK(digest.try_write_bytes(destination));
    CHECK(std::equal(digest.bytes().begin(), digest.bytes().end(), destination.begin()));
    CHECK(std::all_of(destination.begin() + 32, destination.end(), [](const std::byte value) {
        return value == std::byte{0x5a};
    }));
}

struct validation_key final {
    std::int32_t value;

    friend bool operator<(const validation_key left, const validation_key right) noexcept
    {
        return left.value < right.value;
    }
};

class invalid_id_codec final : public merkle_codec<validation_key> {
public:
    explicit invalid_id_codec(std::string id)
        : id_(std::move(id))
    {
    }

    [[nodiscard]] std::string_view encoding_id() const override { return id_; }
    [[nodiscard]] merkle_bytes encode(const validation_key& value) const override
    {
        return int32_merkle_codec{}.encode(value.value);
    }
    [[nodiscard]] validation_key decode(const std::span<const std::byte> encoding) const override
    {
        return validation_key{int32_merkle_codec{}.decode(encoding)};
    }

private:
    std::string id_;
};

TEST(PolicyValidationIdentityAndCompatibilityAreExplicit)
{
    using policy_type = int_tree::policy_type;
    const auto make_with_key_id = [](std::string id) {
        using validation_policy = merkle_search_tree_policy<validation_key, std::int32_t>;
        return validation_policy::natural(
            "policy-validation-v1",
            std::make_shared<invalid_id_codec>(std::move(id)),
            std::make_shared<int32_merkle_codec>());
    };

    CHECK_THROWS_AS(make_int_policy(""), std::invalid_argument);
    CHECK_THROWS_AS(make_int_policy(" \t\r\n"), std::invalid_argument);
    CHECK_THROWS_AS(make_int_policy("\xc2\xa0"), std::invalid_argument);
    CHECK_THROWS_AS(make_int_policy("\xe3\x80\x80"), std::invalid_argument);
    CHECK_THROWS_AS(make_int_policy(std::string{"\xff", 1}), std::invalid_argument);
    for (const auto id : {"", " ", "codec", "-v1", "codec-v", "codec-vx", " codec-v1", "codec-v1 "}) {
        CHECK_THROWS_AS(make_with_key_id(id), std::invalid_argument);
    }
    CHECK_THROWS_AS(make_with_key_id("\xc2\xa0" "codec-v1"), std::invalid_argument);
    CHECK_THROWS_AS(make_with_key_id("codec-v1" "\xe3\x80\x80"), std::invalid_argument);

    const auto first_policy = make_int_policy("same-domain-v1");
    const auto same_domain = make_int_policy("same-domain-v1");
    const auto different = make_int_policy("different-domain-v1");
    CHECK(!first_policy.shares_identity_with(same_domain));
    CHECK(first_policy.is_compatible_with(same_domain));
    CHECK(!first_policy.is_compatible_with(different));
    CHECK_EQ(std::string{"mst-sha256-b16-v2"}, std::string{policy_type::algorithm_id});

    const auto first = int_tree::create(first_policy).set_item(1, 10);
    const auto same = int_tree::create(same_domain).set_item(1, 10);
    const auto other = int_tree::create(different).set_item(1, 10);
    CHECK(first.content_equals(same));
    CHECK(first.map_equals(same));
    CHECK(!first.content_equals(other));
    CHECK(!first.map_equals(other));
    CHECK_THROWS_AS(first.diff(other), merkle_policy_mismatch);
}

TEST(SingleEntryMst2GoldenLocksDomainRootAndExactBlockBytes)
{
    constexpr auto expected_block_hex = std::string_view{
        "4d53543201fe140762a080abb39de83f70e7505c8b94c4baa428eea76d468a0f3163bc56c2"
        "000000000100000001000000040000002a0000000a01666f7274792d74776f"
        "98900ab6355e8ea553b5cd087d6ec4b976dc3e0953e35c6f46bc756ac75ddcb3"
        "98900ab6355e8ea553b5cd087d6ec4b976dc3e0953e35c6f46bc756ac75ddcb3"};
    const auto policy = make_string_policy();
    CHECK_EQ(
        std::string{"fe140762a080abb39de83f70e7505c8b94c4baa428eea76d468a0f3163bc56c2"},
        policy.domain_digest().to_hex());
    CHECK_EQ(
        std::string{"98900ab6355e8ea553b5cd087d6ec4b976dc3e0953e35c6f46bc756ac75ddcb3"},
        policy.empty_digest().to_hex());

    const auto empty = string_tree::create(policy);
    CHECK_EQ(policy.empty_digest(), empty.root_hash());
    CHECK(empty.validate_structure() == tools::data_structures::hamt::merkle_search_tree_statistics{});
    const auto tree = empty.set_item(42, std::optional<std::string>{"forty-two"});
    CHECK_EQ(
        std::string{"1b464818e8934692ad28f35f520fa0c834634e2200f9e5873d0327e6524bcc94"},
        tree.root_hash().to_hex());
    const auto blocks = tree.blocks_preorder();
    CHECK_EQ(std::size_t{1}, blocks.size());
    CHECK_EQ(tree.root_hash(), blocks.front().digest);
    CHECK_EQ(parse_hex(expected_block_hex), *blocks.front().bytes);

    const auto statistics = tree.validate_structure();
    CHECK_EQ(std::size_t{1}, statistics.count);
    CHECK_EQ(std::size_t{1}, statistics.block_count);
    CHECK_EQ(std::size_t{1}, statistics.height);
    CHECK_EQ(std::size_t{1}, statistics.minimum_entries_per_block);
    CHECK_EQ(std::size_t{1}, statistics.maximum_entries_per_block);
    CHECK_EQ(parse_hex(expected_block_hex).size(), statistics.minimum_block_bytes);
    CHECK_EQ(statistics.minimum_block_bytes, statistics.maximum_block_bytes);
}

TEST(CanonicalConstructionIsIndependentOfHistoryAndPolicyIdentity)
{
    constexpr auto entry_count = std::int32_t{1024};
    auto forward_items = std::vector<std::pair<std::int32_t, std::int32_t>>{};
    forward_items.reserve(static_cast<std::size_t>(entry_count));
    for (auto key = std::int32_t{0}; key != entry_count; ++key) {
        forward_items.emplace_back(key, key * 3);
    }
    auto reverse_items = forward_items;
    std::reverse(reverse_items.begin(), reverse_items.end());

    const auto first_policy = make_int_policy();
    const auto second_policy = make_int_policy();
    const auto forward = int_tree::create_range(std::move(forward_items), first_policy);
    const auto reverse = int_tree::create_range(std::move(reverse_items), second_policy);
    CHECK(!forward.shares_policy_with(reverse));
    CHECK(forward.content_equals(reverse));
    CHECK(forward.map_equals(reverse));
    CHECK_EQ(forward.shape().size(), reverse.shape().size());
    CHECK_EQ(forward.block_count(), reverse.block_count());
    CHECK(forward.height() > 1);

    auto incremental = int_tree::create(first_policy);
    for (auto key = std::int32_t{0}; key != entry_count; ++key) {
        incremental = incremental.set_item(key, key * 3);
    }
    CHECK(forward.content_equals(incremental));
    CHECK_EQ(forward.blocks_preorder().size(), incremental.blocks_preorder().size());
    const auto forward_blocks = forward.blocks_preorder();
    const auto incremental_blocks = incremental.blocks_preorder();
    for (auto index = std::size_t{0}; index != forward_blocks.size(); ++index) {
        CHECK_EQ(forward_blocks[index].digest, incremental_blocks[index].digest);
        CHECK_EQ(*forward_blocks[index].bytes, *incremental_blocks[index].bytes);
    }

    auto rebuilt = incremental;
    for (auto key = std::int32_t{0}; key < entry_count; key += 3) {
        rebuilt = rebuilt.remove(key);
    }
    for (auto key = entry_count - 1; key >= 0; --key) {
        if (key % 3 == 0) {
            rebuilt = rebuilt.set_item(key, key * 3);
        }
    }
    CHECK(forward.content_equals(rebuilt));
    CHECK(forward.map_equals(rebuilt));

    const auto statistics = forward.validate_structure();
    CHECK_EQ(static_cast<std::size_t>(entry_count), statistics.count);
    CHECK_EQ(forward.block_count(), statistics.block_count);
    CHECK_EQ(forward.height(), statistics.height);
    const auto shape = forward.shape();
    CHECK_EQ(forward.size(), shape.size());
    CHECK(std::any_of(shape.begin(), shape.end(), [](const auto& item) {
        return item.entries_in_block > 1;
    }));
    CHECK(std::any_of(shape.begin(), shape.end(), [](const auto& item) {
        return item.level > 0;
    }));

    const auto layer_policy = make_int_policy("layer-search-v1");
    constexpr auto required_per_level = std::array<std::size_t, 5>{16, 8, 3, 1, 1};
    auto discovered = std::array<std::vector<std::int32_t>, 5>{};
    for (auto candidate = std::int32_t{0}; candidate != 1'000'000; ++candidate) {
        const auto level = int_tree::policy_type::level(layer_policy.hash_key(candidate));
        if (level < discovered.size()
            && discovered[level].size() < required_per_level[level]) {
            discovered[level].push_back(candidate);
        }
        auto complete = true;
        for (auto exact_level = std::size_t{0}; exact_level != discovered.size(); ++exact_level) {
            complete = complete
                && discovered[exact_level].size() == required_per_level[exact_level];
        }
        if (complete) {
            break;
        }
    }
    for (auto exact_level = std::size_t{0}; exact_level != discovered.size(); ++exact_level) {
        CHECK_EQ(required_per_level[exact_level], discovered[exact_level].size());
        for (const auto key : discovered[exact_level]) {
            CHECK_EQ(
                static_cast<std::uint8_t>(exact_level),
                int_tree::policy_type::level(layer_policy.hash_key(key)));
        }
    }

    auto layered_items = std::vector<std::pair<std::int32_t, std::int32_t>>{};
    for (const auto& layer : discovered) {
        for (const auto key : layer) {
            layered_items.emplace_back(key, -key);
        }
    }
    const auto layered_canonical = int_tree::create_range(layered_items, layer_policy);
    auto shuffled_items = layered_items;
    auto random = std::mt19937{0x5a172026u};
    std::shuffle(shuffled_items.begin(), shuffled_items.end(), random);
    auto layered_incremental = int_tree::create(layer_policy);
    for (const auto& [key, value] : shuffled_items) {
        layered_incremental = layered_incremental.set_item(key, value);
    }
    CHECK(layered_canonical.content_equals(layered_incremental));
    CHECK_EQ(layered_canonical.shape().size(), layered_incremental.shape().size());
    CHECK(!layered_canonical.shape().empty());
    CHECK_EQ(std::uint8_t{4}, layered_canonical.shape().front().level);
    CHECK(layered_canonical.height() >= 4);

    auto removal_order = layered_items;
    std::sort(removal_order.begin(), removal_order.end(), [&layer_policy](const auto& left, const auto& right) {
        const auto left_level = int_tree::policy_type::level(layer_policy.hash_key(left.first));
        const auto right_level = int_tree::policy_type::level(layer_policy.hash_key(right.first));
        return left_level != right_level ? left_level > right_level : left.first < right.first;
    });
    auto remaining = std::map<std::int32_t, std::int32_t>{layered_items.begin(), layered_items.end()};
    auto contracted = layered_incremental;
    for (const auto& [key, value] : removal_order) {
        (void)value;
        contracted = contracted.remove(key);
        remaining.erase(key);
        const auto layer_rebuilt = int_tree::create_range(
            std::vector<std::pair<std::int32_t, std::int32_t>>{remaining.begin(), remaining.end()},
            layer_policy);
        CHECK(contracted.content_equals(layer_rebuilt));
        CHECK_EQ(contracted.size(), contracted.validate_structure().count);
    }
    CHECK(contracted.empty());
    CHECK_EQ(layer_policy.empty_digest(), contracted.root_hash());
}

TEST(PersistentMutationSharingRangeAndDiffHonorMapSemantics)
{
    auto source = int_tree::create(make_int_policy("mutation-v1"));
    for (auto key = std::int32_t{0}; key != 512; ++key) {
        source = source.set_item(key, key * 10);
    }
    const auto original_hash = source.root_hash();
    const auto same_value = source.set_item(200, 2000);
    CHECK(same_value.shares_root_with(source));
    const auto absent = source.remove(-1);
    CHECK(absent.shares_root_with(source));
    CHECK(source.clear().empty());
    CHECK(source.clear().shares_policy_with(source));

    const auto removed = source.remove(7);
    const auto changed = removed.set_item(8, 999);
    const auto target = changed.set_item(1000, 3000);
    CHECK_EQ(original_hash, source.root_hash());
    CHECK(source.contains_key(7));
    CHECK_EQ(80, source.at(8));
    CHECK(!target.contains_key(7));
    CHECK_EQ(999, target.at(8));
    CHECK_EQ(3000, target.at(1000));
    CHECK(target.shared_block_count(source) > 0);
    CHECK(target.shared_block_count(source) < source.block_count());

    const auto range = source.enumerate_range(100, 109);
    CHECK_EQ(std::size_t{10}, range.size());
    for (auto index = std::size_t{0}; index != range.size(); ++index) {
        CHECK_EQ(static_cast<std::int32_t>(100 + index), range[index].key());
        CHECK_EQ(range[index].key() * 10, range[index].value());
    }
    CHECK(source.enumerate_range(-100, -1).empty());
    CHECK_THROWS_AS(source.enumerate_range(2, 1), merkle_range_error);

    const auto differences = source.diff(target);
    CHECK_EQ(std::size_t{3}, differences.size());
    CHECK_EQ(merkle_map_difference_kind::removed, differences[0].kind);
    CHECK_EQ(std::int32_t{7}, *differences[0].key);
    CHECK_EQ(std::int32_t{70}, *differences[0].old_value);
    CHECK(differences[0].new_value == nullptr);
    CHECK_EQ(merkle_map_difference_kind::changed, differences[1].kind);
    CHECK_EQ(std::int32_t{8}, *differences[1].key);
    CHECK_EQ(std::int32_t{80}, *differences[1].old_value);
    CHECK_EQ(std::int32_t{999}, *differences[1].new_value);
    CHECK_EQ(merkle_map_difference_kind::added, differences[2].kind);
    CHECK_EQ(std::int32_t{1000}, *differences[2].key);
    CHECK(differences[2].old_value == nullptr);
    CHECK_EQ(std::int32_t{3000}, *differences[2].new_value);
    CHECK(target.diff(target).empty());
    CHECK(!source.map_equals(target));
    CHECK(!source.content_equals(target));
    CHECK_EQ(source.size(), source.validate_structure().count);
    CHECK_EQ(target.size(), target.validate_structure().count);

    const auto nullable_policy = make_string_policy("nullable-map-v1");
    const auto nullable_source = string_tree::create(nullable_policy)
        .set_item(1, std::nullopt)
        .set_item(2, std::optional<std::string>{"two"});
    CHECK(nullable_source.contains_key(1));
    CHECK(nullable_source.get_entry(1) != nullptr);
    CHECK(nullable_source.try_get(1) != nullptr);
    CHECK(!nullable_source.try_get(1)->has_value());
    CHECK(nullable_source.try_get(99) == nullptr);
    const auto nullable_target = nullable_source
        .set_item(1, std::optional<std::string>{"one"})
        .remove(2)
        .set_item(3, std::nullopt);
    const auto nullable_diff = nullable_source.diff(nullable_target);
    CHECK_EQ(std::size_t{3}, nullable_diff.size());
    CHECK_EQ(merkle_map_difference_kind::changed, nullable_diff[0].kind);
    CHECK(nullable_diff[0].old_value != nullptr);
    CHECK(!nullable_diff[0].old_value->has_value());
    CHECK(nullable_diff[0].new_value != nullptr);
    CHECK_EQ(std::string{"one"}, nullable_diff[0].new_value->value());
    CHECK_EQ(merkle_map_difference_kind::removed, nullable_diff[1].kind);
    CHECK(nullable_diff[1].old_value != nullptr);
    CHECK_EQ(std::string{"two"}, nullable_diff[1].old_value->value());
    CHECK(nullable_diff[1].new_value == nullptr);
    CHECK_EQ(merkle_map_difference_kind::added, nullable_diff[2].kind);
    CHECK(nullable_diff[2].old_value == nullptr);
    CHECK(nullable_diff[2].new_value != nullptr);
    CHECK(!nullable_diff[2].new_value->has_value());
}

class move_only_int final {
public:
    explicit move_only_int(const std::int32_t value)
        : value_(std::make_unique<std::int32_t>(value))
    {
    }

    move_only_int(move_only_int&&) noexcept = default;
    move_only_int& operator=(move_only_int&&) noexcept = default;
    move_only_int(const move_only_int&) = delete;
    move_only_int& operator=(const move_only_int&) = delete;

    [[nodiscard]] std::int32_t value() const noexcept { return *value_; }

    friend bool operator<(const move_only_int& left, const move_only_int& right) noexcept
    {
        return left.value() < right.value();
    }

    friend bool operator==(const move_only_int& left, const move_only_int& right) noexcept
    {
        return left.value() == right.value();
    }

private:
    std::unique_ptr<std::int32_t> value_;
};

class move_only_int_codec final : public merkle_codec<move_only_int> {
public:
    [[nodiscard]] std::string_view encoding_id() const override { return "move-only-i32-v1"; }
    [[nodiscard]] merkle_bytes encode(const move_only_int& value) const override
    {
        return int32_merkle_codec{}.encode(value.value());
    }
    [[nodiscard]] move_only_int decode(const std::span<const std::byte> encoding) const override
    {
        return move_only_int{int32_merkle_codec{}.decode(encoding)};
    }
};

struct equivalent_key final {
    std::int32_t equivalence_class;
    std::string representative;
};

class equivalent_key_comparer final
    : public tools::data_structures::hamt::merkle_key_comparer<equivalent_key> {
public:
    [[nodiscard]] int compare(const equivalent_key& left, const equivalent_key& right) const override
    {
        return left.equivalence_class < right.equivalence_class
            ? -1
            : left.equivalence_class > right.equivalence_class ? 1 : 0;
    }
};

class equivalent_key_codec final : public merkle_codec<equivalent_key> {
public:
    [[nodiscard]] std::string_view encoding_id() const override
    {
        return "equivalence-class-i32-v1";
    }

    [[nodiscard]] merkle_bytes encode(const equivalent_key& value) const override
    {
        return int32_merkle_codec{}.encode(value.equivalence_class);
    }

    [[nodiscard]] equivalent_key decode(const std::span<const std::byte> encoding) const override
    {
        return equivalent_key{int32_merkle_codec{}.decode(encoding), "decoded"};
    }
};

TEST(MoveOnlyKeysAndValuesExposeStableSharedRepresentatives)
{
    using tree_type = merkle_search_tree<move_only_int, move_only_int>;
    const auto policy = tree_type::policy_type::natural(
        "move-only-map-v1",
        std::make_shared<move_only_int_codec>(),
        std::make_shared<move_only_int_codec>());
    auto items = std::vector<std::pair<move_only_int, move_only_int>>{};
    items.emplace_back(move_only_int{2}, move_only_int{20});
    items.emplace_back(move_only_int{1}, move_only_int{10});
    items.emplace_back(move_only_int{3}, move_only_int{30});
    const auto tree = tree_type::create_range(std::move(items), policy);
    CHECK_EQ(std::size_t{3}, tree.size());

    const auto query = move_only_int{2};
    const auto* entry = tree.get_entry(query);
    CHECK(entry != nullptr);
    const auto key_handle = entry->key_handle();
    const auto value_handle = entry->value_handle();
    CHECK_EQ(std::int32_t{2}, key_handle->value());
    CHECK_EQ(std::int32_t{20}, value_handle->value());

    const auto same = tree.set_item(move_only_int{2}, move_only_int{20});
    CHECK(same.shares_root_with(tree));
    const auto updated = tree.set_item(move_only_int{2}, move_only_int{200});
    const auto* updated_entry = updated.get_entry(query);
    CHECK(updated_entry != nullptr);
    CHECK(updated_entry->key_handle() == key_handle);
    CHECK(updated_entry->value_handle() != value_handle);
    CHECK_EQ(std::int32_t{20}, value_handle->value());
    CHECK_EQ(std::int32_t{200}, updated_entry->value().value());
    CHECK_EQ(std::size_t{1}, tree.diff(updated).size());
    CHECK_EQ(std::size_t{3}, updated.validate_structure().count);

    using representative_tree = merkle_search_tree<equivalent_key, std::int32_t>;
    const auto representative_policy = representative_tree::policy_type::create(
        "equivalent-representatives-v1",
        std::make_shared<equivalent_key_comparer>(),
        std::make_shared<equivalent_key_codec>(),
        std::make_shared<int32_merkle_codec>());
    auto representative_items = std::vector<std::pair<equivalent_key, std::int32_t>>{};
    representative_items.emplace_back(equivalent_key{1, "first"}, 10);
    representative_items.emplace_back(equivalent_key{1, "second"}, 20);
    representative_items.emplace_back(equivalent_key{2, "other"}, 30);
    const auto representatives = representative_tree::create_range(
        std::move(representative_items),
        representative_policy);
    const auto alternate = equivalent_key{1, "alternate"};
    const auto* retained = representatives.get_entry(alternate);
    CHECK(retained != nullptr);
    CHECK_EQ(std::string{"first"}, retained->key().representative);
    CHECK_EQ(std::int32_t{20}, retained->value());
    const auto retained_key_handle = retained->key_handle();
    const auto representative_updated = representatives.set_item(
        equivalent_key{1, "replacement"},
        99);
    const auto* after_update = representative_updated.get_entry(alternate);
    CHECK(after_update != nullptr);
    CHECK(after_update->key_handle() == retained_key_handle);
    CHECK_EQ(std::string{"first"}, after_update->key().representative);
    CHECK_EQ(std::int32_t{99}, after_update->value());
    CHECK(representative_updated
        .set_item(equivalent_key{1, "ignored"}, 99)
        .shares_root_with(representative_updated));
    CHECK_EQ(std::size_t{2}, representative_updated.validate_structure().count);
}

TEST(RandomizedPersistentHistoriesMatchOrderedMapAndRetainedSnapshots)
{
    auto random = std::mt19937{0x4d535432u};
    auto operation = std::uniform_int_distribution<int>{0, 4};
    auto key_distribution = std::uniform_int_distribution<std::int32_t>{-200, 600};
    auto value_distribution = std::uniform_int_distribution<std::int32_t>{-100000, 100000};
    auto tree = int_tree::create(make_int_policy("random-model-v1"));
    auto model = std::map<std::int32_t, std::int32_t>{};
    auto snapshots = std::vector<std::pair<int_tree, std::map<std::int32_t, std::int32_t>>>{};

    for (auto step = 0; step != 12000; ++step) {
        const auto key = key_distribution(random);
        const auto value = value_distribution(random);
        switch (operation(random)) {
        case 0:
        case 1:
            tree = tree.set_item(key, value);
            model[key] = value;
            break;
        case 2:
            tree = tree.remove(key);
            model.erase(key);
            break;
        case 3:
            CHECK_EQ(model.contains(key), tree.contains_key(key));
            if (const auto item = model.find(key); item != model.end()) {
                CHECK_EQ(item->second, tree.at(key));
            } else {
                CHECK_THROWS_AS(tree.at(key), std::out_of_range);
            }
            break;
        default:
            if (!model.empty()) {
                const auto minimum = key_distribution(random);
                const auto maximum = key_distribution(random);
                const auto low = (std::min)(minimum, maximum);
                const auto high = (std::max)(minimum, maximum);
                const auto range = tree.enumerate_range(low, high);
                auto expected = model.lower_bound(low);
                for (const auto& entry : range) {
                    CHECK(expected != model.end());
                    CHECK(expected->first <= high);
                    CHECK_EQ(expected->first, entry.key());
                    CHECK_EQ(expected->second, entry.value());
                    ++expected;
                }
                CHECK(expected == model.end() || expected->first > high);
            }
            break;
        }

        if (step % 173 == 0) {
            assert_matches(model, tree);
            CHECK_EQ(tree.size(), tree.validate_structure().count);
        }
        if (step % 997 == 0) {
            snapshots.emplace_back(tree, model);
        }
    }

    assert_matches(model, tree);
    CHECK_EQ(tree.size(), tree.validate_structure().count);
    for (const auto& [snapshot, snapshot_model] : snapshots) {
        assert_matches(snapshot_model, snapshot);
        CHECK_EQ(snapshot.size(), snapshot.validate_structure().count);
    }

    auto shuffled_items = std::vector<std::pair<std::int32_t, std::int32_t>>{
        model.begin(), model.end()};
    std::shuffle(shuffled_items.begin(), shuffled_items.end(), random);
    const auto rebuilt = int_tree::create_range(std::move(shuffled_items), make_int_policy("random-model-v1"));
    CHECK(tree.content_equals(rebuilt));
    CHECK(tree.map_equals(rebuilt));
}

struct failure_control final {
    bool throw_on_compare = false;
    bool throw_on_key_encode = false;
    bool throw_on_value_encode = false;
};

struct failure_key final {
    std::int32_t value;
};

struct failure_value final {
    std::int32_t value;
};

class throwing_comparer final
    : public tools::data_structures::hamt::merkle_key_comparer<failure_key> {
public:
    explicit throwing_comparer(std::shared_ptr<failure_control> control)
        : control_(std::move(control))
    {
    }

    [[nodiscard]] int compare(const failure_key& left, const failure_key& right) const override
    {
        if (control_->throw_on_compare) {
            throw std::runtime_error{"injected comparison failure"};
        }
        return left.value < right.value ? -1 : left.value > right.value ? 1 : 0;
    }

private:
    std::shared_ptr<failure_control> control_;
};

class throwing_key_codec final : public merkle_codec<failure_key> {
public:
    explicit throwing_key_codec(std::shared_ptr<failure_control> control)
        : control_(std::move(control))
    {
    }

    [[nodiscard]] std::string_view encoding_id() const override { return "throwing-key-i32-v1"; }

    [[nodiscard]] merkle_bytes encode(const failure_key& value) const override
    {
        if (control_->throw_on_key_encode) {
            throw std::runtime_error{"injected encoding failure"};
        }
        return int32_merkle_codec{}.encode(value.value);
    }

    [[nodiscard]] failure_key decode(const std::span<const std::byte> encoding) const override
    {
        return failure_key{int32_merkle_codec{}.decode(encoding)};
    }

private:
    std::shared_ptr<failure_control> control_;
};

class throwing_value_codec final : public merkle_codec<failure_value> {
public:
    explicit throwing_value_codec(std::shared_ptr<failure_control> control)
        : control_(std::move(control))
    {
    }

    [[nodiscard]] std::string_view encoding_id() const override { return "throwing-value-i32-v1"; }

    [[nodiscard]] merkle_bytes encode(const failure_value& value) const override
    {
        if (control_->throw_on_value_encode) {
            throw std::runtime_error{"injected encoding failure"};
        }
        return int32_merkle_codec{}.encode(value.value);
    }

    [[nodiscard]] failure_value decode(const std::span<const std::byte> encoding) const override
    {
        return failure_value{int32_merkle_codec{}.decode(encoding)};
    }

private:
    std::shared_ptr<failure_control> control_;
};

TEST(ThrowingComparersAndCodecsLeavePersistentSourcesUntouched)
{
    using tree_type = merkle_search_tree<failure_key, failure_value>;
    const auto control = std::make_shared<failure_control>();
    const auto policy = tree_type::policy_type::create(
        "exception-safety-v1",
        std::make_shared<throwing_comparer>(control),
        std::make_shared<throwing_key_codec>(control),
        std::make_shared<throwing_value_codec>(control));
    const auto source = tree_type::create(policy)
        .set_item(failure_key{1}, failure_value{10})
        .set_item(failure_key{3}, failure_value{30});
    const auto root = source.root_hash();
    const auto blocks = source.blocks_preorder();

    control->throw_on_compare = true;
    CHECK_THROWS_AS(source.set_item(failure_key{2}, failure_value{20}), std::runtime_error);
    CHECK_THROWS_AS(source.remove(failure_key{1}), std::runtime_error);
    control->throw_on_compare = false;
    CHECK_EQ(root, source.root_hash());
    CHECK_EQ(std::size_t{2}, source.size());

    control->throw_on_key_encode = true;
    CHECK_THROWS_AS(source.set_item(failure_key{2}, failure_value{20}), std::runtime_error);
    control->throw_on_key_encode = false;
    control->throw_on_value_encode = true;
    CHECK_THROWS_AS(source.set_item(failure_key{1}, failure_value{11}), std::runtime_error);
    CHECK_THROWS_AS(source.set_item(failure_key{2}, failure_value{20}), std::runtime_error);
    control->throw_on_value_encode = false;

    CHECK_EQ(root, source.root_hash());
    CHECK_EQ(std::int32_t{10}, source.at(failure_key{1}).value);
    CHECK_EQ(std::int32_t{30}, source.at(failure_key{3}).value);
    const auto after_blocks = source.blocks_preorder();
    CHECK_EQ(blocks.size(), after_blocks.size());
    for (auto index = std::size_t{0}; index != blocks.size(); ++index) {
        CHECK(blocks[index].bytes == after_blocks[index].bytes);
        CHECK_EQ(blocks[index].digest, after_blocks[index].digest);
    }
    CHECK_EQ(source.size(), source.validate_structure().count);

    const auto relation_target = source.set_item(failure_key{1}, failure_value{11});
    const auto relation_source_root = source.root_hash();
    const auto relation_target_root = relation_target.root_hash();
    const auto relation_source_blocks = source.blocks_preorder();
    const auto relation_target_blocks = relation_target.blocks_preorder();
    const auto throwing_relation = [](const failure_value&, const failure_value&) -> bool {
        throw std::runtime_error{"injected value-relation failure"};
    };
    CHECK_THROWS_AS(source.map_equals(relation_target, throwing_relation), std::runtime_error);
    CHECK_THROWS_AS(source.diff(relation_target, throwing_relation), std::runtime_error);
    CHECK_EQ(relation_source_root, source.root_hash());
    CHECK_EQ(relation_target_root, relation_target.root_hash());
    const auto source_blocks_after_relation = source.blocks_preorder();
    const auto target_blocks_after_relation = relation_target.blocks_preorder();
    CHECK_EQ(relation_source_blocks.size(), source_blocks_after_relation.size());
    CHECK_EQ(relation_target_blocks.size(), target_blocks_after_relation.size());
    for (auto index = std::size_t{0}; index != relation_source_blocks.size(); ++index) {
        CHECK(relation_source_blocks[index].bytes == source_blocks_after_relation[index].bytes);
    }
    for (auto index = std::size_t{0}; index != relation_target_blocks.size(); ++index) {
        CHECK(relation_target_blocks[index].bytes == target_blocks_after_relation[index].bytes);
    }
}

struct mutable_value final {
    mutable std::int32_t payload;
};

class mutable_value_codec final : public merkle_codec<mutable_value> {
public:
    [[nodiscard]] std::string_view encoding_id() const override { return "mutable-i32-v1"; }
    [[nodiscard]] merkle_bytes encode(const mutable_value& value) const override
    {
        return int32_merkle_codec{}.encode(value.payload);
    }
    [[nodiscard]] mutable_value decode(const std::span<const std::byte> encoding) const override
    {
        return mutable_value{int32_merkle_codec{}.decode(encoding)};
    }
};

TEST(ValidatorDetectsMutationBehindAConstRepresentative)
{
    using tree_type = merkle_search_tree<std::int32_t, mutable_value>;
    const auto policy = tree_type::policy_type::natural(
        "validator-mutation-v1",
        std::make_shared<int32_merkle_codec>(),
        std::make_shared<mutable_value_codec>());
    const auto tree = tree_type::create(policy)
        .set_item(1, mutable_value{10})
        .set_item(2, mutable_value{20});
    CHECK_EQ(std::size_t{2}, tree.validate_structure().count);
    const auto hash_before = tree.root_hash();
    tree.at(2).payload = 999;
    CHECK_EQ(hash_before, tree.root_hash());
    CHECK_THROWS_AS(tree.validate_structure(), merkle_tree_invariant_error);
}

TEST(ConcurrentReadersObserveConsistentRetainedMerkleSnapshots)
{
    auto tree = int_tree::create(make_int_policy("concurrent-read-v1"));
    for (auto key = std::int32_t{0}; key != 512; ++key) {
        tree = tree.set_item(key, key * 7 - 1000);
    }
    const auto expected_root = tree.root_hash();
    const auto expected_blocks = tree.block_count();
    auto failures = std::atomic<int>{0};
    auto failure_messages = std::vector<std::string>{};
    auto failure_mutex = std::mutex{};
    auto threads = std::vector<std::thread>{};
    threads.reserve(8);

    for (auto worker = 0; worker != 8; ++worker) {
        threads.emplace_back([&tree, &expected_root, expected_blocks, &failures, &failure_messages, &failure_mutex] {
            try {
                for (auto pass = 0; pass != 128; ++pass) {
                    CHECK_EQ(std::size_t{512}, tree.size());
                    CHECK_EQ(expected_root, tree.root_hash());
                    CHECK_EQ(expected_blocks, tree.block_count());
                    for (auto key = std::int32_t{0}; key < 512; key += 17) {
                        CHECK_EQ(key * 7 - 1000, tree.at(key));
                    }
                    auto count = std::size_t{0};
                    auto previous = std::int32_t{-1};
                    for (const auto& entry : tree) {
                        CHECK(entry.key() > previous);
                        CHECK_EQ(entry.key() * 7 - 1000, entry.value());
                        previous = entry.key();
                        ++count;
                    }
                    CHECK_EQ(std::size_t{512}, count);
                    const auto range = tree.enumerate_range(200, 215);
                    CHECK_EQ(std::size_t{16}, range.size());
                    CHECK_EQ(std::int32_t{200}, range.front().key());
                    CHECK_EQ(std::int32_t{215}, range.back().key());
                }
            } catch (const std::exception& ex) {
                {
                    const auto lock = std::lock_guard<std::mutex>{failure_mutex};
                    failure_messages.push_back(ex.what());
                }
                failures.fetch_add(1, std::memory_order_relaxed);
            } catch (...) {
                {
                    const auto lock = std::lock_guard<std::mutex>{failure_mutex};
                    failure_messages.push_back("non-standard exception");
                }
                failures.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }
    if (failures.load(std::memory_order_relaxed) != 0) {
        auto message = std::string{};
        {
            const auto lock = std::lock_guard<std::mutex>{failure_mutex};
            message = failure_messages.empty() ? "reader failed" : failure_messages.front();
        }
        fail_message(__FILE__, __LINE__, message);
    }
    CHECK_EQ(tree.size(), tree.validate_structure().count);
}

} // namespace

int main()
{
    if (!tds_enter_headless_test_process()) {
        return EXIT_FAILURE;
    }

    auto failed = 0;
    for (const auto& test : registry()) {
        try {
            test.run();
            std::cout << "[PASS] " << test.name << '\n';
        } catch (const std::exception& ex) {
            ++failed;
            std::cerr << "[FAIL] " << test.name << ": " << ex.what() << '\n';
        } catch (...) {
            ++failed;
            std::cerr << "[FAIL] " << test.name << ": non-standard exception\n";
        }
    }

    if (failed != 0) {
        std::cerr << failed << " test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << registry().size() << " test(s) passed\n";
    return EXIT_SUCCESS;
}
