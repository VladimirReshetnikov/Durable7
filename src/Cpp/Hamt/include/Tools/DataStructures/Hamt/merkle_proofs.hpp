#pragma once

#include <Tools/DataStructures/Hamt/merkle_persistence.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tools::data_structures::hamt {

/// Claim kind encoded by an exact `MSP2` query descriptor.
enum class merkle_proof_kind : std::uint8_t {
    membership = 0,
    non_membership = 1,
    range = 2,
};

/// One authenticated block and the child intervals expanded elsewhere in a proof.
class merkle_proof_step final {
public:
    merkle_proof_step(merkle_block block, std::vector<std::size_t> expanded_child_indexes)
        : block_(std::move(block)),
          expanded_child_indexes_(std::move(expanded_child_indexes))
    {
        std::ranges::sort(expanded_child_indexes_);
        if (std::adjacent_find(
                expanded_child_indexes_.begin(), expanded_child_indexes_.end())
            != expanded_child_indexes_.end()) {
            merkle_persistence_detail::fail(
                merkle_verification_failure_kind::proof_mismatch,
                "a proof step expands one child index more than once",
                block_.digest());
        }
    }

    [[nodiscard]] const merkle_block& block() const noexcept { return block_; }
    [[nodiscard]] std::span<const std::size_t> expanded_child_indexes() const noexcept
    {
        return expanded_child_indexes_;
    }

    friend bool operator==(const merkle_proof_step&, const merkle_proof_step&) = default;

private:
    merkle_block block_;
    std::vector<std::size_t> expanded_child_indexes_;
};

/// Immutable membership, nonmembership, or inclusive-range proof.
class merkle_proof final {
public:
    merkle_proof(
        std::string algorithm_id,
        const merkle_digest domain_digest,
        const merkle_digest root_hash,
        const merkle_proof_kind kind,
        merkle_bytes query,
        std::vector<merkle_proof_step> steps)
        : algorithm_id_(std::move(algorithm_id)),
          domain_digest_(domain_digest),
          root_hash_(root_hash),
          kind_(kind),
          query_(std::move(query)),
          steps_(std::move(steps))
    {
        if (merkle_persistence_detail::is_blank_identifier(algorithm_id_)) {
            throw std::invalid_argument("a Merkle proof algorithm identifier must not be blank");
        }
        auto unique = std::set<merkle_digest>{};
        total_byte_count_ = merkle_persistence_detail::as_u64(
            query_.size(), root_hash_, "a Merkle proof byte count overflowed");
        for (const auto& step : steps_) {
            if (!unique.insert(step.block().digest()).second) {
                merkle_persistence_detail::fail(
                    merkle_verification_failure_kind::duplicate_block,
                    "a proof contains a duplicate block address",
                    step.block().digest());
            }
            const auto length = merkle_persistence_detail::as_u64(
                step.block().size(),
                step.block().digest(),
                "a Merkle proof byte count overflowed");
            if (length > (std::numeric_limits<std::uint64_t>::max)() - total_byte_count_) {
                merkle_persistence_detail::fail(
                    merkle_verification_failure_kind::resource_limit_exceeded,
                    "a Merkle proof byte count overflowed",
                    step.block().digest());
            }
            total_byte_count_ += length;
        }
    }

    [[nodiscard]] const std::string& algorithm_id() const noexcept { return algorithm_id_; }
    [[nodiscard]] merkle_digest domain_digest() const noexcept { return domain_digest_; }
    [[nodiscard]] merkle_digest root_hash() const noexcept { return root_hash_; }
    [[nodiscard]] merkle_proof_kind kind() const noexcept { return kind_; }
    [[nodiscard]] std::span<const std::byte> query() const noexcept { return query_; }
    [[nodiscard]] std::span<const merkle_proof_step> steps() const noexcept { return steps_; }
    [[nodiscard]] std::uint64_t total_byte_count() const noexcept { return total_byte_count_; }

    friend bool operator==(const merkle_proof&, const merkle_proof&) = default;

private:
    std::string algorithm_id_;
    merkle_digest domain_digest_;
    merkle_digest root_hash_;
    merkle_proof_kind kind_;
    merkle_bytes query_;
    std::vector<merkle_proof_step> steps_;
    std::uint64_t total_byte_count_ = 0;
};

/// Immutable proof-verification outcome.
class merkle_proof_verification_result final {
public:
    [[nodiscard]] static merkle_proof_verification_result success(
        const merkle_digest root_hash,
        const std::size_t verified_block_count,
        const std::uint64_t verified_byte_count)
    {
        return merkle_proof_verification_result{
            true,
            merkle_verification_failure_kind::none,
            {},
            root_hash,
            verified_block_count,
            verified_byte_count};
    }

    [[nodiscard]] static merkle_proof_verification_result failure(
        const merkle_verification_error& error,
        const std::size_t verified_block_count,
        const std::uint64_t verified_byte_count,
        const std::optional<merkle_digest> root_hash)
    {
        return merkle_proof_verification_result{
            false,
            error.kind(),
            error.what(),
            root_hash,
            verified_block_count,
            verified_byte_count};
    }

