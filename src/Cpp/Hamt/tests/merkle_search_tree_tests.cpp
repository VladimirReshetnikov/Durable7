/// Conformance tests for the Merkle search tree's wire format and verification rules.
///
/// Digests, block bytes, and proof encodings are checked against fixed expected values, since the
/// point of the format is that independent implementations agree on them exactly.

#include <durable7/hamt/merkle_proofs.hpp>
#include <durable7/test_support/headless_test_process.h>

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

using durable7::hamt::int32_merkle_codec;
using durable7::hamt::int64_merkle_codec;
using durable7::hamt::create_merkle_proof;
using durable7::hamt::create_merkle_range_proof;
using durable7::hamt::create_merkle_sync_pack;
using durable7::hamt::export_merkle_pack;
using durable7::hamt::import_merkle_pack;
using durable7::hamt::in_memory_merkle_block_store;
using durable7::hamt::load_merkle_tree;
using durable7::hamt::merkle_bytes;
using durable7::hamt::merkle_block;
using durable7::hamt::merkle_block_pack;
using durable7::hamt::merkle_block_store;
using durable7::hamt::merkle_codec;
using durable7::hamt::merkle_codec_error;
using durable7::hamt::merkle_digest;
using durable7::hamt::merkle_map_difference_kind;
using durable7::hamt::merkle_policy_mismatch;
using durable7::hamt::merkle_proof;
using durable7::hamt::merkle_proof_kind;
using durable7::hamt::merkle_proof_step;
using durable7::hamt::merkle_range_error;
using durable7::hamt::merkle_search_tree;
using durable7::hamt::merkle_search_tree_policy;
using durable7::hamt::merkle_tree_invariant_error;
using durable7::hamt::merkle_verification_budget;
using durable7::hamt::merkle_verification_error;
using durable7::hamt::merkle_verification_failure_kind;
using durable7::hamt::merge_merkle_trees;
using durable7::hamt::nullable_bytes_merkle_codec;
using durable7::hamt::nullable_utf8_merkle_codec;
using durable7::hamt::rfc4122_guid;
using durable7::hamt::rfc4122_guid_merkle_codec;
using durable7::hamt::save_merkle_tree;
using durable7::hamt::verify_merkle_proof;

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

[[nodiscard]] int_tree::policy_type make_int_policy(std::string policy_id);
[[nodiscard]] string_tree::policy_type make_string_policy(std::string policy_id);

TEST(MerkleCursorNavigatesRanksAndPublishesCanonicalEdits)
{
    const auto source = string_tree::create_range(
        {
            {-10, std::optional<std::string>{"a"}},
            {0, std::nullopt},
            {10, std::optional<std::string>{"c"}},
        },
        make_string_policy("cpp-cursor-v1"));
    const auto keys = std::vector<std::int32_t>{-10, 0, 10};
    for (auto position = std::size_t{0}; position <= source.size(); ++position) {
        const auto cursor = source.get_cursor(position);
        CHECK_EQ(position, cursor.position());
        CHECK_EQ(position == 0, cursor.is_at_start());
        CHECK_EQ(position == source.size(), cursor.is_at_end());
        CHECK(cursor.snapshot().shares_root_with(source));
        CHECK_EQ(position == 0, cursor.peek_previous() == nullptr);
        if (position != 0) {
            CHECK_EQ(keys[position - 1], cursor.peek_previous()->key());
        }
        CHECK_EQ(position == source.size(), cursor.peek_next() == nullptr);
        if (position != source.size()) {
            CHECK_EQ(keys[position], cursor.peek_next()->key());
        }
    }

    CHECK_EQ(std::size_t{1}, source.get_cursor_lower_bound(-5).position());
    CHECK_EQ(std::size_t{2}, source.get_cursor_upper_bound(0).position());
    const auto exact = source.get_cursor_at_key(0);
    CHECK(exact.found);
    CHECK_EQ(std::size_t{1}, exact.cursor.position());
    const auto miss = source.get_cursor_at_key(5);
    CHECK(!miss.found);
    CHECK_EQ(std::size_t{2}, miss.cursor.position());

    const auto no_op = exact.cursor.set_next_value(std::nullopt);
    CHECK(no_op.snapshot().shares_root_with(source));
    const auto changed = exact.cursor
        .set_next_value(std::optional<std::string>{"b"})
        .snapshot();
    CHECK_EQ(std::optional<std::string>{"b"}, changed.at(0));
    CHECK(changed.root_hash() != source.root_hash());
    CHECK(changed.shares_policy_with(source));
    CHECK(!source.at(0).has_value());

    const auto inserted = source
        .get_cursor_lower_bound(5)
        .insert(5, std::optional<std::string>{"five"});
    CHECK_EQ(std::size_t{3}, inserted.position());
    auto inserted_keys = std::vector<std::int32_t>{};
    for (const auto& entry : inserted.snapshot()) {
        inserted_keys.push_back(entry.key());
    }
    CHECK(inserted_keys == std::vector<std::int32_t>({-10, 0, 5, 10}));
    CHECK_EQ(source.root_hash(), inserted.delete_previous().snapshot().root_hash());
    CHECK_THROWS_AS(source.get_cursor(4), std::out_of_range);
    CHECK_THROWS_AS(source.get_cursor().move_previous(), std::logic_error);
    CHECK_THROWS_AS(source.get_cursor_at_end().move_next(), std::logic_error);
    CHECK_THROWS_AS(exact.cursor.insert(0, std::optional<std::string>{"duplicate"}), std::invalid_argument);
    CHECK_THROWS_AS(source.get_cursor().insert(5, std::optional<std::string>{"wrong gap"}), std::invalid_argument);
}

TEST(MerkleCursorMovedFromRemainsAValidVersionHandle)
{
    const auto source = string_tree::create_range(
        {
            {-10, std::optional<std::string>{"a"}},
            {0, std::optional<std::string>{"b"}},
            {10, std::optional<std::string>{"c"}},
        },
        make_string_policy("cpp-cursor-moved-from-v1"));

    // A cursor is an immutable version handle, so a move copies the retained tree rather than
    // stealing it. A defaulted move would empty the tree while copying the position, leaving
    // count() at 0 with position() still 2 and the policy null, which an edit would dereference.
    auto original = source.get_cursor(2);
    const auto moved_to = std::move(original);

    CHECK_EQ(std::size_t{3}, original.count());
    CHECK_EQ(std::size_t{2}, original.position());
    CHECK(!original.is_at_end());
    CHECK_EQ(10, original.peek_next()->key());
    CHECK_EQ(0, original.peek_previous()->key());
    CHECK(original.snapshot().shares_root_with(source));

    CHECK_EQ(original.count(), moved_to.count());
    CHECK_EQ(original.position(), moved_to.position());
    CHECK(moved_to.snapshot().shares_root_with(source));

    // Editing through the moved-from cursor still reaches a live policy and tree.
    const auto edited = original.set_next_value(std::optional<std::string>{"C"});
    CHECK_EQ(std::optional<std::string>{"C"}, edited.snapshot().at(10));

    auto assign_source = source.get_cursor(1);
    auto assign_target = source.get_cursor_at_end();
    assign_target = std::move(assign_source);
    CHECK_EQ(std::size_t{1}, assign_source.position());
    CHECK_EQ(std::size_t{3}, assign_source.count());
    CHECK_EQ(0, assign_source.peek_next()->key());
    CHECK_EQ(std::size_t{1}, assign_target.position());
}

