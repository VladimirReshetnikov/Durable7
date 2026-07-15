#pragma once

#include <tools/data_structures/finger_tree/detail/common.hpp>
#include <tools/data_structures/finger_tree/measures.hpp>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace tools::data_structures::finger_tree {

/// A static ordered-measure policy together with a monoid of lazy update tags.
///
/// `compose(newer, older)` denotes applying `older` first and `newer` second. In
/// addition to the checked callable surface, policies must obey the measure-monoid,
/// tag-monoid, element-action, measure-action, singleton-agreement, and ordered
/// distributivity laws documented by the range-update sequence contract.
template <class Policy, class Element>
concept range_update_algebra =
    measure_policy<Policy, Element>
    && requires(
        const typename Policy::tag_type& tag,
        const typename Policy::tag_type& newer,
        const typename Policy::tag_type& older,
        const Element& element,
        const typename Policy::measure_type& measure,
        const std::size_t count) {
           typename Policy::tag_type;
           { Policy::identity_tag() } -> std::same_as<typename Policy::tag_type>;
           { Policy::is_identity(tag) } -> std::convertible_to<bool>;
           { Policy::compose(newer, older) } -> std::same_as<typename Policy::tag_type>;
           { Policy::apply_element(tag, element) } -> std::same_as<Element>;
           { Policy::apply_measure(tag, measure, count) }
               -> std::same_as<typename Policy::measure_type>;
       };

template <class Element, class Algebra>
concept range_update_sequence_types =
    range_update_algebra<Algebra, Element>
    && std::copyable<Element>
    && std::copyable<typename Algebra::measure_type>
    && std::copyable<typename Algebra::tag_type>;

struct range_update_validation_statistics final {
    std::size_t count = 0;
    std::size_t height = 0;
    std::size_t node_count = 0;
    std::size_t pending_tag_count = 0;
    std::size_t maximum_absolute_balance_factor = 0;

    friend bool operator==(
        const range_update_validation_statistics&,
        const range_update_validation_statistics&) = default;
};

template <class Element, class Algebra>
    requires range_update_sequence_types<Element, Algebra>
class range_update_sequence;

template <class Element, class Algebra>
    requires range_update_sequence_types<Element, Algebra>
struct range_update_split final {
    range_update_sequence<Element, Algebra> left;
    range_update_sequence<Element, Algebra> right;
};

/// An immutable indexed sequence with logarithmic algebraic range updates and queries.
///
/// Nodes form a path-copied implicit-key AVL tree. A node's pending tag has already
/// transformed its own value and cached ordered measure but has not yet transformed
/// its child roots. Structural descent pushes that tag immutably; read-only descent
/// carries inherited tags without publishing replacement nodes. Consequently a
/// nonidentity whole-sequence update replaces exactly one root in O(1), while proper
/// range updates and range-measure queries are O(log n).
template <class Element, class Algebra>
    requires range_update_sequence_types<Element, Algebra>
class range_update_sequence final {
private:
    struct node;
    using node_pointer = std::shared_ptr<const node>;

    struct node final {
        Element value;
        node_pointer left;
        node_pointer right;
        std::size_t height;
        std::size_t count;
        typename Algebra::measure_type measure;
        std::optional<typename Algebra::tag_type> pending;

        node(
            Element value_value,
            node_pointer left_value,
            node_pointer right_value,
            const std::size_t height_value,
            const std::size_t count_value,
            typename Algebra::measure_type measure_value,
            std::optional<typename Algebra::tag_type> pending_value)
            : value(std::move(value_value))
            , left(std::move(left_value))
            , right(std::move(right_value))
            , height(height_value)
            , count(count_value)
            , measure(std::move(measure_value))
            , pending(std::move(pending_value))
        {
        }
    };

public:
    using value_type = Element;
    using algebra_type = Algebra;
    using measure_type = typename algebra_type::measure_type;
    using tag_type = typename algebra_type::tag_type;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using split_result = range_update_split<value_type, algebra_type>;

