#pragma once

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace durable7::finger_tree::tests {

inline constexpr std::string_view replay_seed_environment_variable = "FINGERTREE_REPLAY_SEED";

[[nodiscard]] inline std::uint64_t parse_replay_seed(std::string_view text)
{
    auto base = 10;
    if (text.starts_with("0x") || text.starts_with("0X")) {
        base = 16;
        text.remove_prefix(2);
    }

    if (text.empty()) {
        throw std::invalid_argument("replay seed cannot be empty");
    }

    auto value = std::uint64_t{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, base);
    if (error != std::errc{} || end != text.data() + text.size()) {
        throw std::invalid_argument("replay seed must be an unsigned decimal or 0x-prefixed hexadecimal integer");
    }

    return value;
}

[[nodiscard]] inline std::optional<std::uint64_t>& replay_seed_override_storage() noexcept
{
    static auto value = std::optional<std::uint64_t>{};
    return value;
}

inline void set_replay_seed_override(const std::optional<std::uint64_t> seed) noexcept
{
    replay_seed_override_storage() = seed;
}

[[nodiscard]] inline std::optional<std::uint64_t> replay_seed_override()
{
    if (const auto explicit_seed = replay_seed_override_storage(); explicit_seed.has_value()) {
        return explicit_seed;
    }

    auto environment_seed = std::optional<std::string>{};
#if defined(_MSC_VER)
    auto* buffer = static_cast<char*>(nullptr);
    auto length = std::size_t{};
    if (_dupenv_s(&buffer, &length, replay_seed_environment_variable.data()) != 0) {
        throw std::runtime_error("could not read the replay-seed environment variable");
    }
    if (buffer != nullptr) {
        try {
            environment_seed.emplace(buffer);
        } catch (...) {
            std::free(buffer);
            throw;
        }
        std::free(buffer);
    }
#else
    if (const auto* const buffer = std::getenv(replay_seed_environment_variable.data()); buffer != nullptr) {
        environment_seed.emplace(buffer);
    }
#endif
    if (!environment_seed.has_value() || environment_seed->empty()) {
        return std::nullopt;
    }

    try {
        return parse_replay_seed(*environment_seed);
    } catch (const std::invalid_argument& error) {
        throw std::invalid_argument(
            std::string{replay_seed_environment_variable} + " is invalid: " + error.what());
    }
}

[[nodiscard]] inline std::uint64_t effective_replay_seed(const std::uint64_t default_seed)
{
    constexpr auto nonzero_fallback = std::uint64_t{0x9e3779b97f4a7c15ULL};
    const auto selected = replay_seed_override().value_or(default_seed);
    return selected == 0 ? nonzero_fallback : selected;
}

[[nodiscard]] inline std::vector<std::uint64_t>& captured_replay_seeds() noexcept
{
    static thread_local auto seeds = std::vector<std::uint64_t>{};
    return seeds;
}

inline void reset_captured_replay_seeds() noexcept
{
    captured_replay_seeds().clear();
}

[[nodiscard]] inline std::string format_replay_seed(const std::uint64_t seed)
{
    auto output = std::ostringstream{};
    output << "0x" << std::hex << seed;
    return output.str();
}

inline void capture_replay_seed(const std::uint64_t seed)
{
    auto& seeds = captured_replay_seeds();
    for (const auto seen : seeds) {
        if (seen == seed) {
            return;
        }
    }

    seeds.push_back(seed);
    // Announce before the randomized work begins. Unlike catch-time runner
    // diagnostics, this survives native assertions, aborts, and stack faults.
    std::cerr << "[replay] using seed " << format_replay_seed(seed) << '\n' << std::flush;
}

[[nodiscard]] inline std::vector<std::uint64_t> select_replay_seeds(
    const std::vector<std::uint64_t>& default_seeds)
{
    if (const auto override = replay_seed_override(); override.has_value()) {
        return {effective_replay_seed(*override)};
    }

    auto selected = std::vector<std::uint64_t>{};
    selected.reserve(default_seeds.size());
    for (const auto seed : default_seeds) {
        selected.push_back(effective_replay_seed(seed));
    }

    return selected;
}

} // namespace durable7::finger_tree::tests