TEST(MerkleCursorResolvesEveryInRangeRankRatherThanReportingNoEntry)
{
    // A rank the cached subtree counts cannot locate is a structural-integrity failure, not an
    // absent entry. Reporting it as "no next entry" would let an authenticated collection answer
    // navigation from a broken index, so every in-range rank must resolve to a real entry and
    // only the end gap may report absence.
    auto entries = std::vector<std::pair<std::int32_t, std::optional<std::string>>>{};
    for (auto key = std::int32_t{0}; key != 256; ++key) {
        entries.emplace_back(key, std::optional<std::string>{std::to_string(key)});
    }

    const auto tree = string_tree::create_range(entries, make_string_policy("cpp-cursor-rank-v1"));
    CHECK_EQ(std::size_t{256}, tree.size());

    for (auto position = std::size_t{0}; position != tree.size(); ++position) {
        const auto cursor = tree.get_cursor(position);
        const auto* next = cursor.peek_next();
        CHECK(next != nullptr);
        CHECK_EQ(static_cast<std::int32_t>(position), next->key());
    }

    const auto end = tree.get_cursor_at_end();
    CHECK(end.peek_next() == nullptr);
    CHECK_EQ(std::int32_t{255}, end.peek_previous()->key());
}

TEST(MerkleCursorSetItemUpdatesExistingAndInsertsMissing)
{
    const auto source = string_tree::create_range(
        {
            {-10, std::optional<std::string>{"a"}},
            {0, std::optional<std::string>{"b"}},
            {10, std::optional<std::string>{"c"}},
        },
        make_string_policy("cpp-cursor-set-item-v1"));

    // Updating the exact next key keeps its rank; the edit is byte-identical to the ordinary
    // set_item on the tree, so the published root digest must match.
    const auto exact = source.get_cursor_at_key(0);
    CHECK(exact.found);
    const auto updated = exact.cursor.set_item(0, std::optional<std::string>{"B"});
    CHECK_EQ(std::size_t{1}, updated.position());
    CHECK_EQ(std::optional<std::string>{"B"}, updated.snapshot().at(0));
    CHECK_EQ(
        source.set_item(0, std::optional<std::string>{"B"}).root_hash(),
        updated.snapshot().root_hash());

    // Setting a missing key at its lower-bound gap advances past the inserted entry.
    const auto miss = source.get_cursor_at_key(5);
    CHECK(!miss.found);
    const auto inserted = miss.cursor.set_item(5, std::optional<std::string>{"five"});
    CHECK_EQ(std::size_t{3}, inserted.position());
    CHECK_EQ(std::optional<std::string>{"five"}, inserted.snapshot().at(5));
    CHECK_EQ(
        source.set_item(5, std::optional<std::string>{"five"}).root_hash(),
        inserted.snapshot().root_hash());

    // Setting the exact next key to its current value is an observable no-op that retains the
    // version.
    const auto no_op = exact.cursor.set_item(0, std::optional<std::string>{"b"});
    CHECK(no_op.snapshot().shares_root_with(source));
}

TEST(MerkleCursorCachedRanksAndBoundsMatchSortedModel)
{
    auto items = std::vector<std::pair<std::int32_t, std::int32_t>>{};
    for (auto key = std::int32_t{-500}; key <= 500; ++key) {
        if (key % 7 != 0) {
            items.emplace_back(key, key);
        }
    }
    const auto tree = int_tree::create_range(std::move(items), make_int_policy("cpp-cursor-ranks-v1"));
    auto keys = std::vector<std::int32_t>{};
    for (const auto& entry : tree) {
        keys.push_back(entry.key());
    }
    for (auto position = std::size_t{0}; position != keys.size(); ++position) {
        const auto cursor = tree.get_cursor(position);
        CHECK_EQ(keys[position], cursor.peek_next()->key());
    }
    for (auto probe = std::int32_t{-550}; probe <= 550; probe += 11) {
        const auto lower = std::lower_bound(keys.begin(), keys.end(), probe);
        const auto rank = static_cast<std::size_t>(lower - keys.begin());
        const auto found = lower != keys.end() && *lower == probe;
        CHECK_EQ(rank, tree.get_cursor_lower_bound(probe).position());
        CHECK_EQ(rank + (found ? 1u : 0u), tree.get_cursor_upper_bound(probe).position());
        const auto searched = tree.get_cursor_at_key(probe);
        CHECK_EQ(rank, searched.cursor.position());
        CHECK_EQ(found, searched.found);
    }
}

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

[[nodiscard]] string_tree make_persistence_tree(
    const string_tree::policy_type& policy,
    const std::int32_t count)
{
    auto items = std::vector<std::pair<std::int32_t, std::optional<std::string>>>{};
    items.reserve(static_cast<std::size_t>(count));
    const auto first = -(count / 2);
    for (auto key = first; key != first + count; ++key) {
        items.emplace_back(
            key,
            key % 29 == 0
                ? std::nullopt
                : std::optional<std::string>{"value:" + std::to_string(key)});
    }
    return string_tree::create_range(std::move(items), policy);
}

template <class Action>
merkle_verification_error expect_verification_failure(
    const merkle_verification_failure_kind expected,
    Action&& action)
{
    try {
        std::invoke(std::forward<Action>(action));
    } catch (const merkle_verification_error& error) {
        if (expected != error.kind()) {
            fail_message(
                __FILE__,
                __LINE__,
                "expected verification kind "
                    + std::to_string(static_cast<int>(expected))
                    + ", got " + std::to_string(static_cast<int>(error.kind()))
                    + ": " + error.what());
        }
        CHECK(std::string_view{error.what()}.size() > 0);
        return error;
    }
    fail(__FILE__, __LINE__, "operation throws merkle_verification_error");
}

class injected_merkle_block_store final : public merkle_block_store {
public:
    void inject(merkle_block block)
    {
        blocks_.insert_or_assign(block.digest(), std::move(block));
    }
    [[nodiscard]] std::size_t put_calls() const noexcept { return put_calls_; }

    [[nodiscard]] std::size_t size() const override { return blocks_.size(); }
    [[nodiscard]] std::vector<merkle_digest> digests() const override
    {
        auto result = std::vector<merkle_digest>{};
        for (const auto& [digest, block] : blocks_) {
            (void)block;
            result.push_back(digest);
        }
        return result;
    }
    [[nodiscard]] bool contains(const merkle_digest digest) const override
    {
        return blocks_.contains(digest);
    }
    [[nodiscard]] std::optional<merkle_block> get(const merkle_digest digest) const override
    {
        const auto iterator = blocks_.find(digest);
        return iterator == blocks_.end()
            ? std::nullopt
            : std::optional<merkle_block>{iterator->second};
    }
    bool put(merkle_block block) override
    {
        ++put_calls_;
        const auto iterator = blocks_.find(block.digest());
        if (iterator != blocks_.end()) {
            if (iterator->second == block) {
                return false;
            }
            throw merkle_verification_error::conflicting_block(
                block.digest(), "injected destination conflict");
        }
        blocks_.emplace(block.digest(), std::move(block));
        return true;
    }
    bool remove(const merkle_digest digest) override { return blocks_.erase(digest) != 0; }
    void clear() override { blocks_.clear(); }

private:
    std::map<merkle_digest, merkle_block> blocks_;
    std::size_t put_calls_ = 0;
};