    class const_iterator final {
    private:
        struct frame final {
            const node* current = nullptr;
            std::optional<tag_type> inherited;
        };

    public:
        using iterator_category = std::input_iterator_tag;
        using iterator_concept = std::input_iterator_tag;
        using value_type = Element;
        using difference_type = std::ptrdiff_t;
        using pointer = void;
        using reference = value_type;

        const_iterator() = default;

        [[nodiscard]] value_type operator*() const
        {
            return *current_;
        }

        const_iterator& operator++()
        {
            const auto current_frame = stack_.back();
            auto extension = std::vector<frame>{};
            if (current_frame.current->right != nullptr) {
                extension.reserve(current_frame.current->right->height);
                build_left_path(
                    current_frame.current->right.get(),
                    child_inheritance(*current_frame.current, current_frame.inherited),
                    extension);
            }

            const auto* next_frame = extension.empty()
                ? (stack_.size() > 1 ? std::addressof(stack_[stack_.size() - 2]) : nullptr)
                : std::addressof(extension.back());
            auto next_value = next_frame == nullptr
                ? std::optional<value_type>{}
                : logical_value(*next_frame);

            // All algebra callbacks complete before the iterator publishes its next position.
            // A throwing compose/identity/element callback therefore leaves this iterator retryable.
            stack_.pop_back();
            for (auto& next : extension) {
                stack_.push_back(std::move(next));
            }
            ++position_;
            current_ = std::move(next_value);
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
            const auto left_ended = left.stack_.empty();
            const auto right_ended = right.stack_.empty();
            if (left_ended || right_ended) {
                return left_ended && right_ended;
            }

            return left.owner_ == right.owner_ && left.position_ == right.position_;
        }

    private:
        friend class range_update_sequence;

        explicit const_iterator(node_pointer root)
            : owner_(std::move(root))
        {
            if (owner_ != nullptr) {
                stack_.reserve(owner_->height);
                push_left(owner_.get(), std::nullopt);
                refresh_current();
            }
        }

        void push_left(const node* current, std::optional<tag_type> inherited)
        {
            build_left_path(current, std::move(inherited), stack_);
        }

        static void build_left_path(
            const node* current,
            std::optional<tag_type> inherited,
            std::vector<frame>& destination)
        {
            while (current != nullptr) {
                destination.push_back(frame{current, inherited});
                inherited = child_inheritance(*current, inherited);
                current = current->left.get();
            }
        }

        [[nodiscard]] static std::optional<value_type> logical_value(const frame& next)
        {
            return next.inherited.has_value()
                ? std::optional<value_type>{
                    algebra_type::apply_element(*next.inherited, next.current->value)}
                : std::optional<value_type>{next.current->value};
        }

        void refresh_current()
        {
            if (stack_.empty()) {
                current_.reset();
                return;
            }

            current_ = logical_value(stack_.back());
        }

        node_pointer owner_;
        std::vector<frame> stack_;
        std::optional<value_type> current_;
        size_type position_ = 0;
    };

    range_update_sequence() noexcept = default;

    range_update_sequence(std::initializer_list<value_type> values)
        : root_(values.size() == 0
                ? node_pointer{}
                : build_balanced(
                    std::span<const value_type>{values.begin(), values.size()},
                    0,
                    values.size()))
    {
    }

    [[nodiscard]] static range_update_sequence empty_sequence() noexcept
    {
        return {};
    }

    [[nodiscard]] static range_update_sequence create(const std::span<const value_type> values)
    {
        return values.empty()
            ? range_update_sequence{}
            : from_root(build_balanced(values, 0, values.size()));
    }

