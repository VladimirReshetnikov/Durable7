#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#ifdef small
#undef small
#endif
#else
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#endif

namespace durable7::finger_tree {

/// The deterministic priority attached to one comparison-equivalence class.
struct zip_tree_rank final {
    std::uint32_t geometric = 0;
    std::uint64_t secondary = 0;
    std::uint64_t content = 0;

    friend bool operator==(const zip_tree_rank&, const zip_tree_rank&) = default;
};

namespace canonical_sorted_set_detail {

inline constexpr auto minimum_rank_key_bytes = std::size_t{32};
inline constexpr auto rank_key_bytes = std::size_t{32};

#if defined(_WIN32)

[[noreturn]] inline void throw_bcrypt_failure(const char* operation, const NTSTATUS status)
{
    throw std::runtime_error(
        std::string{operation} + " failed with NTSTATUS "
        + std::to_string(static_cast<std::int32_t>(status)));
}

inline void require_bcrypt_success(const char* operation, const NTSTATUS status)
{
    if (status < 0) {
        throw_bcrypt_failure(operation, status);
    }
}

class bcrypt_provider final {
public:
    bcrypt_provider(const wchar_t* algorithm, const ULONG flags)
    {
        require_bcrypt_success(
            "BCryptOpenAlgorithmProvider",
            BCryptOpenAlgorithmProvider(&handle_, algorithm, nullptr, flags));
        try {
            ULONG copied = 0;
            require_bcrypt_success(
                "BCryptGetProperty(BCRYPT_OBJECT_LENGTH)",
                BCryptGetProperty(
                    handle_,
                    BCRYPT_OBJECT_LENGTH,
                    reinterpret_cast<PUCHAR>(&object_length_),
                    sizeof(object_length_),
                    &copied,
                    0));
            if (copied != sizeof(object_length_)) {
                throw std::runtime_error("BCrypt returned a malformed hash-object length");
            }

            ULONG hash_length = 0;
            require_bcrypt_success(
                "BCryptGetProperty(BCRYPT_HASH_LENGTH)",
                BCryptGetProperty(
                    handle_,
                    BCRYPT_HASH_LENGTH,
                    reinterpret_cast<PUCHAR>(&hash_length),
                    sizeof(hash_length),
                    &copied,
                    0));
            if (copied != sizeof(hash_length) || hash_length != rank_key_bytes) {
                throw std::runtime_error("BCrypt SHA-256 provider returned an unexpected digest length");
            }
        } catch (...) {
            BCryptCloseAlgorithmProvider(handle_, 0);
            handle_ = nullptr;
            throw;
        }
    }

    bcrypt_provider(const bcrypt_provider&) = delete;
    bcrypt_provider& operator=(const bcrypt_provider&) = delete;

    ~bcrypt_provider()
    {
        if (handle_ != nullptr) {
            (void)BCryptCloseAlgorithmProvider(handle_, 0);
        }
    }