struct preflight_key final {
    std::int32_t value;

    friend bool operator<(const preflight_key left, const preflight_key right) noexcept
    {
        return left.value < right.value;
    }
};

struct preflight_value final {
    std::int32_t value;
};

struct preflight_codec_control final {
    std::atomic<int> encode_calls{0};
    std::atomic<int> decode_calls{0};
    std::atomic<bool> bomb{false};

    void reset() noexcept
    {
        encode_calls.store(0, std::memory_order_relaxed);
        decode_calls.store(0, std::memory_order_relaxed);
    }
};

class preflight_key_codec final : public merkle_codec<preflight_key> {
public:
    explicit preflight_key_codec(std::shared_ptr<preflight_codec_control> control)
        : control_(std::move(control))
    {
    }

    [[nodiscard]] std::string_view encoding_id() const override { return "preflight-key-i32-v1"; }
    [[nodiscard]] merkle_bytes encode(const preflight_key& value) const override
    {
        control_->encode_calls.fetch_add(1, std::memory_order_relaxed);
        if (control_->bomb.load(std::memory_order_relaxed)) {
            throw std::runtime_error{"preflight key codec was invoked"};
        }
        return int32_merkle_codec{}.encode(value.value);
    }
    [[nodiscard]] preflight_key decode(const std::span<const std::byte> encoding) const override
    {
        control_->decode_calls.fetch_add(1, std::memory_order_relaxed);
        if (control_->bomb.load(std::memory_order_relaxed)) {
            throw std::runtime_error{"preflight key codec was invoked"};
        }
        return preflight_key{int32_merkle_codec{}.decode(encoding)};
    }

private:
    std::shared_ptr<preflight_codec_control> control_;
};

class preflight_value_codec final : public merkle_codec<preflight_value> {
public:
    explicit preflight_value_codec(std::shared_ptr<preflight_codec_control> control)
        : control_(std::move(control))
    {
    }

    [[nodiscard]] std::string_view encoding_id() const override { return "preflight-value-i32-v1"; }
    [[nodiscard]] merkle_bytes encode(const preflight_value& value) const override
    {
        control_->encode_calls.fetch_add(1, std::memory_order_relaxed);
        if (control_->bomb.load(std::memory_order_relaxed)) {
            throw std::runtime_error{"preflight value codec was invoked"};
        }
        return int32_merkle_codec{}.encode(value.value);
    }
    [[nodiscard]] preflight_value decode(const std::span<const std::byte> encoding) const override
    {
        control_->decode_calls.fetch_add(1, std::memory_order_relaxed);
        if (control_->bomb.load(std::memory_order_relaxed)) {
            throw std::runtime_error{"preflight value codec was invoked"};
        }
        return preflight_value{int32_merkle_codec{}.decode(encoding)};
    }

private:
    std::shared_ptr<preflight_codec_control> control_;
};

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
    CHECK(empty.validate_structure() == durable7::hamt::merkle_search_tree_statistics{});
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