    /// Materializes the source completely before invoking any algebra callback.
    template <std::ranges::input_range Range>
        requires std::constructible_from<value_type, std::ranges::range_reference_t<Range>>
    [[nodiscard]] static range_update_sequence from_range(Range&& values)
    {
        if constexpr (std::same_as<std::remove_cvref_t<Range>, range_update_sequence>) {
            return std::forward<Range>(values);
        } else {
            auto owned = std::vector<value_type>{};
            if constexpr (std::ranges::sized_range<Range>) {
                owned.reserve(static_cast<size_type>(std::ranges::size(values)));
            }
            for (auto&& value : values) {
                owned.emplace_back(std::forward<decltype(value)>(value));
            }
            return create(std::span<const value_type>{owned.data(), owned.size()});
        }
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return root_ == nullptr;
    }

    [[nodiscard]] size_type size() const noexcept
    {
        return count_of(root_);
    }

    [[nodiscard]] measure_type measure() const
    {
        return root_ == nullptr ? empty_measure() : root_->measure;
    }

    /// Returns a logical value by value because inherited lazy tags may transform it.
    [[nodiscard]] value_type at(const size_type index) const
    {
        throw_if_index_out_of_range(index, size());
        return get_element(root_, index);
    }

    [[nodiscard]] value_type operator[](const size_type index) const
    {
        return at(index);
    }

    [[nodiscard]] range_update_sequence prepend(value_type value) const
    {
        return insert_at(0, std::move(value));
    }

    [[nodiscard]] range_update_sequence append(value_type value) const
    {
        return insert_at(size(), std::move(value));
    }

    [[nodiscard]] range_update_sequence push_front(value_type value) const
    {
        return prepend(std::move(value));
    }

    [[nodiscard]] range_update_sequence push_back(value_type value) const
    {
        return append(std::move(value));
    }

    [[nodiscard]] range_update_sequence insert_at(size_type index, value_type value) const
    {
        throw_if_insert_index_out_of_range(index, size());
        (void)checked_add(size(), size_type{1});
        return from_root(insert_node(root_, index, std::move(value)));
    }

    [[nodiscard]] range_update_sequence set_item(size_type index, value_type value) const
    {
        throw_if_index_out_of_range(index, size());
        return from_root(set_node(root_, index, std::move(value)));
    }

    [[nodiscard]] range_update_sequence set_at(size_type index, value_type value) const
    {
        return set_item(index, std::move(value));
    }

    [[nodiscard]] range_update_sequence remove_at(const size_type index) const
    {
        throw_if_index_out_of_range(index, size());
        return from_root(remove_node(root_, index));
    }

    [[nodiscard]] range_update_sequence concat(const range_update_sequence& other) const
    {
        (void)checked_add(size(), other.size());
        if (empty()) {
            return other;
        }
        if (other.empty()) {
            return *this;
        }

        auto removed = extract_minimum(other.root_);
        return from_root(join(root_, std::move(removed.minimum), std::move(removed.remainder)));
    }

    [[nodiscard]] split_result split_at(size_type index) const;

    [[nodiscard]] range_update_sequence get_range(const size_type index, const size_type count) const
    {
        throw_if_range_out_of_bounds(index, count, size());
        if (count == 0) {
            return {};
        }
        if (count == size()) {
            return *this;
        }

        auto [discarded, tail] = split_nodes(root_, index);
        (void)discarded;
        auto [middle, remainder] = split_nodes(std::move(tail), count);
        (void)remainder;
        return from_root(std::move(middle));
    }

    [[nodiscard]] range_update_sequence apply_range(
        const size_type index,
        const size_type count,
        const tag_type& tag) const
    {
        throw_if_range_out_of_bounds(index, count, size());
        if (count == 0) {
            return *this;
        }
        if (algebra_type::is_identity(tag)) {
            return *this;
        }
        if (count == size()) {
            return from_root(apply_subtree(root_, tag));
        }

        auto [left, tail] = split_nodes(root_, index);
        auto [middle, right] = split_nodes(std::move(tail), count);
        middle = apply_subtree(std::move(middle), tag);
        return from_root(concat_nodes(concat_nodes(std::move(left), std::move(middle)), std::move(right)));
    }