    [[nodiscard]] bool valid() const noexcept { return valid_; }
    [[nodiscard]] merkle_verification_failure_kind failure_kind() const noexcept
    {
        return failure_kind_;
    }
    [[nodiscard]] const std::string& failure_message() const noexcept
    {
        return failure_message_;
    }
    [[nodiscard]] std::optional<merkle_digest> computed_root_hash() const noexcept
    {
        return computed_root_hash_;
    }
    [[nodiscard]] std::size_t verified_block_count() const noexcept
    {
        return verified_block_count_;
    }
    [[nodiscard]] std::uint64_t verified_byte_count() const noexcept
    {
        return verified_byte_count_;
    }

private:
    merkle_proof_verification_result(
        const bool valid,
        const merkle_verification_failure_kind failure_kind,
        std::string failure_message,
        const std::optional<merkle_digest> computed_root_hash,
        const std::size_t verified_block_count,
        const std::uint64_t verified_byte_count)
        : valid_(valid),
          failure_kind_(failure_kind),
          failure_message_(std::move(failure_message)),
          computed_root_hash_(computed_root_hash),
          verified_block_count_(verified_block_count),
          verified_byte_count_(verified_byte_count)
    {
    }

    bool valid_;
    merkle_verification_failure_kind failure_kind_;
    std::string failure_message_;
    std::optional<merkle_digest> computed_root_hash_;
    std::size_t verified_block_count_;
    std::uint64_t verified_byte_count_;
};

/// Presence or absence of a merge side; a present nullable value remains present.
template <class V>
class merkle_merge_value final {
public:
    [[nodiscard]] static merkle_merge_value absent() noexcept { return merkle_merge_value{}; }
    [[nodiscard]] static merkle_merge_value present(std::shared_ptr<const V> value)
    {
        if (value == nullptr) {
            throw std::invalid_argument("a present merge value requires a value handle");
        }
        return merkle_merge_value{std::move(value)};
    }

    [[nodiscard]] bool is_present() const noexcept { return value_ != nullptr; }
    [[nodiscard]] const V* value() const noexcept { return value_.get(); }
    [[nodiscard]] std::shared_ptr<const V> value_handle() const noexcept { return value_; }

private:
    merkle_merge_value() = default;
    explicit merkle_merge_value(std::shared_ptr<const V> value)
        : value_(std::move(value))
    {
    }

    std::shared_ptr<const V> value_;
};

/// One true key-level three-way merge conflict with shared representatives.
template <class K, class V>
struct merkle_three_way_merge_conflict final {
    std::shared_ptr<const K> key;
    merkle_merge_value<V> base;
    merkle_merge_value<V> left;
    merkle_merge_value<V> right;
};

enum class merkle_merge_resolution_kind {
    unresolved,
    use_base,
    use_left,
    use_right,
    set_value,
    delete_key,
};

/// Typed conflict resolution supporting move-only values.
template <class V>
class merkle_merge_resolution final {
public:
    [[nodiscard]] static merkle_merge_resolution unresolved()
    {
        return merkle_merge_resolution{merkle_merge_resolution_kind::unresolved};
    }
    [[nodiscard]] static merkle_merge_resolution use_base()
    {
        return merkle_merge_resolution{merkle_merge_resolution_kind::use_base};
    }
    [[nodiscard]] static merkle_merge_resolution use_left()
    {
        return merkle_merge_resolution{merkle_merge_resolution_kind::use_left};
    }
    [[nodiscard]] static merkle_merge_resolution use_right()
    {
        return merkle_merge_resolution{merkle_merge_resolution_kind::use_right};
    }
    [[nodiscard]] static merkle_merge_resolution delete_key()
    {
        return merkle_merge_resolution{merkle_merge_resolution_kind::delete_key};
    }
    [[nodiscard]] static merkle_merge_resolution set_value(V value)
    {
        return merkle_merge_resolution{std::move(value)};
    }

    [[nodiscard]] merkle_merge_resolution_kind kind() const noexcept { return kind_; }

    [[nodiscard]] V take_value() &&
    {
        if (kind_ != merkle_merge_resolution_kind::set_value || !value_.has_value()) {
            throw std::logic_error("this merge resolution does not carry a value");
        }
        return std::move(*value_);
    }

private:
    explicit merkle_merge_resolution(const merkle_merge_resolution_kind kind)
        : kind_(kind)
    {
    }
    explicit merkle_merge_resolution(V value)
        : kind_(merkle_merge_resolution_kind::set_value),
          value_(std::move(value))
    {
    }

    merkle_merge_resolution_kind kind_;
    std::optional<V> value_;
};

/// Complete publishable tree or all unresolved conflicts, never a partial tree.
template <class K, class V>
class merkle_three_way_merge_result final {
public:
    using tree_type = merkle_search_tree<K, V>;
    using conflict_type = merkle_three_way_merge_conflict<K, V>;

    [[nodiscard]] static merkle_three_way_merge_result success(tree_type tree)
    {
        return merkle_three_way_merge_result{std::move(tree), {}};
    }
    [[nodiscard]] static merkle_three_way_merge_result conflicted(
        std::vector<conflict_type> conflicts)
    {
        if (conflicts.empty()) {
            throw std::invalid_argument("a conflicted merge result requires a conflict");
        }
        return merkle_three_way_merge_result{std::nullopt, std::move(conflicts)};
    }