    [[nodiscard]] BCRYPT_ALG_HANDLE handle() const noexcept { return handle_; }
    [[nodiscard]] ULONG object_length() const noexcept { return object_length_; }

private:
    BCRYPT_ALG_HANDLE handle_ = nullptr;
    ULONG object_length_ = 0;
};

[[nodiscard]] inline const bcrypt_provider& sha256_provider()
{
    static const bcrypt_provider provider{BCRYPT_SHA256_ALGORITHM, 0};
    return provider;
}

[[nodiscard]] inline const bcrypt_provider& hmac_sha256_provider()
{
    static const bcrypt_provider provider{BCRYPT_SHA256_ALGORITHM, BCRYPT_ALG_HANDLE_HMAC_FLAG};
    return provider;
}

[[nodiscard]] inline std::array<std::byte, rank_key_bytes> bcrypt_digest(
    const bcrypt_provider& provider,
    const std::span<const std::byte> key,
    const std::span<const std::byte> message)
{
    if (key.size() > (std::numeric_limits<ULONG>::max)()
        || message.size() > (std::numeric_limits<ULONG>::max)()) {
        throw std::length_error("CNG input exceeds the platform ULONG length limit");
    }

    auto hash_object = std::vector<UCHAR>(provider.object_length());
    BCRYPT_HASH_HANDLE hash = nullptr;
    require_bcrypt_success(
        "BCryptCreateHash",
        BCryptCreateHash(
            provider.handle(),
            &hash,
            hash_object.data(),
            static_cast<ULONG>(hash_object.size()),
            key.empty() ? nullptr : reinterpret_cast<PUCHAR>(const_cast<std::byte*>(key.data())),
            static_cast<ULONG>(key.size()),
            0));

    try {
        require_bcrypt_success(
            "BCryptHashData",
            BCryptHashData(
                hash,
                message.empty() ? nullptr : reinterpret_cast<PUCHAR>(const_cast<std::byte*>(message.data())),
                static_cast<ULONG>(message.size()),
                0));
        auto result = std::array<std::byte, rank_key_bytes>{};
        require_bcrypt_success(
            "BCryptFinishHash",
            BCryptFinishHash(
                hash,
                reinterpret_cast<PUCHAR>(result.data()),
                static_cast<ULONG>(result.size()),
                0));
        (void)BCryptDestroyHash(hash);
        return result;
    } catch (...) {
        (void)BCryptDestroyHash(hash);
        throw;
    }
}

[[nodiscard]] inline std::array<std::byte, rank_key_bytes> sha256(
    const std::span<const std::byte> message)
{
    return bcrypt_digest(sha256_provider(), {}, message);
}

[[nodiscard]] inline std::array<std::byte, rank_key_bytes> hmac_sha256(
    const std::span<const std::byte> key,
    const std::span<const std::byte> message)
{
    return bcrypt_digest(hmac_sha256_provider(), key, message);
}

inline void fill_random(const std::span<std::byte> destination)
{
    if (destination.size() > (std::numeric_limits<ULONG>::max)()) {
        throw std::length_error("CNG random request exceeds the platform ULONG length limit");
    }
    require_bcrypt_success(
        "BCryptGenRandom",
        BCryptGenRandom(
            nullptr,
            reinterpret_cast<PUCHAR>(destination.data()),
            static_cast<ULONG>(destination.size()),
            BCRYPT_USE_SYSTEM_PREFERRED_RNG));
}

#else

[[nodiscard]] inline std::array<std::byte, rank_key_bytes> sha256(
    const std::span<const std::byte> message)
{
    auto result = std::array<std::byte, rank_key_bytes>{};
    unsigned int written = 0;
    if (EVP_Digest(
            message.data(),
            message.size(),
            reinterpret_cast<unsigned char*>(result.data()),
            &written,
            EVP_sha256(),
            nullptr)
            != 1
        || written != result.size()) {
        throw std::runtime_error("OpenSSL EVP_Digest(SHA-256) failed");
    }
    return result;
}

[[nodiscard]] inline std::array<std::byte, rank_key_bytes> hmac_sha256(
    const std::span<const std::byte> key,
    const std::span<const std::byte> message)
{
    if (key.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
        throw std::length_error("OpenSSL HMAC key exceeds the platform int length limit");
    }
    auto result = std::array<std::byte, rank_key_bytes>{};
    unsigned int written = 0;
    if (HMAC(
            EVP_sha256(),
            key.data(),
            static_cast<int>(key.size()),
            reinterpret_cast<const unsigned char*>(message.data()),
            message.size(),
            reinterpret_cast<unsigned char*>(result.data()),
            &written)
            == nullptr
        || written != result.size()) {
        throw std::runtime_error("OpenSSL HMAC-SHA-256 failed");
    }
    return result;
}

inline void fill_random(const std::span<std::byte> destination)
{
    if (destination.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
        throw std::length_error("OpenSSL random request exceeds the platform int length limit");
    }
    if (RAND_bytes(
            reinterpret_cast<unsigned char*>(destination.data()),
            static_cast<int>(destination.size()))
        != 1) {
        throw std::runtime_error("OpenSSL RAND_bytes failed");
    }
}

#endif

[[nodiscard]] inline std::uint64_t read_big_endian_u64(const std::byte* source) noexcept
{
    auto result = std::uint64_t{0};
    for (auto index = std::size_t{0}; index != sizeof(result); ++index) {
        result = (result << 8) | std::to_integer<std::uint64_t>(source[index]);
    }
    return result;
}

inline void write_big_endian_u64(const std::uint64_t value, std::byte* destination) noexcept
{
    for (auto index = std::size_t{0}; index != sizeof(value); ++index) {
        destination[index] = static_cast<std::byte>(value >> ((sizeof(value) - 1 - index) * 8));
    }
}

[[nodiscard]] inline std::uint64_t fnv1a_64(const std::span<const std::byte> bytes) noexcept
{
    auto result = std::uint64_t{0xcbf29ce484222325ULL};
    for (const auto value : bytes) {
        result ^= std::to_integer<std::uint8_t>(value);
        result *= 0x100000001b3ULL;
    }
    return result;
}

[[nodiscard]] inline std::uint64_t mix64(std::uint64_t value) noexcept
{
    value ^= value >> 30;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27;
    value *= 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

} // namespace canonical_sorted_set_detail

/// A pinned rank-hash mapping for built-in values.
///
/// Integral values use their width-specific unsigned bit pattern. Strings use FNV-1a over their
/// bytes; FNV is only an encoding-to-rank-input mapping, while HMAC supplies pseudorandom ranks.
template <class T>
struct stable_zip_tree_rank_hash final {
    [[nodiscard]] std::uint64_t operator()(const T& value) const noexcept
        requires(std::integral<T> && !std::same_as<std::remove_cv_t<T>, bool>)
    {
        using unsigned_type = std::make_unsigned_t<T>;
        return static_cast<std::uint64_t>(static_cast<unsigned_type>(value));
    }

    [[nodiscard]] std::uint64_t operator()(const T& value) const noexcept
        requires std::same_as<std::remove_cv_t<T>, bool>
    {
        return value ? 1ULL : 0ULL;
    }
};

template <>
struct stable_zip_tree_rank_hash<std::string> final {
    [[nodiscard]] std::uint64_t operator()(const std::string& value) const noexcept
    {
        return canonical_sorted_set_detail::fnv1a_64(std::as_bytes(std::span{value}));
    }
};

template <>
struct stable_zip_tree_rank_hash<std::string_view> final {
    [[nodiscard]] std::uint64_t operator()(const std::string_view value) const noexcept
    {
        return canonical_sorted_set_detail::fnv1a_64(std::as_bytes(std::span{value}));
    }
};

template <class T>
concept stable_zip_tree_hashable = requires(const T& value) {
    { stable_zip_tree_rank_hash<T>{}(value) } -> std::convertible_to<std::uint64_t>;
};

/// Defines comparison and deterministic pseudorandom ranks for a canonical zip-zip set.
template <class T>
class zip_tree_rank_policy final {
public:
    using value_type = T;
    using comparer_function = std::function<bool(const T&, const T&)>;
    using rank_hash_function = std::function<std::uint64_t(const T&)>;

    /// Creates a new policy with a cryptographically random, unexposed HMAC key.
    [[nodiscard]] static zip_tree_rank_policy random()
        requires stable_zip_tree_hashable<T>
            && std::predicate<std::less<T>&, const T&, const T&>
    {
        return random(std::less<T>{}, stable_zip_tree_rank_hash<T>{});
    }

    /// Creates a reproducible public-seed policy using the pinned built-in hash mapping.
    [[nodiscard]] static zip_tree_rank_policy seeded(const std::uint64_t seed)
        requires stable_zip_tree_hashable<T>
            && std::predicate<std::less<T>&, const T&, const T&>
    {
        return seeded(seed, std::less<T>{}, stable_zip_tree_rank_hash<T>{});
    }

    /// Creates a caller-keyed policy using the pinned built-in hash mapping.
    [[nodiscard]] static zip_tree_rank_policy keyed(const std::span<const std::byte> rank_key)
        requires stable_zip_tree_hashable<T>
            && std::predicate<std::less<T>&, const T&, const T&>
    {
        return keyed(rank_key, std::less<T>{}, stable_zip_tree_rank_hash<T>{});
    }

