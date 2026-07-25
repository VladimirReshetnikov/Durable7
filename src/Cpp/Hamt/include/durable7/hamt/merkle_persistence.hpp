#pragma once

#include <durable7/hamt/merkle_search_tree.hpp>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <shared_mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace durable7::hamt {

/// One immutable serialized block paired with its claimed content address.
class merkle_block final {
public:
    merkle_block(merkle_digest digest, merkle_bytes content)
        : digest_(digest),
          content_(std::make_shared<const merkle_bytes>(std::move(content)))
    {
    }

    merkle_block(const merkle_digest digest, const std::span<const std::byte> content)
        : merkle_block(digest, merkle_bytes{content.begin(), content.end()})
    {
    }

    [[nodiscard]] merkle_digest digest() const noexcept { return digest_; }
    [[nodiscard]] std::span<const std::byte> content() const noexcept { return *content_; }
    [[nodiscard]] std::size_t size() const noexcept { return content_->size(); }
    [[nodiscard]] bool empty() const noexcept { return content_->empty(); }
    [[nodiscard]] merkle_bytes to_bytes() const { return *content_; }

    friend bool operator==(const merkle_block& left, const merkle_block& right) noexcept
    {
        return left.digest_ == right.digest_ && *left.content_ == *right.content_;
    }

private:
    merkle_digest digest_;
    std::shared_ptr<const merkle_bytes> content_;
};

/// Stable classification for rejected blocks, packs, proofs, and resource envelopes.
enum class merkle_verification_failure_kind {
    none,
    unsupported_algorithm,
    domain_mismatch,
    missing_block,
    digest_mismatch,
    malformed_block,
    non_canonical_block,
    duplicate_block,
    conflicting_block,
    invalid_reference,
    cycle_detected,
    proof_mismatch,
    root_mismatch,
    resource_limit_exceeded,
};

/// A precise verified-persistence failure with an optional offending address.
class merkle_verification_error final : public std::runtime_error {
public:
    merkle_verification_error(
        const merkle_verification_failure_kind kind,
        std::string message,
        std::optional<merkle_digest> block_digest = std::nullopt)
        : std::runtime_error(std::move(message)),
          kind_(kind),
          block_digest_(block_digest)
    {
        if (kind == merkle_verification_failure_kind::none) {
            throw std::invalid_argument("a verification error must describe a failure");
        }
    }

    [[nodiscard]] merkle_verification_failure_kind kind() const noexcept { return kind_; }
    [[nodiscard]] std::optional<merkle_digest> block_digest() const noexcept
    {
        return block_digest_;
    }

    [[nodiscard]] static merkle_verification_error conflicting_block(
        const merkle_digest digest,
        std::string message)
    {
        return merkle_verification_error{
            merkle_verification_failure_kind::conflicting_block,
            std::move(message),
            digest};
    }

private:
    merkle_verification_failure_kind kind_;
    std::optional<merkle_digest> block_digest_;
};

/// Concurrent-safe storage for immutable content-addressed blocks.
class merkle_block_store {
public:
    virtual ~merkle_block_store() = default;

    [[nodiscard]] virtual std::size_t size() const = 0;
    [[nodiscard]] virtual std::vector<merkle_digest> digests() const = 0;
    [[nodiscard]] virtual bool contains(merkle_digest digest) const = 0;
    [[nodiscard]] virtual std::optional<merkle_block> get(merkle_digest digest) const = 0;
    virtual bool put(merkle_block block) = 0;
    virtual bool remove(merkle_digest digest) = 0;
    virtual void clear() = 0;

    [[nodiscard]] bool empty() const { return size() == 0; }
};

/// Thread-safe ephemeral block store backed by a reader/writer lock.
class in_memory_merkle_block_store final : public merkle_block_store {
public:
    [[nodiscard]] std::size_t size() const override
    {
        const auto lock = std::shared_lock{mutex_};
        return blocks_.size();
    }

    [[nodiscard]] std::vector<merkle_digest> digests() const override
    {
        const auto lock = std::shared_lock{mutex_};
        auto result = std::vector<merkle_digest>{};
        result.reserve(blocks_.size());
        for (const auto& [digest, block] : blocks_) {
            (void)block;
            result.push_back(digest);
        }
        return result;
    }

    [[nodiscard]] bool contains(const merkle_digest digest) const override
    {
        const auto lock = std::shared_lock{mutex_};
        return blocks_.contains(digest);
    }

    [[nodiscard]] std::optional<merkle_block> get(const merkle_digest digest) const override
    {
        const auto lock = std::shared_lock{mutex_};
        const auto iterator = blocks_.find(digest);
        return iterator == blocks_.end()
            ? std::nullopt
            : std::optional<merkle_block>{iterator->second};
    }

    bool put(merkle_block block) override
    {
        const auto lock = std::unique_lock{mutex_};
        const auto iterator = blocks_.find(block.digest());
        if (iterator != blocks_.end()) {
            if (iterator->second == block) {
                return false;
            }
            throw merkle_verification_error::conflicting_block(
                block.digest(),
                "a Merkle digest is already associated with different block bytes");
        }
        blocks_.emplace(block.digest(), std::move(block));
        return true;
    }

    bool remove(const merkle_digest digest) override
    {
        const auto lock = std::unique_lock{mutex_};
        return blocks_.erase(digest) != 0;
    }

    void clear() override
    {
        const auto lock = std::unique_lock{mutex_};
        blocks_.clear();
    }

private:
    mutable std::shared_mutex mutex_;
    std::map<merkle_digest, merkle_block> blocks_;
};

// Implementation namespace: unstable and deliberately excluded from the supported API.
namespace merkle_persistence_detail {

[[nodiscard]] inline bool is_blank_identifier(const std::string_view value) noexcept
{
    if (value.empty()) {
        return true;
    }
    const auto bytes = merkle_detail::bytes_of(value);
    return !merkle_detail::is_canonical_utf8(bytes)
        || merkle_detail::is_all_unicode_whitespace(bytes);
}

[[noreturn]] inline void fail(
    const merkle_verification_failure_kind kind,
    std::string message,
    std::optional<merkle_digest> digest = std::nullopt)
{
    throw merkle_verification_error{kind, std::move(message), digest};
}

[[nodiscard]] inline std::uint64_t as_u64(
    const std::size_t value,
    const merkle_digest digest,
    const char* message)
{
    if constexpr (sizeof(std::size_t) > sizeof(std::uint64_t)) {
        if (value > static_cast<std::size_t>((std::numeric_limits<std::uint64_t>::max)())) {
            fail(merkle_verification_failure_kind::resource_limit_exceeded, message, digest);
        }
    }
    return static_cast<std::uint64_t>(value);
}

} // namespace merkle_persistence_detail

