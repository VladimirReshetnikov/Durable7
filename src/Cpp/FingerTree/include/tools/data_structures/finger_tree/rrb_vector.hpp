#pragma once

#include <tools/data_structures/finger_tree/detail/common.hpp>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace tools::data_structures::finger_tree {

struct rrb_vector_statistics final {
    std::size_t count = 0;
    std::size_t height = 0;
    std::size_t leaf_count = 0;
    std::size_t branch_count = 0;
    std::size_t regular_branch_count = 0;
    std::size_t relaxed_branch_count = 0;
    std::size_t minimum_leaf_length = 0;
    std::size_t maximum_leaf_length = 0;
    std::size_t minimum_branching_factor = 0;
    std::size_t maximum_branching_factor = 0;

    friend bool operator==(const rrb_vector_statistics&, const rrb_vector_statistics&) = default;
};

template <std::copy_constructible T>
class rrb_vector;

template <std::copy_constructible T>
class rrb_vector_cursor;

template <std::copy_constructible T>
struct rrb_vector_split final {
    rrb_vector<T> left;
    rrb_vector<T> right;
};

template <std::copy_constructible T>
struct rrb_vector_pop final {
    T value;
    rrb_vector<T> rest;
};

template <std::copy_constructible T>
class rrb_vector_builder;

/// Immutable 32-way relaxed radix-balanced vector.
///
/// Regular branches omit cumulative tables and use five-bit radix indexing.
/// Split and concatenation introduce cumulative tables only where child spans
/// become irregular. Concatenation redistributes the boundary seam only and
/// does not guarantee global minimum occupancy away from it. There is
/// deliberately no persistent tail buffer; use the append builder for bulk staging.
template <std::copy_constructible T>
class rrb_vector final {
private:
    static constexpr std::size_t radix_bits_value = 5;
    static constexpr std::size_t branch_factor_value = 32;
    // The base term is the greatest minimum height in the count domain; boundary-only
    // concatenation may legally retain one additional level of slack.
    static constexpr std::size_t maximum_height_value =
        ((std::numeric_limits<std::size_t>::digits) - 1) / radix_bits_value + 1;

    enum class node_kind {
        leaf,
        branch,
    };

    struct node;
    using node_pointer = std::shared_ptr<const node>;

    struct node {
        node(node_kind kind_value, const std::size_t count_value, const std::size_t height_value) noexcept
            : kind(kind_value)
            , count(count_value)
            , height(height_value)
        {
        }

        virtual ~node() = default;

        node_kind kind;
        std::size_t count;
        std::size_t height;
    };

    struct leaf_node final : node {
        explicit leaf_node(std::vector<T> values)
            : node(node_kind::leaf, values.size(), 0)
            , items(std::move(values))
        {
            if (items.empty() || items.size() > branch_factor_value) {
                throw std::logic_error("RRB leaf length is outside 1..32");
            }
        }

        const std::vector<T> items;
    };

    struct branch_node final : node {
        explicit branch_node(std::vector<node_pointer> values)
            : node(node_kind::branch, count_children(values), child_height(values) + 1)
            , children(std::move(values))
            , cumulative_sizes(has_regular_layout(children, this->height)
                    ? std::nullopt
                    : std::optional<std::vector<std::size_t>>{build_sizes(children)})
        {
            if (children.empty() || children.size() > branch_factor_value) {
                throw std::logic_error("RRB branch factor is outside 1..32");
            }
        }

        [[nodiscard]] static bool has_regular_layout(
            const std::vector<node_pointer>& values,
            const std::size_t height) noexcept
        {
            if (values.empty() || height == 0 || height > maximum_height_value) {
                return false;
            }

            const auto shift = height * radix_bits_value;
            // The legal slack height can exceed size_t's radix capacity. Such a branch is
            // necessarily relaxed; avoid an oversized shift while deciding whether it needs sizes.
            if (shift >= std::numeric_limits<std::size_t>::digits) {
                return false;
            }
            const auto child_capacity = std::size_t{1} << shift;
            for (auto index = std::size_t{0}; index + 1 < values.size(); ++index) {
                if (values[index]->count != child_capacity) {
                    return false;
                }
            }

            return values.back()->count <= child_capacity;
        }

        [[nodiscard]] std::pair<std::size_t, std::size_t> find_child(const std::size_t index) const
        {
            if (index >= this->count) {
                throw std::logic_error("RRB branch lookup exceeds cached count");
            }

            if (!cumulative_sizes.has_value()) {
                const auto shift = this->height * radix_bits_value;
                const auto child_index = index >> shift;
                if (child_index >= children.size()) {
                    throw std::logic_error("RRB radix lookup exceeds child array");
                }
                return {child_index, child_index << shift};
            }

            for (auto child_index = std::size_t{0}; child_index < cumulative_sizes->size(); ++child_index) {
                if (index < (*cumulative_sizes)[child_index]) {
                    return {child_index,
                        child_index == 0 ? 0 : (*cumulative_sizes)[child_index - 1]};
                }
            }

            throw std::logic_error("RRB relaxed lookup did not find a child");
        }

