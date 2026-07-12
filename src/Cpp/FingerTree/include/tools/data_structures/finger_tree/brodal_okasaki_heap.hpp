#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace tools::data_structures::finger_tree {

struct brodal_okasaki_heap_statistics final {
    std::size_t count = 0;
    std::size_t root_forest_length = 0;
    std::size_t maximum_rank = 0;
    std::size_t maximum_depth = 0;

    friend bool operator==(const brodal_okasaki_heap_statistics&, const brodal_okasaki_heap_statistics&) = default;
};

/// An immutable Brodal-Okasaki bootstrapped skew-binomial min-heap.
///
/// Insert, minimum, and meld have worst-case O(1) work; delete-minimum has worst-case O(log n)
/// work. Comparator identity is representation policy: heaps meld only when derived from the same
/// retained comparator object.
template <class T, class Less = std::less<T>>
    requires std::copy_constructible<Less>
        && std::predicate<const Less&, const T&, const T&>
class brodal_okasaki_heap final {
private:
    struct reclaimable {
        reclaimable* deferred_next = nullptr;
        virtual ~reclaimable() = default;
    };

    struct tree;
    struct forest;
    using tree_pointer = std::shared_ptr<const tree>;
    using forest_pointer = std::shared_ptr<const forest>;

public:
    using value_type = T;
    using comparer_type = Less;
    using size_type = std::size_t;
    using comparer_handle = std::shared_ptr<const comparer_type>;
    using delete_minimum_result =
        std::pair<std::shared_ptr<const value_type>, brodal_okasaki_heap>;

    class const_iterator final {
    public:
        using iterator_category = std::forward_iterator_tag;
        using iterator_concept = std::forward_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = const T*;
        using reference = const T&;

        const_iterator() = default;

        [[nodiscard]] reference operator*() const { return *pending_.back()->value; }
        [[nodiscard]] pointer operator->() const { return pending_.back()->value.get(); }

        const_iterator& operator++()
        {
            const auto* current = pending_.back();
            pending_.pop_back();
            for (auto children = current->children.get(); children != nullptr; children = children->tail.get()) {
                pending_.push_back(children->head.get());
            }
            return *this;
        }

        const_iterator operator++(int)
        {
            auto previous = *this;
            ++*this;
            return previous;
        }

        friend bool operator==(const const_iterator& left, const const_iterator& right) noexcept
        {
            if (left.pending_.empty() || right.pending_.empty()) {
                return left.pending_.empty() && right.pending_.empty();
            }
            return left.owner_ == right.owner_ && left.pending_ == right.pending_;
        }

    private:
        friend class brodal_okasaki_heap;

        explicit const_iterator(tree_pointer root)
            : owner_(std::move(root))
        {
            if (owner_ != nullptr) {
                pending_.push_back(owner_.get());
            }
        }

        tree_pointer owner_;
        std::vector<const tree*> pending_;
    };

    brodal_okasaki_heap()
        : comparer_(default_comparer())
    {
    }

    explicit brodal_okasaki_heap(comparer_type comparer)
        : comparer_(std::make_shared<comparer_type>(std::move(comparer)))
    {
    }

    explicit brodal_okasaki_heap(comparer_handle comparer)
        : comparer_(std::move(comparer))
    {
        if (comparer_ == nullptr) {
            throw std::invalid_argument("Brodal-Okasaki comparer handle cannot be null");
        }
    }

    template <std::ranges::input_range Range>
        requires std::constructible_from<T, std::ranges::range_reference_t<Range>>
            || (!std::is_lvalue_reference_v<Range&&>
                && std::constructible_from<T, std::ranges::range_rvalue_reference_t<Range>>)
    [[nodiscard]] static brodal_okasaki_heap from_range(Range&& values)
    {
        auto result = brodal_okasaki_heap{};
        return result.insert_range(std::forward<Range>(values));
    }

    template <std::ranges::input_range Range>
        requires std::constructible_from<T, std::ranges::range_reference_t<Range>>
            || (!std::is_lvalue_reference_v<Range&&>
                && std::constructible_from<T, std::ranges::range_rvalue_reference_t<Range>>)
    [[nodiscard]] brodal_okasaki_heap insert_range(Range&& values) const
    {
        auto result = *this;
        for (auto iterator = std::ranges::begin(values); iterator != std::ranges::end(values); ++iterator) {
            if constexpr (!std::is_lvalue_reference_v<Range&&>
                && std::constructible_from<T, std::ranges::range_rvalue_reference_t<Range>>) {
                result = result.insert(std::ranges::iter_move(iterator));
            } else {
                result = result.insert(*iterator);
            }
        }
        return result;
    }