/// Immutable complete or partial transfer pack with unique block addresses.
class merkle_block_pack final {
public:
    merkle_block_pack(
        std::string algorithm_id,
        const merkle_digest domain_digest,
        const merkle_digest root_hash,
        std::vector<merkle_block> blocks)
        : algorithm_id_(std::move(algorithm_id)),
          domain_digest_(domain_digest),
          root_hash_(root_hash),
          blocks_(std::move(blocks))
    {
        if (merkle_persistence_detail::is_blank_identifier(algorithm_id_)) {
            throw std::invalid_argument("a Merkle pack algorithm identifier must not be blank");
        }
        auto unique = std::set<merkle_digest>{};
        for (const auto& block : blocks_) {
            if (!unique.insert(block.digest()).second) {
                merkle_persistence_detail::fail(
                    merkle_verification_failure_kind::duplicate_block,
                    "a Merkle pack contains a duplicate block address",
                    block.digest());
            }
            const auto length = merkle_persistence_detail::as_u64(
                block.size(), block.digest(), "a Merkle pack byte count overflowed");
            if (length > (std::numeric_limits<std::uint64_t>::max)() - total_byte_count_) {
                merkle_persistence_detail::fail(
                    merkle_verification_failure_kind::resource_limit_exceeded,
                    "a Merkle pack byte count overflowed",
                    block.digest());
            }
            total_byte_count_ += length;
            contains_root_block_ = contains_root_block_ || block.digest() == root_hash_;
        }
    }

    [[nodiscard]] const std::string& algorithm_id() const noexcept { return algorithm_id_; }
    [[nodiscard]] merkle_digest domain_digest() const noexcept { return domain_digest_; }
    [[nodiscard]] merkle_digest root_hash() const noexcept { return root_hash_; }
    [[nodiscard]] std::span<const merkle_block> blocks() const noexcept { return blocks_; }
    [[nodiscard]] std::size_t block_count() const noexcept { return blocks_.size(); }
    [[nodiscard]] std::uint64_t total_byte_count() const noexcept { return total_byte_count_; }
    [[nodiscard]] bool contains_root_block() const noexcept { return contains_root_block_; }

    friend bool operator==(const merkle_block_pack&, const merkle_block_pack&) = default;

private:
    std::string algorithm_id_;
    merkle_digest domain_digest_;
    merkle_digest root_hash_;
    std::vector<merkle_block> blocks_;
    std::uint64_t total_byte_count_ = 0;
    bool contains_root_block_ = false;
};

/// One deterministic missing-frontier synchronization round.
class merkle_sync_plan final {
public:
    merkle_sync_plan(
        std::string algorithm_id,
        const merkle_digest domain_digest,
        const merkle_digest local_root_hash,
        const merkle_digest target_root_hash,
        std::vector<merkle_digest> requested_blocks,
        const std::size_t examined_block_count,
        const std::uint64_t examined_byte_count)
        : algorithm_id_(std::move(algorithm_id)),
          domain_digest_(domain_digest),
          local_root_hash_(local_root_hash),
          target_root_hash_(target_root_hash),
          requested_blocks_(std::move(requested_blocks)),
          examined_block_count_(examined_block_count),
          examined_byte_count_(examined_byte_count)
    {
        if (merkle_persistence_detail::is_blank_identifier(algorithm_id_)) {
            throw std::invalid_argument("a Merkle synchronization algorithm identifier must not be blank");
        }
        if (std::set<merkle_digest>{requested_blocks_.begin(), requested_blocks_.end()}.size()
            != requested_blocks_.size()) {
            throw std::invalid_argument("a synchronization plan must not repeat an address");
        }
    }

    [[nodiscard]] const std::string& algorithm_id() const noexcept { return algorithm_id_; }
    [[nodiscard]] merkle_digest domain_digest() const noexcept { return domain_digest_; }
    [[nodiscard]] merkle_digest local_root_hash() const noexcept { return local_root_hash_; }
    [[nodiscard]] merkle_digest target_root_hash() const noexcept { return target_root_hash_; }
    [[nodiscard]] std::span<const merkle_digest> requested_blocks() const noexcept
    {
        return requested_blocks_;
    }
    [[nodiscard]] std::size_t examined_block_count() const noexcept
    {
        return examined_block_count_;
    }
    [[nodiscard]] std::uint64_t examined_byte_count() const noexcept
    {
        return examined_byte_count_;
    }
    [[nodiscard]] bool roots_match() const noexcept
    {
        return local_root_hash_ == target_root_hash_;
    }
    [[nodiscard]] bool requires_blocks() const noexcept { return !requested_blocks_.empty(); }

    friend bool operator==(const merkle_sync_plan&, const merkle_sync_plan&) = default;

private:
    std::string algorithm_id_;
    merkle_digest domain_digest_;
    merkle_digest local_root_hash_;
    merkle_digest target_root_hash_;
    std::vector<merkle_digest> requested_blocks_;
    std::size_t examined_block_count_;
    std::uint64_t examined_byte_count_;
};

/// Seven finite limits checked before allocation, decoding, hashing, and expansion.
class merkle_verification_budget final {
public:
    explicit merkle_verification_budget(
        const std::size_t max_block_count = 1'000'000,
        const std::uint64_t max_total_byte_count = std::uint64_t{1} << 30,
        const std::size_t max_block_byte_count = std::size_t{16} << 20,
        const std::size_t max_depth = 256,
        const std::uint64_t max_entry_count = 100'000'000,
        const std::size_t max_child_references_per_block = 65'536,
        const std::size_t max_proof_query_byte_count = std::size_t{16} << 20)
        : max_block_count_(max_block_count),
          max_total_byte_count_(max_total_byte_count),
          max_block_byte_count_(max_block_byte_count),
          max_depth_(max_depth),
          max_entry_count_(max_entry_count),
          max_child_references_per_block_(max_child_references_per_block),
          max_proof_query_byte_count_(max_proof_query_byte_count)
    {
        if (max_block_count_ == 0 || max_total_byte_count_ == 0
            || max_block_byte_count_ == 0 || max_depth_ == 0
            || max_entry_count_ == 0 || max_child_references_per_block_ == 0
            || max_proof_query_byte_count_ == 0) {
            throw std::invalid_argument("Merkle verification limits must all be positive");
        }
        if (merkle_persistence_detail::as_u64(
                max_block_byte_count_, {}, "the per-block budget exceeds native limits")
                > max_total_byte_count_) {
            throw std::invalid_argument(
                "the per-block byte limit must not exceed the total-byte limit");
        }
        if (merkle_persistence_detail::as_u64(
                max_proof_query_byte_count_, {}, "the query budget exceeds native limits")
                > max_total_byte_count_) {
            throw std::invalid_argument(
                "the proof-query byte limit must not exceed the total-byte limit");
        }
    }