TEST(WideMultiLevelMst2GoldenMatchesEverySiblingPort)
{
    constexpr auto expected_root_block_hex = std::string_view{
        "4d53543201eb6b2bada16d3464d24f5b4b3d54bb5bca33f00d88164de27e95c920c2a1b917"
        "020000000e00000002000000040000003b00000004ffffffc400000004000001d000000004fffffe2f"
        "790b862e0ef81c9e6debdf38c1099c565887fe87aed84f26dfba736de256d4d5"
        "018b1ddc596548b5389c9523ed8ddc027d166d82540611be117f8452a685a608"
        "018b1ddc596548b5389c9523ed8ddc027d166d82540611be117f8452a685a608"};
    const auto policy = make_int_policy("golden-wide-i32-i32-v1");
    const auto keys = std::array<std::int32_t, 14>{0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 38, 44, 59, 464};
    auto items = std::vector<std::pair<std::int32_t, std::int32_t>>{};
    items.reserve(keys.size());
    for (const auto key : keys) {
        items.emplace_back(key, -key - 1);
    }

    const auto tree = int_tree::create_range(std::move(items), policy);
    CHECK_EQ(
        std::string{"eb6b2bada16d3464d24f5b4b3d54bb5bca33f00d88164de27e95c920c2a1b917"},
        policy.domain_digest().to_hex());
    CHECK_EQ(
        std::string{"9afd7ba98ec91f72074c5f2c272ca1334244fb43a631e0fb440e02799eee8755"},
        tree.root_hash().to_hex());
    const auto blocks = tree.blocks_preorder();
    CHECK_EQ(std::size_t{4}, blocks.size());
    CHECK_EQ(tree.root_hash(), blocks.front().digest);
    CHECK_EQ(std::byte{2}, blocks.front().bytes->at(37));
    CHECK_EQ(parse_hex(expected_root_block_hex), *blocks.front().bytes);

    const auto statistics = tree.validate_structure();
    CHECK_EQ(std::size_t{14}, statistics.count);
    CHECK_EQ(std::size_t{4}, statistics.block_count);
    CHECK_EQ(std::size_t{3}, statistics.height);
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
    : public durable7::hamt::merkle_key_comparer<equivalent_key> {
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
    : public durable7::hamt::merkle_key_comparer<failure_key> {
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

TEST(PersistenceGoldenPackAndMsp2QueriesMatchEverySiblingPort)
{
    const auto policy = make_string_policy("golden-int-string-v1");
    const auto tree = string_tree::create(policy)
        .set_item(42, std::optional<std::string>{"forty-two"});
    const auto pack = export_merkle_pack(tree);
    CHECK_EQ(std::size_t{1}, pack.block_count());
    CHECK(pack.contains_root_block());
    CHECK_EQ(tree.root_hash(), pack.blocks().front().digest());
    CHECK_EQ(
        parse_hex(
            "4d53543201fe140762a080abb39de83f70e7505c8b94c4baa428eea76d468a0f3163bc56c2"
            "000000000100000001000000040000002a0000000a01666f7274792d74776f"
            "98900ab6355e8ea553b5cd087d6ec4b976dc3e0953e35c6f46bc756ac75ddcb3"
            "98900ab6355e8ea553b5cd087d6ec4b976dc3e0953e35c6f46bc756ac75ddcb3"),
        pack.blocks().front().to_bytes());

    const auto membership = create_merkle_proof(tree, std::int32_t{42});
    const auto nonmembership = create_merkle_proof(tree, std::int32_t{43});
    const auto range = create_merkle_range_proof(tree, std::int32_t{40}, std::int32_t{44});
    const auto membership_query = merkle_bytes{
        membership.query().begin(), membership.query().end()};
    const auto nonmembership_query = merkle_bytes{
        nonmembership.query().begin(), nonmembership.query().end()};
    const auto range_query = merkle_bytes{range.query().begin(), range.query().end()};
    CHECK_EQ(
        parse_hex("4d53503200000000040000002a0000000a01666f7274792d74776f"),
        membership_query);
    CHECK_EQ(
        parse_hex("4d53503201000000040000002b"),
        nonmembership_query);
    CHECK_EQ(
        parse_hex("4d535032020000000400000028000000040000002c"),
        range_query);
    CHECK(verify_merkle_proof(membership, policy).valid());
    CHECK(verify_merkle_proof(nonmembership, policy).valid());
    CHECK(verify_merkle_proof(range, policy).valid());
}

TEST(PersistenceSaveLoadImportAndPartialOverlayRoundTripExactClosure)
{
    const auto policy = make_string_policy("persistence-algorithms-test-v1");
    const auto tree = make_persistence_tree(policy, 513);
    const auto pack = export_merkle_pack(tree);
    CHECK(tree.block_count() > 2);
    CHECK_EQ(tree.block_count(), pack.block_count());
    CHECK_EQ(pack, export_merkle_pack(tree));

    auto store = in_memory_merkle_block_store{};
    CHECK_EQ(tree.block_count(), save_merkle_tree(tree, store));
    CHECK_EQ(std::size_t{0}, save_merkle_tree(tree, store));
    const auto loaded = load_merkle_tree(tree.root_hash(), policy, store);
    CHECK(tree.content_equals(loaded));
    CHECK(tree.map_equals(loaded));
    CHECK_EQ(tree.validate_structure(), loaded.validate_structure());

    auto imported_store = in_memory_merkle_block_store{};
    const auto imported = import_merkle_pack(pack, policy, &imported_store);
    CHECK(tree.content_equals(imported));
    CHECK_EQ(tree.block_count(), imported_store.size());
    CHECK_EQ(pack, export_merkle_pack(imported));

    const auto root_digest = tree.root_hash();
    const auto root_only = export_merkle_pack(
        tree, std::span<const merkle_digest>{&root_digest, 1});
    CHECK(imported_store.remove(root_digest));
    const auto from_partial = import_merkle_pack(root_only, policy, &imported_store);
    CHECK(tree.content_equals(from_partial));
    CHECK(imported_store.contains(root_digest));

    const auto empty = string_tree::create(policy);
    const auto empty_import = import_merkle_pack(export_merkle_pack(empty), policy);
    CHECK(empty.content_equals(empty_import));
    const auto empty_load = load_merkle_tree(
        empty.root_hash(), policy, in_memory_merkle_block_store{});
    CHECK(empty.content_equals(empty_load));
}

TEST(PersistenceRejectsMissingTamperedMalformedForeignAndCountCorruption)
{
    const auto policy = make_string_policy("persistence-rejection-v1");
    const auto tree = make_persistence_tree(policy, 257);
    const auto pack = export_merkle_pack(tree);
    auto missing_blocks = std::vector<merkle_block>{pack.blocks().begin(), pack.blocks().end()};
    const auto missing_digest = missing_blocks.back().digest();
    missing_blocks.pop_back();
    const auto incomplete = merkle_block_pack{
        pack.algorithm_id(), pack.domain_digest(), pack.root_hash(), std::move(missing_blocks)};
    expect_verification_failure(merkle_verification_failure_kind::missing_block, [&] {
        (void)import_merkle_pack(incomplete, policy);
    });

    const auto root_iterator = std::find_if(
        pack.blocks().begin(), pack.blocks().end(), [&](const auto& block) {
            return block.digest() == pack.root_hash();
        });
    CHECK(root_iterator != pack.blocks().end());
    auto changed_bytes = root_iterator->to_bytes();
    changed_bytes.back() ^= std::byte{0x80};
    auto changed_blocks = std::vector<merkle_block>{pack.blocks().begin(), pack.blocks().end()};
    *std::find_if(changed_blocks.begin(), changed_blocks.end(), [&](const auto& block) {
        return block.digest() == pack.root_hash();
    }) = merkle_block{pack.root_hash(), std::move(changed_bytes)};
    const auto changed_pack = merkle_block_pack{
        pack.algorithm_id(), pack.domain_digest(), pack.root_hash(), std::move(changed_blocks)};
    expect_verification_failure(merkle_verification_failure_kind::digest_mismatch, [&] {
        (void)import_merkle_pack(changed_pack, policy);
    });

    auto wrong_magic_bytes = root_iterator->to_bytes();
    wrong_magic_bytes.front() ^= std::byte{0xff};
    const auto wrong_magic_digest = merkle_digest::hash(wrong_magic_bytes);
    const auto wrong_magic = merkle_block{
        wrong_magic_digest, std::move(wrong_magic_bytes)};
    const auto malformed = merkle_block_pack{
        pack.algorithm_id(), pack.domain_digest(), wrong_magic.digest(), {wrong_magic}};
    expect_verification_failure(merkle_verification_failure_kind::malformed_block, [&] {
        (void)import_merkle_pack(malformed, policy);
    });

    auto trailing_bytes = root_iterator->to_bytes();
    trailing_bytes.push_back(std::byte{0});
    const auto trailing_digest = merkle_digest::hash(trailing_bytes);
    const auto trailing = merkle_block{
        trailing_digest, std::move(trailing_bytes)};
    const auto noncanonical = merkle_block_pack{
        pack.algorithm_id(), pack.domain_digest(), trailing.digest(), {trailing}};
    expect_verification_failure(merkle_verification_failure_kind::non_canonical_block, [&] {
        (void)import_merkle_pack(noncanonical, policy);
    });

    auto count_bytes = root_iterator->to_bytes();
    CHECK(count_bytes.size() > 41);
    count_bytes[41] ^= std::byte{1};
    const auto bad_count_digest = merkle_digest::hash(count_bytes);
    const auto bad_count = merkle_block{
        bad_count_digest, std::move(count_bytes)};
    auto count_blocks = std::vector<merkle_block>{pack.blocks().begin(), pack.blocks().end()};
    *std::find_if(count_blocks.begin(), count_blocks.end(), [&](const auto& block) {
        return block.digest() == pack.root_hash();
    }) = bad_count;
    const auto count_pack = merkle_block_pack{
        pack.algorithm_id(), pack.domain_digest(), bad_count.digest(), std::move(count_blocks)};
    expect_verification_failure(merkle_verification_failure_kind::invalid_reference, [&] {
        (void)import_merkle_pack(count_pack, policy);
    });

    const auto wide_tree = make_persistence_tree(policy, 2049);
    const auto wide_pack = export_merkle_pack(wide_tree);
    const auto wide_root = std::find_if(
        wide_pack.blocks().begin(), wide_pack.blocks().end(), [&](const auto& block) {
            return block.digest() == wide_pack.root_hash();
        });
    CHECK(wide_root != wide_pack.blocks().end());
    auto swapped_bytes = wide_root->to_bytes();
    CHECK(swapped_bytes.size() > 46);
    const auto entry_count =
        (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(swapped_bytes[42])) << 24)
        | (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(swapped_bytes[43])) << 16)
        | (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(swapped_bytes[44])) << 8)
        | static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(swapped_bytes[45]));
    const auto child_count = static_cast<std::size_t>(entry_count) + 1;
    CHECK(child_count <= swapped_bytes.size() / merkle_digest::byte_length);
    const auto child_offset = swapped_bytes.size() - child_count * merkle_digest::byte_length;
    auto nonempty_children = std::vector<std::size_t>{};
    const auto empty_digest = policy.empty_digest();
    for (auto index = std::size_t{0}; index != child_count; ++index) {
        const auto begin = swapped_bytes.begin()
            + static_cast<std::ptrdiff_t>(child_offset + index * merkle_digest::byte_length);
        if (!std::equal(
                begin,
                begin + static_cast<std::ptrdiff_t>(merkle_digest::byte_length),
                empty_digest.bytes().begin())) {
            nonempty_children.push_back(index);
        }
    }
    CHECK(nonempty_children.size() >= 2);
    const auto first_child = swapped_bytes.begin() + static_cast<std::ptrdiff_t>(
        child_offset + nonempty_children[0] * merkle_digest::byte_length);
    const auto second_child = swapped_bytes.begin() + static_cast<std::ptrdiff_t>(
        child_offset + nonempty_children[1] * merkle_digest::byte_length);
    std::swap_ranges(
        first_child,
        first_child + static_cast<std::ptrdiff_t>(merkle_digest::byte_length),
        second_child);
    const auto swapped_root_digest = merkle_digest::hash(swapped_bytes);
    auto swapped_blocks = std::vector<merkle_block>{
        wide_pack.blocks().begin(), wide_pack.blocks().end()};
    *std::find_if(swapped_blocks.begin(), swapped_blocks.end(), [&](const auto& block) {
        return block.digest() == wide_pack.root_hash();
    }) = merkle_block{swapped_root_digest, std::move(swapped_bytes)};
    const auto swapped_pack = merkle_block_pack{
        wide_pack.algorithm_id(),
        wide_pack.domain_digest(),
        swapped_root_digest,
        std::move(swapped_blocks)};
    expect_verification_failure(merkle_verification_failure_kind::invalid_reference, [&] {
        (void)import_merkle_pack(swapped_pack, policy);
    });

    const auto foreign_policy = make_string_policy("foreign-persistence-v1");
    expect_verification_failure(merkle_verification_failure_kind::domain_mismatch, [&] {
        (void)import_merkle_pack(pack, foreign_policy);
    });
    const auto unsupported = merkle_block_pack{
        "mst-sha256-b16-v999",
        pack.domain_digest(),
        pack.root_hash(),
        {pack.blocks().begin(), pack.blocks().end()}};
    expect_verification_failure(merkle_verification_failure_kind::unsupported_algorithm, [&] {
        (void)import_merkle_pack(unsupported, policy);
    });

    auto conflicting_bytes = pack.blocks().front().to_bytes();
    conflicting_bytes.back() ^= std::byte{0x20};
    auto conflicting_store = injected_merkle_block_store{};
    conflicting_store.inject(merkle_block{
        pack.blocks().front().digest(), std::move(conflicting_bytes)});
    expect_verification_failure(merkle_verification_failure_kind::conflicting_block, [&] {
        (void)import_merkle_pack(pack, policy, &conflicting_store);
    });
    CHECK_EQ(std::size_t{0}, conflicting_store.put_calls());
    expect_verification_failure(merkle_verification_failure_kind::conflicting_block, [&] {
        (void)save_merkle_tree(tree, conflicting_store);
    });
    CHECK_EQ(std::size_t{0}, conflicting_store.put_calls());

    auto missing_store = in_memory_merkle_block_store{};
    (void)save_merkle_tree(tree, missing_store);
    CHECK(missing_store.remove(missing_digest));
    expect_verification_failure(merkle_verification_failure_kind::missing_block, [&] {
        (void)load_merkle_tree(tree.root_hash(), policy, missing_store);
    });
}