    [[nodiscard]] bool empty() const noexcept { return root_ == nullptr; }
    [[nodiscard]] size_type size() const noexcept { return count_; }
    [[nodiscard]] const comparer_type& comparer() const noexcept { return *comparer_; }
    [[nodiscard]] const comparer_handle& comparer_policy() const noexcept { return comparer_; }
    [[nodiscard]] const void* root_identity() const noexcept { return root_.get(); }

    [[nodiscard]] bool is_same_version(const brodal_okasaki_heap& other) const noexcept
    {
        return comparer_ == other.comparer_ && root_ == other.root_ && count_ == other.count_;
    }

    [[nodiscard]] const T& minimum() const
    {
        if (root_ == nullptr) {
            throw std::logic_error("Brodal-Okasaki heap is empty");
        }
        return *root_->value;
    }

    [[nodiscard]] const T* try_minimum() const noexcept
    {
        return root_ == nullptr ? nullptr : root_->value.get();
    }

    [[nodiscard]] std::shared_ptr<const T> minimum_handle() const
    {
        if (root_ == nullptr) {
            throw std::logic_error("Brodal-Okasaki heap is empty");
        }
        return root_->value;
    }

    [[nodiscard]] brodal_okasaki_heap insert(const T& value) const
        requires std::copy_constructible<T>
    {
        return insert_handle(std::make_shared<T>(value));
    }

    [[nodiscard]] brodal_okasaki_heap insert(T&& value) const
        requires std::move_constructible<T>
    {
        return insert_handle(std::make_shared<T>(std::move(value)));
    }

    [[nodiscard]] brodal_okasaki_heap meld(const brodal_okasaki_heap& other) const
    {
        ensure_compatible(other);
        if (root_ == nullptr) {
            return other;
        }
        if (other.root_ == nullptr) {
            return *this;
        }

        const auto total = checked_add(count_, other.count_);
        if (less_or_equal(*root_->value, *other.root_->value)) {
            return brodal_okasaki_heap{
                make_tree(0, root_->value, skew_insert(other.root_, root_->children)),
                total,
                comparer_};
        }
        return brodal_okasaki_heap{
            make_tree(0, other.root_->value, skew_insert(root_, other.root_->children)),
            total,
            comparer_};
    }

    [[nodiscard]] brodal_okasaki_heap delete_minimum() const
    {
        if (root_ == nullptr) {
            throw std::logic_error("Brodal-Okasaki heap is empty");
        }
        if (root_->children == nullptr) {
            return brodal_okasaki_heap{nullptr, 0, comparer_};
        }

        const auto [minimum_tree, remainder] = get_minimum(root_->children);
        auto decomposition = split_forest(minimum_tree->rank, minimum_tree->children);
        auto merged = skew_meld(skew_meld(decomposition.trees, remainder), decomposition.embedded);
        auto zeros = forest_values(decomposition.zeros);
        for (auto iterator = zeros.rbegin(); iterator != zeros.rend(); ++iterator) {
            merged = skew_insert(*iterator, merged);
        }
        return brodal_okasaki_heap{
            make_tree(0, minimum_tree->value, std::move(merged)),
            count_ - 1,
            comparer_};
    }

    /// Returns the exact removed representative handle together with the persistent remainder.
    [[nodiscard]] std::optional<delete_minimum_result> try_delete_minimum() const
    {
        if (root_ == nullptr) {
            return std::nullopt;
        }
        return delete_minimum_result{root_->value, delete_minimum()};
    }

    [[nodiscard]] brodal_okasaki_heap clear() const
    {
        return root_ == nullptr ? *this : brodal_okasaki_heap{nullptr, 0, comparer_};
    }

    [[nodiscard]] std::vector<const void*> node_identities() const
    {
        auto result = std::vector<const void*>{};
        if (root_ == nullptr) {
            return result;
        }
        auto seen = std::unordered_set<const tree*>{};
        auto pending = std::vector<const tree*>{root_.get()};
        while (!pending.empty()) {
            const auto* current = pending.back();
            pending.pop_back();
            if (!seen.insert(current).second) {
                continue;
            }
            result.push_back(current);
            for (auto children = current->children.get(); children != nullptr; children = children->tail.get()) {
                pending.push_back(children->head.get());
            }
        }
        return result;
    }