    [[nodiscard]] measure_type measure_range(const size_type index, const size_type count) const
    {
        throw_if_range_out_of_bounds(index, count, size());
        if (count == 0) {
            return empty_measure();
        }
        if (count == size()) {
            return root_->measure;
        }

        return measure_range_node(*root_, index, count, std::nullopt);
    }

    [[nodiscard]] std::vector<value_type> to_vector() const
    {
        auto result = std::vector<value_type>{};
        result.reserve(size());
        for (const auto& value : *this) {
            result.push_back(value);
        }
        return result;
    }

    [[nodiscard]] const_iterator begin() const
    {
        return const_iterator{root_};
    }

    [[nodiscard]] const_iterator end() const noexcept
    {
        return {};
    }

    [[nodiscard]] const_iterator cbegin() const
    {
        return begin();
    }

    [[nodiscard]] const_iterator cend() const noexcept
    {
        return end();
    }

    [[nodiscard]] bool shares_root_with(const range_update_sequence& other) const noexcept
    {
        return root_ == other.root_;
    }

    /// Counts distinct physical nodes reachable from this facade, collapsing shared DAG aliases.
    [[nodiscard]] size_type physical_node_count() const
    {
        auto nodes = std::unordered_set<const node*>{};
        collect_nodes(root_.get(), nodes);
        return nodes.size();
    }

    /// Counts distinct physical nodes reachable from both facades.
    [[nodiscard]] size_type shared_node_count_with(const range_update_sequence& other) const
    {
        auto candidates = std::unordered_set<const node*>{};
        collect_nodes(root_.get(), candidates);
        auto visited = std::unordered_set<const node*>{};
        return count_shared_nodes(other.root_.get(), candidates, visited);
    }

    [[nodiscard]] bool shares_structure_with(const range_update_sequence& other) const
    {
        return shared_node_count_with(other) != 0;
    }

    /// Recomputes AVL metadata and logical cached measures independently of the facade.
    /// Policy exceptions propagate; a structural or cache mismatch returns `std::nullopt`.
    [[nodiscard]] std::optional<range_update_validation_statistics> validate_structure() const
        requires equality_comparable_value<measure_type>
    {
        if (root_ == nullptr) {
            return range_update_validation_statistics{};
        }

        auto cache = std::unordered_map<const node*, validation_result>{};
        const auto result = validate_node(root_.get(), cache);
        return result.has_value()
            ? std::optional<range_update_validation_statistics>{result->statistics}
            : std::nullopt;
    }

private:
    struct removed_minimum final {
        value_type minimum;
        node_pointer remainder;
    };

    struct validation_result final {
        measure_type measure;
        range_update_validation_statistics statistics;
    };

    explicit range_update_sequence(node_pointer root) noexcept
        : root_(std::move(root))
    {
    }

    [[nodiscard]] static range_update_sequence from_root(node_pointer root) noexcept
    {
        return range_update_sequence{std::move(root)};
    }

    [[nodiscard]] static const measure_type& empty_measure()
    {
        static const auto value = algebra_type::empty();
        return value;
    }

    [[nodiscard]] static size_type height_of(const node_pointer& value) noexcept
    {
        return value == nullptr ? 0 : value->height;
    }

    [[nodiscard]] static size_type count_of(const node_pointer& value) noexcept
    {
        return value == nullptr ? 0 : value->count;
    }

    static void throw_if_range_out_of_bounds(
        const size_type index,
        const size_type count,
        const size_type available)
    {
        if (index > available || count > available - index) {
            throw std::out_of_range("range extends past the end of the range-update sequence");
        }
    }