    template <class Less, class RankHash>
        requires std::copy_constructible<std::decay_t<Less>>
            && std::copy_constructible<std::decay_t<RankHash>>
            && std::predicate<std::decay_t<Less>&, const T&, const T&>
            && std::invocable<std::decay_t<RankHash>&, const T&>
            && std::convertible_to<std::invoke_result_t<std::decay_t<RankHash>&, const T&>, std::uint64_t>
    [[nodiscard]] static zip_tree_rank_policy random(Less less, RankHash rank_hash)
    {
        auto key = std::vector<std::byte>(canonical_sorted_set_detail::rank_key_bytes);
        canonical_sorted_set_detail::fill_random(key);
        return create(std::move(less), std::move(rank_hash), std::move(key), std::nullopt);
    }

    template <class Less, class RankHash>
        requires std::copy_constructible<std::decay_t<Less>>
            && std::copy_constructible<std::decay_t<RankHash>>
            && std::predicate<std::decay_t<Less>&, const T&, const T&>
            && std::invocable<std::decay_t<RankHash>&, const T&>
            && std::convertible_to<std::invoke_result_t<std::decay_t<RankHash>&, const T&>, std::uint64_t>
    [[nodiscard]] static zip_tree_rank_policy seeded(
        const std::uint64_t seed,
        Less less,
        RankHash rank_hash)
    {
        auto material = std::array<std::byte, 4 + sizeof(seed)>{
            std::byte{'Z'}, std::byte{'Z'}, std::byte{'T'}, std::byte{'2'}};
        canonical_sorted_set_detail::write_big_endian_u64(seed, material.data() + 4);
        const auto key = canonical_sorted_set_detail::sha256(material);
        return create(
            std::move(less),
            std::move(rank_hash),
            std::vector<std::byte>{key.begin(), key.end()},
            seed);
    }

    template <class Less, class RankHash>
        requires std::copy_constructible<std::decay_t<Less>>
            && std::copy_constructible<std::decay_t<RankHash>>
            && std::predicate<std::decay_t<Less>&, const T&, const T&>
            && std::invocable<std::decay_t<RankHash>&, const T&>
            && std::convertible_to<std::invoke_result_t<std::decay_t<RankHash>&, const T&>, std::uint64_t>
    [[nodiscard]] static zip_tree_rank_policy keyed(
        const std::span<const std::byte> rank_key,
        Less less,
        RankHash rank_hash)
    {
        if (rank_key.size() < canonical_sorted_set_detail::minimum_rank_key_bytes) {
            throw std::invalid_argument("a zip-zip rank key must contain at least 32 bytes");
        }
        return create(
            std::move(less),
            std::move(rank_hash),
            std::vector<std::byte>{rank_key.begin(), rank_key.end()},
            std::nullopt);
    }

    /// Returns the process-local default policy. Copies of this handle retain policy identity.
    [[nodiscard]] static const zip_tree_rank_policy& default_policy()
        requires stable_zip_tree_hashable<T>
            && std::predicate<std::less<T>&, const T&, const T&>
    {
        static const auto value = random();
        return value;
    }

    [[nodiscard]] std::optional<std::uint64_t> public_seed() const noexcept
    {
        return state_->public_seed;
    }

    [[nodiscard]] bool is_same_policy(const zip_tree_rank_policy& other) const noexcept
    {
        return state_ == other.state_;
    }

    /// Compares values under the retained strict weak ordering.
    [[nodiscard]] int compare(const T& left, const T& right) const
    {
        const auto left_before = state_->less(left, right);
        const auto right_before = state_->less(right, left);
        if (left_before && right_before) {
            throw std::logic_error("zip-tree comparer violates irreflexive ordering for the compared values");
        }
        return left_before ? -1 : right_before ? 1 : 0;
    }

    /// Derives the exact ZZT2 HMAC-SHA-256 rank tuple for a value.
    [[nodiscard]] zip_tree_rank rank_for(const T& value) const
    {
        auto source = std::array<std::byte, sizeof(std::uint64_t)>{};
        canonical_sorted_set_detail::write_big_endian_u64(state_->rank_hash(value), source.data());
        const auto digest = canonical_sorted_set_detail::hmac_sha256(state_->rank_key, source);
        const auto primary = canonical_sorted_set_detail::read_big_endian_u64(digest.data());
        return zip_tree_rank{
            static_cast<std::uint32_t>(std::countl_zero(primary)),
            canonical_sorted_set_detail::read_big_endian_u64(digest.data() + 8),
            canonical_sorted_set_detail::read_big_endian_u64(digest.data() + 16)};
    }

private:
    struct state final {
        comparer_function less;
        rank_hash_function rank_hash;
        std::vector<std::byte> rank_key;
        std::optional<std::uint64_t> public_seed;
    };

    explicit zip_tree_rank_policy(std::shared_ptr<const state> state_value)
        : state_(std::move(state_value))
    {
    }

    template <class Less, class RankHash>
    [[nodiscard]] static zip_tree_rank_policy create(
        Less less,
        RankHash rank_hash,
        std::vector<std::byte> rank_key,
        const std::optional<std::uint64_t> seed)
    {
        auto compare = comparer_function{
            [less = std::move(less)](const T& left, const T& right) mutable {
                return static_cast<bool>(std::invoke(less, left, right));
            }};
        auto hash = rank_hash_function{
            [rank_hash = std::move(rank_hash)](const T& value) mutable {
                return static_cast<std::uint64_t>(std::invoke(rank_hash, value));
            }};
        return zip_tree_rank_policy{std::make_shared<state>(
            state{std::move(compare), std::move(hash), std::move(rank_key), seed})};
    }

    std::shared_ptr<const state> state_;
};

struct canonical_sorted_set_statistics final {
    std::size_t count = 0;
    std::size_t height = 0;
    std::uint32_t maximum_geometric_rank = 0;
    std::size_t priority_collision_count = 0;

    friend bool operator==(const canonical_sorted_set_statistics&, const canonical_sorted_set_statistics&) = default;
};

template <class T>
struct canonical_sorted_set_shape_entry final {
    T item;
    std::size_t left_count = 0;
    std::size_t right_count = 0;

    friend bool operator==(const canonical_sorted_set_shape_entry&, const canonical_sorted_set_shape_entry&) = default;
};

/// An immutable policy-canonical sorted set backed by a zip-zip-inspired Cartesian tree.
template <class T>
class canonical_sorted_set final {
private:
    struct node;
    using node_pointer = std::shared_ptr<const node>;

public:
    using value_type = T;
    using size_type = std::size_t;
    using policy_type = zip_tree_rank_policy<T>;