    [[nodiscard]] size_type shared_node_count_with(const brodal_okasaki_heap& other) const
    {
        const auto identities = node_identities();
        const auto known = std::unordered_set<const void*>{identities.begin(), identities.end()};
        auto shared = size_type{0};
        for (const auto identity : other.node_identities()) {
            shared += known.contains(identity) ? 1 : 0;
        }
        return shared;
    }

    [[nodiscard]] brodal_okasaki_heap_statistics validate_structure() const
    {
        if (root_ == nullptr) {
            if (count_ != 0) {
                throw std::logic_error("empty Brodal-Okasaki heap has a nonzero count");
            }
            return {};
        }
        if (root_->rank != 0) {
            throw std::logic_error("Brodal-Okasaki global root must have rank zero");
        }

        const auto root_forest_length = validate_skew_forest(root_->children);
        struct frame final {
            const tree* current;
            size_type depth;
        };
        auto pending = std::vector<frame>{{root_.get(), 1}};
        auto logical_count = size_type{0};
        auto maximum_rank = size_type{0};
        auto maximum_depth = size_type{0};
        while (!pending.empty()) {
            const auto current_frame = pending.back();
            pending.pop_back();
            const auto& current = *current_frame.current;
            logical_count = checked_add(logical_count, 1);
            maximum_rank = (std::max)(maximum_rank, current.rank);
            maximum_depth = (std::max)(maximum_depth, current_frame.depth);
            (void)validate_skew_forest(validate_fused_children(current));

            for (auto children = current.children.get(); children != nullptr; children = children->tail.get()) {
                if (std::invoke(*comparer_, *children->head->value, *current.value)) {
                    throw std::logic_error("Brodal-Okasaki child outranks its parent");
                }
                pending.push_back(frame{children->head.get(), checked_add(current_frame.depth, 1)});
            }
        }
        if (logical_count != count_) {
            throw std::logic_error("Brodal-Okasaki logical count disagrees with its tree graph");
        }
        return brodal_okasaki_heap_statistics{
            count_, root_forest_length, maximum_rank, maximum_depth};
    }

    [[nodiscard]] const_iterator begin() const { return const_iterator{root_}; }
    [[nodiscard]] const_iterator end() const noexcept { return {}; }
    [[nodiscard]] const_iterator cbegin() const { return begin(); }
    [[nodiscard]] const_iterator cend() const noexcept { return end(); }

private:
    struct tree final : reclaimable {
        tree(size_type tree_rank, std::shared_ptr<const T> tree_value, forest_pointer tree_children)
            : rank(tree_rank)
            , value(std::move(tree_value))
            , children(std::move(tree_children))
        {
        }

        size_type rank;
        std::shared_ptr<const T> value;
        forest_pointer children;
    };

    struct forest final : reclaimable {
        forest(tree_pointer forest_head, forest_pointer forest_tail)
            : head(std::move(forest_head))
            , tail(std::move(forest_tail))
        {
        }

        tree_pointer head;
        forest_pointer tail;
    };

    struct deferred_deleter final {
        template <class Node>
        void operator()(const Node* value) const noexcept
        {
            reclaim(const_cast<Node*>(value));
        }
    };

    struct forest_decomposition final {
        forest_pointer zeros;
        forest_pointer trees;
        forest_pointer embedded;
    };

    brodal_okasaki_heap(tree_pointer root, const size_type count, comparer_handle comparer)
        : root_(std::move(root))
        , count_(count)
        , comparer_(std::move(comparer))
    {
    }

    [[nodiscard]] static const comparer_handle& default_comparer()
    {
        static const comparer_handle value = std::make_shared<comparer_type>();
        return value;
    }

    [[nodiscard]] static size_type checked_add(const size_type left, const size_type right)
    {
        if (right > (std::numeric_limits<size_type>::max)() - left) {
            throw std::length_error("Brodal-Okasaki heap size exceeds size_t");
        }
        return left + right;
    }

    [[nodiscard]] static tree_pointer make_tree(
        const size_type rank,
        std::shared_ptr<const T> value,
        forest_pointer children)
    {
        return tree_pointer{
            new tree{rank, std::move(value), std::move(children)},
            deferred_deleter{}};
    }

    [[nodiscard]] static forest_pointer make_forest(tree_pointer head, forest_pointer tail)
    {
        return forest_pointer{
            new forest{std::move(head), std::move(tail)},
            deferred_deleter{}};
    }

    static void reclaim(reclaimable* value) noexcept
    {
        struct reclamation_state final {
            reclaimable* pending = nullptr;
            bool draining = false;
        };
        static thread_local auto state = reclamation_state{};

        value->deferred_next = state.pending;
        state.pending = value;
        if (state.draining) {
            return;
        }

        state.draining = true;
        while (state.pending != nullptr) {
            auto* current = state.pending;
            state.pending = current->deferred_next;
            delete current;
        }
        state.draining = false;
    }