    [[nodiscard]] static node_pointer make_node(
        value_type value,
        node_pointer left,
        node_pointer right)
    {
        // Impossible structural growth is rejected before any user-policy callback.
        const auto count = checked_add(checked_add(count_of(left), size_type{1}), count_of(right));
        const auto height = checked_add((std::max)(height_of(left), height_of(right)), size_type{1});

        auto aggregate = algebra_type::measure(value);
        if (left != nullptr) {
            aggregate = algebra_type::combine(left->measure, aggregate);
        }
        if (right != nullptr) {
            aggregate = algebra_type::combine(aggregate, right->measure);
        }

        return std::make_shared<node>(
            std::move(value),
            std::move(left),
            std::move(right),
            height,
            count,
            std::move(aggregate),
            std::nullopt);
    }

    [[nodiscard]] static node_pointer make_raw_node(
        value_type value,
        node_pointer left,
        node_pointer right,
        const size_type height,
        const size_type count,
        measure_type measure,
        std::optional<tag_type> pending)
    {
        return std::make_shared<node>(
            std::move(value),
            std::move(left),
            std::move(right),
            height,
            count,
            std::move(measure),
            std::move(pending));
    }

    [[nodiscard]] static node_pointer build_balanced(
        const std::span<const value_type> values,
        const size_type start,
        const size_type count)
    {
        if (count == 0) {
            return nullptr;
        }

        const auto left_count = count / 2;
        const auto middle = start + left_count;
        auto left = build_balanced(values, start, left_count);
        auto right = build_balanced(values, middle + 1, count - left_count - 1);
        return make_node(values[middle], std::move(left), std::move(right));
    }

    [[nodiscard]] static node_pointer apply_subtree(node_pointer current, const tag_type& newer)
    {
        auto value = algebra_type::apply_element(newer, current->value);
        auto measure = algebra_type::apply_measure(newer, current->measure, current->count);

        auto pending = std::optional<tag_type>{};
        if (!current->pending.has_value()) {
            pending.emplace(newer);
        } else {
            auto composed = algebra_type::compose(newer, *current->pending);
            if (!algebra_type::is_identity(composed)) {
                pending.emplace(std::move(composed));
            }
        }

        return make_raw_node(
            std::move(value),
            current->left,
            current->right,
            current->height,
            current->count,
            std::move(measure),
            std::move(pending));
    }

    [[nodiscard]] static node_pointer push(node_pointer current)
    {
        if (!current->pending.has_value()) {
            return current;
        }

        auto left = current->left == nullptr
            ? node_pointer{}
            : apply_subtree(current->left, *current->pending);
        auto right = current->right == nullptr
            ? node_pointer{}
            : apply_subtree(current->right, *current->pending);
        return make_raw_node(
            current->value,
            std::move(left),
            std::move(right),
            current->height,
            current->count,
            current->measure,
            std::nullopt);
    }

    [[nodiscard]] static std::optional<tag_type> child_inheritance(
        const node& current,
        const std::optional<tag_type>& inherited)
    {
        if (!current.pending.has_value()) {
            return inherited;
        }
        if (!inherited.has_value()) {
            return current.pending;
        }

        auto composed = algebra_type::compose(*inherited, *current.pending);
        return algebra_type::is_identity(composed)
            ? std::optional<tag_type>{}
            : std::optional<tag_type>{std::move(composed)};
    }

    [[nodiscard]] static value_type get_element(node_pointer current, size_type index)
    {
        auto inherited = std::optional<tag_type>{};
        while (true) {
            const auto left_count = count_of(current->left);
            if (index == left_count) {
                return inherited.has_value()
                    ? algebra_type::apply_element(*inherited, current->value)
                    : current->value;
            }

            inherited = child_inheritance(*current, inherited);
            if (index < left_count) {
                current = current->left;
            } else {
                index -= left_count + 1;
                current = current->right;
            }
        }
    }