    [[nodiscard]] bool success() const noexcept { return merged_tree_.has_value(); }
    [[nodiscard]] const tree_type* merged_tree() const noexcept
    {
        return merged_tree_ ? &*merged_tree_ : nullptr;
    }
    [[nodiscard]] std::optional<tree_type> take_merged_tree() &&
    {
        return std::move(merged_tree_);
    }
    [[nodiscard]] std::span<const conflict_type> unresolved_conflicts() const noexcept
    {
        return conflicts_;
    }

private:
    merkle_three_way_merge_result(
        std::optional<tree_type> tree,
        std::vector<conflict_type> conflicts)
        : merged_tree_(std::move(tree)), conflicts_(std::move(conflicts))
    {
    }

    std::optional<tree_type> merged_tree_;
    std::vector<conflict_type> conflicts_;
};

namespace merkle_proof_detail {

inline constexpr auto proof_magic = std::string_view{"MSP2"};

inline void require_query_length(const std::size_t length)
{
    if (length > static_cast<std::size_t>((std::numeric_limits<std::int32_t>::max)())) {
        throw std::length_error("an MSP2 query field exceeds its signed 32-bit wire limit");
    }
}

inline void append_query_field(merkle_bytes& destination, const std::span<const std::byte> field)
{
    require_query_length(field.size());
    merkle_detail::append_i32_be(destination, static_cast<std::int32_t>(field.size()));
    destination.insert(destination.end(), field.begin(), field.end());
}

[[nodiscard]] inline merkle_bytes encode_point_query(
    const merkle_proof_kind kind,
    const std::span<const std::byte> key,
    const std::optional<std::span<const std::byte>> value)
{
    if ((kind == merkle_proof_kind::membership) != value.has_value()
        || kind == merkle_proof_kind::range) {
        throw std::invalid_argument("only membership queries carry canonical value bytes");
    }
    require_query_length(key.size());
    if (value.has_value()) {
        require_query_length(value->size());
    }
    auto result = merkle_bytes{};
    auto length = std::size_t{proof_magic.size() + 1 + 4 + key.size()};
    if (value.has_value()) {
        if (value->size() > (std::numeric_limits<std::size_t>::max)() - length - 4) {
            throw std::length_error("an MSP2 query exceeds native size limits");
        }
        length += 4 + value->size();
    }
    result.reserve(length);
    const auto magic = merkle_detail::bytes_of(proof_magic);
    result.insert(result.end(), magic.begin(), magic.end());
    result.push_back(merkle_detail::as_byte(static_cast<std::uint8_t>(kind)));
    append_query_field(result, key);
    if (value.has_value()) {
        append_query_field(result, *value);
    }
    return result;
}

[[nodiscard]] inline merkle_bytes encode_range_query(
    const std::span<const std::byte> minimum,
    const std::span<const std::byte> maximum)
{
    require_query_length(minimum.size());
    require_query_length(maximum.size());
    if (maximum.size() > (std::numeric_limits<std::size_t>::max)()
        - proof_magic.size() - 1 - 8 - minimum.size()) {
        throw std::length_error("an MSP2 range query exceeds native size limits");
    }
    auto result = merkle_bytes{};
    result.reserve(proof_magic.size() + 1 + 8 + minimum.size() + maximum.size());
    const auto magic = merkle_detail::bytes_of(proof_magic);
    result.insert(result.end(), magic.begin(), magic.end());
    result.push_back(merkle_detail::as_byte(
        static_cast<std::uint8_t>(merkle_proof_kind::range)));
    append_query_field(result, minimum);
    append_query_field(result, maximum);
    return result;
}

template <class K, class V>
[[nodiscard]] bool child_interval_intersects(
    const merkle_search_tree<K, V>& tree,
    const std::vector<typename merkle_search_tree<K, V>::entry_type>& entries,
    const std::size_t child_index,
    const K& minimum,
    const K& maximum)
{
    return (child_index == 0
            || tree.policy().compare(entries[child_index - 1].key(), maximum) < 0)
        && (child_index == entries.size()
            || tree.policy().compare(entries[child_index].key(), minimum) > 0);
}

template <class K, class V>
void collect_range_steps(
    const merkle_search_tree<K, V>& tree,
    const typename merkle_persistence_detail::access<K, V>::node_pointer& node,
    const K& minimum,
    const K& maximum,
    std::vector<merkle_proof_step>& steps)
{
    using access = merkle_persistence_detail::access<K, V>;
    if (node == nullptr) {
        return;
    }
    const auto& entries = access::entries(*node);
    const auto& children = access::children(*node);
    auto expanded = std::vector<std::size_t>{};
    for (auto index = std::size_t{0}; index != children.size(); ++index) {
        if (children[index] != nullptr
            && child_interval_intersects(tree, entries, index, minimum, maximum)) {
            expanded.push_back(index);
        }
    }
    steps.emplace_back(
        merkle_block{access::digest(*node), access::block_bytes(*node)},
        expanded);
    for (const auto index : expanded) {
        collect_range_steps(tree, children[index], minimum, maximum, steps);
    }
}

} // namespace merkle_proof_detail