    [[nodiscard]] bool less_or_equal(const T& left, const T& right) const
    {
        return !std::invoke(*comparer_, right, left);
    }

    [[nodiscard]] brodal_okasaki_heap insert_handle(std::shared_ptr<const T> value) const
    {
        const auto next_count = checked_add(count_, 1);
        if (root_ == nullptr) {
            return brodal_okasaki_heap{make_tree(0, std::move(value), nullptr), 1, comparer_};
        }
        if (less_or_equal(*value, *root_->value)) {
            return brodal_okasaki_heap{
                make_tree(0, std::move(value), make_forest(root_, nullptr)),
                next_count,
                comparer_};
        }
        auto leaf = make_tree(0, std::move(value), nullptr);
        return brodal_okasaki_heap{
            make_tree(0, root_->value, skew_insert(std::move(leaf), root_->children)),
            next_count,
            comparer_};
    }

    [[nodiscard]] forest_pointer skew_insert(tree_pointer value, forest_pointer values) const
    {
        if (values != nullptr && values->tail != nullptr
            && values->head->rank == values->tail->head->rank) {
            return make_forest(
                skew_link(std::move(value), values->head, values->tail->head),
                values->tail->tail);
        }
        return make_forest(std::move(value), std::move(values));
    }

    [[nodiscard]] tree_pointer skew_link(
        tree_pointer zero,
        tree_pointer first,
        tree_pointer second) const
    {
        if (first->rank != second->rank) {
            throw std::logic_error("Brodal-Okasaki skew-link ranks differ");
        }
        if (less_or_equal(*first->value, *zero->value)
            && less_or_equal(*first->value, *second->value)) {
            return make_tree(
                checked_add(first->rank, 1),
                first->value,
                make_forest(zero, make_forest(second, first->children)));
        }
        if (less_or_equal(*second->value, *zero->value)
            && less_or_equal(*second->value, *first->value)) {
            return make_tree(
                checked_add(second->rank, 1),
                second->value,
                make_forest(zero, make_forest(first, second->children)));
        }
        return make_tree(
            checked_add(first->rank, 1),
            zero->value,
            make_forest(first, make_forest(second, zero->children)));
    }

    [[nodiscard]] tree_pointer link(tree_pointer first, tree_pointer second) const
    {
        if (first->rank != second->rank) {
            throw std::logic_error("Brodal-Okasaki link ranks differ");
        }
        if (less_or_equal(*first->value, *second->value)) {
            return make_tree(
                checked_add(first->rank, 1),
                first->value,
                make_forest(second, first->children));
        }
        return make_tree(
            checked_add(second->rank, 1),
            second->value,
            make_forest(first, second->children));
    }

    [[nodiscard]] std::pair<tree_pointer, forest_pointer> get_minimum(
        const forest_pointer& values) const
    {
        auto nodes = std::vector<forest_pointer>{};
        for (auto current = values; current != nullptr; current = current->tail) {
            nodes.push_back(current);
        }
        auto minimum_index = size_type{0};
        for (auto index = size_type{1}; index < nodes.size(); ++index) {
            if (std::invoke(*comparer_, *nodes[index]->head->value, *nodes[minimum_index]->head->value)) {
                minimum_index = index;
            }
        }
        auto remainder = nodes[minimum_index]->tail;
        for (auto index = minimum_index; index != 0; --index) {
            remainder = make_forest(nodes[index - 1]->head, std::move(remainder));
        }
        return {nodes[minimum_index]->head, std::move(remainder)};
    }

    [[nodiscard]] forest_decomposition split_forest(
        size_type rank,
        forest_pointer values) const
    {
        auto zeros = forest_pointer{};
        auto trees = forest_pointer{};
        while (rank != 0) {
            if (values == nullptr) {
                throw std::logic_error("invalid Brodal-Okasaki skew-binomial forest");
            }
            const auto first = values->head;
            if (rank == 1 && values->tail == nullptr) {
                trees = make_forest(first, std::move(trees));
                values = nullptr;
                break;
            }
            if (values->tail == nullptr) {
                throw std::logic_error("incomplete Brodal-Okasaki skew-binomial forest");
            }
            const auto second = values->tail->head;
            const auto rest = values->tail->tail;
            if (rank == 1) {
                if (second->rank == 0) {
                    zeros = make_forest(first, std::move(zeros));
                    trees = make_forest(second, std::move(trees));
                    values = rest;
                } else {
                    trees = make_forest(first, std::move(trees));
                    values = values->tail;
                }
                break;
            }
            if (first->rank == second->rank) {
                trees = make_forest(first, make_forest(second, std::move(trees)));
                values = rest;
                break;
            }
            if (first->rank == 0) {
                zeros = make_forest(first, std::move(zeros));
                trees = make_forest(second, std::move(trees));
                values = rest;
            } else {
                trees = make_forest(first, std::move(trees));
                values = values->tail;
            }
            --rank;
        }
        return forest_decomposition{std::move(zeros), std::move(trees), std::move(values)};
    }