        [[nodiscard]] bool regular() const noexcept
        {
            return !cumulative_sizes.has_value();
        }

        const std::vector<node_pointer> children;
        const std::optional<std::vector<std::size_t>> cumulative_sizes;

    private:
        [[nodiscard]] static std::size_t count_children(const std::vector<node_pointer>& values)
        {
            auto count = std::size_t{0};
            for (const auto& child : values) {
                if (!child) {
                    throw std::logic_error("RRB branch cannot contain a null child");
                }
                count = checked_add(count, child->count);
            }
            return count;
        }

        [[nodiscard]] static std::size_t child_height(const std::vector<node_pointer>& values)
        {
            if (values.empty() || !values.front()) {
                throw std::logic_error("RRB branch cannot be empty or start with a null child");
            }

            const auto height = values.front()->height;
            for (const auto& child : values) {
                if (!child || child->height != height) {
                    throw std::logic_error("RRB branch children must have one height");
                }
            }
            return height;
        }

        [[nodiscard]] static std::vector<std::size_t> build_sizes(
            const std::vector<node_pointer>& values)
        {
            auto result = std::vector<std::size_t>{};
            result.reserve(values.size());
            auto count = std::size_t{0};
            for (const auto& child : values) {
                count = checked_add(count, child->count);
                result.push_back(count);
            }
            return result;
        }
    };

    struct validation_accumulator final {
        std::size_t leaf_count = 0;
        std::size_t branch_count = 0;
        std::size_t regular_branch_count = 0;
        std::size_t relaxed_branch_count = 0;
        std::size_t minimum_leaf_length = (std::numeric_limits<std::size_t>::max)();
        std::size_t maximum_leaf_length = 0;
        std::size_t minimum_branching_factor = (std::numeric_limits<std::size_t>::max)();
        std::size_t maximum_branching_factor = 0;

        void add_leaf(const std::size_t length) noexcept
        {
            ++leaf_count;
            minimum_leaf_length = (std::min)(minimum_leaf_length, length);
            maximum_leaf_length = (std::max)(maximum_leaf_length, length);
        }

        void add_branch(const std::size_t factor, const bool regular) noexcept
        {
            ++branch_count;
            if (regular) {
                ++regular_branch_count;
            } else {
                ++relaxed_branch_count;
            }
            minimum_branching_factor = (std::min)(minimum_branching_factor, factor);
            maximum_branching_factor = (std::max)(maximum_branching_factor, factor);
        }
    };

public:
    using value_type = T;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using reference = const value_type&;
    using const_reference = const value_type&;
    using builder = rrb_vector_builder<T>;

    static constexpr size_type branch_factor = branch_factor_value;

    class const_iterator;

    rrb_vector() = default;

    rrb_vector(std::initializer_list<value_type> items)
        : root_(build_tree(items.begin(), items.end()))
    {
    }

    template <std::input_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
        requires std::constructible_from<value_type, std::iter_reference_t<Iterator>>
    rrb_vector(Iterator first, Sentinel last)
        : root_(build_tree(std::move(first), std::move(last)))
    {
    }

    [[nodiscard]] static rrb_vector empty_vector() noexcept
    {
        return {};
    }

    [[nodiscard]] static rrb_vector create(std::span<const value_type> items)
    {
        return rrb_vector{items.begin(), items.end()};
    }

    template <std::ranges::input_range Range>
        requires std::constructible_from<value_type, std::ranges::range_reference_t<Range>>
    [[nodiscard]] static rrb_vector from_range(Range&& items)
    {
        return rrb_vector{std::ranges::begin(items), std::ranges::end(items)};
    }

    [[nodiscard]] static builder create_builder();

    [[nodiscard]] builder to_builder() const;

    [[nodiscard]] bool empty() const noexcept
    {
        return !root_;
    }

    [[nodiscard]] size_type size() const noexcept
    {
        return root_ ? root_->count : 0;
    }

    [[nodiscard]] size_type height() const noexcept
    {
        return root_ ? root_->height : 0;
    }

    /// Creates an immutable cursor at a logical gap in `0..size()`.
    [[nodiscard]] rrb_vector_cursor<value_type> get_cursor(size_type position = 0) const;

    [[nodiscard]] const_reference at(const size_type index) const
    {
        throw_if_index_out_of_range(index, size());
        return get(root_, index);
    }

    [[nodiscard]] const_reference operator[](const size_type index) const
    {
        return at(index);
    }

    [[nodiscard]] const value_type* try_get(const size_type index) const
    {
        return index < size() ? &get(root_, index) : nullptr;
    }

    [[nodiscard]] const_reference front() const
    {
        throw_if_empty();
        auto current = root_.get();
        while (current->kind == node_kind::branch) {
            current = as_branch(current).children.front().get();
        }
        return as_leaf(current).items.front();
    }