/// Creates the canonical exact `MSP2` membership or nonmembership proof for a key.
template <class K, class V>
[[nodiscard]] merkle_proof create_merkle_proof(
    const merkle_search_tree<K, V>& tree,
    const K& key)
{
    using access = merkle_persistence_detail::access<K, V>;
    const auto key_bytes = tree.policy().key_codec().encode(key);
    auto steps = std::vector<merkle_proof_step>{};
    auto node = access::root(tree);
    auto kind = merkle_proof_kind::non_membership;
    const merkle_bytes* value_bytes = nullptr;
    while (node != nullptr) {
        const auto [position, found] = access::find_position(tree, access::entries(*node), key);
        auto expanded = std::vector<std::size_t>{};
        if (!found && access::children(*node)[position] != nullptr) {
            expanded.push_back(position);
        }
        steps.emplace_back(
            merkle_block{access::digest(*node), access::block_bytes(*node)},
            std::move(expanded));
        if (found) {
            kind = merkle_proof_kind::membership;
            value_bytes = &access::entries(*node)[position].value_bytes();
            break;
        }
        node = access::children(*node)[position];
    }
    return merkle_proof{
        std::string{merkle_search_tree_policy<K, V>::algorithm_id},
        tree.policy().domain_digest(),
        tree.root_hash(),
        kind,
        merkle_proof_detail::encode_point_query(
            kind,
            key_bytes,
            value_bytes == nullptr
                ? std::nullopt
                : std::optional<std::span<const std::byte>>{*value_bytes}),
        std::move(steps)};
}

/// Creates a canonical completeness proof for an inclusive comparator-ordered range.
template <class K, class V>
[[nodiscard]] merkle_proof create_merkle_range_proof(
    const merkle_search_tree<K, V>& tree,
    const K& minimum,
    const K& maximum)
{
    using access = merkle_persistence_detail::access<K, V>;
    if (tree.policy().compare(minimum, maximum) > 0) {
        throw merkle_range_error{};
    }
    const auto minimum_bytes = tree.policy().key_codec().encode(minimum);
    const auto maximum_bytes = tree.policy().key_codec().encode(maximum);
    auto steps = std::vector<merkle_proof_step>{};
    merkle_proof_detail::collect_range_steps(
        tree, access::root(tree), minimum, maximum, steps);
    return merkle_proof{
        std::string{merkle_search_tree_policy<K, V>::algorithm_id},
        tree.policy().domain_digest(),
        tree.root_hash(),
        merkle_proof_kind::range,
        merkle_proof_detail::encode_range_query(minimum_bytes, maximum_bytes),
        std::move(steps)};
}

