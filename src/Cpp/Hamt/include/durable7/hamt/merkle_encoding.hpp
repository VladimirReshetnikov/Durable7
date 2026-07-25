#pragma once

#include <durable7/hamt/detail/sha256.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <ostream>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace durable7::hamt {

using merkle_bytes = std::vector<std::byte>;

/// A malformed, noncanonical, or otherwise rejected codec value.
class merkle_codec_error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// Encodes and decodes one injective, versioned canonical value representation.
template <class T>
class merkle_codec {
public:
    virtual ~merkle_codec() = default;

    /// Returns the stable identifier ending in `-v` followed by decimal digits.
    [[nodiscard]] virtual std::string_view encoding_id() const = 0;

    /// Encodes one value into newly owned canonical bytes.
    [[nodiscard]] virtual merkle_bytes encode(const T& value) const = 0;

    /// Decodes exactly one complete canonical encoding.
    [[nodiscard]] virtual T decode(std::span<const std::byte> encoding) const = 0;
};

namespace merkle_detail {

[[nodiscard]] constexpr std::byte as_byte(const std::uint8_t value) noexcept
{
    return static_cast<std::byte>(value);
}

[[nodiscard]] constexpr std::uint8_t as_u8(const std::byte value) noexcept
{
    return std::to_integer<std::uint8_t>(value);
}

[[nodiscard]] inline std::span<const std::byte> bytes_of(const std::string_view value) noexcept
{
    return std::as_bytes(std::span{value.data(), value.size()});
}

[[nodiscard]] inline bool is_continuation(const std::uint8_t value) noexcept
{
    return value >= 0x80 && value <= 0xbf;
}

/// Validates shortest-form UTF-8 containing only Unicode scalar values.
[[nodiscard]] inline bool is_canonical_utf8(const std::span<const std::byte> bytes) noexcept
{
    auto index = std::size_t{0};
    while (index != bytes.size()) {
        const auto first = as_u8(bytes[index]);
        if (first <= 0x7f) {
            ++index;
            continue;
        }

        if (first >= 0xc2 && first <= 0xdf) {
            if (index + 1 >= bytes.size() || !is_continuation(as_u8(bytes[index + 1]))) {
                return false;
            }
            index += 2;
            continue;
        }

        if (first >= 0xe0 && first <= 0xef) {
            if (index + 2 >= bytes.size()) {
                return false;
            }
            const auto second = as_u8(bytes[index + 1]);
            const auto third = as_u8(bytes[index + 2]);
            const auto second_valid = first == 0xe0
                ? second >= 0xa0 && second <= 0xbf
                : first == 0xed
                    ? second >= 0x80 && second <= 0x9f
                    : is_continuation(second);
            if (!second_valid || !is_continuation(third)) {
                return false;
            }
            index += 3;
            continue;
        }

        if (first >= 0xf0 && first <= 0xf4) {
            if (index + 3 >= bytes.size()) {
                return false;
            }
            const auto second = as_u8(bytes[index + 1]);
            const auto third = as_u8(bytes[index + 2]);
            const auto fourth = as_u8(bytes[index + 3]);
            const auto second_valid = first == 0xf0
                ? second >= 0x90 && second <= 0xbf
                : first == 0xf4
                    ? second >= 0x80 && second <= 0x8f
                    : is_continuation(second);
            if (!second_valid || !is_continuation(third) || !is_continuation(fourth)) {
                return false;
            }
            index += 4;
            continue;
        }

        return false;
    }
    return true;
}

/// Decodes one scalar from bytes already accepted by `is_canonical_utf8`.
[[nodiscard]] inline std::uint32_t decode_canonical_utf8_scalar(
    const std::span<const std::byte> bytes,
    std::size_t& index) noexcept
{
    const auto first = as_u8(bytes[index++]);
    if (first <= 0x7f) {
        return first;
    }

    const auto continuation_count = first <= 0xdf ? std::size_t{1}
        : first <= 0xef ? std::size_t{2}
                        : std::size_t{3};
    auto result = static_cast<std::uint32_t>(
        first & (continuation_count == 1 ? 0x1f : continuation_count == 2 ? 0x0f : 0x07));
    for (auto remaining = continuation_count; remaining != 0; --remaining) {
        result = (result << 6) | (as_u8(bytes[index++]) & 0x3f);
    }
    return result;
}

/// Matches Unicode's White_Space property, and therefore .NET/Rust/Kotlin identifier trimming.
[[nodiscard]] constexpr bool is_unicode_whitespace(const std::uint32_t code_point) noexcept
{
    return (code_point >= 0x0009 && code_point <= 0x000d)
        || code_point == 0x0020
        || code_point == 0x0085
        || code_point == 0x00a0
        || code_point == 0x1680
        || (code_point >= 0x2000 && code_point <= 0x200a)
        || code_point == 0x2028
        || code_point == 0x2029
        || code_point == 0x202f
        || code_point == 0x205f
        || code_point == 0x3000;
}

[[nodiscard]] inline bool is_all_unicode_whitespace(
    const std::span<const std::byte> bytes) noexcept
{
    auto index = std::size_t{0};
    while (index != bytes.size()) {
        if (!is_unicode_whitespace(decode_canonical_utf8_scalar(bytes, index))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] inline bool has_unicode_whitespace_edge(
    const std::span<const std::byte> bytes) noexcept
{
    auto index = std::size_t{0};
    const auto first = decode_canonical_utf8_scalar(bytes, index);
    auto last = first;
    while (index != bytes.size()) {
        last = decode_canonical_utf8_scalar(bytes, index);
    }
    return is_unicode_whitespace(first) || is_unicode_whitespace(last);
}

inline void require_exact_length(
    const std::span<const std::byte> encoding,
    const std::size_t expected,
    const std::string_view encoding_id)
{
    if (encoding.size() != expected) {
        throw merkle_codec_error(
            "invalid " + std::string{encoding_id} + " encoding: expected exactly "
            + std::to_string(expected) + " bytes");
    }
}

inline void append_i32_be(merkle_bytes& destination, const std::int32_t value)
{
    const auto bits = std::bit_cast<std::uint32_t>(value);
    destination.push_back(as_byte(static_cast<std::uint8_t>(bits >> 24)));
    destination.push_back(as_byte(static_cast<std::uint8_t>(bits >> 16)));
    destination.push_back(as_byte(static_cast<std::uint8_t>(bits >> 8)));
    destination.push_back(as_byte(static_cast<std::uint8_t>(bits)));
}

[[nodiscard]] inline std::int32_t read_i32_be(const std::span<const std::byte> source)
{
    require_exact_length(source, sizeof(std::int32_t), "i32-be-v1");
    const auto bits = (static_cast<std::uint32_t>(as_u8(source[0])) << 24)
        | (static_cast<std::uint32_t>(as_u8(source[1])) << 16)
        | (static_cast<std::uint32_t>(as_u8(source[2])) << 8)
        | static_cast<std::uint32_t>(as_u8(source[3]));
    return std::bit_cast<std::int32_t>(bits);
}

inline void append_i64_be(merkle_bytes& destination, const std::int64_t value)
{
    const auto bits = std::bit_cast<std::uint64_t>(value);
    for (auto shift = 56; shift >= 0; shift -= 8) {
        destination.push_back(as_byte(static_cast<std::uint8_t>(bits >> shift)));
    }
}

[[nodiscard]] inline std::int64_t read_i64_be(const std::span<const std::byte> source)
{
    require_exact_length(source, sizeof(std::int64_t), "i64-be-v1");
    auto bits = std::uint64_t{0};
    for (const auto value : source) {
        bits = (bits << 8) | as_u8(value);
    }
    return std::bit_cast<std::int64_t>(bits);
}

inline void require_framed_length(const std::size_t size)
{
    if (size > static_cast<std::size_t>((std::numeric_limits<std::int32_t>::max)())) {
        throw std::length_error("Merkle framed field exceeds the signed 32-bit wire limit");
    }
}

} // namespace merkle_detail

/// Canonical signed 32-bit big-endian codec (`i32-be-v1`).
class int32_merkle_codec final : public merkle_codec<std::int32_t> {
public:
    [[nodiscard]] std::string_view encoding_id() const override { return "i32-be-v1"; }

    [[nodiscard]] merkle_bytes encode(const std::int32_t& value) const override
    {
        auto result = merkle_bytes{};
        result.reserve(sizeof(value));
        merkle_detail::append_i32_be(result, value);
        return result;
    }

    [[nodiscard]] std::int32_t decode(const std::span<const std::byte> encoding) const override
    {
        return merkle_detail::read_i32_be(encoding);
    }
};

/// Canonical signed 64-bit big-endian codec (`i64-be-v1`).
class int64_merkle_codec final : public merkle_codec<std::int64_t> {
public:
    [[nodiscard]] std::string_view encoding_id() const override { return "i64-be-v1"; }

    [[nodiscard]] merkle_bytes encode(const std::int64_t& value) const override
    {
        auto result = merkle_bytes{};
        result.reserve(sizeof(value));
        merkle_detail::append_i64_be(result, value);
        return result;
    }

    [[nodiscard]] std::int64_t decode(const std::span<const std::byte> encoding) const override
    {
        return merkle_detail::read_i64_be(encoding);
    }
};

/// Canonical nullable UTF-8 codec (`nullable-utf8-v1`).
class nullable_utf8_merkle_codec final : public merkle_codec<std::optional<std::string>> {
public:
    [[nodiscard]] std::string_view encoding_id() const override { return "nullable-utf8-v1"; }

    [[nodiscard]] merkle_bytes encode(const std::optional<std::string>& value) const override
    {
        if (!value.has_value()) {
            return {std::byte{0}};
        }
        const auto payload = merkle_detail::bytes_of(*value);
        if (!merkle_detail::is_canonical_utf8(payload)) {
            throw merkle_codec_error("invalid nullable-utf8-v1 value: payload is not canonical UTF-8");
        }
        auto result = merkle_bytes{};
        result.reserve(payload.size() + 1);
        result.push_back(std::byte{1});
        result.insert(result.end(), payload.begin(), payload.end());
        return result;
    }

    [[nodiscard]] std::optional<std::string> decode(
        const std::span<const std::byte> encoding) const override
    {
        if (encoding.empty()) {
            throw merkle_codec_error("invalid nullable-utf8-v1 encoding: nullable tag is missing");
        }
        const auto tag = merkle_detail::as_u8(encoding.front());
        if (tag == 0) {
            if (encoding.size() != 1) {
                throw merkle_codec_error("invalid nullable-utf8-v1 encoding: null has trailing bytes");
            }
            return std::nullopt;
        }
        if (tag != 1) {
            throw merkle_codec_error("invalid nullable-utf8-v1 encoding: nullable tag must be zero or one");
        }
        const auto payload = encoding.subspan(1);
        if (!merkle_detail::is_canonical_utf8(payload)) {
            throw merkle_codec_error("invalid nullable-utf8-v1 encoding: payload is not canonical UTF-8");
        }
        return std::string{
            reinterpret_cast<const char*>(payload.data()),
            payload.size()};
    }
};

/// Canonical nullable byte-vector codec (`nullable-bytes-v1`).
class nullable_bytes_merkle_codec final : public merkle_codec<std::optional<merkle_bytes>> {
public:
    [[nodiscard]] std::string_view encoding_id() const override { return "nullable-bytes-v1"; }

    [[nodiscard]] merkle_bytes encode(const std::optional<merkle_bytes>& value) const override
    {
        if (!value.has_value()) {
            return {std::byte{0}};
        }
        auto result = merkle_bytes{};
        result.reserve(value->size() + 1);
        result.push_back(std::byte{1});
        result.insert(result.end(), value->begin(), value->end());
        return result;
    }

    [[nodiscard]] std::optional<merkle_bytes> decode(
        const std::span<const std::byte> encoding) const override
    {
        if (encoding.empty()) {
            throw merkle_codec_error("invalid nullable-bytes-v1 encoding: nullable tag is missing");
        }
        const auto tag = merkle_detail::as_u8(encoding.front());
        if (tag == 0) {
            if (encoding.size() != 1) {
                throw merkle_codec_error("invalid nullable-bytes-v1 encoding: null has trailing bytes");
            }
            return std::nullopt;
        }
        if (tag != 1) {
            throw merkle_codec_error("invalid nullable-bytes-v1 encoding: nullable tag must be zero or one");
        }
        return merkle_bytes{encoding.begin() + 1, encoding.end()};
    }
};

/// An RFC-4122/network-order 16-byte GUID value.
class rfc4122_guid final {
public:
    static constexpr std::size_t byte_length = 16;

    constexpr rfc4122_guid() = default;
    explicit constexpr rfc4122_guid(const std::array<std::byte, byte_length> bytes) noexcept
        : bytes_(bytes)
    {
    }

    [[nodiscard]] constexpr const std::array<std::byte, byte_length>& bytes() const noexcept
    {
        return bytes_;
    }

    friend constexpr bool operator==(const rfc4122_guid&, const rfc4122_guid&) = default;
    friend constexpr auto operator<=>(const rfc4122_guid&, const rfc4122_guid&) = default;

private:
    std::array<std::byte, byte_length> bytes_{};
};

/// Canonical RFC-4122/network-order GUID codec (`guid-rfc4122-v1`).
class rfc4122_guid_merkle_codec final : public merkle_codec<rfc4122_guid> {
public:
    [[nodiscard]] std::string_view encoding_id() const override { return "guid-rfc4122-v1"; }

    [[nodiscard]] merkle_bytes encode(const rfc4122_guid& value) const override
    {
        return {value.bytes().begin(), value.bytes().end()};
    }

    [[nodiscard]] rfc4122_guid decode(const std::span<const std::byte> encoding) const override
    {
        merkle_detail::require_exact_length(encoding, rfc4122_guid::byte_length, encoding_id());
        auto bytes = std::array<std::byte, rfc4122_guid::byte_length>{};
        std::ranges::copy(encoding, bytes.begin());
        return rfc4122_guid{bytes};
    }
};

/// A 256-bit SHA-256 content digest.
class merkle_digest final {
public:
    static constexpr std::size_t byte_length = detail::sha256_digest_size;
    static constexpr std::size_t hex_length = byte_length * 2;
    using storage_type = std::array<std::byte, byte_length>;

    constexpr merkle_digest() = default;

    [[nodiscard]] static merkle_digest from_bytes(const std::span<const std::byte> bytes)
    {
        if (bytes.size() != byte_length) {
            throw std::invalid_argument("a Merkle digest must contain exactly 32 bytes");
        }
        auto result = storage_type{};
        std::ranges::copy(bytes, result.begin());
        return merkle_digest{result};
    }

    [[nodiscard]] static std::optional<merkle_digest> try_from_bytes(
        const std::span<const std::byte> bytes) noexcept
    {
        if (bytes.size() != byte_length) {
            return std::nullopt;
        }
        auto result = storage_type{};
        std::ranges::copy(bytes, result.begin());
        return merkle_digest{result};
    }

    [[nodiscard]] static merkle_digest from_hex(const std::string_view hex)
    {
        const auto result = try_from_hex(hex);
        if (!result.has_value()) {
            throw std::invalid_argument(
                "a Merkle digest must contain exactly 64 hexadecimal characters");
        }
        return *result;
    }

    [[nodiscard]] static std::optional<merkle_digest> try_from_hex(
        const std::string_view hex) noexcept
    {
        if (hex.size() != hex_length) {
            return std::nullopt;
        }
        auto bytes = storage_type{};
        for (auto index = std::size_t{0}; index != bytes.size(); ++index) {
            const auto high = hex_value(static_cast<unsigned char>(hex[index * 2]));
            const auto low = hex_value(static_cast<unsigned char>(hex[index * 2 + 1]));
            if (high < 0 || low < 0) {
                return std::nullopt;
            }
            bytes[index] = merkle_detail::as_byte(
                static_cast<std::uint8_t>((high << 4) | low));
        }
        return merkle_digest{bytes};
    }

    [[nodiscard]] static merkle_digest hash(const std::span<const std::byte> bytes)
    {
        return merkle_digest{detail::sha256(bytes)};
    }

    [[nodiscard]] constexpr const storage_type& bytes() const noexcept { return bytes_; }
    [[nodiscard]] constexpr std::span<const std::byte> as_span() const noexcept { return bytes_; }

    /// Writes only after verifying that the entire digest fits.
    void write_bytes(const std::span<std::byte> destination) const
    {
        if (destination.size() < byte_length) {
            throw std::length_error("digest destination requires at least 32 bytes");
        }
        std::ranges::copy(bytes_, destination.begin());
    }

    [[nodiscard]] bool try_write_bytes(const std::span<std::byte> destination) const noexcept
    {
        if (destination.size() < byte_length) {
            return false;
        }
        std::ranges::copy(bytes_, destination.begin());
        return true;
    }

    [[nodiscard]] std::string to_hex() const
    {
        constexpr auto digits = std::string_view{"0123456789abcdef"};
        auto result = std::string(hex_length, '0');
        for (auto index = std::size_t{0}; index != bytes_.size(); ++index) {
            const auto value = merkle_detail::as_u8(bytes_[index]);
            result[index * 2] = digits[value >> 4];
            result[index * 2 + 1] = digits[value & 0x0f];
        }
        return result;
    }

    friend constexpr bool operator==(const merkle_digest&, const merkle_digest&) = default;
    friend constexpr auto operator<=>(const merkle_digest&, const merkle_digest&) = default;

private:
    explicit constexpr merkle_digest(storage_type bytes) noexcept
        : bytes_(bytes)
    {
    }

    [[nodiscard]] static constexpr int hex_value(const unsigned char value) noexcept
    {
        if (value >= '0' && value <= '9') {
            return value - '0';
        }
        if (value >= 'a' && value <= 'f') {
            return value - 'a' + 10;
        }
        if (value >= 'A' && value <= 'F') {
            return value - 'A' + 10;
        }
        return -1;
    }

    storage_type bytes_{};
};

inline std::ostream& operator<<(std::ostream& output, const merkle_digest& digest)
{
    return output << digest.to_hex();
}

/// Compares Merkle-search-tree keys for ordering and equivalence.
template <class K>
class merkle_key_comparer {
public:
    virtual ~merkle_key_comparer() = default;
    [[nodiscard]] virtual int compare(const K& left, const K& right) const = 0;
};

/// Natural strict-weak ordering lifted to a three-way comparison result.
template <class K, class Less = std::less<K>>
class natural_merkle_key_comparer final : public merkle_key_comparer<K> {
public:
    explicit natural_merkle_key_comparer(Less less = {})
        : less_(std::move(less))
    {
    }

    [[nodiscard]] int compare(const K& left, const K& right) const override
    {
        if (std::invoke(less_, left, right)) {
            return -1;
        }
        return std::invoke(less_, right, left) ? 1 : 0;
    }

private:
    Less less_;
};

/// Deterministic comparison, encoding, and SHA-256 domain for one Merkle search tree family.
template <class K, class V>
class merkle_search_tree_policy final {
private:
    struct state final {
        std::string policy_id;
        std::shared_ptr<const merkle_key_comparer<K>> comparer;
        std::shared_ptr<const merkle_codec<K>> key_codec;
        std::shared_ptr<const merkle_codec<V>> value_codec;
        std::string key_encoding_id;
        std::string value_encoding_id;
        merkle_digest domain_digest;
        merkle_digest empty_digest;
    };

public:
    static constexpr std::string_view algorithm_id = "mst-sha256-b16-v2";

    /// Creates an explicit deterministic policy.
    [[nodiscard]] static merkle_search_tree_policy create(
        std::string policy_id,
        std::shared_ptr<const merkle_key_comparer<K>> comparer,
        std::shared_ptr<const merkle_codec<K>> key_codec,
        std::shared_ptr<const merkle_codec<V>> value_codec)
    {
        if (comparer == nullptr || key_codec == nullptr || value_codec == nullptr) {
            throw std::invalid_argument("Merkle policy components must not be null");
        }
        validate_policy_id(policy_id);
        auto key_encoding_id = validate_encoding_id(key_codec->encoding_id(), "key codec");
        auto value_encoding_id = validate_encoding_id(value_codec->encoding_id(), "value codec");
        const auto domain_digest = hash_framed(
            0x50,
            {merkle_detail::bytes_of(algorithm_id),
             merkle_detail::bytes_of(policy_id),
             merkle_detail::bytes_of(key_encoding_id),
             merkle_detail::bytes_of(value_encoding_id)});

        auto empty_manifest = merkle_bytes{};
        empty_manifest.reserve(5 + merkle_digest::byte_length);
        const auto magic = merkle_detail::bytes_of("MST2");
        empty_manifest.insert(empty_manifest.end(), magic.begin(), magic.end());
        empty_manifest.push_back(std::byte{0});
        empty_manifest.insert(
            empty_manifest.end(),
            domain_digest.bytes().begin(),
            domain_digest.bytes().end());
        const auto empty_digest = merkle_digest::hash(empty_manifest);

        return merkle_search_tree_policy{std::make_shared<state>(state{
            std::move(policy_id),
            std::move(comparer),
            std::move(key_codec),
            std::move(value_codec),
            std::move(key_encoding_id),
            std::move(value_encoding_id),
            domain_digest,
            empty_digest})};
    }

    /// Creates a natural-order deterministic policy.
    template <class Less = std::less<K>>
    [[nodiscard]] static merkle_search_tree_policy natural(
        std::string policy_id,
        std::shared_ptr<const merkle_codec<K>> key_codec,
        std::shared_ptr<const merkle_codec<V>> value_codec,
        Less less = {})
    {
        return create(
            std::move(policy_id),
            std::make_shared<natural_merkle_key_comparer<K, Less>>(std::move(less)),
            std::move(key_codec),
            std::move(value_codec));
    }

    [[nodiscard]] const std::string& policy_id_value() const noexcept { return state_->policy_id; }
    [[nodiscard]] const merkle_key_comparer<K>& comparer() const noexcept { return *state_->comparer; }
    [[nodiscard]] const merkle_codec<K>& key_codec() const noexcept { return *state_->key_codec; }
    [[nodiscard]] const merkle_codec<V>& value_codec() const noexcept { return *state_->value_codec; }
    [[nodiscard]] const std::string& key_encoding_id() const noexcept { return state_->key_encoding_id; }
    [[nodiscard]] const std::string& value_encoding_id() const noexcept { return state_->value_encoding_id; }
    [[nodiscard]] merkle_digest domain_digest() const noexcept { return state_->domain_digest; }
    [[nodiscard]] merkle_digest empty_digest() const noexcept { return state_->empty_digest; }

    [[nodiscard]] int compare(const K& left, const K& right) const
    {
        return state_->comparer->compare(left, right);
    }

    [[nodiscard]] bool shares_identity_with(const merkle_search_tree_policy& other) const noexcept
    {
        return state_ == other.state_;
    }

    [[nodiscard]] bool is_compatible_with(const merkle_search_tree_policy& other) const noexcept
    {
        return domain_digest() == other.domain_digest();
    }

    [[nodiscard]] merkle_digest hash_key_bytes(
        const std::span<const std::byte> key_bytes) const
    {
        return hash_framed(0x4b, {domain_digest().as_span(), key_bytes});
    }

    [[nodiscard]] merkle_digest hash_key(const K& key) const
    {
        const auto bytes = key_codec().encode(key);
        return hash_key_bytes(bytes);
    }

    [[nodiscard]] merkle_digest hash_bytes(const std::span<const std::byte> bytes) const
    {
        return merkle_digest::hash(bytes);
    }

    /// Counts leading zero base-16 digits, yielding a level from zero through 64.
    [[nodiscard]] static std::uint8_t level(const merkle_digest digest) noexcept
    {
        auto result = std::uint8_t{0};
        for (const auto octet : digest.bytes()) {
            const auto value = merkle_detail::as_u8(octet);
            if ((value & 0xf0) != 0) {
                return result;
            }
            ++result;
            if ((value & 0x0f) != 0) {
                return result;
            }
            ++result;
        }
        return result;
    }

private:
    explicit merkle_search_tree_policy(std::shared_ptr<const state> state_value)
        : state_(std::move(state_value))
    {
    }

    static void validate_policy_id(const std::string_view value)
    {
        const auto bytes = merkle_detail::bytes_of(value);
        if (value.empty()
            || !merkle_detail::is_canonical_utf8(bytes)
            || merkle_detail::is_all_unicode_whitespace(bytes)) {
            throw std::invalid_argument(
                "a Merkle policy id must be nonempty canonical UTF-8 and not only whitespace");
        }
        merkle_detail::require_framed_length(value.size());
    }

    [[nodiscard]] static std::string validate_encoding_id(
        const std::string_view value,
        const char* field)
    {
        const auto marker = value.rfind("-v");
        const auto valid_version = marker != std::string_view::npos
            && marker != 0
            && marker + 2 < value.size()
            && std::ranges::all_of(value.substr(marker + 2), [](const unsigned char character) {
                   return character >= '0' && character <= '9';
               });
        const auto bytes = merkle_detail::bytes_of(value);
        const auto canonical_utf8 = merkle_detail::is_canonical_utf8(bytes);
        const auto edge_space = canonical_utf8 && !value.empty()
            && merkle_detail::has_unicode_whitespace_edge(bytes);
        if (value.empty() || edge_space || !valid_version || !canonical_utf8) {
            throw std::invalid_argument(
                std::string{"invalid "} + field
                + " encoding id: expected unpadded canonical UTF-8 ending in -v<digits>");
        }
        merkle_detail::require_framed_length(value.size());
        return std::string{value};
    }

    [[nodiscard]] static merkle_digest hash_framed(
        const std::uint8_t tag,
        const std::initializer_list<std::span<const std::byte>> fields)
    {
        auto length = std::size_t{1};
        for (const auto field : fields) {
            merkle_detail::require_framed_length(field.size());
            constexpr auto frame_length = std::size_t{4};
            const auto maximum = (std::numeric_limits<std::size_t>::max)();
            if (length > maximum - frame_length
                || field.size() > maximum - length - frame_length) {
                throw std::length_error("Merkle framed hash input exceeds native size limits");
            }
            length += frame_length + field.size();
        }
        auto bytes = merkle_bytes{};
        bytes.reserve(length);
        bytes.push_back(merkle_detail::as_byte(tag));
        for (const auto field : fields) {
            merkle_detail::append_i32_be(bytes, static_cast<std::int32_t>(field.size()));
            bytes.insert(bytes.end(), field.begin(), field.end());
        }
        return merkle_digest::hash(bytes);
    }

    std::shared_ptr<const state> state_;
};

} // namespace durable7::hamt