    [[nodiscard]] std::size_t max_block_count() const noexcept { return max_block_count_; }
    [[nodiscard]] std::uint64_t max_total_byte_count() const noexcept
    {
        return max_total_byte_count_;
    }
    [[nodiscard]] std::size_t max_block_byte_count() const noexcept
    {
        return max_block_byte_count_;
    }
    [[nodiscard]] std::size_t max_depth() const noexcept { return max_depth_; }
    [[nodiscard]] std::uint64_t max_entry_count() const noexcept { return max_entry_count_; }
    [[nodiscard]] std::size_t max_child_references_per_block() const noexcept
    {
        return max_child_references_per_block_;
    }
    [[nodiscard]] std::size_t max_proof_query_byte_count() const noexcept
    {
        return max_proof_query_byte_count_;
    }

    [[nodiscard]] merkle_verification_budget with_max_block_count(const std::size_t value) const
    {
        return copy(value, max_total_byte_count_, max_block_byte_count_, max_depth_,
            max_entry_count_, max_child_references_per_block_, max_proof_query_byte_count_);
    }
    [[nodiscard]] merkle_verification_budget with_max_total_byte_count(const std::uint64_t value) const
    {
        return copy(max_block_count_, value, max_block_byte_count_, max_depth_,
            max_entry_count_, max_child_references_per_block_, max_proof_query_byte_count_);
    }
    [[nodiscard]] merkle_verification_budget with_max_block_byte_count(const std::size_t value) const
    {
        const auto query = max_proof_query_byte_count_ == max_block_byte_count_
            ? value
            : max_proof_query_byte_count_;
        return copy(max_block_count_, max_total_byte_count_, value, max_depth_,
            max_entry_count_, max_child_references_per_block_, query);
    }
    [[nodiscard]] merkle_verification_budget with_max_depth(const std::size_t value) const
    {
        return copy(max_block_count_, max_total_byte_count_, max_block_byte_count_, value,
            max_entry_count_, max_child_references_per_block_, max_proof_query_byte_count_);
    }
    [[nodiscard]] merkle_verification_budget with_max_entry_count(const std::uint64_t value) const
    {
        return copy(max_block_count_, max_total_byte_count_, max_block_byte_count_, max_depth_,
            value, max_child_references_per_block_, max_proof_query_byte_count_);
    }
    [[nodiscard]] merkle_verification_budget with_max_child_references_per_block(
        const std::size_t value) const
    {
        return copy(max_block_count_, max_total_byte_count_, max_block_byte_count_, max_depth_,
            max_entry_count_, value, max_proof_query_byte_count_);
    }
    [[nodiscard]] merkle_verification_budget with_max_proof_query_byte_count(
        const std::size_t value) const
    {
        return copy(max_block_count_, max_total_byte_count_, max_block_byte_count_, max_depth_,
            max_entry_count_, max_child_references_per_block_, value);
    }

    friend bool operator==(const merkle_verification_budget&, const merkle_verification_budget&)
        = default;

private:
    [[nodiscard]] static merkle_verification_budget copy(
        const std::size_t blocks,
        const std::uint64_t total,
        const std::size_t block_bytes,
        const std::size_t depth,
        const std::uint64_t entries,
        const std::size_t children,
        const std::size_t query)
    {
        return merkle_verification_budget{
            blocks, total, block_bytes, depth, entries, children, query};
    }

    std::size_t max_block_count_;
    std::uint64_t max_total_byte_count_;
    std::size_t max_block_byte_count_;
    std::size_t max_depth_;
    std::uint64_t max_entry_count_;
    std::size_t max_child_references_per_block_;
    std::size_t max_proof_query_byte_count_;
};

namespace merkle_persistence_detail {

/// Internal bridge that reuses the core's canonical entry and node machinery.
/// Consumers must not name this implementation-detail type.
template <class K, class V>
struct access final {
    using tree_type = merkle_search_tree<K, V>;
    using entry_type = typename tree_type::entry_type;
    using node = typename tree_type::node;
    using node_pointer = typename tree_type::node_pointer;
    using policy_type = typename tree_type::policy_type;

    [[nodiscard]] static const node_pointer& root(const tree_type& tree) noexcept
    {
        return tree.root_;
    }
    [[nodiscard]] static const std::vector<entry_type>& entries(const node& value) noexcept
    {
        return value.entries;
    }
    [[nodiscard]] static const std::vector<node_pointer>& children(const node& value) noexcept
    {
        return value.children;
    }
    [[nodiscard]] static std::uint8_t level(const node& value) noexcept { return value.level; }
    [[nodiscard]] static std::size_t count(const node& value) noexcept { return value.count; }
    [[nodiscard]] static const K& minimum_key(const node& value) noexcept
    {
        return *value.minimum_key;
    }
    [[nodiscard]] static const K& maximum_key(const node& value) noexcept
    {
        return *value.maximum_key;
    }
    [[nodiscard]] static merkle_digest digest(const node& value) noexcept { return value.digest; }
    [[nodiscard]] static const merkle_bytes& block_bytes(const node& value) noexcept
    {
        return *value.block_bytes;
    }
    [[nodiscard]] static std::vector<node_pointer> nodes_preorder(const tree_type& tree)
    {
        return tree.nodes_preorder();
    }
    [[nodiscard]] static std::pair<std::size_t, bool> find_position(
        const tree_type& tree,
        const std::vector<entry_type>& entries_value,
        const K& key)
    {
        return tree.find_position(entries_value, key);
    }
    [[nodiscard]] static entry_type from_encoded(
        K key,
        V value,
        merkle_bytes key_bytes,
        merkle_bytes value_bytes,
        const std::uint8_t level_value)
    {
        return entry_type::from_encoded(
            std::move(key),
            std::move(value),
            std::move(key_bytes),
            std::move(value_bytes),
            level_value);
    }
    [[nodiscard]] static entry_type replace_value(
        const entry_type& entry,
        V value,
        merkle_bytes value_bytes)
    {
        return entry.replacing_value(std::move(value), std::move(value_bytes));
    }
    [[nodiscard]] static node_pointer new_node(
        const tree_type& tree,
        const std::uint8_t level_value,
        std::vector<entry_type> entries_value,
        std::vector<node_pointer> children_value)
    {
        return tree.new_node(
            level_value,
            std::move(entries_value),
            std::move(children_value));
    }
    [[nodiscard]] static node_pointer build_canonical(
        const tree_type& tree,
        const std::span<const entry_type> entries_value)
    {
        return tree.build_canonical(entries_value);
    }
    [[nodiscard]] static tree_type from_root(node_pointer root_value, policy_type policy)
    {
        return tree_type{std::move(root_value), std::move(policy)};
    }
};

template <class K, class V>
using persistence_access = access<K, V>;

template <class K, class V>
using persistence_node = typename persistence_access<K, V>::node;

template <class K, class V>
using persistence_node_pointer = typename persistence_access<K, V>::node_pointer;

template <class K, class V>
using persistence_entry = typename persistence_access<K, V>::entry_type;

class verification_context final {
public:
    explicit verification_context(const merkle_verification_budget& budget)
        : budget_(budget)
    {
    }

