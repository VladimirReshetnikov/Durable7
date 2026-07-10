#include <tools/data_structures/finger_tree/finger_tree.hpp>

#include <array>
#include <cstddef>
#include <iostream>
#include <string_view>

namespace ft = tools::data_structures::finger_tree;

int main()
{
    if (ft::library_name != std::string_view{"Tools.DataStructures.FingerTree.Cpp"}) {
        std::cerr << "unexpected installed library metadata\n";
        return 1;
    }

    const auto source = std::array{1, 2, 3, 4};
    const auto deque = ft::persistent_deque<int>::from_range(source).push_front(0).push_back(5);
    const auto weighted = ft::finger_tree<int, ft::sum_measure<int>>{5, 1, 4};
    const auto selected = ft::try_select_by_cumulative_weight(weighted, 5);
    const auto text = ft::to_text_rope("alpha\nbeta\ngamma");

    if (deque.size() != 6 || deque.front() != 0 || deque.back() != 5 || !selected.has_value()
        || selected->value != 1 || ft::line_count(text) != 3 || ft::get_line(text, 1) != "beta") {
        std::cerr << "installed public API smoke check failed\n";
        return 1;
    }

    std::cout << "installed package consumer passed\n";
    return 0;
}