namespace merkle_proof_detail {

template <class T>
[[nodiscard]] T decode_query_value(
    const merkle_codec<T>& codec,
    const std::span<const std::byte> bytes,
    const char* field,
    const merkle_digest root_hash)
{
    T value = [&]() -> T {
        try {
            return codec.decode(bytes);
        } catch (const merkle_codec_error&) {
            merkle_persistence_detail::fail(
                merkle_verification_failure_kind::proof_mismatch,
                std::string{"a proof query contains an invalid "} + field + " encoding",
                root_hash);
        } catch (const std::invalid_argument&) {
            merkle_persistence_detail::fail(
                merkle_verification_failure_kind::proof_mismatch,
                std::string{"a proof query contains an invalid "} + field + " encoding",
                root_hash);
        }
    }();
    merkle_bytes canonical;
    try {
        canonical = codec.encode(value);
    } catch (const merkle_codec_error&) {
        merkle_persistence_detail::fail(
            merkle_verification_failure_kind::proof_mismatch,
            std::string{"a proof query "} + field + " could not be re-encoded",
            root_hash);
    } catch (const std::invalid_argument&) {
        merkle_persistence_detail::fail(
            merkle_verification_failure_kind::proof_mismatch,
            std::string{"a proof query "} + field + " could not be re-encoded",
            root_hash);
    }
    if (!std::ranges::equal(canonical, bytes)) {
        merkle_persistence_detail::fail(
            merkle_verification_failure_kind::proof_mismatch,
            std::string{"a proof query "} + field + " is not canonically encoded",
            root_hash);
    }
    return value;
}

inline merkle_persistence_detail::byte_reader query_reader(const merkle_proof& proof)
{
    auto reader = merkle_persistence_detail::byte_reader{
        proof.query(),
        proof.root_hash(),
        merkle_verification_failure_kind::proof_mismatch};
    if (!std::ranges::equal(
            reader.take(proof_magic.size()), merkle_detail::bytes_of(proof_magic))) {
        merkle_persistence_detail::fail(
            merkle_verification_failure_kind::proof_mismatch,
            "a proof query has the wrong magic",
            proof.root_hash());
    }
    if (reader.read_u8() != static_cast<std::uint8_t>(proof.kind())) {
        merkle_persistence_detail::fail(
            merkle_verification_failure_kind::proof_mismatch,
            "a proof query kind disagrees with its envelope",
            proof.root_hash());
    }
    return reader;
}

template <class K>
struct point_query final {
    K key;
    std::optional<std::span<const std::byte>> value_bytes;
};

template <class K>
struct range_query final {
    K minimum;
    K maximum;
};

template <class K, class V>
[[nodiscard]] point_query<K> decode_point_query(
    const merkle_search_tree<K, V>& verifier,
    const merkle_proof& proof)
{
    if (proof.kind() == merkle_proof_kind::range) {
        merkle_persistence_detail::fail(
            merkle_verification_failure_kind::proof_mismatch,
            "a point proof has the wrong kind",
            proof.root_hash());
    }
    auto reader = query_reader(proof);
    const auto key_bytes = reader.read_length_prefixed();
    auto value_bytes = std::optional<std::span<const std::byte>>{};
    if (proof.kind() == merkle_proof_kind::membership) {
        value_bytes = reader.read_length_prefixed();
        (void)decode_query_value(
            verifier.policy().value_codec(), *value_bytes, "value", proof.root_hash());
    }
    if (!reader.at_end()) {
        merkle_persistence_detail::fail(
            merkle_verification_failure_kind::proof_mismatch,
            "a point-proof query has trailing bytes",
            proof.root_hash());
    }
    return point_query<K>{
        decode_query_value(
            verifier.policy().key_codec(), key_bytes, "key", proof.root_hash()),
        value_bytes};
}

template <class K, class V>
[[nodiscard]] range_query<K> decode_range_query(
    const merkle_search_tree<K, V>& verifier,
    const merkle_proof& proof)
{
    if (proof.kind() != merkle_proof_kind::range) {
        merkle_persistence_detail::fail(
            merkle_verification_failure_kind::proof_mismatch,
            "a range proof has the wrong kind",
            proof.root_hash());
    }
    auto reader = query_reader(proof);
    auto minimum = decode_query_value(
        verifier.policy().key_codec(),
        reader.read_length_prefixed(),
        "key",
        proof.root_hash());
    auto maximum = decode_query_value(
        verifier.policy().key_codec(),
        reader.read_length_prefixed(),
        "key",
        proof.root_hash());
    if (!reader.at_end()) {
        merkle_persistence_detail::fail(
            merkle_verification_failure_kind::proof_mismatch,
            "a range-proof query has trailing bytes",
            proof.root_hash());
    }
    if (verifier.policy().compare(minimum, maximum) > 0) {
        merkle_persistence_detail::fail(
            merkle_verification_failure_kind::proof_mismatch,
            "a range-proof query has reversed bounds",
            proof.root_hash());
    }
    return range_query<K>{std::move(minimum), std::move(maximum)};
}

inline void require_expanded_exactly(
    const merkle_proof_step& step,
    const std::span<const std::size_t> expected)
{
    if (!std::ranges::equal(step.expanded_child_indexes(), expected)) {
        merkle_persistence_detail::fail(
            merkle_verification_failure_kind::proof_mismatch,
            "a proof step expands a noncanonical set of child intervals",
            step.block().digest());
    }
}

template <class K, class V>
void validate_proof_reference(
    const merkle_search_tree<K, V>& verifier,
    const merkle_persistence_detail::decoded_block<K, V>& parent,
    const std::size_t child_index,
    const merkle_persistence_detail::decoded_block<K, V>& child)
{
    if (child.level >= parent.level) {
        merkle_persistence_detail::fail(
            merkle_verification_failure_kind::invalid_reference,
            "an expanded proof child is not below its parent level",
            child.block.digest());
    }
    for (const auto& entry : child.entries) {
        if (child_index != 0
            && verifier.policy().compare(
                   entry.key(), parent.entries[child_index - 1].key()) <= 0) {
            merkle_persistence_detail::fail(
                merkle_verification_failure_kind::invalid_reference,
                "an expanded proof child crosses its lower separator",
                child.block.digest());
        }
        if (child_index != parent.entries.size()
            && verifier.policy().compare(
                   entry.key(), parent.entries[child_index].key()) >= 0) {
            merkle_persistence_detail::fail(
                merkle_verification_failure_kind::invalid_reference,
                "an expanded proof child crosses its upper separator",
                child.block.digest());
        }
    }
}

template <class K, class V>
void verify_point(
    const merkle_search_tree<K, V>& verifier,
    const merkle_proof& proof,
    const std::map<merkle_digest, merkle_persistence_detail::decoded_block<K, V>>& decoded,
    const std::map<merkle_digest, const merkle_proof_step*>& steps,
    std::set<merkle_digest>& visited,
    merkle_persistence_detail::verification_context& context)
{
    using access = merkle_persistence_detail::access<K, V>;
    const auto query = decode_point_query(verifier, proof);
    auto digest = proof.root_hash();
    auto depth = std::size_t{1};
    while (true) {
        context.check_depth(depth, digest);
        if (!visited.insert(digest).second) {
            merkle_persistence_detail::fail(
                merkle_verification_failure_kind::cycle_detected,
                "a proof expansion contains a cycle",
                digest);
        }
        const auto block_iterator = decoded.find(digest);
        const auto step_iterator = steps.find(digest);
        if (block_iterator == decoded.end() || step_iterator == steps.end()) {
            merkle_persistence_detail::fail(
                merkle_verification_failure_kind::missing_block,
                "a proof omits an expanded point-query block",
                digest);
        }
        const auto& block = block_iterator->second;
        const auto& step = *step_iterator->second;
        const auto [position, found] = access::find_position(
            verifier, block.entries, query.key);
        if (found) {
            require_expanded_exactly(step, {});
            if (proof.kind() != merkle_proof_kind::membership) {
                merkle_persistence_detail::fail(
                    merkle_verification_failure_kind::proof_mismatch,
                    "the proof claims absence for a present key",
                    digest);
            }
            if (!query.value_bytes.has_value()
                || !std::ranges::equal(
                    block.entries[position].value_bytes(), *query.value_bytes)) {
                merkle_persistence_detail::fail(
                    merkle_verification_failure_kind::proof_mismatch,
                    "the membership value claim disagrees with the authenticated entry",
                    digest);
            }
            return;
        }
        const auto child_digest = block.child_digests[position];
        if (child_digest == verifier.policy().empty_digest()) {
            require_expanded_exactly(step, {});
            if (proof.kind() != merkle_proof_kind::non_membership) {
                merkle_persistence_detail::fail(
                    merkle_verification_failure_kind::proof_mismatch,
                    "the proof claims membership for an absent key",
                    digest);
            }
            return;
        }
        const auto expected = std::array<std::size_t, 1>{position};
        require_expanded_exactly(step, expected);
        const auto child = decoded.find(child_digest);
        if (child == decoded.end()) {
            merkle_persistence_detail::fail(
                merkle_verification_failure_kind::missing_block,
                "a proof omits its expanded child block",
                child_digest);
        }
        validate_proof_reference(verifier, block, position, child->second);
        digest = child_digest;
        if (depth == (std::numeric_limits<std::size_t>::max)()) {
            merkle_persistence_detail::fail(
                merkle_verification_failure_kind::resource_limit_exceeded,
                "a proof depth overflowed",
                digest);
        }
        ++depth;
    }
}

template <class K, class V>
void verify_range_block(
    const merkle_search_tree<K, V>& verifier,
    const merkle_digest digest,
    const K& minimum,
    const K& maximum,
    const std::map<merkle_digest, merkle_persistence_detail::decoded_block<K, V>>& decoded,
    const std::map<merkle_digest, const merkle_proof_step*>& steps,
    std::set<merkle_digest>& visited,
    merkle_persistence_detail::verification_context& context,
    const std::size_t depth)
{
    context.check_depth(depth, digest);
    if (!visited.insert(digest).second) {
        merkle_persistence_detail::fail(
            merkle_verification_failure_kind::cycle_detected,
            "a range-proof expansion contains a repeated block",
            digest);
    }
    const auto block_iterator = decoded.find(digest);
    const auto step_iterator = steps.find(digest);
    if (block_iterator == decoded.end() || step_iterator == steps.end()) {
        merkle_persistence_detail::fail(
            merkle_verification_failure_kind::missing_block,
            "a range proof omits an expanded block",
            digest);
    }
    const auto& block = block_iterator->second;
    const auto& step = *step_iterator->second;
    auto expected = std::vector<std::size_t>{};
    for (auto index = std::size_t{0}; index != block.child_digests.size(); ++index) {
        if (block.child_digests[index] != verifier.policy().empty_digest()
            && child_interval_intersects(
                verifier, block.entries, index, minimum, maximum)) {
            expected.push_back(index);
        }
    }
    require_expanded_exactly(step, expected);
    for (const auto index : expected) {
        const auto child_digest = block.child_digests[index];
        const auto child = decoded.find(child_digest);
        if (child == decoded.end()) {
            merkle_persistence_detail::fail(
                merkle_verification_failure_kind::missing_block,
                "a range proof omits an expanded child block",
                child_digest);
        }
        validate_proof_reference(verifier, block, index, child->second);
        if (depth == (std::numeric_limits<std::size_t>::max)()) {
            merkle_persistence_detail::fail(
                merkle_verification_failure_kind::resource_limit_exceeded,
                "a range-proof depth overflowed",
                child_digest);
        }
        verify_range_block(
            verifier,
            child_digest,
            minimum,
            maximum,
            decoded,
            steps,
            visited,
            context,
            depth + 1);
    }
}

template <class K, class V>
void verify_empty_query(
    const merkle_search_tree<K, V>& verifier,
    const merkle_proof& proof)
{
    switch (proof.kind()) {
    case merkle_proof_kind::non_membership:
        (void)decode_point_query(verifier, proof);
        return;
    case merkle_proof_kind::range:
        (void)decode_range_query(verifier, proof);
        return;
    case merkle_proof_kind::membership:
        merkle_persistence_detail::fail(
            merkle_verification_failure_kind::proof_mismatch,
            "an empty tree cannot prove membership",
            proof.root_hash());
    }
    merkle_persistence_detail::fail(
        merkle_verification_failure_kind::proof_mismatch,
        "a proof has an unsupported kind",
        proof.root_hash());
}

} // namespace merkle_proof_detail