    [[nodiscard]] const_reference back() const
    {
        throw_if_empty();
        auto current = root_.get();
        while (current->kind == node_kind::branch) {
            current = as_branch(current).children.back().get();
        }
        return as_leaf(current).items.back();
    }

    [[nodiscard]] rrb_vector set_item(const size_type index, value_type value) const
    {
        throw_if_index_out_of_range(index, size());
        auto root = set(root_, index, value);
        return root == root_ ? *this : rrb_vector{std::move(root)};
    }

    [[nodiscard]] rrb_vector add_last(value_type value) const
    {
        auto items = std::vector<value_type>{};
        items.reserve(1);
        items.push_back(std::move(value));
        return concat(rrb_vector{make_leaf(std::move(items))});
    }

    [[nodiscard]] rrb_vector add_first(value_type value) const
    {
        auto items = std::vector<value_type>{};
        items.reserve(1);
        items.push_back(std::move(value));
        return rrb_vector{make_leaf(std::move(items))}.concat(*this);
    }

    [[nodiscard]] rrb_vector push_back(value_type value) const
    {
        return add_last(std::move(value));
    }

    [[nodiscard]] rrb_vector push_front(value_type value) const
    {
        return add_first(std::move(value));
    }

    [[nodiscard]] rrb_vector concat(const rrb_vector& other) const
    {
        if (empty()) {
            return other;
        }
        if (other.empty()) {
            return *this;
        }
        (void)checked_add(size(), other.size());

        auto roots = concat_nodes(root_, other.root_);
        return from_root(roots.size() == 1 ? roots.front() : make_branch(std::move(roots)));
    }

    [[nodiscard]] rrb_vector_split<T> split_at(size_type index) const;

    template <std::ranges::input_range Range>
        requires(!std::same_as<std::remove_cvref_t<Range>, rrb_vector>)
            && std::constructible_from<value_type, std::ranges::range_reference_t<Range>>
    [[nodiscard]] rrb_vector insert_range(const size_type index, Range&& items) const
    {
        throw_if_insert_index_out_of_range(index, size());
        return insert_range(index, from_range(std::forward<Range>(items)));
    }

    /// Inserts one element before `index` through a single split, append, and concatenation.
    /// This is the single-element form of insert_range and never materializes an intermediate
    /// one-element vector.
    [[nodiscard]] rrb_vector insert_at(const size_type index, value_type value) const
    {
        throw_if_insert_index_out_of_range(index, size());
        (void)checked_add(size(), size_type{1});

        auto [left_root, right_root] = split_node(root_, index);
        return from_root(std::move(left_root))
            .add_last(std::move(value))
            .concat(from_root(std::move(right_root)));
    }

    [[nodiscard]] rrb_vector insert_range(const size_type index, const rrb_vector& items) const
    {
        throw_if_insert_index_out_of_range(index, size());
        if (items.empty()) {
            return *this;
        }

        auto [left_root, right_root] = split_node(root_, index);
        return from_root(std::move(left_root)).concat(items).concat(from_root(std::move(right_root)));
    }

    [[nodiscard]] rrb_vector remove_range(const size_type index, const size_type count) const
    {
        check_range(index, count);
        if (count == 0) {
            return *this;
        }

        auto [left_root, tail_root] = split_node(root_, index);
        auto [removed_root, right_root] = split_node(tail_root, count);
        (void)removed_root;
        return from_root(std::move(left_root)).concat(from_root(std::move(right_root)));
    }

    [[nodiscard]] rrb_vector_pop<T> pop_last() const;

    [[nodiscard]] std::optional<rrb_vector_pop<T>> try_pop_last() const;

    [[nodiscard]] const_iterator begin() const
    {
        return const_iterator{root_};
    }

    [[nodiscard]] const_iterator end() const noexcept
    {
        return const_iterator{};
    }

    [[nodiscard]] const_iterator cbegin() const { return begin(); }
    [[nodiscard]] const_iterator cend() const noexcept { return end(); }

    [[nodiscard]] std::vector<value_type> to_vector() const
    {
        auto result = std::vector<value_type>{};
        result.reserve(size());
        for (const auto& item : *this) {
            result.push_back(item);
        }
        return result;
    }

    [[nodiscard]] const void* root_identity() const noexcept
    {
        return root_.get();
    }

    [[nodiscard]] bool shares_root_with(const rrb_vector& other) const noexcept
    {
        return root_ == other.root_;
    }

    [[nodiscard]] std::vector<const void*> leaf_identities() const
    {
        auto result = std::vector<const void*>{};
        result.reserve(size() / branch_factor_value + (size() % branch_factor_value != 0 ? 1 : 0));
        collect_leaf_identities(root_, result);
        return result;
    }

