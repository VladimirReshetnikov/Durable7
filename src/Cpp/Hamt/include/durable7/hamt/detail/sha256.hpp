/// The SHA-256 implementation the default Merkle policy hashes with.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#if defined(_MSC_VER)
#pragma comment(lib, "bcrypt.lib")
#endif
#else
#include <openssl/evp.h>
#endif

namespace durable7::hamt::detail {

inline constexpr std::size_t sha256_digest_size = 32;

#if defined(_WIN32)

/// Raises the error a failed CNG call reported.
[[noreturn]] inline void throw_bcrypt_failure(const char* operation, const NTSTATUS status)
{
    throw std::runtime_error(
        std::string{operation} + " failed with NTSTATUS "
        + std::to_string(static_cast<std::int32_t>(status)));
}

/// Fails when a CNG call reports an error.
inline void require_bcrypt_success(const char* operation, const NTSTATUS status)
{
    if (status < 0) {
        throw_bcrypt_failure(operation, status);
    }
}

/// The interface a SHA-256 implementation is supplied through.
class sha256_provider final {
public:
    /// Constructs the sha256 provider from the given parts.
    sha256_provider()
    {
        require_bcrypt_success(
            "BCryptOpenAlgorithmProvider(SHA-256)",
            BCryptOpenAlgorithmProvider(&handle_, BCRYPT_SHA256_ALGORITHM, nullptr, 0));
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
                throw std::runtime_error("BCrypt returned a malformed SHA-256 object length");
            }

            ULONG digest_length = 0;
            require_bcrypt_success(
                "BCryptGetProperty(BCRYPT_HASH_LENGTH)",
                BCryptGetProperty(
                    handle_,
                    BCRYPT_HASH_LENGTH,
                    reinterpret_cast<PUCHAR>(&digest_length),
                    sizeof(digest_length),
                    &copied,
                    0));
            if (copied != sizeof(digest_length) || digest_length != sha256_digest_size) {
                throw std::runtime_error("BCrypt SHA-256 provider returned an unexpected digest length");
            }
        } catch (...) {
            (void)BCryptCloseAlgorithmProvider(handle_, 0);
            handle_ = nullptr;
            throw;
        }
    }

    /// Takes a second handle on the same collection version; the nodes are shared, not copied.
    sha256_provider(const sha256_provider&) = delete;
    sha256_provider& operator=(const sha256_provider&) = delete;

    /// Constructs the sha256 provider from the given parts.
    ~sha256_provider()
    {
        if (handle_ != nullptr) {
            (void)BCryptCloseAlgorithmProvider(handle_, 0);
        }
    }

    /// How many bytes the value occupies.
    /// A handle on the stored value.
    [[nodiscard]] BCRYPT_ALG_HANDLE handle() const noexcept { return handle_; }
    [[nodiscard]] ULONG object_length() const noexcept { return object_length_; }

private:
    BCRYPT_ALG_HANDLE handle_ = nullptr;
    ULONG object_length_ = 0;
};

/// The shared platform SHA-256 provider, created once and reused.
[[nodiscard]] inline const sha256_provider& native_sha256_provider()
{
    static const sha256_provider provider{};
    return provider;
}

/// The SHA-256 digest of the input.
[[nodiscard]] inline std::array<std::byte, sha256_digest_size> sha256(
    const std::span<const std::byte> message)
{
    if (message.size() > (std::numeric_limits<ULONG>::max)()) {
        throw std::length_error("CNG SHA-256 input exceeds the platform ULONG length limit");
    }

    const auto& provider = native_sha256_provider();
    auto hash_object = std::vector<UCHAR>(provider.object_length());
    BCRYPT_HASH_HANDLE hash = nullptr;
    require_bcrypt_success(
        "BCryptCreateHash(SHA-256)",
        BCryptCreateHash(
            provider.handle(),
            &hash,
            hash_object.data(),
            static_cast<ULONG>(hash_object.size()),
            nullptr,
            0,
            0));

    try {
        auto empty_input = UCHAR{0};
        auto* input = message.empty()
            ? &empty_input
            : reinterpret_cast<PUCHAR>(const_cast<std::byte*>(message.data()));
        require_bcrypt_success(
            "BCryptHashData(SHA-256)",
            BCryptHashData(
                hash,
                input,
                static_cast<ULONG>(message.size()),
                0));
        auto result = std::array<std::byte, sha256_digest_size>{};
        require_bcrypt_success(
            "BCryptFinishHash(SHA-256)",
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

#else

/// The SHA-256 digest of the input.
[[nodiscard]] inline std::array<std::byte, sha256_digest_size> sha256(
    const std::span<const std::byte> message)
{
    auto result = std::array<std::byte, sha256_digest_size>{};
    unsigned int written = 0;
    const auto* data = message.empty()
        ? static_cast<const void*>("")
        : static_cast<const void*>(message.data());
    if (EVP_Digest(
            data,
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

#endif

} // namespace durable7::hamt::detail