    [[nodiscard]] static node_pointer insert_node(
        node_pointer original,
        const size_type index,
        value_type value)
    {
        if (original == nullptr) {
            return make_node(std::move(value), nullptr, nullptr);
        }

        auto current = push(std::move(original));
        const auto left_count = count_of(current->left);
        if (index <= left_count) {
            auto left = insert_node(current->left, index, std::move(value));
            return balance(make_node(current->value, std::move(left), current->right));
        }

        auto right = insert_node(
            current->right,
            index - left_count - 1,
            std::move(value));
        return balance(make_node(current->value, current->left, std::move(right)));
    }

    [[nodiscard]] static node_pointer set_node(
        node_pointer original,
        const size_type index,
        value_type value)
    {
        auto current = push(std::move(original));
        const auto left_count = count_of(current->left);
        if (index < left_count) {
            auto left = set_node(current->left, index, std::move(value));
            return make_node(current->value, std::move(left), current->right);
        }
        if (index > left_count) {
            auto right = set_node(
                current->right,
                index - left_count - 1,
                std::move(value));
            return make_node(current->value, current->left, std::move(right));
        }

        return make_node(std::move(value), current->left, current->right);
    }

    [[nodiscard]] static node_pointer remove_node(node_pointer original, const size_type index)
    {
        auto current = push(std::move(original));
        const auto left_count = count_of(current->left);
        if (index < left_count) {
            auto left = remove_node(current->left, index);
            return balance(make_node(current->value, std::move(left), current->right));
        }
        if (index > left_count) {
            auto right = remove_node(current->right, index - left_count - 1);
            return balance(make_node(current->value, current->left, std::move(right)));
        }
        if (current->left == nullptr) {
            return current->right;
        }
        if (current->right == nullptr) {
            return current->left;
        }

        auto removed = extract_minimum(current->right);
        return balance(make_node(
            std::move(removed.minimum),
            current->left,
            std::move(removed.remainder)));
    }

    [[nodiscard]] static removed_minimum extract_minimum(node_pointer original)
    {
        auto current = push(std::move(original));
        if (current->left == nullptr) {
            return removed_minimum{current->value, current->right};
        }

        auto removed = extract_minimum(current->left);
        auto remainder = balance(make_node(
            current->value,
            std::move(removed.remainder),
            current->right));
        return removed_minimum{std::move(removed.minimum), std::move(remainder)};
    }

    [[nodiscard]] static std::pair<node_pointer, node_pointer> split_nodes(
        node_pointer root,
        const size_type index)
    {
        if (root == nullptr) {
            return {nullptr, nullptr};
        }
        if (index == 0) {
            return {nullptr, std::move(root)};
        }
        if (index == root->count) {
            return {std::move(root), nullptr};
        }

        auto current = push(std::move(root));
        const auto left_count = count_of(current->left);
        if (index <= left_count) {
            auto [left, middle] = split_nodes(current->left, index);
            auto right = join(std::move(middle), current->value, current->right);
            return {std::move(left), std::move(right)};
        }

        auto [middle, right] = split_nodes(
            current->right,
            index - left_count - 1);
        auto left = join(current->left, current->value, std::move(middle));
        return {std::move(left), std::move(right)};
    }

    [[nodiscard]] static node_pointer join(
        node_pointer left,
        value_type pivot,
        node_pointer right)
    {
        const auto left_height = height_of(left);
        const auto right_height = height_of(right);
        const auto height_difference = left_height > right_height
            ? left_height - right_height
            : right_height - left_height;
        if (height_difference <= 1) {
            return make_node(std::move(pivot), std::move(left), std::move(right));
        }
        if (left_height > right_height) {
            auto current = push(std::move(left));
            auto boundary = join(current->right, std::move(pivot), std::move(right));
            return balance(make_node(current->value, current->left, std::move(boundary)));
        }

        auto current = push(std::move(right));
        auto leading = join(std::move(left), std::move(pivot), current->left);
        return balance(make_node(current->value, std::move(leading), current->right));
    }