    class const_iterator final {
    public:
        using iterator_category = std::forward_iterator_tag;
        using iterator_concept = std::forward_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = const T*;
        using reference = const T&;

        const_iterator() = default;

        [[nodiscard]] reference operator*() const { return *path_.back()->item; }
        [[nodiscard]] pointer operator->() const { return path_.back()->item.get(); }

        const_iterator& operator++()
        {
            const auto* current = path_.back();
            path_.pop_back();
            push_left(current->right.get());
            return *this;
        }

        const_iterator operator++(int)
        {
            auto previous = *this;
            ++*this;
            return previous;
        }

        friend bool operator==(const const_iterator& left, const const_iterator& right) noexcept
        {
            return left.current() == right.current();
        }

    private:
        friend class canonical_sorted_set;

        explicit const_iterator(node_pointer root)
            : owner_(std::move(root))
        {
            path_.reserve(owner_ == nullptr ? 0 : owner_->height);
            push_left(owner_.get());
        }

        [[nodiscard]] const node* current() const noexcept
        {
            return path_.empty() ? nullptr : path_.back();
        }

        void push_left(const node* current)
        {
            while (current != nullptr) {
                path_.push_back(current);
                current = current->left.get();
            }
        }

        node_pointer owner_;
        std::vector<const node*> path_;
    };

    canonical_sorted_set()
        requires stable_zip_tree_hashable<T>
            && std::predicate<std::less<T>&, const T&, const T&>
        : policy_(policy_type::default_policy())
    {
    }

    explicit canonical_sorted_set(policy_type policy)
        : policy_(std::move(policy))
    {
    }

    template <std::ranges::input_range Range>
        requires std::constructible_from<T, std::ranges::range_reference_t<Range>>
            || (!std::is_lvalue_reference_v<Range&&>
                && std::constructible_from<T, std::ranges::range_rvalue_reference_t<Range>>)
    [[nodiscard]] static canonical_sorted_set from_range(Range&& values, policy_type policy)
    {
        auto pending = std::vector<std::shared_ptr<const T>>{};
        if constexpr (std::ranges::sized_range<Range>) {
            pending.reserve(static_cast<size_type>(std::ranges::size(values)));
        }
        for (auto iterator = std::ranges::begin(values); iterator != std::ranges::end(values); ++iterator) {
            if constexpr (!std::is_lvalue_reference_v<Range&&>
                && std::constructible_from<T, std::ranges::range_rvalue_reference_t<Range>>) {
                pending.push_back(std::make_shared<T>(std::ranges::iter_move(iterator)));
            } else {
                pending.push_back(std::make_shared<T>(*iterator));
            }
        }
        return from_representatives(std::move(pending), std::move(policy));
    }

    template <std::ranges::input_range Range>
        requires stable_zip_tree_hashable<T>
            && std::predicate<std::less<T>&, const T&, const T&>
            && (std::constructible_from<T, std::ranges::range_reference_t<Range>>
                || (!std::is_lvalue_reference_v<Range&&>
                    && std::constructible_from<T, std::ranges::range_rvalue_reference_t<Range>>))
    [[nodiscard]] static canonical_sorted_set from_range(Range&& values)
    {
        return from_range(std::forward<Range>(values), policy_type::default_policy());
    }

    [[nodiscard]] bool empty() const noexcept { return root_ == nullptr; }
    [[nodiscard]] size_type size() const noexcept { return root_ == nullptr ? 0 : root_->count; }
    [[nodiscard]] size_type height() const noexcept { return root_ == nullptr ? 0 : root_->height; }
    [[nodiscard]] const policy_type& policy() const noexcept { return policy_; }

    [[nodiscard]] const void* root_identity() const noexcept { return root_.get(); }

    [[nodiscard]] bool shares_root_with(const canonical_sorted_set& other) const noexcept
    {
        return root_ == other.root_;
    }

    [[nodiscard]] bool is_same_version(const canonical_sorted_set& other) const noexcept
    {
        return policy_.is_same_policy(other.policy_) && root_ == other.root_;
    }

    [[nodiscard]] std::vector<const void*> node_identities() const
    {
        auto result = std::vector<const void*>{};
        result.reserve(size());
        if (root_ == nullptr) {
            return result;
        }
        auto pending = std::vector<const node*>{root_.get()};
        while (!pending.empty()) {
            const auto* current = pending.back();
            pending.pop_back();
            result.push_back(current);
            if (current->right != nullptr) {
                pending.push_back(current->right.get());
            }
            if (current->left != nullptr) {
                pending.push_back(current->left.get());
            }
        }
        return result;
    }

    [[nodiscard]] size_type shared_node_count_with(const canonical_sorted_set& other) const
    {
        auto identities = node_identities();
        auto known = std::unordered_set<const void*>{identities.begin(), identities.end()};
        auto count = size_type{0};
        for (const auto identity : other.node_identities()) {
            count += known.contains(identity) ? 1 : 0;
        }
        return count;
    }

    [[nodiscard]] std::vector<canonical_sorted_set_shape_entry<T>> topology_signature() const
        requires std::copy_constructible<T>
    {
        auto result = std::vector<canonical_sorted_set_shape_entry<T>>{};
        result.reserve(size());
        if (root_ == nullptr) {
            return result;
        }
        auto pending = std::vector<const node*>{root_.get()};
        while (!pending.empty()) {
            const auto* current = pending.back();
            pending.pop_back();
            result.push_back(canonical_sorted_set_shape_entry<T>{
                *current->item,
                current->left == nullptr ? 0 : current->left->count,
                current->right == nullptr ? 0 : current->right->count});
            if (current->right != nullptr) {
                pending.push_back(current->right.get());
            }
            if (current->left != nullptr) {
                pending.push_back(current->left.get());
            }
        }
        return result;
    }

    [[nodiscard]] bool contains(const T& value) const { return find_node(value) != nullptr; }

    /// Returns the number of stored items ordered strictly before `value`, which is also the
    /// rank of its lower-bound gap. One O(h) descent guided by the cached subtree counts.
    [[nodiscard]] size_type count_less_than(const T& value) const
    {
        return count_before(value, false);
    }