    [[nodiscard]] rrb_vector_statistics structure_statistics() const
    {
        if (!root_) {
            return {};
        }

        auto accumulator = validation_accumulator{};
        const auto [count, actual_height] = validate_node(root_, true, accumulator);
        if (count != size() || actual_height != height() || actual_height > maximum_height_value) {
            throw std::logic_error("RRB root count or height invariant failed");
        }

        return rrb_vector_statistics{
            count,
            actual_height,
            accumulator.leaf_count,
            accumulator.branch_count,
            accumulator.regular_branch_count,
            accumulator.relaxed_branch_count,
            accumulator.leaf_count == 0 ? 0 : accumulator.minimum_leaf_length,
            accumulator.maximum_leaf_length,
            accumulator.branch_count == 0 ? 0 : accumulator.minimum_branching_factor,
            accumulator.maximum_branching_factor};
    }

    void validate_invariants() const
    {
        (void)structure_statistics();
    }

    class const_iterator final {
    public:
        using iterator_concept = std::forward_iterator_tag;
        using iterator_category = std::forward_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = const T*;
        using reference = const T&;

        const_iterator() = default;
        const_iterator(const const_iterator&) = default;
        const_iterator& operator=(const const_iterator&) = default;
        const_iterator(const_iterator&&) noexcept = default;
        const_iterator& operator=(const_iterator&&) noexcept = default;

        [[nodiscard]] reference operator*() const
        {
            return leaf_->items[offset_];
        }

        [[nodiscard]] pointer operator->() const
        {
            return &(**this);
        }

        const_iterator& operator++()
        {
            advance();
            return *this;
        }

        const_iterator operator++(int)
        {
            auto copy = *this;
            advance();
            return copy;
        }

        friend bool operator==(const const_iterator& left, const const_iterator& right) noexcept
        {
            if (left.leaf_ == nullptr || right.leaf_ == nullptr) {
                return left.leaf_ == right.leaf_;
            }
            return left.owner_identity_ == right.owner_identity_ && left.position_ == right.position_;
        }

    private:
        friend class rrb_vector;

        struct frame final {
            const branch_node* branch = nullptr;
            size_type next_child = 0;
        };

        explicit const_iterator(node_pointer root)
            : root_owner_(std::move(root))
            , owner_identity_(root_owner_.get())
        {
            if (root_owner_) {
                stack_.reserve(root_owner_->height);
                descend_left(root_owner_.get());
            }
        }

        void descend_left(const node* current)
        {
            while (current->kind == node_kind::branch) {
                const auto& branch = as_branch(current);
                stack_.push_back(frame{&branch, 1});
                current = branch.children.front().get();
            }
            leaf_ = &as_leaf(current);
            offset_ = 0;
        }

        void advance()
        {
            if (leaf_ == nullptr) {
                return;
            }

            ++position_;
            if (++offset_ < leaf_->items.size()) {
                return;
            }

            leaf_ = nullptr;
            while (!stack_.empty()) {
                auto& top = stack_.back();
                if (top.next_child == top.branch->children.size()) {
                    stack_.pop_back();
                    continue;
                }

                const auto* next = top.branch->children[top.next_child++].get();
                descend_left(next);
                return;
            }
        }

        node_pointer root_owner_;
        std::vector<frame> stack_;
        const leaf_node* leaf_ = nullptr;
        const void* owner_identity_ = nullptr;
        size_type offset_ = 0;
        size_type position_ = 0;
    };

private:
    friend class rrb_vector_builder<T>;

    explicit rrb_vector(node_pointer root) noexcept
        : root_(std::move(root))
    {
    }

    [[nodiscard]] static const leaf_node& as_leaf(const node* value)
    {
        return *static_cast<const leaf_node*>(value);
    }

    [[nodiscard]] static const branch_node& as_branch(const node* value)
    {
        return *static_cast<const branch_node*>(value);
    }

    [[nodiscard]] static node_pointer make_leaf(std::vector<value_type> items)
    {
        return std::make_shared<leaf_node>(std::move(items));
    }

    [[nodiscard]] static node_pointer make_branch(std::vector<node_pointer> children)
    {
        return std::make_shared<branch_node>(std::move(children));
    }

    [[nodiscard]] static rrb_vector from_root(node_pointer root)
    {
        while (root && root->kind == node_kind::branch) {
            const auto& branch = as_branch(root.get());
            if (branch.children.size() != 1) {
                break;
            }
            root = branch.children.front();
        }
        return root ? rrb_vector{std::move(root)} : rrb_vector{};
    }

    template <std::input_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
    [[nodiscard]] static node_pointer build_tree(Iterator first, Sentinel last)
    {
        auto leaves = std::vector<node_pointer>{};
        auto tail = std::vector<value_type>{};
        tail.reserve(branch_factor_value);
        for (; first != last; ++first) {
            tail.push_back(*first);
            if (tail.size() != branch_factor_value) {
                continue;
            }
            leaves.push_back(make_leaf(std::move(tail)));
            tail = std::vector<value_type>{};
            tail.reserve(branch_factor_value);
        }
        if (!tail.empty()) {
            leaves.push_back(make_leaf(std::move(tail)));
        }
        return build_level(std::move(leaves));
    }

