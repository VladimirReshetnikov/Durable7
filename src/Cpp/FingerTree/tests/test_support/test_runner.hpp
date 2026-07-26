/// A minimal test runner: named cases, run in order, each result reported.

#pragma once

#include "replay_seed.hpp"

#include <exception>
#include <functional>
#include <iostream>
#include <optional>
#include <source_location>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace durable7::finger_tree::tests {

/// Raised when an assertion fails, carrying the message the report shows.
class test_failure final : public std::runtime_error {
public:
    /// Constructs the test failure from the given parts.
    explicit test_failure(const std::string& message)
        : std::runtime_error(message)
    {
    }
};

/// Fails the current case unless the condition holds.
inline void require(
    const bool condition,
    const std::string_view expression,
    const std::source_location location = std::source_location::current())
{
    if (condition) {
        return;
    }

    std::ostringstream message;
    message << location.file_name() << ':' << location.line() << ": requirement failed: " << expression;
    throw test_failure(message.str());
}

/// Fails the current case unless the two values are equal, reporting both.
template <class T, class U>
void require_equal(
    const T& actual,
    const U& expected,
    const std::string_view actual_expression,
    const std::string_view expected_expression,
    const std::source_location location = std::source_location::current())
{
    if (actual == expected) {
        return;
    }

    std::ostringstream message;
    message << location.file_name() << ':' << location.line() << ": equality failed: " << actual_expression
            << " != " << expected_expression << " (actual " << actual << ", expected " << expected << ')';
    throw test_failure(message.str());
}

/// Fails the current case unless the action raises the expected exception.
template <class Exception, class Function>
void require_throws(
    Function&& function,
    const std::string_view expression,
    const std::source_location location = std::source_location::current())
{
    try {
        std::forward<Function>(function)();
    } catch (const Exception&) {
        return;
    } catch (const std::exception& ex) {
        std::ostringstream message;
        message << location.file_name() << ':' << location.line() << ": expected exception for " << expression
                << ", but caught different exception: " << ex.what();
        throw test_failure(message.str());
    } catch (...) {
        std::ostringstream message;
        message << location.file_name() << ':' << location.line() << ": expected exception for " << expression
                << ", but caught non-standard exception";
        throw test_failure(message.str());
    }

    std::ostringstream message;
    message << location.file_name() << ':' << location.line() << ": expected exception for " << expression;
    throw test_failure(message.str());
}

/// A test suite: named cases run in order, reporting each result.
class suite final {
public:
    using test_function = std::function<void()>;

    /// A version with the key's whole group of values replaced.
    void set_group(std::string group)
    {
        if (group.empty()) {
            throw std::invalid_argument("test group cannot be empty");
        }

        current_group_ = std::move(group);
    }

    /// A collection containing the given element; returns the receiver when already present.
    void add(std::string name, test_function body)
    {
        if (current_group_.empty()) {
            throw std::logic_error("set_group must be called before registering tests");
        }

        tests_.push_back(test_case{current_group_, std::move(name), std::move(body)});
    }