    /// Returns the number of stored items not ordered after `value`, which is also the rank of
    /// its upper-bound gap. One O(h) descent guided by the cached subtree counts.
    [[nodiscard]] size_type count_at_most(const T& value) const
    {
        return count_before(value, true);
    }

    /// Returns the representative at an in-order rank, or nullptr when the rank is at or past
    /// the end. One O(h) descent guided by the cached subtree counts; no comparisons are made,
    /// so the ordering policy is never invoked. The reference follows this snapshot's lifetime.
    [[nodiscard]] const T* try_select(size_type rank) const noexcept
    {
        const auto* current = root_.get();
        while (current != nullptr) {
            const auto left_count = current->left == nullptr ? size_type{0} : current->left->count;
            if (rank < left_count) {
                current = current->left.get();
                continue;
            }
            if (rank == left_count) {
                return current->item.get();
            }
            rank -= left_count + 1;
            current = current->right.get();
        }
        return nullptr;
    }

    [[nodiscard]] const T* try_get(const T& equivalent_value) const
    {
        const auto* found = find_node(equivalent_value);
        return found == nullptr ? nullptr : found->item.get();
    }

    [[nodiscard]] canonical_sorted_set add(const T& value) const
        requires std::copy_constructible<T>
    {
        const auto* existing = find_node(value);
        if (existing != nullptr) {
            ensure_equivalent_rank(*existing, value);
            return *this;
        }
        return add_representative(std::make_shared<T>(value));
    }

    [[nodiscard]] canonical_sorted_set add(T&& value) const
        requires std::move_constructible<T>
    {
        const auto* existing = find_node(value);
        if (existing != nullptr) {
            ensure_equivalent_rank(*existing, value);
            return *this;
        }
        return add_representative(std::make_shared<T>(std::move(value)));
    }

    [[nodiscard]] canonical_sorted_set remove(const T& value) const
    {
        auto removed = false;
        auto root = remove_node(root_, value, policy_, removed);
        return removed ? canonical_sorted_set{std::move(root), policy_} : *this;
    }

    [[nodiscard]] canonical_sorted_set clear() const
    {
        return empty() ? *this : canonical_sorted_set{policy_};
    }

    [[nodiscard]] canonical_sorted_set union_with(const canonical_sorted_set& other) const
    {
        ensure_compatible(other);
        if (root_ == other.root_) {
            return *this;
        }
        auto result = *this;
        for (auto item = other.begin(); item != other.end(); ++item) {
            const auto* source = other.find_node(*item);
            result = result.add_representative(source->item);
        }
        return result;
    }

    [[nodiscard]] canonical_sorted_set intersect(const canonical_sorted_set& other) const
    {
        ensure_compatible(other);
        if (root_ == other.root_) {
            return *this;
        }
        auto result = canonical_sorted_set{policy_};
        for (auto item = begin(); item != end(); ++item) {
            if (other.contains(*item)) {
                result = result.add_representative(find_node(*item)->item);
            }
        }
        return result;
    }

    [[nodiscard]] canonical_sorted_set except(const canonical_sorted_set& other) const
    {
        ensure_compatible(other);
        if (root_ == other.root_) {
            return canonical_sorted_set{policy_};
        }
        auto result = *this;
        for (const auto& item : other) {
            result = result.remove(item);
        }
        return result;
    }

    /// Compares mathematical sets under this set's comparer, independent of rank-policy identity.
    [[nodiscard]] bool set_equals(const canonical_sorted_set& other) const
    {
        if (root_ == other.root_) {
            return true;
        }
        if (policy_.is_same_policy(other.policy_)) {
            return size() == other.size()
                && content_hash() == other.content_hash()
                && nodes_equal(root_, other.root_, policy_);
        }
        const auto normalized = normalize_under_this_policy(other);
        if (normalized.size() != size()) {
            return false;
        }
        auto left = begin();
        for (const auto* right : normalized) {
            if (policy_.compare(*left, *right) != 0) {
                return false;
            }
            ++left;
        }
        return true;
    }

    [[nodiscard]] bool is_subset_of(const canonical_sorted_set& other) const
    {
        const auto normalized = normalize_under_this_policy(other);
        auto left = begin();
        auto right = normalized.begin();
        while (left != end() && right != normalized.end()) {
            const auto comparison = policy_.compare(*left, **right);
            if (comparison < 0) {
                return false;
            }
            if (comparison > 0) {
                ++right;
            } else {
                ++left;
                ++right;
            }
        }
        return left == end();
    }

    [[nodiscard]] bool is_superset_of(const canonical_sorted_set& other) const
    {
        const auto normalized = normalize_under_this_policy(other);
        return std::ranges::all_of(normalized, [this](const T* value) { return contains(*value); });
    }

    [[nodiscard]] bool is_proper_subset_of(const canonical_sorted_set& other) const
    {
        const auto normalized = normalize_under_this_policy(other);
        return size() < normalized.size() && is_subset_of(other);
    }

    [[nodiscard]] bool is_proper_superset_of(const canonical_sorted_set& other) const
    {
        const auto normalized = normalize_under_this_policy(other);
        return size() > normalized.size()
            && std::ranges::all_of(normalized, [this](const T* value) { return contains(*value); });
    }

    [[nodiscard]] bool overlaps(const canonical_sorted_set& other) const
    {
        const auto normalized = normalize_under_this_policy(other);
        return std::ranges::any_of(normalized, [this](const T* value) { return contains(*value); });
    }

    [[nodiscard]] std::uint64_t content_hash() const
    {
        return root_ == nullptr ? 0 : root_->digest();
    }