    [[nodiscard]] const merkle_verification_budget& budget() const noexcept { return budget_; }
    [[nodiscard]] std::size_t block_count() const noexcept { return block_count_; }
    [[nodiscard]] std::uint64_t total_bytes() const noexcept { return total_bytes_; }

    void check_depth(
        const std::size_t depth,
        const std::optional<merkle_digest> digest = std::nullopt) const
    {
        if (depth == 0 || depth > budget_.max_depth()) {
            fail(
                merkle_verification_failure_kind::resource_limit_exceeded,
                "Merkle verification exceeded the maximum reference depth",
                digest);
        }
    }

    [[nodiscard]] bool account(const merkle_block& block, const std::size_t depth)
    {
        check_depth(depth, block.digest());
        if (blocks_.contains(block.digest())) {
            return false;
        }
        if (block.size() > budget_.max_block_byte_count()) {
            fail(
                merkle_verification_failure_kind::resource_limit_exceeded,
                "a Merkle block exceeds the per-block byte budget",
                block.digest());
        }
        const auto length = as_u64(
            block.size(), block.digest(), "a Merkle block length exceeds verification limits");
        if (block_count_ >= budget_.max_block_count()
            || length > budget_.max_total_byte_count() - total_bytes_) {
            fail(
                merkle_verification_failure_kind::resource_limit_exceeded,
                "Merkle verification exceeded its block or total-byte budget",
                block.digest());
        }
        blocks_.insert(block.digest());
        ++block_count_;
        total_bytes_ += length;
        return true;
    }

    void account_entries(const std::size_t count, const merkle_digest digest)
    {
        const auto value = as_u64(count, digest, "a Merkle entry count exceeds verification limits");
        if (value > budget_.max_entry_count() - entry_count_) {
            fail(
                merkle_verification_failure_kind::resource_limit_exceeded,
                "Merkle verification exceeded its decoded-entry budget",
                digest);
        }
        entry_count_ += value;
    }

    void account_proof_query(const std::size_t byte_count, const merkle_digest root_hash)
    {
        const auto length = as_u64(
            byte_count, root_hash, "a proof query length exceeds verification limits");
        if (byte_count > budget_.max_proof_query_byte_count()
            || length > budget_.max_total_byte_count() - total_bytes_) {
            fail(
                merkle_verification_failure_kind::resource_limit_exceeded,
                "a Merkle proof query exceeds its query or total-byte budget",
                root_hash);
        }
        total_bytes_ += length;
    }

private:
    const merkle_verification_budget& budget_;
    std::size_t block_count_ = 0;
    std::uint64_t total_bytes_ = 0;
    std::uint64_t entry_count_ = 0;
    std::set<merkle_digest> blocks_;
};

inline void verify_envelope(
    const std::string_view algorithm_id,
    const merkle_digest domain_digest,
    const merkle_digest expected_domain)
{
    if (algorithm_id != "mst-sha256-b16-v2") {
        fail(
            merkle_verification_failure_kind::unsupported_algorithm,
            "the Merkle envelope names an unsupported algorithm");
    }
    if (domain_digest != expected_domain) {
        fail(
            merkle_verification_failure_kind::domain_mismatch,
            "the Merkle envelope belongs to another policy domain");
    }
}

inline void preflight_store(
    const std::span<const merkle_block> blocks,
    const merkle_block_store& store)
{
    for (const auto& block : blocks) {
        const auto existing = store.get(block.digest());
        if (existing.has_value() && *existing != block) {
            fail(
                merkle_verification_failure_kind::conflicting_block,
                "a destination address already contains different block bytes",
                block.digest());
        }
    }
}

} // namespace merkle_persistence_detail

/// Exports a tree's complete closure in deterministic preorder.
template <class K, class V>
[[nodiscard]] merkle_block_pack export_merkle_pack(const merkle_search_tree<K, V>& tree)
{
    using access = merkle_persistence_detail::access<K, V>;
    auto blocks = std::vector<merkle_block>{};
    const auto nodes = access::nodes_preorder(tree);
    blocks.reserve(nodes.size());
    for (const auto& node : nodes) {
        blocks.emplace_back(access::digest(*node), access::block_bytes(*node));
    }
    return merkle_block_pack{
        std::string{merkle_search_tree_policy<K, V>::algorithm_id},
        tree.policy().domain_digest(),
        tree.root_hash(),
        std::move(blocks)};
}

/// Exports unique explicitly requested blocks in the supplied transfer order.
template <class K, class V>
[[nodiscard]] merkle_block_pack export_merkle_pack(
    const merkle_search_tree<K, V>& tree,
    const std::span<const merkle_digest> digests)
{
    using access = merkle_persistence_detail::access<K, V>;
    auto by_digest = std::map<merkle_digest, typename access::node_pointer>{};
    for (const auto& node : access::nodes_preorder(tree)) {
        by_digest.emplace(access::digest(*node), node);
    }
    auto unique = std::set<merkle_digest>{};
    auto blocks = std::vector<merkle_block>{};
    blocks.reserve(digests.size());
    for (const auto digest : digests) {
        if (!unique.insert(digest).second) {
            merkle_persistence_detail::fail(
                merkle_verification_failure_kind::duplicate_block,
                "a block was requested more than once",
                digest);
        }
        const auto iterator = by_digest.find(digest);
        if (iterator == by_digest.end()) {
            merkle_persistence_detail::fail(
                merkle_verification_failure_kind::missing_block,
                "a requested digest does not name a block in this tree",
                digest);
        }
        blocks.emplace_back(digest, access::block_bytes(*iterator->second));
    }
    return merkle_block_pack{
        std::string{merkle_search_tree_policy<K, V>::algorithm_id},
        tree.policy().domain_digest(),
        tree.root_hash(),
        std::move(blocks)};
}