TEST(PersistenceSevenBudgetsAndProofShapeArePreflightedStrictly)
{
    const auto policy = make_string_policy("persistence-budget-v1");
    const auto tree = make_persistence_tree(policy, 513);
    const auto pack = export_merkle_pack(tree);
    const auto root = std::find_if(pack.blocks().begin(), pack.blocks().end(), [&](const auto& block) {
        return block.digest() == pack.root_hash();
    });
    CHECK(root != pack.blocks().end());
    const auto maximum_block = std::max_element(
        pack.blocks().begin(), pack.blocks().end(), [](const auto& left, const auto& right) {
            return left.size() < right.size();
        })->size();
    const auto defaults = merkle_verification_budget{};
    const auto limits = std::array<merkle_verification_budget, 6>{
        defaults.with_max_block_count(1),
        merkle_verification_budget{
            defaults.max_block_count(), maximum_block, maximum_block,
            defaults.max_depth(), defaults.max_entry_count(),
            defaults.max_child_references_per_block(), maximum_block},
        defaults.with_max_block_byte_count(root->size() - 1),
        defaults.with_max_depth(1),
        defaults.with_max_entry_count(1),
        defaults.with_max_child_references_per_block(1)};
    for (const auto& budget : limits) {
        expect_verification_failure(
            merkle_verification_failure_kind::resource_limit_exceeded,
            [&] { (void)import_merkle_pack(pack, policy, nullptr, budget); });
    }
    CHECK_THROWS_AS(merkle_verification_budget{0}, std::invalid_argument);
    CHECK_THROWS_AS(
        (merkle_verification_budget{1, 10, 11, 1, 1, 1, 10}),
        std::invalid_argument);
    CHECK_THROWS_AS(
        (merkle_verification_budget{1, 10, 10, 1, 1, 1, 11}),
        std::invalid_argument);

    auto deep_proof = create_merkle_proof(tree, std::int32_t{0});
    if (deep_proof.steps().size() <= 1) {
        for (auto key = std::int32_t{-256}; key <= 256; ++key) {
            auto candidate = create_merkle_proof(tree, key);
            if (candidate.steps().size() > deep_proof.steps().size()) {
                deep_proof = std::move(candidate);
            }
        }
    }
    CHECK(deep_proof.steps().size() > 1);
    const auto query_budget = defaults.with_max_proof_query_byte_count(
        deep_proof.query().size() - 1);
    const auto query_result = verify_merkle_proof(deep_proof, policy, query_budget);
    CHECK(!query_result.valid());
    CHECK_EQ(merkle_verification_failure_kind::resource_limit_exceeded, query_result.failure_kind());
    CHECK_EQ(std::size_t{0}, query_result.verified_block_count());
    CHECK_EQ(std::uint64_t{0}, query_result.verified_byte_count());

    const auto step_result = verify_merkle_proof(
        deep_proof, policy, defaults.with_max_block_count(1));
    CHECK(!step_result.valid());
    CHECK_EQ(merkle_verification_failure_kind::resource_limit_exceeded, step_result.failure_kind());
    CHECK_EQ(std::size_t{0}, step_result.verified_block_count());
    CHECK_EQ(
        static_cast<std::uint64_t>(deep_proof.query().size()),
        step_result.verified_byte_count());

    auto expanded_steps = std::vector<merkle_proof_step>{
        deep_proof.steps().begin(), deep_proof.steps().end()};
    expanded_steps.front() = merkle_proof_step{expanded_steps.front().block(), {0, 1}};
    const auto expanded_proof = merkle_proof{
        deep_proof.algorithm_id(),
        deep_proof.domain_digest(),
        deep_proof.root_hash(),
        deep_proof.kind(),
        merkle_bytes{deep_proof.query().begin(), deep_proof.query().end()},
        std::move(expanded_steps)};
    const auto expansion_result = verify_merkle_proof(
        expanded_proof,
        policy,
        defaults.with_max_child_references_per_block(1));
    CHECK(!expansion_result.valid());
    CHECK_EQ(merkle_verification_failure_kind::resource_limit_exceeded, expansion_result.failure_kind());
    CHECK_EQ(std::size_t{0}, expansion_result.verified_block_count());
    CHECK_EQ(
        static_cast<std::uint64_t>(expanded_proof.query().size()),
        expansion_result.verified_byte_count());

    using audit_tree = merkle_search_tree<preflight_key, preflight_value>;
    const auto codec_control = std::make_shared<preflight_codec_control>();
    const auto audit_policy = audit_tree::policy_type::natural(
        "proof-preflight-audit-v1",
        std::make_shared<preflight_key_codec>(codec_control),
        std::make_shared<preflight_value_codec>(codec_control));
    auto audit_items = std::vector<std::pair<preflight_key, preflight_value>>{};
    for (auto key = std::int32_t{-256}; key <= 256; ++key) {
        audit_items.emplace_back(preflight_key{key}, preflight_value{key * 3});
    }
    const auto audit_tree_value = audit_tree::create_range(std::move(audit_items), audit_policy);
    auto audit_proof = create_merkle_proof(audit_tree_value, preflight_key{0});
    for (auto key = std::int32_t{-256}; key <= 256; ++key) {
        auto candidate = create_merkle_proof(audit_tree_value, preflight_key{key});
        if (candidate.steps().size() > audit_proof.steps().size()) {
            audit_proof = std::move(candidate);
        }
    }
    CHECK(audit_proof.steps().size() > 1);
    codec_control->reset();
    codec_control->bomb.store(true, std::memory_order_relaxed);
    const auto check_no_codec_calls = [&] {
        CHECK_EQ(0, codec_control->encode_calls.load(std::memory_order_relaxed));
        CHECK_EQ(0, codec_control->decode_calls.load(std::memory_order_relaxed));
    };

    auto oversized_query = merkle_bytes{audit_proof.query().begin(), audit_proof.query().end()};
    oversized_query.push_back(std::byte{0});
    const auto query_first = merkle_proof{
        "unsupported-before-query-v1",
        audit_proof.domain_digest(),
        audit_proof.root_hash(),
        audit_proof.kind(),
        std::move(oversized_query),
        {audit_proof.steps().begin(), audit_proof.steps().end()}};
    const auto query_first_result = verify_merkle_proof(
        query_first,
        audit_policy,
        merkle_verification_budget{}.with_max_proof_query_byte_count(
            query_first.query().size() - 1));
    CHECK_EQ(
        merkle_verification_failure_kind::resource_limit_exceeded,
        query_first_result.failure_kind());
    check_no_codec_calls();

    const auto shape_first = merkle_proof{
        "unsupported-after-shape-v1",
        audit_proof.domain_digest(),
        audit_proof.root_hash(),
        audit_proof.kind(),
        {audit_proof.query().begin(), audit_proof.query().end()},
        {audit_proof.steps().begin(), audit_proof.steps().end()}};
    const auto step_first_result = verify_merkle_proof(
        shape_first,
        audit_policy,
        merkle_verification_budget{}.with_max_block_count(1));
    CHECK_EQ(
        merkle_verification_failure_kind::resource_limit_exceeded,
        step_first_result.failure_kind());
    CHECK_EQ(std::size_t{0}, step_first_result.verified_block_count());
    CHECK_EQ(
        static_cast<std::uint64_t>(shape_first.query().size()),
        step_first_result.verified_byte_count());
    check_no_codec_calls();

    auto audit_expanded_steps = std::vector<merkle_proof_step>{
        audit_proof.steps().begin(), audit_proof.steps().end()};
    audit_expanded_steps.front() = merkle_proof_step{
        audit_expanded_steps.front().block(), {0, 1}};
    const auto expansion_first = merkle_proof{
        "unsupported-after-expansion-v1",
        audit_proof.domain_digest(),
        audit_proof.root_hash(),
        audit_proof.kind(),
        {audit_proof.query().begin(), audit_proof.query().end()},
        std::move(audit_expanded_steps)};
    const auto expansion_first_result = verify_merkle_proof(
        expansion_first,
        audit_policy,
        merkle_verification_budget{}.with_max_child_references_per_block(1));
    CHECK_EQ(
        merkle_verification_failure_kind::resource_limit_exceeded,
        expansion_first_result.failure_kind());
    CHECK_EQ(std::size_t{0}, expansion_first_result.verified_block_count());
    check_no_codec_calls();

    const auto envelope_before_block = verify_merkle_proof(
        shape_first,
        audit_policy,
        merkle_verification_budget{}.with_max_block_byte_count(
            shape_first.steps().front().block().size() - 1));
    CHECK_EQ(
        merkle_verification_failure_kind::unsupported_algorithm,
        envelope_before_block.failure_kind());
    CHECK_EQ(std::size_t{0}, envelope_before_block.verified_block_count());
    check_no_codec_calls();
    codec_control->bomb.store(false, std::memory_order_relaxed);
}