    /// Validates ordering, rank reproducibility, heap order, and cached representation metadata.
    [[nodiscard]] canonical_sorted_set_statistics validate_structure() const
    {
        if (root_ == nullptr) {
            return {};
        }
        struct frame final {
            const node* current;
            const T* lower;
            const T* upper;
            size_type depth;
        };

        auto pending = std::vector<frame>{{root_.get(), nullptr, nullptr, 1}};
        auto priorities = std::vector<std::pair<std::uint32_t, std::uint64_t>>{};
        priorities.reserve(size());
        auto count = size_type{0};
        auto maximum_depth = size_type{0};
        auto maximum_geometric = std::uint32_t{0};
        while (!pending.empty()) {
            const auto current_frame = pending.back();
            pending.pop_back();
            const auto& current = *current_frame.current;
            if ((current_frame.lower != nullptr && policy_.compare(*current.item, *current_frame.lower) <= 0)
                || (current_frame.upper != nullptr && policy_.compare(*current.item, *current_frame.upper) >= 0)) {
                throw std::logic_error("a canonical sorted-set node crosses its key bound");
            }
            if (current.rank != policy_.rank_for(*current.item)) {
                throw std::logic_error("a canonical sorted-set rank is not reproducible");
            }
            if (current.left != nullptr && !higher(current, *current.left, policy_)) {
                throw std::logic_error("a canonical sorted-set left child outranks its parent");
            }
            if (current.right != nullptr && !higher(current, *current.right, policy_)) {
                throw std::logic_error("a canonical sorted-set right child outranks its parent");
            }

            const auto expected_count = checked_count(current.left, current.right);
            const auto expected_height = checked_height(current.left, current.right);
            if (current.count != expected_count || current.height != expected_height) {
                throw std::logic_error("a canonical sorted-set node has invalid cached metadata");
            }

            ++count;
            maximum_depth = (std::max)(maximum_depth, current_frame.depth);
            maximum_geometric = (std::max)(maximum_geometric, current.rank.geometric);
            priorities.emplace_back(current.rank.geometric, current.rank.secondary);
            if (current.right != nullptr) {
                pending.push_back(frame{
                    current.right.get(),
                    current.item.get(),
                    current_frame.upper,
                    checked_increment(current_frame.depth)});
            }
            if (current.left != nullptr) {
                pending.push_back(frame{
                    current.left.get(),
                    current_frame.lower,
                    current.item.get(),
                    checked_increment(current_frame.depth)});
            }
        }

        std::ranges::sort(priorities);
        auto collisions = size_type{0};
        for (auto index = size_type{1}; index < priorities.size(); ++index) {
            collisions += priorities[index] == priorities[index - 1] ? 1 : 0;
        }
        if (count != size() || maximum_depth != height()) {
            throw std::logic_error("canonical sorted-set root metadata disagrees with the validated tree");
        }
        return canonical_sorted_set_statistics{count, maximum_depth, maximum_geometric, collisions};
    }

    [[nodiscard]] const_iterator begin() const { return const_iterator{root_}; }
    [[nodiscard]] const_iterator end() const noexcept { return {}; }
    [[nodiscard]] const_iterator cbegin() const { return begin(); }
    [[nodiscard]] const_iterator cend() const noexcept { return end(); }

private:
    struct node final {
        std::shared_ptr<const T> item;
        zip_tree_rank rank;
        node_pointer left;
        node_pointer right;
        size_type count;
        size_type height;
        mutable std::atomic<std::uint64_t> digest_value{0};
        mutable std::atomic<bool> digest_ready{false};

        node(
            std::shared_ptr<const T> item_value,
            const zip_tree_rank rank_value,
            node_pointer left_value,
            node_pointer right_value)
            : item(std::move(item_value))
            , rank(rank_value)
            , left(std::move(left_value))
            , right(std::move(right_value))
            , count(checked_count(left, right))
            , height(checked_height(left, right))
        {
        }

        ~node() noexcept
        {
            if (left == nullptr && right == nullptr) {
                return;
            }
            // Following the smaller unique subtree while deferring the larger bounds this fixed
            // work stack by the number of bits in size_type: every continued subtree is at most
            // half its parent, so deferred ancestors form at most one entry per size bit. Shared
            // children can be released immediately; any last release uses this same destructor.
            constexpr auto maximum_pending = (std::numeric_limits<size_type>::digits) + 1;
            auto pending = std::array<node_pointer, maximum_pending>{};
            auto pending_count = size_type{0};
            const auto choose_next = [&pending, &pending_count](
                                         node_pointer first,
                                         node_pointer second) noexcept -> node_pointer {
                if (first != nullptr && first.use_count() != 1) {
                    first.reset();
                }
                if (second != nullptr && second.use_count() != 1) {
                    second.reset();
                }
                if (first == nullptr) {
                    return second;
                }
                if (second == nullptr) {
                    return first;
                }
                if (first->count > second->count) {
                    std::swap(first, second);
                }
                pending[pending_count++] = std::move(second);
                return first;
            };

            auto current = choose_next(std::move(left), std::move(right));
            while (current != nullptr || pending_count != 0) {
                if (current == nullptr) {
                    current = std::move(pending[--pending_count]);
                }
                auto* mutable_current = const_cast<node*>(current.get());
                auto current_left = std::move(mutable_current->left);
                auto current_right = std::move(mutable_current->right);
                current.reset();
                current = choose_next(std::move(current_left), std::move(current_right));
            }
        }

        [[nodiscard]] std::uint64_t digest() const
        {
            if (digest_ready.load(std::memory_order_acquire)) {
                return digest_value.load(std::memory_order_relaxed);
            }
            struct frame final {
                const node* current;
                bool expanded;
            };
            auto pending = std::vector<frame>{{this, false}};
            if (height <= (std::numeric_limits<size_type>::max)() / 2) {
                pending.reserve(height * 2);
            }
            while (!pending.empty()) {
                const auto current_frame = pending.back();
                pending.pop_back();
                auto& current = *current_frame.current;
                if (current.digest_ready.load(std::memory_order_acquire)) {
                    continue;
                }
                if (!current_frame.expanded) {
                    pending.push_back(frame{&current, true});
                    if (current.right != nullptr
                        && !current.right->digest_ready.load(std::memory_order_acquire)) {
                        pending.push_back(frame{current.right.get(), false});
                    }
                    if (current.left != nullptr
                        && !current.left->digest_ready.load(std::memory_order_acquire)) {
                        pending.push_back(frame{current.left.get(), false});
                    }
                    continue;
                }

                const auto left_hash = current.left == nullptr
                    ? 0x243f6a8885a308d3ULL
                    : current.left->digest_value.load(std::memory_order_relaxed);
                const auto right_hash = current.right == nullptr
                    ? 0x13198a2e03707344ULL
                    : current.right->digest_value.load(std::memory_order_relaxed);
                const auto value = canonical_sorted_set_detail::mix64(
                    current.rank.content
                    ^ std::rotl(left_hash, 17)
                    ^ std::rotl(right_hash, 43));
                current.digest_value.store(value, std::memory_order_relaxed);
                current.digest_ready.store(true, std::memory_order_release);
            }
            return digest_value.load(std::memory_order_relaxed);
        }
    };