/// Verifies an untrusted proof with query/shape preflight before allocation, hashing, or codecs.
template <class K, class V>
[[nodiscard]] merkle_proof_verification_result verify_merkle_proof(
    const merkle_proof& proof,
    const merkle_search_tree_policy<K, V>& policy,
    const merkle_verification_budget& budget = merkle_verification_budget{})
{
    auto context = merkle_persistence_detail::verification_context{budget};
    try {
        context.account_proof_query(proof.query().size(), proof.root_hash());

        // These public-envelope counts are intentionally checked before maps, envelope hashing,
        // block hashing, codec invocation, or decoded-entry allocation.
        if (proof.steps().size() > budget.max_block_count()) {
            merkle_persistence_detail::fail(
                merkle_verification_failure_kind::resource_limit_exceeded,
                "a Merkle proof exceeds the block-count budget",
                proof.root_hash());
        }
        for (const auto& step : proof.steps()) {
            if (step.expanded_child_indexes().size()
                > budget.max_child_references_per_block()) {
                merkle_persistence_detail::fail(
                    merkle_verification_failure_kind::resource_limit_exceeded,
                    "a proof step exceeds the child-reference budget",
                    proof.root_hash());
            }
        }
        merkle_persistence_detail::verify_envelope(
            proof.algorithm_id(), proof.domain_digest(), policy.domain_digest());
        auto preflight_bytes = context.total_bytes();
        for (const auto& step : proof.steps()) {
            if (step.block().size() > budget.max_block_byte_count()) {
                merkle_persistence_detail::fail(
                    merkle_verification_failure_kind::resource_limit_exceeded,
                    "a proof block exceeds the per-block byte budget",
                    step.block().digest());
            }
            const auto length = merkle_persistence_detail::as_u64(
                step.block().size(),
                step.block().digest(),
                "a proof block length exceeds verification limits");
            if (length > budget.max_total_byte_count() - preflight_bytes) {
                merkle_persistence_detail::fail(
                    merkle_verification_failure_kind::resource_limit_exceeded,
                    "a proof exceeds the total-byte budget",
                    step.block().digest());
            }
            preflight_bytes += length;
        }
        const auto verifier = merkle_search_tree<K, V>::create(policy);
        auto decoded = std::map<
            merkle_digest,
            merkle_persistence_detail::decoded_block<K, V>>{};
        auto steps = std::map<merkle_digest, const merkle_proof_step*>{};
        for (const auto& step : proof.steps()) {
            auto block = merkle_persistence_detail::decode_block(
                verifier, step.block(), context, 1);
            if (!decoded.emplace(step.block().digest(), std::move(block)).second
                || !steps.emplace(step.block().digest(), &step).second) {
                merkle_persistence_detail::fail(
                    merkle_verification_failure_kind::duplicate_block,
                    "a proof repeats a block address",
                    step.block().digest());
            }
            const auto& decoded_block = decoded.find(step.block().digest())->second;
            for (const auto index : step.expanded_child_indexes()) {
                if (index >= decoded_block.child_digests.size()
                    || decoded_block.child_digests[index] == policy.empty_digest()) {
                    merkle_persistence_detail::fail(
                        merkle_verification_failure_kind::proof_mismatch,
                        "a proof expands an invalid or empty child interval",
                        step.block().digest());
                }
            }
        }

        if (proof.root_hash() == policy.empty_digest()) {
            if (!proof.steps().empty()) {
                merkle_persistence_detail::fail(
                    merkle_verification_failure_kind::proof_mismatch,
                    "an empty-root proof must not contain blocks",
                    proof.root_hash());
            }
            merkle_proof_detail::verify_empty_query(verifier, proof);
        } else {
            if (!decoded.contains(proof.root_hash())) {
                merkle_persistence_detail::fail(
                    merkle_verification_failure_kind::root_mismatch,
                    "the proof does not contain its declared root block",
                    proof.root_hash());
            }
            auto visited = std::set<merkle_digest>{};
            if (proof.kind() == merkle_proof_kind::range) {
                const auto query = merkle_proof_detail::decode_range_query(verifier, proof);
                merkle_proof_detail::verify_range_block(
                    verifier,
                    proof.root_hash(),
                    query.minimum,
                    query.maximum,
                    decoded,
                    steps,
                    visited,
                    context,
                    1);
            } else {
                merkle_proof_detail::verify_point(
                    verifier, proof, decoded, steps, visited, context);
            }
            if (visited.size() != proof.steps().size()) {
                merkle_persistence_detail::fail(
                    merkle_verification_failure_kind::proof_mismatch,
                    "the proof contains blocks outside the canonical query expansion",
                    proof.root_hash());
            }
        }
        return merkle_proof_verification_result::success(
            proof.root_hash(), context.block_count(), context.total_bytes());
    } catch (const merkle_verification_error& error) {
        return merkle_proof_verification_result::failure(
            error,
            context.block_count(),
            context.total_bytes(),
            proof.root_hash());
    }
}

