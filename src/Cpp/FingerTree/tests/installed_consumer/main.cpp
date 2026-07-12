#include <tools/data_structures/finger_tree/finger_tree.hpp>

#include <array>
#include <cstddef>
#include <iostream>
#include <string_view>

namespace ft = tools::data_structures::finger_tree;

namespace {

struct sum_monoid final {
    using measure_type = int;

    [[nodiscard]] static constexpr measure_type empty() noexcept
    {
        return 0;
    }

    [[nodiscard]] static constexpr measure_type combine(const measure_type left, const measure_type right) noexcept
    {
        return left + right;
    }
};

} // namespace

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
    const auto rrb = ft::rrb_vector<int>::from_range(source).set_item(2, 30).concat(ft::rrb_vector<int>{5, 6});
    const auto canonical_policy = ft::zip_tree_rank_policy<int>::seeded(0x1234);
    const auto canonical = ft::canonical_sorted_set<int>::from_range(source, canonical_policy).add(5);
    const auto brodal = ft::brodal_okasaki_heap<int>::from_range(source).insert(0);
    const auto brodal_deletion = brodal.try_delete_minimum();
    const auto text = ft::to_text_rope("alpha\nbeta\ngamma");
    auto daba = ft::daba_lite<int, sum_monoid>{};
    daba.insert(7);
    daba.insert(11);
    daba.evict();

    if (deque.size() != 6 || deque.front() != 0 || deque.back() != 5 || !selected.has_value()
        || selected->value != 1 || rrb.size() != 6 || rrb[2] != 30 || rrb.back() != 6
        || canonical.size() != 5 || canonical.content_hash() == 0 || !canonical.contains(3)
        || brodal.minimum() != 0 || !brodal_deletion.has_value()
        || *brodal_deletion->first != 0 || brodal_deletion->second.minimum() != 1
        || ft::line_count(text) != 3 || ft::get_line(text, 1) != "beta" || daba.aggregate() != 11) {
        std::cerr << "installed public API smoke check failed\n";
        return 1;
    }

    std::cout << "installed package consumer passed\n";
    return 0;
}