    struct path_step final {
        node_pointer current;
        bool went_left;
    };

    struct merge_step final {
        node_pointer current;
        bool chose_left;
    };

    struct builder_node final {
        std::shared_ptr<const T> item;
        zip_tree_rank rank;
        std::optional<size_type> left;
        std::optional<size_type> right;
        node_pointer frozen;
    };

    canonical_sorted_set(node_pointer root, policy_type policy)
        : root_(std::move(root))
        , policy_(std::move(policy))
    {
    }

    [[nodiscard]] static size_type checked_increment(const size_type value)
    {
        if (value == (std::numeric_limits<size_type>::max)()) {
            throw std::length_error("canonical sorted-set metadata exceeds size_t");
        }
        return value + 1;
    }

    [[nodiscard]] static size_type checked_count(const node_pointer& left, const node_pointer& right)
    {
        const auto left_count = left == nullptr ? 0 : left->count;
        const auto right_count = right == nullptr ? 0 : right->count;
        constexpr auto maximum = (std::numeric_limits<size_type>::max)();
        if (left_count > maximum - 1 || right_count > maximum - 1 - left_count) {
            throw std::length_error("canonical sorted-set count exceeds size_t");
        }
        return 1 + left_count + right_count;
    }

    [[nodiscard]] static size_type checked_height(const node_pointer& left, const node_pointer& right)
    {
        return checked_increment((std::max)(
            left == nullptr ? 0 : left->height,
            right == nullptr ? 0 : right->height));
    }

    [[nodiscard]] static bool higher(
        const T& left_item,
        const zip_tree_rank left_rank,
        const T& right_item,
        const zip_tree_rank right_rank,
        const policy_type& policy)
    {
        if (left_rank.geometric != right_rank.geometric) {
            return left_rank.geometric > right_rank.geometric;
        }
        if (left_rank.secondary != right_rank.secondary) {
            return left_rank.secondary > right_rank.secondary;
        }
        return policy.compare(left_item, right_item) < 0;
    }

    [[nodiscard]] static bool higher(
        const node& left,
        const node& right,
        const policy_type& policy)
    {
        return higher(*left.item, left.rank, *right.item, right.rank, policy);
    }

    [[nodiscard]] static canonical_sorted_set from_representatives(
        std::vector<std::shared_ptr<const T>> pending,
        policy_type policy)
    {
        if (pending.empty()) {
            return canonical_sorted_set{std::move(policy)};
        }
        std::stable_sort(
            pending.begin(),
            pending.end(),
            [&policy](const auto& left, const auto& right) {
                return policy.compare(*left, *right) < 0;
            });

        auto nodes = std::vector<builder_node>{};
        nodes.reserve(pending.size());
        for (auto index = size_type{0}; index < pending.size();) {
            const auto rank = policy.rank_for(*pending[index]);
            auto end = index + 1;
            while (end < pending.size() && policy.compare(*pending[index], *pending[end]) == 0) {
                if (policy.rank_for(*pending[end]) != rank) {
                    throw_incoherent_rank_hash();
                }
                ++end;
            }
            nodes.push_back(builder_node{std::move(pending[index]), rank, {}, {}, {}});
            index = end;
        }

        auto spine = std::vector<size_type>{};
        spine.reserve(nodes.size());
        for (auto index = size_type{0}; index < nodes.size(); ++index) {
            auto left = std::optional<size_type>{};
            while (!spine.empty()) {
                const auto previous = spine.back();
                if (!higher(
                        *nodes[index].item,
                        nodes[index].rank,
                        *nodes[previous].item,
                        nodes[previous].rank,
                        policy)) {
                    break;
                }
                left = previous;
                spine.pop_back();
            }
            nodes[index].left = left;
            if (!spine.empty()) {
                nodes[spine.back()].right = index;
            }
            spine.push_back(index);
        }
        const auto root_index = spine.front();

        auto freeze = std::vector<std::pair<size_type, bool>>{{root_index, false}};
        if (nodes.size() <= (std::numeric_limits<size_type>::max)() / 2) {
            freeze.reserve(nodes.size() * 2);
        }
        while (!freeze.empty()) {
            const auto [index, expanded] = freeze.back();
            freeze.pop_back();
            auto& current = nodes[index];
            if (expanded) {
                current.frozen = std::make_shared<node>(
                    current.item,
                    current.rank,
                    current.left.has_value() ? nodes[*current.left].frozen : nullptr,
                    current.right.has_value() ? nodes[*current.right].frozen : nullptr);
                continue;
            }
            freeze.emplace_back(index, true);
            if (current.right.has_value()) {
                freeze.emplace_back(*current.right, false);
            }
            if (current.left.has_value()) {
                freeze.emplace_back(*current.left, false);
            }
        }
        return canonical_sorted_set{nodes[root_index].frozen, std::move(policy)};
    }

    /// Counts the items ordered before `value`, optionally including an equivalent item.
    /// Each descent step folds in the whole left subtree through its cached count, so the
    /// ordering policy is invoked once per level rather than once per stored item.
    [[nodiscard]] size_type count_before(const T& value, const bool include_equivalent) const
    {
        auto rank = size_type{0};
        const auto* current = root_.get();
        while (current != nullptr) {
            const auto comparison = policy_.compare(value, *current->item);
            if (comparison < 0) {
                current = current->left.get();
                continue;
            }
            const auto left_count = current->left == nullptr ? size_type{0} : current->left->count;
            if (comparison == 0) {
                return include_equivalent ? rank + left_count + 1 : rank + left_count;
            }
            rank += left_count + 1;
            current = current->right.get();
        }
        return rank;
    }

    [[nodiscard]] const node* find_node(const T& value) const
    {
        auto* current = root_.get();
        while (current != nullptr) {
            const auto comparison = policy_.compare(value, *current->item);
            if (comparison == 0) {
                return current;
            }
            current = comparison < 0 ? current->left.get() : current->right.get();
        }
        return nullptr;
    }

    void ensure_equivalent_rank(const node& existing, const T& equivalent) const
    {
        if (existing.rank != policy_.rank_for(equivalent)) {
            throw_incoherent_rank_hash();
        }
    }