    [[nodiscard]] forest_pointer skew_meld(
        const forest_pointer& left,
        const forest_pointer& right) const
    {
        auto bins = std::vector<tree_pointer>{};
        const auto add_tree = [this, &bins](tree_pointer current) {
            while (true) {
                if (current->rank >= bins.size()) {
                    bins.resize(checked_add(current->rank, 1));
                }
                auto& bin = bins[current->rank];
                if (bin == nullptr) {
                    bin = std::move(current);
                    return;
                }
                current = link(std::move(current), std::move(bin));
            }
        };
        for (auto values = left; values != nullptr; values = values->tail) {
            add_tree(values->head);
        }
        for (auto values = right; values != nullptr; values = values->tail) {
            add_tree(values->head);
        }
        auto result = forest_pointer{};
        for (auto index = bins.size(); index != 0; --index) {
            if (bins[index - 1] != nullptr) {
                result = make_forest(std::move(bins[index - 1]), std::move(result));
            }
        }
        return result;
    }

    [[nodiscard]] static std::vector<tree_pointer> forest_values(const forest_pointer& values)
    {
        auto result = std::vector<tree_pointer>{};
        for (auto current = values; current != nullptr; current = current->tail) {
            result.push_back(current->head);
        }
        return result;
    }

    [[nodiscard]] static forest_pointer validate_fused_children(const tree& value)
    {
        auto rank = value.rank;
        auto values = value.children;
        while (rank != 0) {
            if (values == nullptr) {
                throw std::logic_error("ranked Brodal-Okasaki tree is missing structural children");
            }
            const auto first = values->head;
            if (rank == 1) {
                if (first->rank != 0) {
                    throw std::logic_error("rank-one Brodal-Okasaki tree must begin with a rank-zero child");
                }
                if (values->tail == nullptr) {
                    return nullptr;
                }
                return values->tail->head->rank == 0 ? values->tail->tail : values->tail;
            }
            if (values->tail == nullptr) {
                throw std::logic_error("ranked Brodal-Okasaki tree has incomplete child encoding");
            }
            const auto second = values->tail->head;
            if (first->rank == second->rank) {
                if (first->rank != rank - 1) {
                    throw std::logic_error("skew-linked Brodal-Okasaki tree has invalid child ranks");
                }
                return values->tail->tail;
            }
            if (first->rank == 0) {
                if (second->rank != rank - 1) {
                    throw std::logic_error("skew-linked Brodal-Okasaki tree has invalid ranked child");
                }
                values = values->tail->tail;
            } else {
                if (first->rank != rank - 1) {
                    throw std::logic_error("linked Brodal-Okasaki tree has invalid ranked child");
                }
                values = values->tail;
            }
            --rank;
        }
        return values;
    }

    [[nodiscard]] static size_type validate_skew_forest(forest_pointer values)
    {
        auto length = size_type{0};
        auto previous_rank = std::optional<size_type>{};
        auto duplicate = false;
        for (; values != nullptr; values = values->tail) {
            const auto rank = values->head->rank;
            if (previous_rank.has_value() && rank < *previous_rank) {
                throw std::logic_error("Brodal-Okasaki forest ranks are not nondecreasing");
            }
            if (previous_rank.has_value() && rank == *previous_rank) {
                if (length != 1 || duplicate) {
                    throw std::logic_error("only the first two Brodal-Okasaki forest ranks may be equal");
                }
                duplicate = true;
            }
            previous_rank = rank;
            ++length;
        }
        return length;
    }

    void ensure_compatible(const brodal_okasaki_heap& other) const
    {
        if (comparer_ != other.comparer_) {
            throw std::invalid_argument("Brodal-Okasaki melding requires the same comparer object");
        }
    }

    tree_pointer root_;
    size_type count_ = 0;
    comparer_handle comparer_;
};

} // namespace tools::data_structures::finger_tree