/// Writes a complete closure only after preflighting every destination conflict.
template <class K, class V>
std::size_t save_merkle_tree(
    const merkle_search_tree<K, V>& tree,
    merkle_block_store& store)
{
    const auto pack = export_merkle_pack(tree);
    merkle_persistence_detail::preflight_store(pack.blocks(), store);
    auto added = std::size_t{0};
    for (const auto& block : pack.blocks()) {
        if (store.put(block)) {
            if (added == (std::numeric_limits<std::size_t>::max)()) {
                merkle_persistence_detail::fail(
                    merkle_verification_failure_kind::resource_limit_exceeded,
                    "a saved block count overflowed");
            }
            ++added;
        }
    }
    return added;
}

/// Exports every target block absent from a receiver, pruning known verified closures.
template <class K, class V>
[[nodiscard]] merkle_block_pack create_merkle_sync_pack(
    const merkle_search_tree<K, V>& tree,
    const merkle_block_store& receiver)
{
    using access = merkle_persistence_detail::access<K, V>;
    auto missing = std::vector<merkle_block>{};
    auto pending = std::vector<typename access::node_pointer>{};
    if (access::root(tree) != nullptr) {
        pending.push_back(access::root(tree));
    }
    while (!pending.empty()) {
        auto node = std::move(pending.back());
        pending.pop_back();
        if (receiver.contains(access::digest(*node))) {
            continue;
        }
        missing.emplace_back(access::digest(*node), access::block_bytes(*node));
        const auto& children = access::children(*node);
        for (auto iterator = children.rbegin(); iterator != children.rend(); ++iterator) {
            if (*iterator != nullptr) {
                pending.push_back(*iterator);
            }
        }
    }
    return merkle_block_pack{
        std::string{merkle_search_tree_policy<K, V>::algorithm_id},
        tree.policy().domain_digest(),
        tree.root_hash(),
        std::move(missing)};
}

/// Plans one iterative missing-frontier synchronization round toward a target tree.
template <class K, class V>
[[nodiscard]] merkle_sync_plan plan_merkle_sync(
    const merkle_search_tree<K, V>& target,
    const merkle_search_tree<K, V>& local,
    const merkle_block_store& receiver)
{
    using access = merkle_persistence_detail::access<K, V>;
    if (!target.policy().is_compatible_with(local.policy())) {
        throw merkle_policy_mismatch{};
    }
    if (target.root_hash() == local.root_hash()) {
        return merkle_sync_plan{
            std::string{merkle_search_tree_policy<K, V>::algorithm_id},
            target.policy().domain_digest(),
            local.root_hash(),
            target.root_hash(),
            {},
            0,
            0};
    }
    auto requested = std::vector<merkle_digest>{};
    auto pending = std::vector<typename access::node_pointer>{};
    if (access::root(target) != nullptr) {
        pending.push_back(access::root(target));
    }
    auto examined_blocks = std::size_t{0};
    auto examined_bytes = std::uint64_t{0};
    while (!pending.empty()) {
        auto node = std::move(pending.back());
        pending.pop_back();
        if (examined_blocks == (std::numeric_limits<std::size_t>::max)()) {
            throw std::overflow_error("a synchronization examined-block count overflowed");
        }
        ++examined_blocks;
        const auto length = merkle_persistence_detail::as_u64(
            access::block_bytes(*node).size(),
            access::digest(*node),
            "a synchronization examined-byte count overflowed");
        if (length > (std::numeric_limits<std::uint64_t>::max)() - examined_bytes) {
            throw std::overflow_error("a synchronization examined-byte count overflowed");
        }
        examined_bytes += length;
        if (!receiver.contains(access::digest(*node))) {
            requested.push_back(access::digest(*node));
            continue;
        }
        const auto& children = access::children(*node);
        for (auto iterator = children.rbegin(); iterator != children.rend(); ++iterator) {
            if (*iterator != nullptr) {
                pending.push_back(*iterator);
            }
        }
    }
    return merkle_sync_plan{
        std::string{merkle_search_tree_policy<K, V>::algorithm_id},
        target.policy().domain_digest(),
        local.root_hash(),
        target.root_hash(),
        std::move(requested),
        examined_blocks,
        examined_bytes};
}

namespace merkle_persistence_detail {

class byte_reader final {
public:
    byte_reader(
        const std::span<const std::byte> bytes,
        const std::optional<merkle_digest> digest = std::nullopt,
        const merkle_verification_failure_kind failure_kind =
            merkle_verification_failure_kind::malformed_block)
        : bytes_(bytes), digest_(digest), failure_kind_(failure_kind)
    {
    }

    [[nodiscard]] bool at_end() const noexcept { return offset_ == bytes_.size(); }

    [[nodiscard]] std::span<const std::byte> take(const std::size_t length)
    {
        if (length > bytes_.size() - offset_) {
            fail(failure_kind_, "a serialized Merkle field is truncated", digest_);
        }
        const auto result = bytes_.subspan(offset_, length);
        offset_ += length;
        return result;
    }

    [[nodiscard]] std::uint8_t read_u8()
    {
        return merkle_detail::as_u8(take(1).front());
    }

    [[nodiscard]] std::int32_t read_i32()
    {
        const auto value = take(4);
        const auto bits = (static_cast<std::uint32_t>(merkle_detail::as_u8(value[0])) << 24)
            | (static_cast<std::uint32_t>(merkle_detail::as_u8(value[1])) << 16)
            | (static_cast<std::uint32_t>(merkle_detail::as_u8(value[2])) << 8)
            | static_cast<std::uint32_t>(merkle_detail::as_u8(value[3]));
        return std::bit_cast<std::int32_t>(bits);
    }