    /// Runs the operation.
    [[nodiscard]] int run(const int argument_count, const char* const* arguments) const
    {
        auto options = runner_options{};
        try {
            options = parse_options(argument_count, arguments);
        } catch (const std::invalid_argument& error) {
            std::cerr << "test-runner argument error: " << error.what() << '\n';
            print_usage(std::cerr, argument_count == 0 ? "fingertree_smoke_tests" : arguments[0]);
            return 2;
        }

        if (options.seed.has_value()) {
            set_replay_seed_override(options.seed);
        } else {
            try {
                // Validate the environment override before the first randomized
                // case so a malformed value is a runner error, not a test defect.
                (void)replay_seed_override();
            } catch (const std::invalid_argument& error) {
                std::cerr << "test-runner seed error: " << error.what() << '\n';
                return 2;
            }
        }

        if (options.help) {
            print_usage(std::cout, argument_count == 0 ? "fingertree_smoke_tests" : arguments[0]);
            return 0;
        }

        if (options.list) {
            auto listed = std::size_t{0};
            for (const auto& test : tests_) {
                if (matches(test, options)) {
                    std::cout << test.group << '\t' << test.name << '\n';
                    ++listed;
                }
            }
            if (listed == 0) {
                std::cerr << "no tests matched the requested group/filter\n";
                return 2;
            }
            return 0;
        }

        auto failures = 0;
        auto selected = std::size_t{0};

        for (const auto& test : tests_) {
            if (!matches(test, options)) {
                continue;
            }

            ++selected;
            reset_captured_replay_seeds();
            try {
                test.body();
                std::cout << "[pass] " << test.name << '\n';
            } catch (const std::exception& ex) {
                ++failures;
                std::cerr << "[fail] " << test.name << ": " << ex.what() << '\n';
                print_replay_seeds();
            } catch (...) {
                ++failures;
                std::cerr << "[fail] " << test.name << ": unknown exception\n";
                print_replay_seeds();
            }
        }

        if (selected == 0) {
            std::cerr << "no tests matched the requested group/filter\n";
            return 2;
        }

        if (failures != 0) {
            std::cerr << failures << " test(s) failed out of " << selected << " selected\n";
            return 1;
        }

        std::cout << selected << " test(s) passed\n";
        return 0;
    }

private:
    struct runner_options final {
        std::optional<std::string> group;
        std::optional<std::string> filter;
        std::optional<std::uint64_t> seed;
        bool list = false;
        bool help = false;
    };

    struct test_case final {
        std::string group;
        std::string name;
        test_function body;
    };

    [[nodiscard]] static runner_options parse_options(
        const int argument_count,
        const char* const* arguments)
    {
        auto options = runner_options{};
        for (auto index = 1; index < argument_count; ++index) {
            const auto argument = std::string_view{arguments[index]};
            const auto require_value = [&](const std::string_view option) -> std::string_view {
                if (++index >= argument_count) {
                    throw std::invalid_argument(std::string{option} + " requires a value");
                }

                return arguments[index];
            };

            if (argument == "--group") {
                options.group = require_value(argument);
            } else if (argument == "--filter") {
                options.filter = require_value(argument);
            } else if (argument == "--seed") {
                options.seed = parse_replay_seed(require_value(argument));
            } else if (argument == "--list") {
                options.list = true;
            } else if (argument == "--help" || argument == "-h") {
                options.help = true;
            } else {
                throw std::invalid_argument("unknown option: " + std::string{argument});
            }
        }

        return options;
    }

    [[nodiscard]] static bool matches(const test_case& test, const runner_options& options)
    {
        if (options.group.has_value() && test.group != *options.group) {
            return false;
        }

        return !options.filter.has_value() || test.name.find(*options.filter) != std::string::npos;
    }

    static void print_replay_seeds()
    {
        const auto& seeds = captured_replay_seeds();
        if (seeds.empty()) {
            return;
        }

        std::cerr << "[replay] seed" << (seeds.size() == 1 ? "" : "s") << ':';
        for (const auto seed : seeds) {
            std::cerr << ' ' << format_replay_seed(seed);
        }
        std::cerr << " (rerun with --seed <seed> or " << replay_seed_environment_variable << ")\n";
    }

    static void print_usage(std::ostream& output, const std::string_view executable)
    {
        output << "usage: " << executable
               << " [--group <name>] [--filter <substring>] [--seed <decimal|0xhex>] [--list]\n";
    }

    std::vector<test_case> tests_;
    std::string current_group_;
};

} // namespace durable7::finger_tree::tests

#define FT_REQUIRE(expression) \
    ::durable7::finger_tree::tests::require((expression), #expression)

#define FT_REQUIRE_EQUAL(actual, expected) \
    ::durable7::finger_tree::tests::require_equal((actual), (expected), #actual, #expected)

#define FT_REQUIRE_THROWS(exception_type, expression) \
    ::durable7::finger_tree::tests::require_throws<exception_type>([&] { (void)(expression); }, #expression)