TEST(PersistenceProofsRejectTamperingExtrasMissingStepsAndBadExpansions)
{
    const auto policy = make_string_policy("proof-adversarial-v1");
    const auto tree = make_persistence_tree(policy, 513);
    auto membership = create_merkle_proof(tree, std::int32_t{0});
    if (membership.steps().size() <= 1) {
        for (auto key = std::int32_t{-256}; key <= 256; ++key) {
            auto candidate = create_merkle_proof(tree, key);
            if (candidate.kind() == merkle_proof_kind::membership
                && candidate.steps().size() > membership.steps().size()) {
                membership = std::move(candidate);
            }
        }
    }
    const auto nonmembership = create_merkle_proof(tree, std::int32_t{10000});
    const auto range = create_merkle_range_proof(tree, std::int32_t{-31}, std::int32_t{47});
    CHECK(verify_merkle_proof(membership, policy).valid());
    CHECK(verify_merkle_proof(nonmembership, policy).valid());
    CHECK(verify_merkle_proof(range, policy).valid());

    auto changed_query = merkle_bytes{membership.query().begin(), membership.query().end()};
    changed_query.back() ^= std::byte{1};
    const auto query_tampered = merkle_proof{
        membership.algorithm_id(),
        membership.domain_digest(),
        membership.root_hash(),
        membership.kind(),
        std::move(changed_query),
        {membership.steps().begin(), membership.steps().end()}};
    const auto query_tampered_result = verify_merkle_proof(query_tampered, policy);
    CHECK(!query_tampered_result.valid());
    CHECK_EQ(
        merkle_verification_failure_kind::proof_mismatch,
        query_tampered_result.failure_kind());
    CHECK_EQ(query_tampered.steps().size(), query_tampered_result.verified_block_count());
    CHECK_EQ(query_tampered.total_byte_count(), query_tampered_result.verified_byte_count());

    auto changed_steps = std::vector<merkle_proof_step>{
        membership.steps().begin(), membership.steps().end()};
    auto changed_block_bytes = changed_steps.front().block().to_bytes();
    changed_block_bytes.back() ^= std::byte{0x40};
    changed_steps.front() = merkle_proof_step{
        merkle_block{changed_steps.front().block().digest(), std::move(changed_block_bytes)},
        {changed_steps.front().expanded_child_indexes().begin(),
         changed_steps.front().expanded_child_indexes().end()}};
    const auto block_tampered = merkle_proof{
        membership.algorithm_id(),
        membership.domain_digest(),
        membership.root_hash(),
        membership.kind(),
        {membership.query().begin(), membership.query().end()},
        std::move(changed_steps)};
    const auto block_tampered_result = verify_merkle_proof(block_tampered, policy);
    CHECK_EQ(
        merkle_verification_failure_kind::digest_mismatch,
        block_tampered_result.failure_kind());
    CHECK_EQ(std::size_t{1}, block_tampered_result.verified_block_count());
    CHECK_EQ(
        static_cast<std::uint64_t>(block_tampered.query().size()
            + block_tampered.steps().front().block().size()),
        block_tampered_result.verified_byte_count());

    auto missing_steps = std::vector<merkle_proof_step>{
        membership.steps().begin(), membership.steps().end()};
    CHECK(missing_steps.size() > 1);
    missing_steps.pop_back();
    const auto missing = merkle_proof{
        membership.algorithm_id(),
        membership.domain_digest(),
        membership.root_hash(),
        membership.kind(),
        {membership.query().begin(), membership.query().end()},
        std::move(missing_steps)};
    CHECK_EQ(
        merkle_verification_failure_kind::missing_block,
        verify_merkle_proof(missing, policy).failure_kind());

    auto wrong_expansion = std::vector<merkle_proof_step>{
        membership.steps().begin(), membership.steps().end()};
    const auto expanded = std::find_if(
        wrong_expansion.begin(), wrong_expansion.end(), [](const auto& step) {
            return !step.expanded_child_indexes().empty();
        });
    CHECK(expanded != wrong_expansion.end());
    *expanded = merkle_proof_step{expanded->block(), {}};
    const auto expansion = merkle_proof{
        membership.algorithm_id(),
        membership.domain_digest(),
        membership.root_hash(),
        membership.kind(),
        {membership.query().begin(), membership.query().end()},
        std::move(wrong_expansion)};
    CHECK_EQ(
        merkle_verification_failure_kind::proof_mismatch,
        verify_merkle_proof(expansion, policy).failure_kind());

    auto extra_steps = std::vector<merkle_proof_step>{
        membership.steps().begin(), membership.steps().end()};
    const auto proof_digests = [&] {
        auto result = std::set<merkle_digest>{};
        for (const auto& step : extra_steps) {
            result.insert(step.block().digest());
        }
        return result;
    }();
    const auto pack = export_merkle_pack(tree);
    const auto extra = std::find_if(pack.blocks().begin(), pack.blocks().end(), [&](const auto& block) {
        return !proof_digests.contains(block.digest());
    });
    CHECK(extra != pack.blocks().end());
    extra_steps.emplace_back(*extra, std::vector<std::size_t>{});
    const auto extra_proof = merkle_proof{
        membership.algorithm_id(),
        membership.domain_digest(),
        membership.root_hash(),
        membership.kind(),
        {membership.query().begin(), membership.query().end()},
        std::move(extra_steps)};
    CHECK_EQ(
        merkle_verification_failure_kind::proof_mismatch,
        verify_merkle_proof(extra_proof, policy).failure_kind());

    CHECK_THROWS_AS(create_merkle_range_proof(tree, 2, 1), merkle_range_error);
    const auto empty = string_tree::create(policy);
    CHECK(verify_merkle_proof(create_merkle_proof(empty, std::int32_t{1}), policy).valid());
    CHECK(verify_merkle_proof(
        create_merkle_range_proof(empty, std::int32_t{-1}, std::int32_t{1}), policy).valid());
}