    [[nodiscard]] std::span<const std::byte> read_length_prefixed()
    {
        const auto length = read_i32();
        if (length < 0) {
            fail(failure_kind_, "a length-prefixed Merkle field has a negative length", digest_);
        }
        return take(static_cast<std::size_t>(length));
    }

private:
    std::span<const std::byte> bytes_;
    std::optional<merkle_digest> digest_;
    merkle_verification_failure_kind failure_kind_;
    std::size_t offset_ = 0;
};

template <class K, class V>
struct decoded_block final {
    merkle_block block;
    std::uint8_t level;
    std::size_t subtree_count;
    std::vector<persistence_entry<K, V>> entries;
    std::vector<merkle_digest> child_digests;
};

template <class T>
[[nodiscard]] T decode_canonical(
    const merkle_codec<T>& codec,
    const std::span<const std::byte> bytes,
    const char* field,
    const merkle_digest digest)
{
    T value = [&]() -> T {
        try {
            return codec.decode(bytes);
        } catch (const merkle_codec_error&) {
            fail(
                merkle_verification_failure_kind::malformed_block,
                std::string{"a Merkle "} + field + " codec rejected serialized entry bytes",
                digest);
        } catch (const std::invalid_argument&) {
            fail(
                merkle_verification_failure_kind::malformed_block,
                std::string{"a Merkle "} + field + " codec rejected serialized entry bytes",
                digest);
        }
    }();
    merkle_bytes canonical;
    try {
        canonical = codec.encode(value);
    } catch (const merkle_codec_error&) {
        fail(
            merkle_verification_failure_kind::non_canonical_block,
            std::string{"a decoded Merkle "} + field + " could not be re-encoded",
            digest);
    } catch (const std::invalid_argument&) {
        fail(
            merkle_verification_failure_kind::non_canonical_block,
            std::string{"a decoded Merkle "} + field + " could not be re-encoded",
            digest);
    }
    if (!std::ranges::equal(canonical, bytes)) {
        fail(
            merkle_verification_failure_kind::non_canonical_block,
            std::string{"a decoded Merkle "} + field
                + " does not round-trip to its exact bytes",
            digest);
    }
    return value;
}

template <class K, class V>
[[nodiscard]] merkle_bytes encode_raw_block(
    const merkle_search_tree<K, V>& tree,
    const std::uint8_t level,
    const std::size_t subtree_count,
    const std::span<const persistence_entry<K, V>> entries,
    const std::span<const merkle_digest> child_digests,
    const merkle_digest digest)
{
    constexpr auto header_length = std::size_t{4 + 1 + merkle_digest::byte_length + 1 + 4 + 4};
    if (subtree_count > static_cast<std::size_t>((std::numeric_limits<std::int32_t>::max)())
        || entries.size() > static_cast<std::size_t>((std::numeric_limits<std::int32_t>::max)())
        || child_digests.size() != entries.size() + 1) {
        fail(
            merkle_verification_failure_kind::resource_limit_exceeded,
            "a Merkle block count exceeds its wire limits",
            digest);
    }
    auto length = header_length;
    const auto add_length = [&length, digest](const std::size_t value) {
        if (value > (std::numeric_limits<std::size_t>::max)() - length) {
            fail(
                merkle_verification_failure_kind::resource_limit_exceeded,
                "a Merkle block length overflowed",
                digest);
        }
        length += value;
    };
    if (child_digests.size() > (std::numeric_limits<std::size_t>::max)()
        / merkle_digest::byte_length) {
        fail(
            merkle_verification_failure_kind::resource_limit_exceeded,
            "a Merkle block length overflowed",
            digest);
    }
    add_length(child_digests.size() * merkle_digest::byte_length);
    for (const auto& entry : entries) {
        add_length(8);
        add_length(entry.key_bytes().size());
        add_length(entry.value_bytes().size());
    }
    auto result = merkle_bytes{};
    result.reserve(length);
    const auto magic = merkle_detail::bytes_of("MST2");
    result.insert(result.end(), magic.begin(), magic.end());
    result.push_back(std::byte{1});
    const auto domain = tree.policy().domain_digest();
    result.insert(result.end(), domain.bytes().begin(), domain.bytes().end());
    result.push_back(merkle_detail::as_byte(level));
    merkle_detail::append_i32_be(result, static_cast<std::int32_t>(subtree_count));
    merkle_detail::append_i32_be(result, static_cast<std::int32_t>(entries.size()));
    for (const auto& entry : entries) {
        merkle_detail::append_i32_be(result, static_cast<std::int32_t>(entry.key_bytes().size()));
        result.insert(result.end(), entry.key_bytes().begin(), entry.key_bytes().end());
        merkle_detail::append_i32_be(result, static_cast<std::int32_t>(entry.value_bytes().size()));
        result.insert(result.end(), entry.value_bytes().begin(), entry.value_bytes().end());
    }
    for (const auto child : child_digests) {
        result.insert(result.end(), child.bytes().begin(), child.bytes().end());
    }
    return result;
}

template <class K, class V>
[[nodiscard]] decoded_block<K, V> decode_block(
    const merkle_search_tree<K, V>& verifier,
    merkle_block block,
    verification_context& context,
    const std::size_t depth)
{
    constexpr auto header_length = std::size_t{4 + 1 + merkle_digest::byte_length + 1 + 4 + 4};
    const auto first_visit = context.account(block, depth);
    const auto bytes = block.content();
    if (verifier.policy().hash_bytes(bytes) != block.digest()) {
        fail(
            merkle_verification_failure_kind::digest_mismatch,
            "a block's bytes do not match its claimed digest",
            block.digest());
    }
    if (bytes.size() < header_length + merkle_digest::byte_length) {
        fail(
            merkle_verification_failure_kind::malformed_block,
            "a Merkle block is shorter than the minimum encoding",
            block.digest());
    }
    auto reader = byte_reader{bytes, block.digest()};
    if (!std::ranges::equal(reader.take(4), merkle_detail::bytes_of("MST2"))) {
        fail(
            merkle_verification_failure_kind::malformed_block,
            "a Merkle block has the wrong magic",
            block.digest());
    }
    if (reader.read_u8() != 1) {
        fail(
            merkle_verification_failure_kind::malformed_block,
            "a Merkle block has an unsupported tag",
            block.digest());
    }
    const auto domain = merkle_digest::from_bytes(reader.take(merkle_digest::byte_length));
    if (domain != verifier.policy().domain_digest()) {
        fail(
            merkle_verification_failure_kind::domain_mismatch,
            "a Merkle block belongs to another policy domain",
            block.digest());
    }
    const auto level = reader.read_u8();
    if (level > 64) {
        fail(
            merkle_verification_failure_kind::malformed_block,
            "a Merkle block level exceeds the SHA-256 nibble range",
            block.digest());
    }
    const auto subtree_count_signed = reader.read_i32();
    const auto entry_count_signed = reader.read_i32();
    if (subtree_count_signed <= 0 || entry_count_signed <= 0
        || subtree_count_signed < entry_count_signed) {
        fail(
            merkle_verification_failure_kind::malformed_block,
            "a Merkle block has invalid entry counts",
            block.digest());
    }
    const auto subtree_count = static_cast<std::size_t>(subtree_count_signed);
    const auto entry_count = static_cast<std::size_t>(entry_count_signed);
    if (entry_count >= context.budget().max_child_references_per_block()) {
        fail(
            merkle_verification_failure_kind::resource_limit_exceeded,
            "a Merkle block exceeds the child-reference budget",
            block.digest());
    }
    constexpr auto minimum_per_entry = std::size_t{8 + merkle_digest::byte_length};
    if (entry_count > ((std::numeric_limits<std::size_t>::max)()
            - header_length - merkle_digest::byte_length) / minimum_per_entry
        || header_length + entry_count * minimum_per_entry + merkle_digest::byte_length
            > bytes.size()) {
        fail(
            merkle_verification_failure_kind::malformed_block,
            "a Merkle block cannot contain its declared entries and child references",
            block.digest());
    }
    if (first_visit) {
        context.account_entries(entry_count, block.digest());
    }

    auto entries = std::vector<persistence_entry<K, V>>{};
    entries.reserve(entry_count);
    for (auto index = std::size_t{0}; index != entry_count; ++index) {
        const auto key_span = reader.read_length_prefixed();
        const auto value_span = reader.read_length_prefixed();
        auto key = decode_canonical(verifier.policy().key_codec(), key_span, "key", block.digest());
        auto value = decode_canonical(
            verifier.policy().value_codec(), value_span, "value", block.digest());
        auto key_bytes = merkle_bytes{key_span.begin(), key_span.end()};
        auto value_bytes = merkle_bytes{value_span.begin(), value_span.end()};
        const auto entry_level = merkle_search_tree_policy<K, V>::level(
            verifier.policy().hash_key_bytes(key_bytes));
        if (entry_level != level) {
            fail(
                merkle_verification_failure_kind::non_canonical_block,
                "an entry is stored at a level other than its hash-derived level",
                block.digest());
        }
        if (!entries.empty()
            && verifier.policy().compare(entries.back().key(), key) >= 0) {
            fail(
                merkle_verification_failure_kind::non_canonical_block,
                "Merkle block entries are not strictly comparator-ordered",
                block.digest());
        }
        entries.push_back(persistence_access<K, V>::from_encoded(
            std::move(key),
            std::move(value),
            std::move(key_bytes),
            std::move(value_bytes),
            entry_level));
    }
    auto child_digests = std::vector<merkle_digest>{};
    child_digests.reserve(entry_count + 1);
    for (auto index = std::size_t{0}; index != entry_count + 1; ++index) {
        (void)index;
        child_digests.push_back(
            merkle_digest::from_bytes(reader.take(merkle_digest::byte_length)));
    }
    if (!reader.at_end()) {
        fail(
            merkle_verification_failure_kind::non_canonical_block,
            "a Merkle block has trailing bytes",
            block.digest());
    }
    const auto canonical = encode_raw_block(
        verifier,
        level,
        subtree_count,
        std::span<const persistence_entry<K, V>>{entries},
        std::span<const merkle_digest>{child_digests},
        block.digest());
    if (!std::ranges::equal(canonical, bytes)) {
        fail(
            merkle_verification_failure_kind::non_canonical_block,
            "a Merkle block is not the unique canonical encoding",
            block.digest());
    }
    return decoded_block<K, V>{
        std::move(block),
        level,
        subtree_count,
        std::move(entries),
        std::move(child_digests)};
}

template <class K, class V>
void validate_reference(
    const merkle_search_tree<K, V>& tree,
    const decoded_block<K, V>& parent,
    const std::size_t child_index,
    const persistence_node<K, V>& child)
{
    using access = persistence_access<K, V>;
    if (access::level(child) >= parent.level) {
        fail(
            merkle_verification_failure_kind::invalid_reference,
            "a child block is not below its parent level",
            access::digest(child));
    }
    if (child_index != 0
        && tree.policy().compare(
               access::minimum_key(child), parent.entries[child_index - 1].key()) <= 0) {
        fail(
            merkle_verification_failure_kind::invalid_reference,
            "a child block crosses its lower key separator",
            access::digest(child));
    }
    if (child_index != parent.entries.size()
        && tree.policy().compare(
               access::maximum_key(child), parent.entries[child_index].key()) >= 0) {
        fail(
            merkle_verification_failure_kind::invalid_reference,
            "a child block crosses its upper key separator",
            access::digest(child));
    }
}

template <class K, class V>
[[nodiscard]] persistence_node_pointer<K, V> load_node(
    const merkle_search_tree<K, V>& verifier,
    const merkle_digest digest,
    const merkle_block_store& store,
    verification_context& context,
    std::map<merkle_digest, persistence_node_pointer<K, V>>& cache,
    std::set<merkle_digest>& active,
    const std::size_t depth)
{
    using access = persistence_access<K, V>;
    context.check_depth(depth, digest);
    if (const auto cached = cache.find(digest); cached != cache.end()) {
        return cached->second;
    }
    if (!active.insert(digest).second) {
        fail(
            merkle_verification_failure_kind::cycle_detected,
            "the Merkle closure contains a reference cycle",
            digest);
    }
    try {
        auto block = store.get(digest);
        if (!block.has_value()) {
            fail(
                merkle_verification_failure_kind::missing_block,
                "a required Merkle block is absent",
                digest);
        }
        auto decoded = decode_block(verifier, std::move(*block), context, depth);
        auto children = std::vector<persistence_node_pointer<K, V>>{};
        children.reserve(decoded.child_digests.size());
        for (auto index = std::size_t{0}; index != decoded.child_digests.size(); ++index) {
            const auto child_digest = decoded.child_digests[index];
            if (child_digest == verifier.policy().empty_digest()) {
                children.push_back(nullptr);
                continue;
            }
            if (depth == (std::numeric_limits<std::size_t>::max)()) {
                fail(
                    merkle_verification_failure_kind::resource_limit_exceeded,
                    "a Merkle reference depth overflowed",
                    child_digest);
            }
            auto child = load_node(
                verifier,
                child_digest,
                store,
                context,
                cache,
                active,
                depth + 1);
            validate_reference(verifier, decoded, index, *child);
            children.push_back(std::move(child));
        }
        auto actual_count = decoded.entries.size();
        for (const auto& child : children) {
            if (child != nullptr) {
                if (access::count(*child) > (std::numeric_limits<std::size_t>::max)()
                    - actual_count) {
                    fail(
                        merkle_verification_failure_kind::resource_limit_exceeded,
                        "Merkle closure metadata overflowed",
                        digest);
                }
                actual_count += access::count(*child);
            }
        }
        if (actual_count != decoded.subtree_count) {
            fail(
                merkle_verification_failure_kind::invalid_reference,
                "a block's declared entry count disagrees with its closure",
                digest);
        }
        persistence_node_pointer<K, V> node;
        try {
            node = access::new_node(
                verifier,
                decoded.level,
                decoded.entries,
                std::move(children));
        } catch (const std::length_error&) {
            fail(
                merkle_verification_failure_kind::resource_limit_exceeded,
                "Merkle node metadata or canonical encoding overflowed",
                digest);
        } catch (const merkle_tree_invariant_error&) {
            fail(
                merkle_verification_failure_kind::invalid_reference,
                "a verified Merkle node violates core invariants",
                digest);
        }
        if (access::digest(*node) != digest
            || access::block_bytes(*node) != decoded.block.to_bytes()) {
            fail(
                merkle_verification_failure_kind::non_canonical_block,
                "decoded block bytes do not round-trip canonically",
                digest);
        }
        cache.emplace(digest, node);
        active.erase(digest);
        return node;
    } catch (...) {
        active.erase(digest);
        throw;
    }
}

class overlay_block_store final : public merkle_block_store {
public:
    overlay_block_store(
        const std::map<merkle_digest, merkle_block>& staged,
        const merkle_block_store* fallback)
        : staged_(staged), fallback_(fallback)
    {
    }

