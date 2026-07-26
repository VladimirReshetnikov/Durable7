/// Runs each sample and checks it completes, so the samples cannot rot.

#include "sample_runs.hpp"

#include <durable7/test_support/headless_test_process.h>

#include <cstdlib>
#include <exception>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace samples = durable7::finger_tree::samples;

namespace {

void require_contains(const std::string_view transcript, const std::string_view marker)
{
    if (!transcript.contains(marker)) {
        throw std::runtime_error("sample transcript omitted marker: " + std::string{marker});
    }
}

template <class Run>
[[nodiscard]] std::string capture_twice(Run run)
{
    auto first = std::ostringstream{};
    auto second = std::ostringstream{};
    run(first);
    run(second);
    if (first.str() != second.str()) {
        throw std::runtime_error("sample transcript is not deterministic");
    }
    return first.str();
}

} // namespace

int main()
{
    if (!d7_enter_headless_test_process()) {
        return EXIT_FAILURE;
    }

    try {
        const auto showcase = capture_twice(samples::showcase::run);
        require_contains(showcase, "Act 1 - Meldable priority queue");
        require_contains(showcase, "Act 2 - Cumulative-weight selection");
        require_contains(showcase, "Act 4 - Interval overlap queries");
        require_contains(showcase, "Act 5 - O(1) reversible deque view");
        require_contains(showcase, "Done. Each update returned a new value");

        const auto snapshots = capture_twice(samples::persistent_snapshots::run);
        require_contains(snapshots, "Act 1 - Undo/redo over retained structural snapshots");
        require_contains(snapshots, "Act 2 - O(log n) line/column navigation");
        require_contains(snapshots, "Act 3 - Atomic single-writer/concurrent-reader snapshot publication");
        require_contains(snapshots, "reader observed 5 whole immutable versions");
        require_contains(snapshots, "Done. Every retained or published version is immutable");

        std::cout << "2 deterministic sample transcripts passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "sample smoke failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