TEST(PersistenceClosurePrunedPacksAndIterativeSyncConverge)
{
    const auto policy = make_string_policy("sync-planning-v1");
    const auto target = make_persistence_tree(policy, 1025);
    const auto local = make_persistence_tree(policy, 127);
    auto receiver = in_memory_merkle_block_store{};
    CHECK_EQ(export_merkle_pack(target), create_merkle_sync_pack(target, receiver));

    auto rounds = std::size_t{0};
    while (true) {
        const auto plan = durable7::hamt::plan_merkle_sync(
            target, local, receiver);
        CHECK_EQ(target.root_hash(), plan.target_root_hash());
        CHECK_EQ(local.root_hash(), plan.local_root_hash());
        if (!plan.requires_blocks()) {
            break;
        }
        CHECK(!plan.requested_blocks().empty());
        const auto transfer = export_merkle_pack(target, plan.requested_blocks());
        for (const auto& block : transfer.blocks()) {
            (void)receiver.put(block);
        }
        ++rounds;
        CHECK(rounds <= target.height() + 1);
    }
    CHECK(rounds > 1);
    const auto loaded = load_merkle_tree(target.root_hash(), policy, receiver);
    CHECK(target.content_equals(loaded));
    const auto finished = durable7::hamt::plan_merkle_sync(
        target, loaded, receiver);
    CHECK(finished.roots_match());
    CHECK(!finished.requires_blocks());
    CHECK_EQ(std::size_t{0}, finished.examined_block_count());
    CHECK(create_merkle_sync_pack(target, receiver).blocks().empty());

    auto root_only_store = in_memory_merkle_block_store{};
    const auto root = target.root_hash();
    const auto root_pack = export_merkle_pack(
        target, std::span<const merkle_digest>{&root, 1});
    for (const auto& block : root_pack.blocks()) {
        (void)root_only_store.put(block);
    }
    CHECK(create_merkle_sync_pack(target, root_only_store).blocks().empty());
}