    [[nodiscard]] std::size_t size() const override { return digests().size(); }
    [[nodiscard]] std::vector<merkle_digest> digests() const override
    {
        auto result = std::set<merkle_digest>{};
        for (const auto& [digest, block] : staged_) {
            (void)block;
            result.insert(digest);
        }
        if (fallback_ != nullptr) {
            const auto fallback_digests = fallback_->digests();
            result.insert(fallback_digests.begin(), fallback_digests.end());
        }
        return {result.begin(), result.end()};
    }
    [[nodiscard]] bool contains(const merkle_digest digest) const override
    {
        return staged_.contains(digest)
            || (fallback_ != nullptr && fallback_->contains(digest));
    }
    [[nodiscard]] std::optional<merkle_block> get(const merkle_digest digest) const override
    {
        if (const auto iterator = staged_.find(digest); iterator != staged_.end()) {
            return iterator->second;
        }
        return fallback_ == nullptr ? std::nullopt : fallback_->get(digest);
    }
    bool put(merkle_block) override
    {
        throw std::logic_error("a verification overlay is read-only");
    }
    bool remove(merkle_digest) override
    {
        throw std::logic_error("a verification overlay is read-only");
    }
    void clear() override { throw std::logic_error("a verification overlay is read-only"); }

private:
    const std::map<merkle_digest, merkle_block>& staged_;
    const merkle_block_store* fallback_;
};

template <class K, class V>
[[nodiscard]] merkle_search_tree<K, V> load_with_context(
    const merkle_digest root_hash,
    const merkle_search_tree_policy<K, V>& policy,
    const merkle_block_store& store,
    verification_context& context)
{
    using access = persistence_access<K, V>;
    auto verifier = merkle_search_tree<K, V>::create(policy);
    if (root_hash == policy.empty_digest()) {
        return verifier;
    }
    auto cache = std::map<merkle_digest, persistence_node_pointer<K, V>>{};
    auto active = std::set<merkle_digest>{};
    auto root = load_node(verifier, root_hash, store, context, cache, active, 1);
    if (access::digest(*root) != root_hash) {
        fail(
            merkle_verification_failure_kind::root_mismatch,
            "the reconstructed root does not match the requested hash",
            root_hash);
    }
    auto tree = access::from_root(std::move(root), policy);
    try {
        (void)tree.validate_structure();
    } catch (const merkle_tree_invariant_error&) {
        fail(
            merkle_verification_failure_kind::invalid_reference,
            "the verified closure failed final structure validation",
            root_hash);
    }
    return tree;
}

} // namespace merkle_persistence_detail