    [[nodiscard]] static node_pointer build_level(std::vector<node_pointer> nodes)
    {
        if (nodes.empty()) {
            return {};
        }

        while (nodes.size() > 1) {
            auto parents = std::vector<node_pointer>{};
            parents.reserve(nodes.size() / branch_factor_value
                + (nodes.size() % branch_factor_value != 0 ? 1 : 0));
            for (auto start = std::size_t{0}; start < nodes.size(); start += branch_factor_value) {
                const auto length = (std::min)(branch_factor_value, nodes.size() - start);
                auto children = std::vector<node_pointer>{
                    nodes.begin() + static_cast<std::ptrdiff_t>(start),
                    nodes.begin() + static_cast<std::ptrdiff_t>(start + length)};
                parents.push_back(make_branch(std::move(children)));
            }
            nodes = std::move(parents);
        }
        return nodes.front();
    }

    [[nodiscard]] static const_reference get(node_pointer current, size_type index)
    {
        while (current->kind == node_kind::branch) {
            const auto& branch = as_branch(current.get());
            const auto [child_index, before] = branch.find_child(index);
            index -= before;
            current = branch.children[child_index];
        }
        return as_leaf(current.get()).items[index];
    }

    [[nodiscard]] static node_pointer set(
        const node_pointer& current,
        const size_type index,
        const value_type& value)
    {
        if (current->kind == node_kind::leaf) {
            const auto& leaf = as_leaf(current.get());
            if constexpr (equality_comparable_value<value_type>) {
                if (std::addressof(leaf.items[index]) == std::addressof(value) || leaf.items[index] == value) {
                    return current;
                }
            }
            auto items = std::vector<value_type>{};
            items.reserve(leaf.items.size());
            for (auto item_index = std::size_t{0}; item_index < leaf.items.size(); ++item_index) {
                items.push_back(item_index == index ? value : leaf.items[item_index]);
            }
            return make_leaf(std::move(items));
        }

        const auto& branch = as_branch(current.get());
        const auto [child_index, before] = branch.find_child(index);
        auto child = set(branch.children[child_index], index - before, value);
        if (child == branch.children[child_index]) {
            return current;
        }
        auto children = branch.children;
        children[child_index] = std::move(child);
        return make_branch(std::move(children));
    }

    [[nodiscard]] static std::vector<node_pointer> concat_nodes(
        const node_pointer& left,
        const node_pointer& right)
    {
        if (left->height == right->height) {
            return concat_same_height(left, right);
        }
        if (left->height > right->height) {
            const auto& branch = as_branch(left.get());
            auto boundary = concat_nodes(branch.children.back(), right);
            auto children = std::vector<node_pointer>{branch.children.begin(), branch.children.end() - 1};
            children.insert(children.end(), boundary.begin(), boundary.end());
            return partition(std::move(children));
        }

        const auto& branch = as_branch(right.get());
        auto leading = concat_nodes(left, branch.children.front());
        leading.insert(leading.end(), branch.children.begin() + 1, branch.children.end());
        return partition(std::move(leading));
    }

    [[nodiscard]] static std::vector<node_pointer> concat_same_height(
        const node_pointer& left,
        const node_pointer& right)
    {
        if (left->kind == node_kind::leaf) {
            const auto& left_leaf = as_leaf(left.get());
            const auto& right_leaf = as_leaf(right.get());
            if (left_leaf.items.size() == branch_factor_value
                && right_leaf.items.size() == branch_factor_value) {
                return {left, right};
            }

            auto combined = std::vector<value_type>{};
            combined.reserve(checked_add(left_leaf.items.size(), right_leaf.items.size()));
            for (const auto& item : left_leaf.items) {
                combined.push_back(item);
            }
            for (const auto& item : right_leaf.items) {
                combined.push_back(item);
            }
            if (combined.size() <= branch_factor_value) {
                return {make_leaf(std::move(combined))};
            }

            const auto split = combined.size() / 2;
            auto first = std::vector<value_type>{combined.begin(), combined.begin() + static_cast<std::ptrdiff_t>(split)};
            auto second = std::vector<value_type>{combined.begin() + static_cast<std::ptrdiff_t>(split), combined.end()};
            return {make_leaf(std::move(first)), make_leaf(std::move(second))};
        }

        const auto& left_branch = as_branch(left.get());
        const auto& right_branch = as_branch(right.get());
        auto boundary = concat_same_height(left_branch.children.back(), right_branch.children.front());
        auto children = std::vector<node_pointer>{left_branch.children.begin(), left_branch.children.end() - 1};
        children.insert(children.end(), boundary.begin(), boundary.end());
        children.insert(children.end(), right_branch.children.begin() + 1, right_branch.children.end());
        return partition(std::move(children));
    }