namespace merkle_merge_detail {

template <class K, class V>
struct merge_side final {
    const typename merkle_search_tree<K, V>::entry_type* entry = nullptr;

    [[nodiscard]] bool present() const noexcept { return entry != nullptr; }
    [[nodiscard]] merkle_merge_value<V> value() const
    {
        return entry == nullptr
            ? merkle_merge_value<V>::absent()
            : merkle_merge_value<V>::present(entry->value_handle());
    }
};

template <class K, class V>
[[nodiscard]] merge_side<K, V> take_side(
    const merkle_search_tree_policy<K, V>& policy,
    const std::vector<typename merkle_search_tree<K, V>::entry_type>& entries,
    const K& key,
    const std::size_t index)
{
    return index < entries.size() && policy.compare(entries[index].key(), key) == 0
        ? merge_side<K, V>{&entries[index]}
        : merge_side<K, V>{};
}

template <class V, class ValuesEqual>
[[nodiscard]] bool values_equal(
    const merkle_merge_value<V>& left,
    const merkle_merge_value<V>& right,
    ValuesEqual& equal)
{
    if (left.is_present() != right.is_present()) {
        return false;
    }
    return !left.is_present() || std::invoke(equal, *left.value(), *right.value());
}

template <class K, class V>
void append_side(
    std::vector<typename merkle_search_tree<K, V>::entry_type>& output,
    const merge_side<K, V> side)
{
    if (side.entry != nullptr) {
        output.push_back(*side.entry);
    }
}

} // namespace merkle_merge_detail