    [[nodiscard]] static node_pointer concat_nodes(node_pointer left, node_pointer right)
    {
        if (left == nullptr) {
            return right;
        }
        if (right == nullptr) {
            return left;
        }

        auto removed = extract_minimum(std::move(right));
        return join(std::move(left), std::move(removed.minimum), std::move(removed.remainder));
    }

    [[nodiscard]] static node_pointer balance(node_pointer original)
    {
        auto current = std::move(original);
        const auto left_height = height_of(current->left);
        const auto right_height = height_of(current->right);
        if (left_height > right_height && left_height - right_height > 1) {
            const auto& left = current->left;
            if (height_of(left->left) < height_of(left->right)) {
                current = push(std::move(current));
                auto rotated = rotate_left(current->left);
                current = make_node(current->value, std::move(rotated), current->right);
            }
            return rotate_right(std::move(current));
        }
        if (right_height > left_height && right_height - left_height > 1) {
            const auto& right = current->right;
            if (height_of(right->right) < height_of(right->left)) {
                current = push(std::move(current));
                auto rotated = rotate_right(current->right);
                current = make_node(current->value, current->left, std::move(rotated));
            }
            return rotate_left(std::move(current));
        }

        return current;
    }

    [[nodiscard]] static node_pointer rotate_left(node_pointer original)
    {
        auto current = push(std::move(original));
        auto pivot = push(current->right);
        auto lower = make_node(current->value, current->left, pivot->left);
        return make_node(pivot->value, std::move(lower), pivot->right);
    }

    [[nodiscard]] static node_pointer rotate_right(node_pointer original)
    {
        auto current = push(std::move(original));
        auto pivot = push(current->left);
        auto lower = make_node(current->value, pivot->right, current->right);
        return make_node(pivot->value, pivot->left, std::move(lower));
    }

    [[nodiscard]] static measure_type measure_range_node(
        const node& current,
        const size_type start,
        const size_type count,
        const std::optional<tag_type>& inherited)
    {
        if (start == 0 && count == current.count) {
            return inherited.has_value()
                ? algebra_type::apply_measure(*inherited, current.measure, current.count)
                : current.measure;
        }

        const auto end = start + count;
        const auto left_count = count_of(current.left);
        auto child_computed = false;
        auto child_tag = std::optional<tag_type>{};
        const auto get_child_tag = [&]() -> const std::optional<tag_type>& {
            if (!child_computed) {
                child_tag = child_inheritance(current, inherited);
                child_computed = true;
            }
            return child_tag;
        };

        auto result = std::optional<measure_type>{};
        const auto add = [&](measure_type part) {
            if (result.has_value()) {
                result = algebra_type::combine(*result, part);
            } else {
                result.emplace(std::move(part));
            }
        };

        if (start < left_count) {
            const auto child_count = (std::min)(count, left_count - start);
            add(measure_range_node(*current.left, start, child_count, get_child_tag()));
        }
        if (start <= left_count && end > left_count) {
            auto singleton = algebra_type::measure(current.value);
            if (inherited.has_value()) {
                singleton = algebra_type::apply_measure(*inherited, singleton, 1);
            }
            add(std::move(singleton));
        }

        const auto right_start = left_count + 1;
        if (end > right_start) {
            const auto local_start = start > right_start ? start - right_start : 0;
            const auto local_end = end - right_start;
            add(measure_range_node(
                *current.right,
                local_start,
                local_end - local_start,
                get_child_tag()));
        }

        return std::move(*result);
    }

