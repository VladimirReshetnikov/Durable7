/// A sample walking through the library's main collections.

#include "sample_runs.hpp"

#include <durable7/finger_tree/finger_tree.hpp>

#include <array>
#include <cstddef>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace durable7::finger_tree::samples::showcase {
namespace {

template <class Range>
void write_values(std::ostream& output, const Range& values)
{
    auto separator = std::string_view{};
    for (const auto& value : values) {
        output << separator << value;
        separator = ", ";
    }
}

void priority_queue_act(std::ostream& output)
{
    output << "Act 1 - Meldable priority queue\n";
    auto queue = priority_queue<std::string, int>{}
                     .enqueue("deploy", 3)
                     .enqueue("alert", 1)
                     .enqueue("backup", 5)
                     .enqueue("build", 2);
    const auto extra = priority_queue<std::string, int>{}.enqueue("hotfix", 0).enqueue("report", 4);
    queue = queue.meld(extra);

    output << "  drain order: ";
    auto separator = std::string_view{};
    while (const auto next = queue.try_dequeue()) {
        output << separator << next->element << '(' << next->priority << ')';
        separator = ", ";
        queue = next->rest;
    }
    output << "\n\n";
}

void weighted_selection_act(std::ostream& output)
{
    output << "Act 2 - Cumulative-weight selection\n";
    using measure = product_measure<int, size_measure<int>, sum_measure<int>>;
    const auto weights = finger_tree<int, measure>{5, 1, 4};
    constexpr std::array names{std::string_view{"apple"}, std::string_view{"fig"}, std::string_view{"date"}};

    for (const auto target : {0, 4, 5, 6, 9}) {
        const auto selected = try_select_by_cumulative_weight(weights, target);
        output << "  target " << target << " -> " << names[selected->index_before]
               << " (weight " << selected->value << ")\n";
    }
    output << '\n';
}

void sorted_collection_act(std::ostream& output)
{
    output << "Act 3 - Order-statistic sorted collections\n";
    const auto values = std::array{42, 7, 19, 42, 3, 88, 51, 12, 19};
    const auto set = sorted_set<int>::from_range(values);
    const auto middle = set[set.size() / 2];
    output << "  distinct " << set.size() << ": min " << set.min() << ", median " << middle
           << ", max " << set.max() << ", median rank " << *set.index_of(middle) << '\n';
    output << "  values in [10, 55]: ";
    write_values(output, set.get_range(10, 55).to_vector());
    output << '\n';

    const auto map = sorted_map<int, std::string>{
        {10, "ten"}, {25, "twenty-five"}, {40, "forty"}, {55, "fifty-five"}, {70, "seventy"}};
    const auto floor = map.try_floor_entry(50);
    const auto ceiling = map.try_ceiling_entry(50);
    output << "  map floor/ceiling of 50: " << floor->first << " / " << ceiling->first << "\n\n";
}

void interval_act(std::ostream& output)
{
    output << "Act 4 - Interval overlap queries\n";
    const auto intervals = interval_tree<int>{}.insert(1, 5).insert(3, 8).insert(10, 15).insert(12, 20).insert(22, 25);
    for (const auto query : {interval<int>{2, 4}, interval<int>{11, 13}, interval<int>{21, 23}}) {
        const auto matches = intervals.find_overlaps(query);
        output << "  [" << query.low << ',' << query.high << "] -> " << matches.size() << " overlap(s)";
        if (!matches.empty()) {
            output << ": ";
            auto separator = std::string_view{};
            for (const auto& match : matches) {
                output << separator << '[' << match.low << ',' << match.high << ']';
                separator = ", ";
            }
        }
        output << '\n';
    }
    output << '\n';
}

void reversible_act(std::ostream& output)
{
    output << "Act 5 - O(1) reversible deque view\n";
    const auto values = std::array{1, 2, 3, 4, 5, 6, 7, 8};
    const auto forward = reversible_deque<int>::from_range(values);
    const auto reversed = forward.reverse();
    output << "  forward: ";
    write_values(output, forward.to_vector());
    output << "\n  reverse: ";
    write_values(output, reversed.to_vector());
    output << "\n  concat size: " << forward.concat(reversed).size() << "\n\n";
}

} // namespace

void run(std::ostream& output)
{
    output << "FingerTree showcase: one measured core, many persistent structures\n"
              "================================================================\n\n";
    priority_queue_act(output);
    weighted_selection_act(output);
    sorted_collection_act(output);
    interval_act(output);
    reversible_act(output);
    output << "Done. Each update returned a new value while retaining prior versions.\n";
}

} // namespace durable7::finger_tree::samples::showcase