    [[nodiscard]] static std::vector<node_pointer> partition(std::vector<node_pointer> children)
    {
        if (children.size() <= branch_factor_value) {
            return {make_branch(std::move(children))};
        }

        const auto split = children.size() / 2;
        auto first = std::vector<node_pointer>{children.begin(), children.begin() + static_cast<std::ptrdiff_t>(split)};
        auto second = std::vector<node_pointer>{children.begin() + static_cast<std::ptrdiff_t>(split), children.end()};
        return {make_branch(std::move(first)), make_branch(std::move(second))};
    }

    [[nodiscard]] static std::pair<node_pointer, node_pointer> split_node(
        const node_pointer& current,
        const size_type index)
    {
        if (!current) {
            return {};
        }
        if (index == 0) {
            return {{}, current};
        }
        if (index == current->count) {
            return {current, {}};
        }

        if (current->kind == node_kind::leaf) {
            const auto& items = as_leaf(current.get()).items;
            auto left = std::vector<value_type>{items.begin(), items.begin() + static_cast<std::ptrdiff_t>(index)};
            auto right = std::vector<value_type>{items.begin() + static_cast<std::ptrdiff_t>(index), items.end()};
            return {make_leaf(std::move(left)), make_leaf(std::move(right))};
        }

        const auto& branch = as_branch(current.get());
        const auto [child_index, before] = branch.find_child(index);
        auto [child_left, child_right] = split_node(branch.children[child_index], index - before);

        auto left_children = std::vector<node_pointer>{
            branch.children.begin(),
            branch.children.begin() + static_cast<std::ptrdiff_t>(child_index)};
        if (child_left) {
            left_children.push_back(std::move(child_left));
        }

        auto right_children = std::vector<node_pointer>{};
        right_children.reserve(branch.children.size() - child_index);
        if (child_right) {
            right_children.push_back(std::move(child_right));
        }
        right_children.insert(
            right_children.end(),
            branch.children.begin() + static_cast<std::ptrdiff_t>(child_index + 1),
            branch.children.end());
        return {build_same_height(std::move(left_children)), build_same_height(std::move(right_children))};
    }

    [[nodiscard]] static node_pointer build_same_height(std::vector<node_pointer> nodes)
    {
        return nodes.empty() ? node_pointer{} : make_branch(std::move(nodes));
    }

    static void collect_leaf_identities(const node_pointer& current, std::vector<const void*>& result)
    {
        if (!current) {
            return;
        }
        if (current->kind == node_kind::leaf) {
            result.push_back(current.get());
            return;
        }
        for (const auto& child : as_branch(current.get()).children) {
            collect_leaf_identities(child, result);
        }
    }

    [[nodiscard]] static std::pair<std::size_t, std::size_t> validate_node(
        const node_pointer& current,
        const bool root,
        validation_accumulator& accumulator)
    {
        if (!current) {
            throw std::logic_error("RRB nonempty traversal encountered null node");
        }
        if (current->kind == node_kind::leaf) {
            const auto& leaf = as_leaf(current.get());
            const auto count = leaf.items.size();
            accumulator.add_leaf(count);
            if (count == 0 || count > branch_factor_value || current->count != count || current->height != 0) {
                throw std::logic_error("RRB leaf invariant failed");
            }
            return {count, 0};
        }

        const auto& branch = as_branch(current.get());
        const auto factor = branch.children.size();
        accumulator.add_branch(factor, branch.regular());
        if (factor == 0 || factor > branch_factor_value || (root && factor == 1)) {
            throw std::logic_error("RRB branch/root density invariant failed");
        }

        auto count = std::size_t{0};
        auto child_height = std::optional<std::size_t>{};
        for (const auto& child : branch.children) {
            const auto [child_count, actual_height] = validate_node(child, false, accumulator);
            if (child_height.has_value() && *child_height != actual_height) {
                throw std::logic_error("RRB branch children have unequal heights");
            }
            child_height = actual_height;
            count = checked_add(count, child_count);
        }

        const auto height = *child_height + 1;
        if (current->count != count || current->height != height) {
            throw std::logic_error("RRB branch cached count or height failed");
        }

        const auto regular = branch_node::has_regular_layout(branch.children, height);
        if (branch.regular() != regular) {
            throw std::logic_error("RRB regular/relaxed classification failed");
        }
        if (branch.cumulative_sizes.has_value()) {
            if (regular || branch.cumulative_sizes->size() != factor) {
                throw std::logic_error("RRB relaxed size-table shape failed");
            }
            auto cumulative = std::size_t{0};
            for (auto index = std::size_t{0}; index < factor; ++index) {
                cumulative = checked_add(cumulative, branch.children[index]->count);
                if ((*branch.cumulative_sizes)[index] != cumulative) {
                    throw std::logic_error("RRB relaxed cumulative size failed");
                }
            }
        }
        return {count, height};
    }

    void throw_if_empty() const
    {
        if (empty()) {
            throw std::logic_error("rrb_vector is empty");
        }
    }

    void check_range(const size_type index, const size_type count) const
    {
        if (index > size() || count > size() - index) {
            throw std::out_of_range("range extends past the end of rrb_vector");
        }
    }

    node_pointer root_;
};