TEST(PersistenceThreeWayMergeIsPresentNullSafeAndNeverPublishesPartialOutput)
{
    const auto policy = make_string_policy("three-way-merge-v1");
    const auto base = string_tree::create(policy)
        .set_item(1, std::optional<std::string>{"one"})
        .set_item(2, std::optional<std::string>{"two"})
        .set_item(3, std::optional<std::string>{"three"});
    const auto left = base.set_item(1, std::optional<std::string>{"ONE"});
    const auto right = base.set_item(2, std::optional<std::string>{"TWO"});
    const auto disjoint = merge_merkle_trees(base, left, right);
    CHECK(disjoint.success());
    CHECK_EQ(std::string{"ONE"}, disjoint.merged_tree()->at(1).value());
    CHECK_EQ(std::string{"TWO"}, disjoint.merged_tree()->at(2).value());
    CHECK(disjoint.merged_tree()->get_entry(1)->shares_identity_with(*left.get_entry(1)));
    CHECK(disjoint.merged_tree()->get_entry(2)->shares_identity_with(*right.get_entry(2)));

    const auto conflict_left = base.set_item(3, std::optional<std::string>{"left"});
    const auto conflict_right = base.set_item(3, std::optional<std::string>{"right"});
    const auto unresolved = merge_merkle_trees(base, conflict_left, conflict_right);
    CHECK(!unresolved.success());
    CHECK(unresolved.merged_tree() == nullptr);
    CHECK_EQ(std::size_t{1}, unresolved.unresolved_conflicts().size());
    CHECK_EQ(std::int32_t{3}, *unresolved.unresolved_conflicts().front().key);

    const auto resolved = merge_merkle_trees(
        base,
        conflict_left,
        conflict_right,
        [](const auto&) {
            return durable7::hamt::merkle_merge_resolution<
                std::optional<std::string>>::set_value(std::optional<std::string>{"resolved"});
        });
    CHECK(resolved.success());
    CHECK_EQ(std::string{"resolved"}, resolved.merged_tree()->at(3).value());

    const auto present_null = base.set_item(1, std::nullopt);
    const auto deleted = base.remove(1);
    const auto null_conflict = merge_merkle_trees(base, present_null, deleted);
    CHECK(!null_conflict.success());
    CHECK(null_conflict.unresolved_conflicts().front().left.is_present());
    CHECK(!null_conflict.unresolved_conflicts().front().left.value()->has_value());
    CHECK(!null_conflict.unresolved_conflicts().front().right.is_present());
    const auto keep_null = merge_merkle_trees(
        base,
        present_null,
        deleted,
        [](const auto&) {
            return durable7::hamt::merkle_merge_resolution<
                std::optional<std::string>>::use_left();
        });
    CHECK(keep_null.success());
    CHECK(keep_null.merged_tree()->contains_key(1));
    CHECK(!keep_null.merged_tree()->at(1).has_value());

    const auto base_root = base.root_hash();
    CHECK_THROWS_AS(
        merge_merkle_trees(
            base,
            conflict_left,
            conflict_right,
            [](const auto&) -> durable7::hamt::merkle_merge_resolution<
                std::optional<std::string>> {
                throw std::runtime_error{"resolver failure"};
            }),
        std::runtime_error);
    CHECK_EQ(base_root, base.root_hash());
    CHECK_EQ(std::string{"three"}, base.at(3).value());
}

TEST(PersistenceMoveOnlyKeysAndValuesLoadProveImportAndMerge)
{
    using tree_type = merkle_search_tree<move_only_int, move_only_int>;
    const auto policy = tree_type::policy_type::natural(
        "move-only-persistence-v1",
        std::make_shared<move_only_int_codec>(),
        std::make_shared<move_only_int_codec>());
    const auto base = tree_type::create(policy)
        .set_item(move_only_int{1}, move_only_int{10})
        .set_item(move_only_int{3}, move_only_int{30});
    auto store = in_memory_merkle_block_store{};
    CHECK_EQ(base.block_count(), save_merkle_tree(base, store));
    const auto loaded = load_merkle_tree(base.root_hash(), policy, store);
    const auto key_one = move_only_int{1};
    CHECK_EQ(std::int32_t{10}, loaded.at(key_one).value());
    CHECK(base.content_equals(loaded));
    const auto imported = import_merkle_pack(export_merkle_pack(base), policy);
    CHECK_EQ(std::int32_t{30}, imported.at(move_only_int{3}).value());

    const auto proof = create_merkle_proof(base, key_one);
    CHECK(verify_merkle_proof(proof, policy).valid());
    const auto absence = create_merkle_proof(base, move_only_int{2});
    CHECK(verify_merkle_proof(absence, policy).valid());

    const auto left = base.set_item(move_only_int{1}, move_only_int{11});
    const auto right = base.set_item(move_only_int{2}, move_only_int{20});
    const auto merged = merge_merkle_trees(base, left, right);
    CHECK(merged.success());
    CHECK_EQ(std::int32_t{11}, merged.merged_tree()->at(move_only_int{1}).value());
    CHECK_EQ(std::int32_t{20}, merged.merged_tree()->at(move_only_int{2}).value());
    CHECK(merged.merged_tree()->get_entry(move_only_int{1})->shares_identity_with(
        *left.get_entry(move_only_int{1})));

    const auto conflict_right = base.set_item(move_only_int{1}, move_only_int{12});
    const auto unresolved = merge_merkle_trees(base, left, conflict_right);
    CHECK(!unresolved.success());
    CHECK(unresolved.merged_tree() == nullptr);
    CHECK_EQ(std::size_t{1}, unresolved.unresolved_conflicts().size());
    const auto resolved = merge_merkle_trees(
        base,
        left,
        conflict_right,
        [](const auto&) {
            return durable7::hamt::merkle_merge_resolution<move_only_int>::set_value(
                move_only_int{13});
        });
    CHECK(resolved.success());
    CHECK_EQ(std::int32_t{13}, resolved.merged_tree()->at(move_only_int{1}).value());
}

TEST(PersistenceStoreLoadProofAndSyncAreConcurrentSafe)
{
    const auto policy = make_string_policy("persistence-concurrency-v1");
    const auto tree = make_persistence_tree(policy, 513);
    const auto pack = export_merkle_pack(tree);
    const auto proof = create_merkle_range_proof(tree, std::int32_t{-20}, std::int32_t{20});
    auto store = in_memory_merkle_block_store{};
    auto failures = std::atomic<int>{0};
    auto threads = std::vector<std::thread>{};
    threads.reserve(8);
    for (auto worker = 0; worker != 8; ++worker) {
        threads.emplace_back([&, worker] {
            try {
                for (auto pass = 0; pass != 16; ++pass) {
                    for (const auto& block : pack.blocks()) {
                        (void)store.put(block);
                    }
                    CHECK(verify_merkle_proof(proof, policy).valid());
                    const auto loaded = load_merkle_tree(tree.root_hash(), policy, store);
                    CHECK(tree.content_equals(loaded));
                    const auto sync = create_merkle_sync_pack(tree, store);
                    CHECK(sync.blocks().empty());
                    CHECK(store.contains(pack.blocks()[
                        static_cast<std::size_t>(worker + pass) % pack.block_count()].digest()));
                }
            } catch (...) {
                failures.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    CHECK_EQ(0, failures.load(std::memory_order_relaxed));
    CHECK_EQ(tree.block_count(), store.size());
}

} // namespace

int main()
{
    if (!d7_enter_headless_test_process()) {
        return EXIT_FAILURE;
    }

    auto failed = 0;
    for (const auto& test : registry()) {
        try {
            test.run();
            std::cout << "[PASS] " << test.name << '\n' << std::flush;
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