/// Loads and fully verifies a closure under seven finite limits.
template <class K, class V>
[[nodiscard]] merkle_search_tree<K, V> load_merkle_tree(
    const merkle_digest root_hash,
    const merkle_search_tree_policy<K, V>& policy,
    const merkle_block_store& store,
    const merkle_verification_budget& budget = merkle_verification_budget{})
{
    auto context = merkle_persistence_detail::verification_context{budget};
    return merkle_persistence_detail::load_with_context(
        root_hash, policy, store, context);
}

/// Verifies a complete or partial pack and publishes only after closure/conflict preflight.
template <class K, class V>
[[nodiscard]] merkle_search_tree<K, V> import_merkle_pack(
    const merkle_block_pack& pack,
    const merkle_search_tree_policy<K, V>& policy,
    merkle_block_store* destination_store = nullptr,
    const merkle_verification_budget& budget = merkle_verification_budget{})
{
    merkle_persistence_detail::verify_envelope(
        pack.algorithm_id(), pack.domain_digest(), policy.domain_digest());
    if (pack.block_count() > budget.max_block_count()
        || pack.total_byte_count() > budget.max_total_byte_count()) {
        merkle_persistence_detail::fail(
            merkle_verification_failure_kind::resource_limit_exceeded,
            "a Merkle pack exceeds its block or total-byte budget",
            pack.root_hash());
    }
    for (const auto& block : pack.blocks()) {
        if (block.size() > budget.max_block_byte_count()) {
            merkle_persistence_detail::fail(
                merkle_verification_failure_kind::resource_limit_exceeded,
                "a Merkle pack block exceeds its per-block byte budget",
                block.digest());
        }
    }

    auto context = merkle_persistence_detail::verification_context{budget};
    const auto verifier = merkle_search_tree<K, V>::create(policy);
    auto staged = std::map<merkle_digest, merkle_block>{};
    for (const auto& block : pack.blocks()) {
        (void)merkle_persistence_detail::decode_block(verifier, block, context, 1);
        if (!staged.emplace(block.digest(), block).second) {
            merkle_persistence_detail::fail(
                merkle_verification_failure_kind::duplicate_block,
                "a Merkle pack repeats a block address",
                block.digest());
        }
    }
    const auto overlay = merkle_persistence_detail::overlay_block_store{
        staged, destination_store};
    auto loaded = merkle_persistence_detail::load_with_context(
        pack.root_hash(), policy, overlay, context);
    if (destination_store != nullptr) {
        merkle_persistence_detail::preflight_store(pack.blocks(), *destination_store);
        for (const auto& block : pack.blocks()) {
            (void)destination_store->put(block);
        }
    }
    return loaded;
}

} // namespace durable7::hamt