    [[nodiscard]] canonical_sorted_set add_representative(std::shared_ptr<const T> item) const
    {
        const auto* existing = find_node(*item);
        if (existing != nullptr) {
            ensure_equivalent_rank(*existing, *item);
            return *this;
        }
        const auto rank = policy_.rank_for(*item);
        auto inserted = std::make_shared<node>(item, rank, nullptr, nullptr);
        return canonical_sorted_set{insert_node(root_, std::move(inserted), policy_), policy_};
    }

    [[nodiscard]] static node_pointer insert_node(
        node_pointer root,
        node_pointer item,
        const policy_type& policy)
    {
        if (root == nullptr) {
            return item;
        }
        auto path = std::vector<path_step>{};
        path.reserve(root->height);
        auto current = root;
        while (current != nullptr && !higher(*item, *current, policy)) {
            const auto went_left = policy.compare(*item->item, *current->item) < 0;
            path.push_back(path_step{current, went_left});
            current = went_left ? current->left : current->right;
        }
        auto [left, right] = split_node(current, *item->item, policy);
        auto result = std::make_shared<node>(item->item, item->rank, std::move(left), std::move(right));
        while (!path.empty()) {
            const auto step = std::move(path.back());
            path.pop_back();
            result = step.went_left
                ? std::make_shared<node>(step.current->item, step.current->rank, result, step.current->right)
                : std::make_shared<node>(step.current->item, step.current->rank, step.current->left, result);
        }
        return result;
    }

    [[nodiscard]] static std::pair<node_pointer, node_pointer> split_node(
        node_pointer root,
        const T& value,
        const policy_type& policy)
    {
        auto path = std::vector<path_step>{};
        path.reserve(root == nullptr ? 0 : root->height);
        auto current = root;
        while (current != nullptr) {
            const auto went_left = policy.compare(value, *current->item) < 0;
            path.push_back(path_step{current, went_left});
            current = went_left ? current->left : current->right;
        }

        auto left = node_pointer{};
        auto right = node_pointer{};
        while (!path.empty()) {
            const auto step = std::move(path.back());
            path.pop_back();
            if (step.went_left) {
                right = std::make_shared<node>(
                    step.current->item,
                    step.current->rank,
                    std::move(right),
                    step.current->right);
            } else {
                left = std::make_shared<node>(
                    step.current->item,
                    step.current->rank,
                    step.current->left,
                    std::move(left));
            }
        }
        return {std::move(left), std::move(right)};
    }

    [[nodiscard]] static node_pointer remove_node(
        node_pointer root,
        const T& value,
        const policy_type& policy,
        bool& removed)
    {
        auto path = std::vector<path_step>{};
        path.reserve(root == nullptr ? 0 : root->height);
        auto current = root;
        while (current != nullptr) {
            const auto comparison = policy.compare(value, *current->item);
            if (comparison == 0) {
                break;
            }
            const auto went_left = comparison < 0;
            path.push_back(path_step{current, went_left});
            current = went_left ? current->left : current->right;
        }
        if (current == nullptr) {
            removed = false;
            return root;
        }

        removed = true;
        auto result = merge_nodes(current->left, current->right, policy);
        while (!path.empty()) {
            const auto step = std::move(path.back());
            path.pop_back();
            result = step.went_left
                ? std::make_shared<node>(step.current->item, step.current->rank, result, step.current->right)
                : std::make_shared<node>(step.current->item, step.current->rank, step.current->left, result);
        }
        return result;
    }

    [[nodiscard]] static node_pointer merge_nodes(
        node_pointer left,
        node_pointer right,
        const policy_type& policy)
    {
        if (left == nullptr) {
            return right;
        }
        if (right == nullptr) {
            return left;
        }
        auto path = std::vector<merge_step>{};
        path.reserve((std::max)(left->height, right->height));
        while (left != nullptr && right != nullptr) {
            if (higher(*left, *right, policy)) {
                path.push_back(merge_step{left, true});
                left = left->right;
            } else {
                path.push_back(merge_step{right, false});
                right = right->left;
            }
        }
        auto result = left == nullptr ? right : left;
        while (!path.empty()) {
            const auto step = std::move(path.back());
            path.pop_back();
            result = step.chose_left
                ? std::make_shared<node>(step.current->item, step.current->rank, step.current->left, result)
                : std::make_shared<node>(step.current->item, step.current->rank, result, step.current->right);
        }
        return result;
    }

    [[nodiscard]] static bool nodes_equal(
        node_pointer left,
        node_pointer right,
        const policy_type& policy)
    {
        auto pending = std::vector<std::pair<node_pointer, node_pointer>>{{std::move(left), std::move(right)}};
        while (!pending.empty()) {
            auto [left_node, right_node] = std::move(pending.back());
            pending.pop_back();
            if (left_node == right_node) {
                continue;
            }
            if (left_node == nullptr || right_node == nullptr
                || policy.compare(*left_node->item, *right_node->item) != 0) {
                return false;
            }
            pending.emplace_back(left_node->right, right_node->right);
            pending.emplace_back(left_node->left, right_node->left);
        }
        return true;
    }

    [[nodiscard]] std::vector<const T*> normalize_under_this_policy(
        const canonical_sorted_set& other) const
    {
        auto values = std::vector<const T*>{};
        values.reserve(other.size());
        for (const auto& value : other) {
            values.push_back(std::addressof(value));
        }
        std::stable_sort(
            values.begin(),
            values.end(),
            [this](const T* left, const T* right) { return policy_.compare(*left, *right) < 0; });
        values.erase(
            std::unique(
                values.begin(),
                values.end(),
                [this](const T* left, const T* right) { return policy_.compare(*left, *right) == 0; }),
            values.end());
        return values;
    }

    void ensure_compatible(const canonical_sorted_set& other) const
    {
        if (!policy_.is_same_policy(other.policy_)) {
            throw std::invalid_argument("canonical set algebra requires the same retained rank-policy object");
        }
    }

    [[noreturn]] static void throw_incoherent_rank_hash()
    {
        throw std::logic_error(
            "the rank hash is not constant on the set comparer's equivalence classes");
    }

    node_pointer root_;
    policy_type policy_;
};

template <class T>
canonical_sorted_set(zip_tree_rank_policy<T>) -> canonical_sorted_set<T>;

} // namespace durable7::finger_tree