/// An immutable snapshot-plus-position gap cursor over a relaxed radix-balanced vector.
template <std::copy_constructible T>
class rrb_vector_cursor final {
public:
    using value_type = T;
    using size_type = std::size_t;

    rrb_vector_cursor() = delete;
    rrb_vector_cursor(const rrb_vector_cursor&) = default;
    rrb_vector_cursor& operator=(const rrb_vector_cursor&) = default;

    rrb_vector_cursor(rrb_vector_cursor&& other) noexcept(
        std::is_nothrow_copy_constructible_v<rrb_vector<value_type>>)
        : snapshot_(other.snapshot_)
        , position_(other.position_)
    {
    }

    rrb_vector_cursor& operator=(rrb_vector_cursor&& other) noexcept(
        std::is_nothrow_copy_assignable_v<rrb_vector<value_type>>)
    {
        if (this != &other) {
            snapshot_ = other.snapshot_;
            position_ = other.position_;
        }
        return *this;
    }

    [[nodiscard]] size_type size() const noexcept { return snapshot_.size(); }
    [[nodiscard]] bool empty() const noexcept { return snapshot_.empty(); }
    [[nodiscard]] size_type position() const noexcept { return position_; }
    [[nodiscard]] bool is_at_start() const noexcept { return position_ == 0; }
    [[nodiscard]] bool is_at_end() const noexcept { return position_ == snapshot_.size(); }

    [[nodiscard]] const value_type* try_peek_previous() const &
    {
        return position_ == 0 ? nullptr : snapshot_.try_get(position_ - 1);
    }

    const value_type* try_peek_previous() const && = delete;

    [[nodiscard]] const value_type* try_peek_next() const &
    {
        return snapshot_.try_get(position_);
    }

    const value_type* try_peek_next() const && = delete;

    [[nodiscard]] rrb_vector_cursor move_previous() const
    {
        if (is_at_start()) {
            throw std::logic_error("RRB cursor is already at the start");
        }
        return rrb_vector_cursor{snapshot_, position_ - 1};
    }

    [[nodiscard]] rrb_vector_cursor move_next() const
    {
        if (is_at_end()) {
            throw std::logic_error("RRB cursor is already at the end");
        }
        return rrb_vector_cursor{snapshot_, position_ + 1};
    }

    [[nodiscard]] rrb_vector_cursor seek(const size_type position) const
    {
        if (position > snapshot_.size()) {
            throw std::out_of_range("cursor position is outside the RRB vector bounds");
        }
        return position == position_ ? *this : rrb_vector_cursor{snapshot_, position};
    }

    /// Inserts one element at the gap. The element goes straight through the vector's own
    /// single-element insertion, without materializing an intermediate range or vector.
    [[nodiscard]] rrb_vector_cursor insert(value_type value) const
    {
        return rrb_vector_cursor{
            snapshot_.insert_at(position_, std::move(value)),
            checked_add(position_, size_type{1})};
    }

    template <std::ranges::input_range Range>
        requires(!std::same_as<std::remove_cvref_t<Range>, rrb_vector<value_type>>)
            && std::constructible_from<value_type, std::ranges::range_reference_t<Range>>
    [[nodiscard]] rrb_vector_cursor insert_range(Range&& values) const
    {
        auto middle = rrb_vector<value_type>::from_range(std::forward<Range>(values));
        return middle.empty() ? *this : insert_vector(middle);
    }

    /// Splices an existing vector through its persistent split/concat operations.
    [[nodiscard]] rrb_vector_cursor insert_vector(const rrb_vector<value_type>& values) const
    {
        if (values.empty()) {
            return *this;
        }
        return rrb_vector_cursor{
            snapshot_.insert_range(position_, values),
            checked_add(position_, values.size())};
    }

    [[nodiscard]] rrb_vector_cursor delete_previous() const
    {
        if (is_at_start()) {
            throw std::logic_error("RRB cursor has no previous element");
        }
        return rrb_vector_cursor{snapshot_.remove_range(position_ - 1, 1), position_ - 1};
    }

    [[nodiscard]] rrb_vector_cursor delete_next() const
    {
        if (is_at_end()) {
            throw std::logic_error("RRB cursor has no next element");
        }
        return rrb_vector_cursor{snapshot_.remove_range(position_, 1), position_};
    }

    [[nodiscard]] rrb_vector_cursor replace_next(value_type value) const
    {
        if (is_at_end()) {
            throw std::logic_error("RRB cursor has no next element");
        }
        return rrb_vector_cursor{snapshot_.set_item(position_, std::move(value)), position_};
    }

    [[nodiscard]] rrb_vector<value_type> snapshot() const { return snapshot_; }

private:
    friend class rrb_vector<T>;

    rrb_vector_cursor(rrb_vector<value_type> snapshot, const size_type position)
        : snapshot_(std::move(snapshot))
        , position_(position)
    {
    }

    rrb_vector<value_type> snapshot_;
    size_type position_;
};

