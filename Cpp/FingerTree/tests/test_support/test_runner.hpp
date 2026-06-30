#pragma once

#include <exception>
#include <functional>
#include <iostream>
#include <source_location>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tools::data_structures::finger_tree::tests {

class test_failure final : public std::runtime_error {
public:
    explicit test_failure(const std::string& message)
        : std::runtime_error(message)
    {
    }
};

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

class suite final {
public:
    using test_function = std::function<void()>;

    void add(std::string name, test_function body)
    {
        tests_.push_back(test_case{std::move(name), std::move(body)});
    }

    [[nodiscard]] int run() const
    {
        auto failures = 0;

        for (const auto& test : tests_) {
            try {
                test.body();
                std::cout << "[pass] " << test.name << '\n';
            } catch (const std::exception& ex) {
                ++failures;
                std::cerr << "[fail] " << test.name << ": " << ex.what() << '\n';
            } catch (...) {
                ++failures;
                std::cerr << "[fail] " << test.name << ": unknown exception\n";
            }
        }

        if (failures != 0) {
            std::cerr << failures << " test(s) failed out of " << tests_.size() << '\n';
            return 1;
        }

        std::cout << tests_.size() << " test(s) passed\n";
        return 0;
    }

private:
    struct test_case final {
        std::string name;
        test_function body;
    };

    std::vector<test_case> tests_;
};

} // namespace tools::data_structures::finger_tree::tests

#define FT_REQUIRE(expression) \
    ::tools::data_structures::finger_tree::tests::require((expression), #expression)

#define FT_REQUIRE_EQUAL(actual, expected) \
    ::tools::data_structures::finger_tree::tests::require_equal((actual), (expected), #actual, #expected)

#define FT_REQUIRE_THROWS(exception_type, expression) \
    ::tools::data_structures::finger_tree::tests::require_throws<exception_type>([&] { (void)(expression); }, #expression)