    [[nodiscard]] static std::optional<validation_result> validate_node(
        const node* current,
        std::unordered_map<const node*, validation_result>& cache)
        requires equality_comparable_value<measure_type>
    {
        if (const auto found = cache.find(current); found != cache.end()) {
            return found->second;
        }

        auto left = validation_result{empty_measure(), {}};
        if (current->left != nullptr) {
            auto candidate = validate_node(current->left.get(), cache);
            if (!candidate.has_value()) {
                return std::nullopt;
            }
            left = std::move(*candidate);
        }

        auto right = validation_result{empty_measure(), {}};
        if (current->right != nullptr) {
            auto candidate = validate_node(current->right.get(), cache);
            if (!candidate.has_value()) {
                return std::nullopt;
            }
            right = std::move(*candidate);
        }

        const auto expected_count = checked_add(
            checked_add(left.statistics.count, size_type{1}),
            right.statistics.count);
        const auto expected_height = checked_add(
            (std::max)(left.statistics.height, right.statistics.height),
            size_type{1});
        const auto balance_factor = left.statistics.height > right.statistics.height
            ? left.statistics.height - right.statistics.height
            : right.statistics.height - left.statistics.height;
        if (current->count != expected_count
            || current->height != expected_height
            || balance_factor > 1) {
            return std::nullopt;
        }
        if (current->pending.has_value() && algebra_type::is_identity(*current->pending)) {
            return std::nullopt;
        }

        auto left_measure = left.measure;
        if (current->pending.has_value() && left.statistics.count != 0) {
            left_measure = algebra_type::apply_measure(
                *current->pending,
                left.measure,
                left.statistics.count);
        }
        auto right_measure = right.measure;
        if (current->pending.has_value() && right.statistics.count != 0) {
            right_measure = algebra_type::apply_measure(
                *current->pending,
                right.measure,
                right.statistics.count);
        }

        auto expected_measure = algebra_type::measure(current->value);
        if (left.statistics.count != 0) {
            expected_measure = algebra_type::combine(left_measure, expected_measure);
        }
        if (right.statistics.count != 0) {
            expected_measure = algebra_type::combine(expected_measure, right_measure);
        }
        if (!(expected_measure == current->measure)) {
            return std::nullopt;
        }

        auto statistics = range_update_validation_statistics{};
        statistics.count = expected_count;
        statistics.height = expected_height;
        statistics.node_count = expected_count;
        statistics.pending_tag_count = checked_add(
            checked_add(left.statistics.pending_tag_count, current->pending.has_value() ? size_type{1} : 0),
            right.statistics.pending_tag_count);
        statistics.maximum_absolute_balance_factor = (std::max)({
            balance_factor,
            left.statistics.maximum_absolute_balance_factor,
            right.statistics.maximum_absolute_balance_factor});

        auto result = validation_result{current->measure, statistics};
        cache.emplace(current, result);
        return result;
    }

    static void collect_nodes(const node* current, std::unordered_set<const node*>& destination)
    {
        if (current == nullptr || !destination.insert(current).second) {
            return;
        }
        collect_nodes(current->left.get(), destination);
        collect_nodes(current->right.get(), destination);
    }

    [[nodiscard]] static size_type count_shared_nodes(
        const node* current,
        const std::unordered_set<const node*>& candidates,
        std::unordered_set<const node*>& visited)
    {
        if (current == nullptr || !visited.insert(current).second) {
            return 0;
        }

        return (candidates.contains(current) ? size_type{1} : 0)
            + count_shared_nodes(current->left.get(), candidates, visited)
            + count_shared_nodes(current->right.get(), candidates, visited);
    }

    node_pointer root_;
};

template <class Element, class Algebra>
    requires range_update_sequence_types<Element, Algebra>
[[nodiscard]] range_update_split<Element, Algebra>
range_update_sequence<Element, Algebra>::split_at(const size_type index) const
{
    throw_if_split_index_out_of_range(index, size());
    if (index == 0) {
        return {{}, *this};
    }
    if (index == size()) {
        return {*this, {}};
    }

    auto [left, right] = split_nodes(root_, index);
    return {from_root(std::move(left)), from_root(std::move(right))};
}

} // namespace tools::data_structures::finger_tree