template <std::copy_constructible T>
[[nodiscard]] rrb_vector_cursor<T>
rrb_vector<T>::get_cursor(const size_type position) const
{
    if (position > size()) {
        throw std::out_of_range("cursor position is outside the RRB vector bounds");
    }
    return rrb_vector_cursor<value_type>{*this, position};
}

template <std::copy_constructible T>
class rrb_vector_builder final {
public:
    using value_type = T;
    using size_type = std::size_t;

    rrb_vector_builder()
        : rrb_vector_builder(rrb_vector<T>{})
    {
    }

    explicit rrb_vector_builder(rrb_vector<T> prefix)
        : prefix_(std::move(prefix))
    {
        tail_.reserve(rrb_vector<T>::branch_factor);
    }

    [[nodiscard]] size_type size() const noexcept
    {
        return prefix_.size() + staged_count_;
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return size() == 0;
    }

    void add(value_type value)
    {
        ensure_can_add(1);
        if (tail_.size() + 1 < rrb_vector<T>::branch_factor) {
            tail_.push_back(std::move(value));
            ++staged_count_;
            return;
        }

        auto candidate = tail_;
        candidate.push_back(std::move(value));
        auto leaf = rrb_vector<T>::make_leaf(std::move(candidate));
        leaves_.push_back(std::move(leaf));
        tail_.clear();
        ++staged_count_;
    }

    template <std::ranges::input_range Range>
        requires std::constructible_from<value_type, std::ranges::range_reference_t<Range>>
    void add_range(Range&& items)
    {
        auto candidate = *this;
        for (auto&& item : items) {
            candidate.add(value_type{item});
        }
        swap(candidate);
    }

    void clear() noexcept
    {
        prefix_ = rrb_vector<T>{};
        leaves_.clear();
        tail_.clear();
        staged_count_ = 0;
    }

    [[nodiscard]] rrb_vector<T> to_immutable()
    {
        if (staged_count_ == 0) {
            return prefix_;
        }

        auto nodes = leaves_;
        if (!tail_.empty()) {
            nodes.push_back(rrb_vector<T>::make_leaf(tail_));
        }
        auto staged = rrb_vector<T>{rrb_vector<T>::build_level(std::move(nodes))};
        auto result = prefix_.empty() ? staged : prefix_.concat(staged);

        prefix_ = result;
        leaves_.clear();
        tail_.clear();
        staged_count_ = 0;
        return result;
    }

    void swap(rrb_vector_builder& other) noexcept
    {
        using std::swap;
        swap(prefix_, other.prefix_);
        swap(leaves_, other.leaves_);
        swap(tail_, other.tail_);
        swap(staged_count_, other.staged_count_);
    }

private:
    void ensure_can_add(const size_type count) const
    {
        if (count > (std::numeric_limits<size_type>::max)() - size()) {
            throw std::overflow_error("rrb_vector builder size overflow");
        }
    }

    rrb_vector<T> prefix_;
    std::vector<typename rrb_vector<T>::node_pointer> leaves_;
    std::vector<T> tail_;
    size_type staged_count_ = 0;
};

template <std::copy_constructible T>
[[nodiscard]] typename rrb_vector<T>::builder rrb_vector<T>::create_builder()
{
    return builder{};
}

template <std::copy_constructible T>
[[nodiscard]] typename rrb_vector<T>::builder rrb_vector<T>::to_builder() const
{
    return builder{*this};
}

template <std::copy_constructible T>
[[nodiscard]] rrb_vector_split<T> rrb_vector<T>::split_at(const size_type index) const
{
    throw_if_split_index_out_of_range(index, size());
    if (index == 0) {
        return {rrb_vector{}, *this};
    }
    if (index == size()) {
        return {*this, rrb_vector{}};
    }
    auto [left, right] = split_node(root_, index);
    return {from_root(std::move(left)), from_root(std::move(right))};
}

template <std::copy_constructible T>
[[nodiscard]] rrb_vector_pop<T> rrb_vector<T>::pop_last() const
{
    throw_if_empty();
    return rrb_vector_pop<T>{back(), remove_range(size() - 1, 1)};
}

template <std::copy_constructible T>
[[nodiscard]] std::optional<rrb_vector_pop<T>> rrb_vector<T>::try_pop_last() const
{
    return empty() ? std::nullopt : std::optional<rrb_vector_pop<T>>{pop_last()};
}

template <std::copy_constructible T>
    requires equality_comparable_value<T>
[[nodiscard]] bool operator==(const rrb_vector_split<T>& left, const rrb_vector_split<T>& right)
{
    return detail::sequence_equal(left.left, right.left)
        && detail::sequence_equal(left.right, right.right);
}

template <std::copy_constructible T>
    requires equality_comparable_value<T>
[[nodiscard]] bool operator==(const rrb_vector_pop<T>& left, const rrb_vector_pop<T>& right)
{
    return left.value == right.value && detail::sequence_equal(left.rest, right.rest);
}

} // namespace tools::data_structures::finger_tree
