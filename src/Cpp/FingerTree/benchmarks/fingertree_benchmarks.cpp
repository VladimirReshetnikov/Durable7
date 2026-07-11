#include "benchmark_allocation.hpp"

#include <tools/data_structures/finger_tree/finger_tree.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ft = tools::data_structures::finger_tree;
namespace bench = tools::data_structures::finger_tree::benchmarks;

namespace {

using clock_type = std::chrono::steady_clock;

struct options final {
    bool short_mode = false;
    bool list_only = false;
    std::string filter;
};

struct observation final {
    std::string_view name;
    std::size_t size;
    std::size_t iterations;
    double nanoseconds_per_operation;
    std::optional<double> allocations_per_operation;
    std::optional<double> bytes_per_operation;
    std::uint64_t checksum;
};

constexpr std::array benchmark_names{
    std::string_view{"persistence_branching"},
    std::string_view{"deque_endpoint"},
    std::string_view{"deque_endpoint_read"},
    std::string_view{"deque_indexed_read"},
    std::string_view{"deque_catenation"},
    std::string_view{"rrb_indexed_read"},
    std::string_view{"rope_indexed_read"},
    std::string_view{"rrb_catenation"},
    std::string_view{"rope_catenation"},
    std::string_view{"reversible_reverse"},
    std::string_view{"reversible_endpoint"},
    std::string_view{"reversible_endpoint_read"},
    std::string_view{"reversible_catenation"},
    std::string_view{"weighted_selection"},
    std::string_view{"sorted_search"},
    std::string_view{"rope_edit"},
    std::string_view{"rope_split"},
    std::string_view{"rope_slice"},
    std::string_view{"rope_navigation"},
    std::string_view{"rope_linear_navigation"},
    std::string_view{"priority_meld"},
    std::string_view{"interval_overlap_query"},
};

[[nodiscard]] options parse_options(const int argc, char** argv)
{
    auto result = options{};
    for (auto index = 1; index < argc; ++index) {
        const auto argument = std::string_view{argv[index]};
        if (argument == "--short") {
            result.short_mode = true;
        } else if (argument == "--list") {
            result.list_only = true;
        } else if (argument == "--help" || argument == "-h") {
            std::cout << "usage: fingertree_benchmarks [--short] [--filter NAME] [--list]\n";
            std::exit(EXIT_SUCCESS);
        } else if (argument == "--filter") {
            if (++index == argc) {
                throw std::invalid_argument("--filter requires a substring");
            }
            result.filter = argv[index];
        } else if (argument.starts_with("--filter=")) {
            result.filter = argument.substr(std::string_view{"--filter="}.size());
        } else {
            throw std::invalid_argument("unknown benchmark option: " + std::string{argument});
        }
    }
    return result;
}

[[nodiscard]] bool selected(const options& settings, const std::string_view name)
{
    return settings.filter.empty() || name.contains(settings.filter);
}

template <class Operation>
[[nodiscard]] observation measure(
    const std::string_view name,
    const std::size_t size,
    const std::size_t iterations,
    const bool count_allocations,
    Operation&& operation)
{
    auto warmup = std::uint64_t{0};
    for (auto index = std::size_t{0}; index < (std::min)(iterations, std::size_t{4}); ++index) {
        warmup ^= static_cast<std::uint64_t>(operation(index));
    }

    auto checksum = warmup;
    auto scope = bench::allocation_counting_scope{count_allocations};
    const auto start = clock_type::now();
    for (auto index = std::size_t{0}; index < iterations; ++index) {
        checksum += static_cast<std::uint64_t>(operation(index));
    }
    const auto stop = clock_type::now();
    const auto allocation_count = scope.allocations();
    const auto allocated_bytes = scope.bytes_allocated();
    const auto elapsed = std::chrono::duration<double, std::nano>{stop - start}.count();
#ifndef FINGERTREE_DISABLE_ALLOCATION_TRACKING
    const auto allocations_per_operation = count_allocations
        ? std::optional<double>{static_cast<double>(allocation_count) / static_cast<double>(iterations)}
        : std::nullopt;
    const auto bytes_per_operation = count_allocations
        ? std::optional<double>{static_cast<double>(allocated_bytes) / static_cast<double>(iterations)}
        : std::nullopt;
#else
    (void)allocation_count;
    (void)allocated_bytes;
    const auto allocations_per_operation = std::optional<double>{};
    const auto bytes_per_operation = std::optional<double>{};
#endif
    return observation{
        name,
        size,
        iterations,
        elapsed / static_cast<double>(iterations),
        allocations_per_operation,
        bytes_per_operation,
        checksum};
}

void print(const observation& value)
{
    std::cout << value.name << ',' << value.size << ',' << value.iterations << ',' << std::fixed
              << std::setprecision(2) << value.nanoseconds_per_operation << ',';
    if (value.allocations_per_operation.has_value()) {
        std::cout << *value.allocations_per_operation << ',' << *value.bytes_per_operation;
    } else {
        std::cout << "n/a,n/a";
    }
    std::cout << ',' << value.checksum << '\n';
}

[[nodiscard]] ft::persistent_deque<int> make_deque(const std::size_t size)
{
    auto result = ft::persistent_deque<int>{};
    for (auto index = std::size_t{0}; index < size; ++index) {
        result = result.push_back(static_cast<int>(index));
    }
    const auto forced = result.to_vector();
    if (forced.size() != size) {
        throw std::logic_error("deque benchmark setup failed");
    }
    return result;
}

[[nodiscard]] ft::reversible_deque<int> make_reversible(const std::size_t size)
{
    auto values = std::vector<int>(size);
    for (auto index = std::size_t{0}; index < size; ++index) {
        values[index] = static_cast<int>(index);
    }
    return ft::reversible_deque<int>::from_range(values);
}

void run_persistence_branching(const options& settings)
{
    constexpr auto name = std::string_view{"persistence_branching"};
    if (!selected(settings, name)) {
        return;
    }

    const auto sizes = std::array<std::size_t, 3>{100, 10'000, 1'000'000};
    auto allocation_rates = std::vector<double>{};
    for (const auto size : sizes) {
        const auto base = make_deque(size);
        const auto iterations = settings.short_mode ? (size == 1'000'000 ? 32U : 256U)
                                                    : (size == 1'000'000 ? 512U : 8'192U);
        const auto result = measure(name, size, iterations, true, [&](const std::size_t iteration) {
            const auto branch = base.push_back(static_cast<int>(iteration));
            return branch.size() + static_cast<std::size_t>(branch.back());
        });
        print(result);
        if (result.allocations_per_operation.has_value()) {
            allocation_rates.push_back(*result.allocations_per_operation);
        }
    }

    if (allocation_rates.size() == sizes.size()) {
        const auto [minimum, maximum] = std::minmax_element(allocation_rates.begin(), allocation_rates.end());
        if (*maximum > *minimum + 1.0) {
            throw std::runtime_error("persistence branching allocations are not size-flat");
        }
    }
}

void run_deque_catenation(const options& settings)
{
    constexpr auto name = std::string_view{"deque_catenation"};
    if (!selected(settings, name)) {
        return;
    }
    for (const auto size : {100U, 10'000U, 100'000U}) {
        const auto left = make_deque(size / 2);
        const auto right = make_deque(size - size / 2);
        const auto iterations = settings.short_mode ? 128U : 4'096U;
        print(measure(name, size, iterations, false, [&](const std::size_t) {
            const auto joined = left.concat(right);
            return joined.size() + static_cast<std::size_t>(joined.front() + joined.back());
        }));
    }
}

void run_deque_reads(const options& settings)
{
    constexpr auto endpoint_name = std::string_view{"deque_endpoint"};
    constexpr auto endpoint_read_name = std::string_view{"deque_endpoint_read"};
    constexpr auto indexed_name = std::string_view{"deque_indexed_read"};
    if (!selected(settings, endpoint_name)
        && !selected(settings, endpoint_read_name)
        && !selected(settings, indexed_name)) {
        return;
    }
    for (const auto size : {100U, 10'000U, 100'000U}) {
        const auto deque = make_deque(size);
        if (selected(settings, endpoint_name)) {
            const auto iterations = settings.short_mode ? 128U : 4'096U;
            print(measure(endpoint_name, size, iterations, false, [&](const std::size_t iteration) {
                const auto updated = deque.push_front(-static_cast<int>(iteration) - 1)
                                         .push_back(static_cast<int>(iteration));
                return updated.size() + static_cast<std::size_t>(updated.back() - updated.front());
            }));
        }
        if (selected(settings, endpoint_read_name)) {
            const auto iterations = settings.short_mode ? 512U : 32'768U;
            print(measure(endpoint_read_name, size, iterations, false, [&](const std::size_t) {
                return static_cast<std::size_t>(deque.front() + deque.back());
            }));
        }
        if (selected(settings, indexed_name)) {
            const auto iterations = settings.short_mode ? 512U : 32'768U;
            print(measure(indexed_name, size, iterations, false, [&](const std::size_t iteration) {
                return static_cast<std::size_t>(deque[(iteration * 7'919U) % size]);
            }));
        }
    }
}

[[nodiscard]] std::vector<int> make_integer_values(const std::size_t size)
{
    auto values = std::vector<int>(size);
    for (auto index = std::size_t{0}; index < size; ++index) {
        values[index] = static_cast<int>(index);
    }
    return values;
}

void run_rrb_vector(const options& settings)
{
    constexpr auto rrb_index_name = std::string_view{"rrb_indexed_read"};
    constexpr auto rope_index_name = std::string_view{"rope_indexed_read"};
    constexpr auto rrb_concat_name = std::string_view{"rrb_catenation"};
    constexpr auto rope_concat_name = std::string_view{"rope_catenation"};
    if (!selected(settings, rrb_index_name)
        && !selected(settings, rope_index_name)
        && !selected(settings, rrb_concat_name)
        && !selected(settings, rope_concat_name)) {
        return;
    }

    for (const auto size : {100U, 10'000U, 100'000U}) {
        const auto values = make_integer_values(size);
        const auto rrb = ft::rrb_vector<int>::from_range(values);
        const auto rope = ft::rope<int>::from_range(values);
        const auto read_iterations = settings.short_mode ? 256U : 16'384U;
        if (selected(settings, rrb_index_name)) {
            print(measure(rrb_index_name, size, read_iterations, false, [&](const std::size_t iteration) {
                return static_cast<std::size_t>(rrb[(iteration * 7'919U) % size]);
            }));
        }
        if (selected(settings, rope_index_name)) {
            print(measure(rope_index_name, size, read_iterations, false, [&](const std::size_t iteration) {
                return static_cast<std::size_t>(rope[(iteration * 7'919U) % size]);
            }));
        }

        const auto middle = size / 2;
        const auto left_values = std::span<const int>{values}.first(middle);
        const auto right_values = std::span<const int>{values}.subspan(middle);
        const auto left_rrb = ft::rrb_vector<int>::create(left_values);
        const auto right_rrb = ft::rrb_vector<int>::create(right_values);
        const auto left_rope = ft::rope<int>::create(left_values);
        const auto right_rope = ft::rope<int>::create(right_values);
        const auto concat_iterations = settings.short_mode ? 128U : 4'096U;
        if (selected(settings, rrb_concat_name)) {
            print(measure(rrb_concat_name, size, concat_iterations, false, [&](const std::size_t) {
                const auto joined = left_rrb.concat(right_rrb);
                return joined.size() + static_cast<std::size_t>(joined.front() + joined.back());
            }));
        }
        if (selected(settings, rope_concat_name)) {
            print(measure(rope_concat_name, size, concat_iterations, false, [&](const std::size_t) {
                const auto joined = left_rope.concat(right_rope);
                return joined.size() + static_cast<std::size_t>(joined.front() + joined.back());
            }));
        }
    }
}

void run_reversible_reverse(const options& settings)
{
    constexpr auto name = std::string_view{"reversible_reverse"};
    if (!selected(settings, name)) {
        return;
    }
    for (const auto size : {100U, 10'000U, 100'000U}) {
        const auto base = make_reversible(size);
        const auto iterations = settings.short_mode ? 256U : 16'384U;
        print(measure(name, size, iterations, false, [&](const std::size_t) {
            const auto reversed = base.reverse();
            return reversed.size() + static_cast<std::size_t>(reversed.front());
        }));
    }
}

void run_reversible_overhead(const options& settings)
{
    constexpr auto endpoint_name = std::string_view{"reversible_endpoint"};
    constexpr auto endpoint_read_name = std::string_view{"reversible_endpoint_read"};
    constexpr auto catenation_name = std::string_view{"reversible_catenation"};
    if (!selected(settings, endpoint_name)
        && !selected(settings, endpoint_read_name)
        && !selected(settings, catenation_name)) {
        return;
    }
    for (const auto size : {100U, 10'000U, 100'000U}) {
        const auto deque = make_reversible(size);
        if (selected(settings, endpoint_name)) {
            const auto iterations = settings.short_mode ? 128U : 4'096U;
            print(measure(endpoint_name, size, iterations, false, [&](const std::size_t iteration) {
                const auto updated = deque.push_front(-static_cast<int>(iteration) - 1)
                                         .push_back(static_cast<int>(iteration));
                return updated.size() + static_cast<std::size_t>(updated.back() - updated.front());
            }));
        }
        if (selected(settings, endpoint_read_name)) {
            const auto iterations = settings.short_mode ? 512U : 32'768U;
            print(measure(endpoint_read_name, size, iterations, false, [&](const std::size_t) {
                return static_cast<std::size_t>(deque.front() + deque.back());
            }));
        }
        if (selected(settings, catenation_name)) {
            const auto midpoint = size / 2;
            auto left_values = std::vector<int>(midpoint);
            auto right_values = std::vector<int>(size - midpoint);
            for (auto index = std::size_t{0}; index < midpoint; ++index) {
                left_values[index] = static_cast<int>(index);
            }
            for (auto index = std::size_t{0}; index < right_values.size(); ++index) {
                right_values[index] = static_cast<int>(midpoint + index);
            }
            const auto left = ft::reversible_deque<int>::from_range(left_values);
            const auto right = ft::reversible_deque<int>::from_range(right_values);
            const auto concat_iterations = settings.short_mode ? 128U : 4'096U;
            print(measure(catenation_name, size, concat_iterations, false, [&](const std::size_t) {
                const auto joined = left.concat(right);
                return joined.size() + static_cast<std::size_t>(joined.front() + joined.back());
            }));
        }
    }
}

void run_weighted_selection(const options& settings)
{
    constexpr auto name = std::string_view{"weighted_selection"};
    if (!selected(settings, name)) {
        return;
    }
    using measure_type = ft::product_measure<int, ft::size_measure<int>, ft::sum_measure<int>>;
    for (const auto size : {100U, 10'000U, 100'000U}) {
        const auto values = std::vector<int>(size, 1);
        const auto tree = ft::finger_tree<int, measure_type>::from_range(values);
        const auto iterations = settings.short_mode ? 256U : 16'384U;
        print(measure(name, size, iterations, false, [&](const std::size_t iteration) {
            const auto target = static_cast<int>((iteration * 7'919U) % size);
            const auto hit = ft::try_select_by_cumulative_weight(tree, target);
            return hit->index_before + static_cast<std::size_t>(hit->value);
        }));
    }
}

void run_sorted_search(const options& settings)
{
    constexpr auto name = std::string_view{"sorted_search"};
    if (!selected(settings, name)) {
        return;
    }
    for (const auto size : {100U, 10'000U, 100'000U}) {
        auto values = std::vector<int>(size);
        for (auto index = std::size_t{0}; index < size; ++index) {
            values[index] = static_cast<int>(index * 2);
        }
        const auto set = ft::sorted_set<int>::from_range(values);
        const auto iterations = settings.short_mode ? 256U : 16'384U;
        print(measure(name, size, iterations, false, [&](const std::size_t iteration) {
            const auto key = static_cast<int>((iteration * 7'919U) % (size * 2));
            const auto index = set.index_of(key);
            return index.value_or(set.size());
        }));
    }
}

[[nodiscard]] std::string make_document(const std::size_t lines)
{
    auto text = std::string{};
    text.reserve(lines * 11);
    for (auto line = std::size_t{0}; line < lines; ++line) {
        text += "line-";
        text += std::to_string(line % 100'000);
        text += '\n';
    }
    return text;
}

void run_rope_edit(const options& settings)
{
    constexpr auto edit_name = std::string_view{"rope_edit"};
    constexpr auto split_name = std::string_view{"rope_split"};
    constexpr auto slice_name = std::string_view{"rope_slice"};
    if (!selected(settings, edit_name) && !selected(settings, split_name) && !selected(settings, slice_name)) {
        return;
    }
    for (const auto size : {100U, 10'000U, 100'000U}) {
        const auto text = std::string(size, 'x');
        const auto base = ft::to_text_rope(text);
        const auto iterations = settings.short_mode ? 128U : 4'096U;
        if (selected(settings, edit_name)) {
            print(measure(edit_name, size, iterations, false, [&](const std::size_t iteration) {
                const auto edited = base.insert_range((iteration * 7'919U) % (size + 1), std::string_view{"edit\n"});
                return edited.size() + edited.measure();
            }));
        }
        if (selected(settings, split_name)) {
            print(measure(split_name, size, iterations, false, [&](const std::size_t iteration) {
                const auto split = base.split_at((iteration * 7'919U) % (size + 1));
                return split.left.size() + split.right.size();
            }));
        }
        if (selected(settings, slice_name)) {
            print(measure(slice_name, size, iterations, false, [&](const std::size_t iteration) {
                const auto start = (iteration * 7'919U) % size;
                const auto length = (std::min)(std::size_t{64}, size - start);
                const auto slice = base.slice(start, length);
                return slice.size() + slice.measure();
            }));
        }
    }
}

void run_rope_navigation(const options& settings)
{
    constexpr auto measured_name = std::string_view{"rope_navigation"};
    constexpr auto linear_name = std::string_view{"rope_linear_navigation"};
    if (!selected(settings, measured_name) && !selected(settings, linear_name)) {
        return;
    }
    for (const auto lines : {100U, 10'000U, 100'000U}) {
        const auto text = make_document(lines);
        const auto rope = ft::to_text_rope(text);
        const auto iterations = settings.short_mode ? 128U : 4'096U;
        if (selected(settings, measured_name)) {
            print(measure(measured_name, text.size(), iterations, false, [&](const std::size_t iteration) {
                const auto line = (iteration * 7'919U) % lines;
                const auto offset = ft::offset_of(rope, line, 2);
                const auto location = ft::line_column_of(rope, offset);
                return offset + location.line + location.column;
            }));
        }
        if (selected(settings, linear_name)) {
            print(measure(linear_name, text.size(), iterations, false, [&](const std::size_t iteration) {
                const auto target_line = (iteration * 7'919U) % lines;
                auto current_line = std::size_t{0};
                auto offset = std::size_t{0};
                while (current_line < target_line) {
                    if (text[offset++] == '\n') {
                        ++current_line;
                    }
                }
                return offset + 2;
            }));
        }
    }
}

[[nodiscard]] ft::priority_queue<int, int> make_queue(const std::size_t size, const int salt)
{
    auto result = ft::priority_queue<int, int>{};
    for (auto index = std::size_t{0}; index < size; ++index) {
        result = result.enqueue(static_cast<int>(index), static_cast<int>((index * 97U + salt) % 10'007U));
    }
    (void)result.try_peek();
    return result;
}

void run_priority_meld(const options& settings)
{
    constexpr auto name = std::string_view{"priority_meld"};
    if (!selected(settings, name)) {
        return;
    }
    for (const auto size : {100U, 5'000U, 20'000U}) {
        const auto left = make_queue(size / 2, 11);
        const auto right = make_queue(size - size / 2, 29);
        const auto iterations = settings.short_mode ? 128U : 4'096U;
        print(measure(name, size, iterations, false, [&](const std::size_t) {
            const auto joined = left.meld(right);
            return joined.size() + static_cast<std::size_t>(*joined.try_peek_priority());
        }));
    }
}

[[nodiscard]] ft::interval_tree<int> make_intervals(const std::size_t size)
{
    auto result = ft::interval_tree<int>{};
    for (auto index = std::size_t{0}; index < size; ++index) {
        const auto low = static_cast<int>(index * 3);
        result = result.insert(low, low + static_cast<int>(5 + index % 29));
    }
    return result;
}

void run_interval_queries(const options& settings)
{
    constexpr auto name = std::string_view{"interval_overlap_query"};
    if (!selected(settings, name)) {
        return;
    }
    for (const auto size : {100U, 5'000U, 20'000U}) {
        const auto tree = make_intervals(size);
        const auto iterations = settings.short_mode ? 128U : 4'096U;
        print(measure(name, size, iterations, false, [&](const std::size_t iteration) {
            const auto point = static_cast<int>((iteration * 7'919U) % (size * 3));
            const auto overlaps = tree.find_overlaps({point, point + 7});
            return overlaps.size() + (overlaps.empty() ? 0U : static_cast<std::size_t>(overlaps.front().low));
        }));
    }
}

} // namespace

int main(const int argc, char** argv)
{
    try {
        const auto settings = parse_options(argc, argv);
        if (settings.list_only) {
            for (const auto name : benchmark_names) {
                std::cout << name << '\n';
            }
            return EXIT_SUCCESS;
        }
        if (!settings.filter.empty()
            && std::ranges::none_of(benchmark_names, [&](const std::string_view name) {
                   return name.contains(settings.filter);
               })) {
            throw std::invalid_argument("benchmark filter matched no cases: " + settings.filter);
        }

        std::cout << "FingerTree dependency-free benchmark harness\n"
                     "contract: persistent updates retain the source version and share structure; mutable baselines "
                     "may update in place and are not semantically equivalent\n"
                     "mode: "
                  << (settings.short_mode ? "short sanity" : "measurement") << "\n"
                  << "case,size,iterations,ns/op,allocations/op,bytes/op,checksum\n";

        run_persistence_branching(settings);
        run_deque_reads(settings);
        run_deque_catenation(settings);
        run_rrb_vector(settings);
        run_reversible_reverse(settings);
        run_reversible_overhead(settings);
        run_weighted_selection(settings);
        run_sorted_search(settings);
        run_rope_edit(settings);
        run_rope_navigation(settings);
        run_priority_meld(settings);
        run_interval_queries(settings);
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "benchmark failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