/// Combines descendants relative to a common base, withholding output on unresolved conflicts.
template <class K, class V, class Resolver, class ValuesEqual = std::equal_to<V>>
[[nodiscard]] merkle_three_way_merge_result<K, V> merge_merkle_trees(
    const merkle_search_tree<K, V>& base,
    const merkle_search_tree<K, V>& left,
    const merkle_search_tree<K, V>& right,
    Resolver resolver,
    ValuesEqual values_equal = {})
{
    using tree_type = merkle_search_tree<K, V>;
    using entry_type = typename tree_type::entry_type;
    using conflict_type = merkle_three_way_merge_conflict<K, V>;
    using access = merkle_persistence_detail::access<K, V>;
    if (!base.policy().is_compatible_with(left.policy())
        || !base.policy().is_compatible_with(right.policy())) {
        throw merkle_policy_mismatch{};
    }
    if (left.root_hash() == base.root_hash()) {
        return merkle_three_way_merge_result<K, V>::success(right);
    }
    if (right.root_hash() == base.root_hash() || left.root_hash() == right.root_hash()) {
        return merkle_three_way_merge_result<K, V>::success(left);
    }

    auto base_entries = std::vector<entry_type>{};
    auto left_entries = std::vector<entry_type>{};
    auto right_entries = std::vector<entry_type>{};
    base_entries.reserve(base.size());
    left_entries.reserve(left.size());
    right_entries.reserve(right.size());
    for (const auto& entry : base) {
        base_entries.push_back(entry);
    }
    for (const auto& entry : left) {
        left_entries.push_back(entry);
    }
    for (const auto& entry : right) {
        right_entries.push_back(entry);
    }
    auto output = std::vector<entry_type>{};
    output.reserve((std::max)({base.size(), left.size(), right.size()}));
    auto conflicts = std::vector<conflict_type>{};
    auto base_index = std::size_t{0};
    auto left_index = std::size_t{0};
    auto right_index = std::size_t{0};
    while (base_index != base_entries.size()
        || left_index != left_entries.size()
        || right_index != right_entries.size()) {
        const entry_type* selected = base_index == base_entries.size()
            ? nullptr
            : &base_entries[base_index];
        if (left_index != left_entries.size()
            && (selected == nullptr
                || base.policy().compare(left_entries[left_index].key(), selected->key()) < 0)) {
            selected = &left_entries[left_index];
        }
        if (right_index != right_entries.size()
            && (selected == nullptr
                || base.policy().compare(right_entries[right_index].key(), selected->key()) < 0)) {
            selected = &right_entries[right_index];
        }
        if (selected == nullptr) {
            throw merkle_tree_invariant_error("a merge iteration failed to select a key");
        }
        const auto& key = selected->key();
        const auto base_side = merkle_merge_detail::take_side(
            base.policy(), base_entries, key, base_index);
        const auto left_side = merkle_merge_detail::take_side(
            base.policy(), left_entries, key, left_index);
        const auto right_side = merkle_merge_detail::take_side(
            base.policy(), right_entries, key, right_index);
        base_index += base_side.present() ? 1 : 0;
        left_index += left_side.present() ? 1 : 0;
        right_index += right_side.present() ? 1 : 0;
        const auto base_value = base_side.value();
        const auto left_value = left_side.value();
        const auto right_value = right_side.value();
        if (merkle_merge_detail::values_equal(left_value, base_value, values_equal)) {
            merkle_merge_detail::append_side(output, right_side);
            continue;
        }
        if (merkle_merge_detail::values_equal(right_value, base_value, values_equal)
            || merkle_merge_detail::values_equal(left_value, right_value, values_equal)) {
            merkle_merge_detail::append_side(output, left_side);
            continue;
        }

        const auto representative = left_side.present()
            ? left_side.entry
            : right_side.present() ? right_side.entry : base_side.entry;
        if (representative == nullptr) {
            throw merkle_tree_invariant_error("a merge conflict has no representative entry");
        }
        auto conflict = conflict_type{
            representative->key_handle(),
            base_value,
            left_value,
            right_value};
        auto resolution = std::invoke(resolver, std::as_const(conflict));
        switch (resolution.kind()) {
        case merkle_merge_resolution_kind::unresolved:
            conflicts.push_back(std::move(conflict));
            break;
        case merkle_merge_resolution_kind::use_base:
            merkle_merge_detail::append_side(output, base_side);
            break;
        case merkle_merge_resolution_kind::use_left:
            merkle_merge_detail::append_side(output, left_side);
            break;
        case merkle_merge_resolution_kind::use_right:
            merkle_merge_detail::append_side(output, right_side);
            break;
        case merkle_merge_resolution_kind::delete_key:
            break;
        case merkle_merge_resolution_kind::set_value: {
            auto value = std::move(resolution).take_value();
            auto value_bytes = base.policy().value_codec().encode(value);
            output.push_back(access::replace_value(
                *representative, std::move(value), std::move(value_bytes)));
            break;
        }
        }
    }
    if (!conflicts.empty()) {
        return merkle_three_way_merge_result<K, V>::conflicted(std::move(conflicts));
    }
    auto root = access::build_canonical(base, output);
    return merkle_three_way_merge_result<K, V>::success(
        access::from_root(std::move(root), base.policy()));
}

/// Three-way merge without a resolver; every true conflict remains unresolved.
template <class K, class V>
[[nodiscard]] merkle_three_way_merge_result<K, V> merge_merkle_trees(
    const merkle_search_tree<K, V>& base,
    const merkle_search_tree<K, V>& left,
    const merkle_search_tree<K, V>& right)
{
    const auto unresolved = [](const merkle_three_way_merge_conflict<K, V>&) {
        return merkle_merge_resolution<V>::unresolved();
    };
    return merge_merkle_trees(base, left, right, unresolved, std::equal_to<V>{});
}

} // namespace tools::data_structures::hamt
