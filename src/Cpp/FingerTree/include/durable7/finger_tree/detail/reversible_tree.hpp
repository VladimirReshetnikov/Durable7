/// The reversible deque's underlying tree, carrying the orientation flag reversal flips.
///
/// Every read consults the flag rather than the stored order, so reversing shares the whole tree
/// instead of rebuilding it.

#pragma once

#include <durable7/finger_tree/detail/common.hpp>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

namespace durable7::finger_tree::detail {

template <class T>
class rev_node;

template <class T>
class rev_tree_cursor;

template <class T>
class rev_element;

template <class T>
class rev_tree;

template <class T>
struct rev_tree_rep;

/// A reversible tree's prefix or suffix digit.
template <class T>
using rev_digit = std::vector<rev_element<T>>;

/// Which of the reversible tree's three cases a node is.
enum class rev_tree_kind {
    empty,
    single,
    deep
};

/// An endpoint element together with the tree remaining.
template <class T>
struct rev_view_result final {
    rev_element<T> value;
    rev_tree<T> rest;
};

/// The two reversible trees a split produced.
template <class T>
struct rev_split_result final {
    rev_tree<T> left;
    rev_element<T> hit;
    std::size_t index_in_hit;
    rev_tree<T> right;
};

/// Sums a range of subtree sizes.
template <class T>
[[nodiscard]] std::size_t rev_sum_sizes(const rev_digit<T>& elements);

/// Mirrors a subtree so a reversed handle reads it in the right order.
template <class T>
[[nodiscard]] rev_digit<T> rev_mirror_reversed(const rev_digit<T>& elements);

/// Builds a deep reversible node from its prefix, middle, and suffix.
template <class T>
[[nodiscard]] rev_tree<T> rev_make_deep(rev_digit<T> prefix, rev_tree<T> middle, rev_digit<T> suffix);

/// Builds a reversible tree from a digit.
template <class T>
[[nodiscard]] rev_tree<T> rev_from_digit(const rev_digit<T>& elements);

/// Rebuilds a deep reversible node whose prefix may have been emptied.
template <class T>
[[nodiscard]] rev_tree<T> rev_deep_left(rev_digit<T> prefix, rev_tree<T> middle, rev_digit<T> suffix);

/// Rebuilds a deep reversible node whose suffix may have been emptied.
template <class T>
[[nodiscard]] rev_tree<T> rev_deep_right(rev_digit<T> prefix, rev_tree<T> middle, rev_digit<T> suffix);

/// Groups elements into nodes, as concatenation's seam rebuild requires.
template <class T>
[[nodiscard]] rev_digit<T> rev_nodes(const rev_digit<T>& elements);

/// Concatenates two reversible trees.
template <class T>
[[nodiscard]] rev_tree<T> rev_concat(rev_tree<T> left, rev_tree<T> right);

/// Concatenates two reversible trees with elements carried between them.
template <class T>
[[nodiscard]] rev_tree<T> rev_concat_with_middle(
    rev_tree<T> left,
    const rev_digit<T>& middle,
    rev_tree<T> right);

/// One element of a reversible tree.
template <class T>
class rev_element final {
public:
    using node_pointer = std::shared_ptr<const rev_node<T>>;

    /// The leaf element this value holds.
    [[nodiscard]] static rev_element leaf(T value)
    {
        return rev_element{leaf_storage{std::move(value)}};
    }

    /// The underlying node.
    [[nodiscard]] static rev_element node(node_pointer value)
    {
        if (value == nullptr) {
            throw std::invalid_argument("reversible node element cannot hold a null node");
        }

        return rev_element{std::move(value)};
    }

    /// A two-child node.
    [[nodiscard]] static rev_element node2(rev_element first, rev_element second);

    /// A three-child node.
    [[nodiscard]] static rev_element node3(rev_element first, rev_element second, rev_element third);

    /// Whether this node is a leaf.
    [[nodiscard]] bool is_leaf() const noexcept
    {
        return std::holds_alternative<leaf_storage>(storage_);
    }

    /// Whether this value holds a node rather than an element.
    [[nodiscard]] bool is_node() const noexcept
    {
        return std::holds_alternative<node_pointer>(storage_);
    }

    /// Number of elements in the collection.
    [[nodiscard]] std::size_t size() const;

    /// The first leaf element.
    [[nodiscard]] const T& first_leaf() const;

    /// The last leaf element.
    [[nodiscard]] const T& last_leaf() const;

    /// The leaf element this value holds.
    [[nodiscard]] const T& leaf_value() const
    {
        return std::get<leaf_storage>(storage_).value;
    }

    /// The node this value holds.
    [[nodiscard]] const rev_node<T>& node_value() const
    {
        return *std::get<node_pointer>(storage_);
    }

    /// The collection in the opposite order.
    [[nodiscard]] rev_element mirror() const;

    /// The leaf element at the given index.
    [[nodiscard]] T get_leaf(std::size_t index) const;

    /// A copy with the leaf element at the given index replaced.
    [[nodiscard]] rev_element set_leaf(std::size_t index, T value) const;

    /// Copies the leaf elements out.
    void copy_leaves(std::vector<T>& sink) const;

    /// Checks the structural invariants and returns the element count it derived while doing so.
    [[nodiscard]] std::size_t validate_and_count() const;

private:
    friend class rev_tree_cursor<T>;

    struct leaf_storage final {
        T value;
    };

    using storage_type = std::variant<leaf_storage, node_pointer>;

    explicit rev_element(storage_type storage)
        : storage_(std::move(storage))
    {
    }

    storage_type storage_;
};

/// One interior node of a reversible tree.
template <class T>
class rev_node final {
public:
    using element_type = rev_element<T>;
    using digit_type = rev_digit<T>;

    /// Builds a node from the given children.
    [[nodiscard]] static std::shared_ptr<const rev_node> make(digit_type children)
    {
        return std::make_shared<const rev_node>(std::move(children));
    }

    /// Constructs the rev node from the given parts.
    explicit rev_node(digit_type children)
        : children_(std::move(children))
        , size_(validated_size(children_))
        , first_forward_(children_.front().first_leaf())
        , last_forward_(children_.back().last_leaf())
    {
    }

    /// How many children this node has.
    [[nodiscard]] std::size_t child_count() const noexcept
    {
        return children_.size();
    }

    /// Number of elements in the node.
    [[nodiscard]] std::size_t size() const noexcept
    {
        return size_;
    }

    /// The first leaf element.
    [[nodiscard]] const T& first_leaf() const noexcept
    {
        return reversed_ ? last_forward_ : first_forward_;
    }

    /// The last leaf element.
    [[nodiscard]] const T& last_leaf() const noexcept
    {
        return reversed_ ? first_forward_ : last_forward_;
    }

    /// The node in the opposite order.
    [[nodiscard]] rev_node mirror() const
    {
        return rev_node{children_, size_, first_forward_, last_forward_, !reversed_};
    }

    /// The child at the given index in the orientation the handle presents.
    [[nodiscard]] element_type logical_child(const std::size_t index) const
    {
        return reversed_ ? children_[children_.size() - 1 - index].mirror() : children_[index];
    }

    /// The children in the orientation the handle presents.
    [[nodiscard]] digit_type logical_children() const
    {
        auto result = digit_type{};
        result.reserve(children_.size());
        for (std::size_t index = 0; index < children_.size(); ++index) {
            result.push_back(logical_child(index));
        }

        return result;
    }

    /// The leaf element at the given index.
    [[nodiscard]] T get_leaf(std::size_t index) const
    {
        for (std::size_t child_index = 0; child_index < child_count(); ++child_index) {
            auto child = logical_child(child_index);
            if (index < child.size()) {
                return child.get_leaf(index);
            }

            index -= child.size();
        }

        throw std::logic_error("reversible node leaf index is out of range");
    }

    /// A copy with the leaf element at the given index replaced.
    [[nodiscard]] element_type set_leaf(std::size_t index, T value) const
    {
        auto children = logical_children();
        for (std::size_t child_index = 0; child_index < children.size(); ++child_index) {
            if (index < children[child_index].size()) {
                children[child_index] = children[child_index].set_leaf(index, std::move(value));
                return element_type::node(make(std::move(children)));
            }

            index -= children[child_index].size();
        }

        throw std::logic_error("reversible node set index is out of range");
    }

    /// Copies the leaf elements out.
    void copy_leaves(std::vector<T>& sink) const
    {
        for (std::size_t index = 0; index < children_.size(); ++index) {
            logical_child(index).copy_leaves(sink);
        }
    }

    /// Checks the structural invariants and returns the element count it derived while doing so.
    [[nodiscard]] std::size_t validate_and_count() const
    {
        if (children_.size() < 2 || children_.size() > 3) {
            throw std::logic_error("reversible node arity is outside two or three");
        }

        auto computed = std::size_t{0};
        for (const auto& child : children_) {
            computed = checked_add(computed, child.validate_and_count());
        }

        if (computed != size_) {
            throw std::logic_error("reversible node cached size mismatch");
        }

        return computed;
    }

private:
    friend class rev_tree_cursor<T>;

    [[nodiscard]] static std::size_t validated_size(const digit_type& children)
    {
        if (children.size() < 2 || children.size() > 3) {
            throw std::logic_error("reversible node arity must be two or three");
        }

        return rev_sum_sizes(children);
    }

    rev_node(digit_type children, std::size_t size, T first_forward, T last_forward, bool reversed)
        : children_(std::move(children))
        , size_(size)
        , first_forward_(std::move(first_forward))
        , last_forward_(std::move(last_forward))
        , reversed_(reversed)
    {
    }

    digit_type children_;
    std::size_t size_;
    T first_forward_;
    T last_forward_;
    bool reversed_ = false;
};

template <class T>
rev_element<T> rev_element<T>::node2(rev_element first, rev_element second)
{
    auto children = rev_digit<T>{};
    children.reserve(2);
    children.push_back(std::move(first));
    children.push_back(std::move(second));
    return node(rev_node<T>::make(std::move(children)));
}

template <class T>
rev_element<T> rev_element<T>::node3(rev_element first, rev_element second, rev_element third)
{
    auto children = rev_digit<T>{};
    children.reserve(3);
    children.push_back(std::move(first));
    children.push_back(std::move(second));
    children.push_back(std::move(third));
    return node(rev_node<T>::make(std::move(children)));
}

template <class T>
std::size_t rev_element<T>::size() const
{
    return is_leaf() ? 1 : node_value().size();
}

template <class T>
const T& rev_element<T>::first_leaf() const
{
    return is_leaf() ? leaf_value() : node_value().first_leaf();
}

template <class T>
const T& rev_element<T>::last_leaf() const
{
    return is_leaf() ? leaf_value() : node_value().last_leaf();
}

template <class T>
rev_element<T> rev_element<T>::mirror() const
{
    if (is_leaf()) {
        return *this;
    }

    return node(std::make_shared<const rev_node<T>>(node_value().mirror()));
}

template <class T>
T rev_element<T>::get_leaf(const std::size_t index) const
{
    return is_leaf() ? leaf_value() : node_value().get_leaf(index);
}

template <class T>
rev_element<T> rev_element<T>::set_leaf(const std::size_t index, T value) const
{
    return is_leaf() ? leaf(std::move(value)) : node_value().set_leaf(index, std::move(value));
}

template <class T>
void rev_element<T>::copy_leaves(std::vector<T>& sink) const
{
    if (is_leaf()) {
        sink.push_back(leaf_value());
        return;
    }

    node_value().copy_leaves(sink);
}

template <class T>
std::size_t rev_element<T>::validate_and_count() const
{
    return is_leaf() ? 1 : node_value().validate_and_count();
}

/// The reversible deque's underlying tree, carrying the orientation flag that makes reversal
/// constant time.
template <class T>
class rev_tree final {
public:
    using element_type = rev_element<T>;
    using digit_type = rev_digit<T>;

    /// Takes a second handle on the same tree version; the nodes are shared, not copied.
    rev_tree();

    /// Whether the tree holds no elements.
    [[nodiscard]] static rev_tree empty();

    /// A tree holding exactly one element.
    [[nodiscard]] static rev_tree single(element_type element);

    /// A tree with a prefix, a middle, and a suffix.
    [[nodiscard]] static rev_tree deep(digit_type prefix, rev_tree middle, digit_type suffix);

    /// Takes a second handle on the same tree version; the nodes are shared, not copied.
    explicit rev_tree(std::shared_ptr<const rev_tree_rep<T>> rep)
        : rep_(std::move(rep))
    {
    }

    /// The shared empty tree. One instance is reused, since an empty tree has nothing to
    /// distinguish.
    [[nodiscard]] bool empty_state() const noexcept;

    /// Number of elements in the tree.
    [[nodiscard]] std::size_t size() const noexcept;

    /// The first leaf element.
    [[nodiscard]] const T& first_leaf() const;

    /// The last leaf element.
    [[nodiscard]] const T& last_leaf() const;

    /// The tree in the opposite order.
    [[nodiscard]] rev_tree mirror() const;

    /// A tree with the element added at the front.
    [[nodiscard]] rev_tree cons(element_type value) const;

    /// A tree with the element added at the back.
    [[nodiscard]] rev_tree snoc(element_type value) const;

    /// Views the leftmost element together with the rest, or nothing when empty.
    [[nodiscard]] std::optional<rev_view_result<T>> try_view_left() const;

    /// Views the rightmost element together with the rest, or nothing when empty.
    [[nodiscard]] std::optional<rev_view_result<T>> try_view_right() const;

    /// The leaf element at the given index.
    [[nodiscard]] T get_leaf(std::size_t index) const;

    /// A copy with the leaf element at the given index replaced.
    [[nodiscard]] rev_tree set_leaf(std::size_t index, T value) const;

    /// Splits at the first point where the accumulated measure satisfies the predicate.
    [[nodiscard]] rev_split_result<T> split_tree(std::size_t index) const;

    /// Copies the leaf elements out.
    void copy_leaves(std::vector<T>& sink) const;

    /// Checks the structural invariants and returns the element count it derived while doing so.
    [[nodiscard]] std::size_t validate_and_count() const;

    /// The structure's height.
    [[nodiscard]] std::size_t depth() const noexcept;

    /// The address this value occupies, for sharing assertions.
    [[nodiscard]] const void* identity() const noexcept
    {
        return rep_.get();
    }

private:
    template <class U>
    friend rev_tree<U> rev_concat_with_middle(rev_tree<U> left, const rev_digit<U>& middle, rev_tree<U> right);
    friend class rev_tree_cursor<T>;

    std::shared_ptr<const rev_tree_rep<T>> rep_;
};

/// The base of a reversible tree's three cases.
template <class T>
struct rev_tree_rep {
    using tree_type = rev_tree<T>;
    using element_type = rev_element<T>;
    using digit_type = rev_digit<T>;

    /// An empty tree.
    virtual ~rev_tree_rep() = default;

    /// Which case this value is.
    [[nodiscard]] virtual rev_tree_kind kind() const noexcept = 0;
    /// The shared empty tree. One instance is reused, since an empty tree has nothing to
    /// distinguish.
    [[nodiscard]] virtual bool empty_state() const noexcept = 0;
    /// Number of elements in the tree.
    [[nodiscard]] virtual std::size_t size() const noexcept = 0;
    /// The first leaf element.
    [[nodiscard]] virtual const T& first_leaf() const = 0;
    /// The last leaf element.
    [[nodiscard]] virtual const T& last_leaf() const = 0;
    /// The tree in the opposite order.
    [[nodiscard]] virtual tree_type mirror() const = 0;
    /// A tree with the element added at the front.
    [[nodiscard]] virtual tree_type cons(element_type value) const = 0;
    /// A tree with the element added at the back.
    [[nodiscard]] virtual tree_type snoc(element_type value) const = 0;
    /// Views the leftmost element together with the rest, or nothing when empty.
    [[nodiscard]] virtual std::optional<rev_view_result<T>> try_view_left() const = 0;
    /// Views the rightmost element together with the rest, or nothing when empty.
    [[nodiscard]] virtual std::optional<rev_view_result<T>> try_view_right() const = 0;
    /// The leaf element at the given index.
    [[nodiscard]] virtual T get_leaf(std::size_t index) const = 0;
    /// A copy with the leaf element at the given index replaced.
    [[nodiscard]] virtual tree_type set_leaf(std::size_t index, T value) const = 0;
    /// Splits at the first point where the accumulated measure satisfies the predicate.
    [[nodiscard]] virtual rev_split_result<T> split_tree(std::size_t index) const = 0;
    /// Copies the leaf elements out.
    virtual void copy_leaves(std::vector<T>& sink) const = 0;
    /// Checks the structural invariants and returns the element count it derived while doing so.
    [[nodiscard]] virtual std::size_t validate_and_count() const = 0;
    /// The structure's height.
    [[nodiscard]] virtual std::size_t depth() const noexcept = 0;
};

/// The empty reversible tree.
template <class T>
struct empty_rev_tree_rep final : rev_tree_rep<T> {
    using tree_type = rev_tree<T>;
    using element_type = rev_element<T>;

    /// Copies the leaf elements out.
    /// Splits at the first point where the accumulated measure satisfies the predicate.
    /// A copy with the leaf element at the given index replaced.
    /// The leaf element at the given index.
    /// Views the rightmost element together with the rest, or nothing when empty.
    /// Views the leftmost element together with the rest, or nothing when empty.
    /// A tree with the element added at the back.
    /// A tree with the element added at the front.
    /// The tree in the opposite order.
    /// The last leaf element.
    /// The first leaf element.
    /// Number of elements in the tree.
    /// The shared empty tree. One instance is reused, since an empty tree has nothing to
    /// distinguish.
    /// Which case this value is.
    [[nodiscard]] rev_tree_kind kind() const noexcept override { return rev_tree_kind::empty; }
    [[nodiscard]] bool empty_state() const noexcept override { return true; }
    [[nodiscard]] std::size_t size() const noexcept override { return 0; }
    [[nodiscard]] const T& first_leaf() const override { throw empty_error(); }
    [[nodiscard]] const T& last_leaf() const override { throw empty_error(); }
    [[nodiscard]] tree_type mirror() const override { return tree_type::empty(); }
    [[nodiscard]] tree_type cons(element_type value) const override { return tree_type::single(std::move(value)); }
    [[nodiscard]] tree_type snoc(element_type value) const override { return tree_type::single(std::move(value)); }
    [[nodiscard]] std::optional<rev_view_result<T>> try_view_left() const override { return std::nullopt; }
    [[nodiscard]] std::optional<rev_view_result<T>> try_view_right() const override { return std::nullopt; }
    [[nodiscard]] T get_leaf(std::size_t) const override { throw empty_error(); }
    [[nodiscard]] tree_type set_leaf(std::size_t, T) const override { throw empty_error(); }
    [[nodiscard]] rev_split_result<T> split_tree(std::size_t) const override { throw empty_error(); }
    void copy_leaves(std::vector<T>&) const override {}
    /// The structure's height.
    /// Checks the structural invariants and returns the element count it derived while doing so.
    [[nodiscard]] std::size_t validate_and_count() const override { return 0; }
    [[nodiscard]] std::size_t depth() const noexcept override { return 0; }

private:
    [[nodiscard]] static std::logic_error empty_error()
    {
        return std::logic_error("internal reversible tree empty access");
    }
};

/// A reversible tree holding exactly one element.
template <class T>
struct single_rev_tree_rep final : rev_tree_rep<T> {
    using tree_type = rev_tree<T>;
    using element_type = rev_element<T>;

    /// An empty tree using the supplied policy, which it retains.
    explicit single_rev_tree_rep(element_type element)
        : element(std::move(element))
    {
    }

    /// A tree with the element added at the front.
    /// The tree in the opposite order.
    /// The last leaf element.
    /// The first leaf element.
    /// Number of elements in the tree.
    /// The shared empty tree. One instance is reused, since an empty tree has nothing to
    /// distinguish.
    /// Which case this value is.
    [[nodiscard]] rev_tree_kind kind() const noexcept override { return rev_tree_kind::single; }
    [[nodiscard]] bool empty_state() const noexcept override { return false; }
    [[nodiscard]] std::size_t size() const noexcept override { return element.size(); }
    [[nodiscard]] const T& first_leaf() const override { return element.first_leaf(); }
    [[nodiscard]] const T& last_leaf() const override { return element.last_leaf(); }
    [[nodiscard]] tree_type mirror() const override { return tree_type::single(element.mirror()); }
    [[nodiscard]] tree_type cons(element_type value) const override
    {
        return rev_make_deep(rev_digit<T>{std::move(value)}, tree_type::empty(), rev_digit<T>{element});
    }
    /// A tree with the element added at the back.
    [[nodiscard]] tree_type snoc(element_type value) const override
    {
        return rev_make_deep(rev_digit<T>{element}, tree_type::empty(), rev_digit<T>{std::move(value)});
    }
    /// Views the leftmost element together with the rest, or nothing when empty.
    [[nodiscard]] std::optional<rev_view_result<T>> try_view_left() const override
    {
        return rev_view_result<T>{element, tree_type::empty()};
    }
    /// Views the rightmost element together with the rest, or nothing when empty.
    [[nodiscard]] std::optional<rev_view_result<T>> try_view_right() const override
    {
        return rev_view_result<T>{element, tree_type::empty()};
    }
    /// A copy with the leaf element at the given index replaced.
    /// The leaf element at the given index.
    [[nodiscard]] T get_leaf(const std::size_t index) const override { return element.get_leaf(index); }
    [[nodiscard]] tree_type set_leaf(const std::size_t index, T value) const override
    {
        return tree_type::single(element.set_leaf(index, std::move(value)));
    }
    /// Splits at the first point where the accumulated measure satisfies the predicate.
    [[nodiscard]] rev_split_result<T> split_tree(const std::size_t index) const override
    {
        return rev_split_result<T>{tree_type::empty(), element, index, tree_type::empty()};
    }
    /// Copies the leaf elements out.
    void copy_leaves(std::vector<T>& sink) const override { element.copy_leaves(sink); }
    /// The structure's height.
    /// Checks the structural invariants and returns the element count it derived while doing so.
    [[nodiscard]] std::size_t validate_and_count() const override { return element.validate_and_count(); }
    [[nodiscard]] std::size_t depth() const noexcept override { return 0; }

    element_type element;
};

/// A reversible tree with a prefix, a middle, and a suffix.
template <class T>
struct deep_rev_tree_rep final : rev_tree_rep<T> {
    using tree_type = rev_tree<T>;
    using element_type = rev_element<T>;
    using digit_type = rev_digit<T>;

    /// An empty tree using the supplied policies, which it retains.
    deep_rev_tree_rep(digit_type prefix, tree_type middle, digit_type suffix)
        : prefix(std::move(prefix))
        , middle(std::move(middle))
        , suffix(std::move(suffix))
        , total_size(checked_add(checked_add(rev_sum_sizes(this->prefix), this->middle.size()),
              rev_sum_sizes(this->suffix)))
    {
        validate_digit_lengths();
    }

    /// An empty tree using the supplied policies, which it retains.
    deep_rev_tree_rep(digit_type prefix, tree_type middle, digit_type suffix, std::size_t total_size, bool reversed)
        : prefix(std::move(prefix))
        , middle(std::move(middle))
        , suffix(std::move(suffix))
        , total_size(total_size)
        , reversed(reversed)
    {
        validate_digit_lengths();
    }

    /// The first leaf element.
    /// Number of elements in the tree.
    /// The shared empty tree. One instance is reused, since an empty tree has nothing to
    /// distinguish.
    /// Which case this value is.
    [[nodiscard]] rev_tree_kind kind() const noexcept override { return rev_tree_kind::deep; }
    [[nodiscard]] bool empty_state() const noexcept override { return false; }
    [[nodiscard]] std::size_t size() const noexcept override { return total_size; }
    [[nodiscard]] const T& first_leaf() const override
    {
        return reversed ? suffix.back().last_leaf() : prefix.front().first_leaf();
    }
    /// The last leaf element.
    [[nodiscard]] const T& last_leaf() const override
    {
        return reversed ? prefix.front().first_leaf() : suffix.back().last_leaf();
    }
    /// The prefix in the orientation the handle presents, which a reversed handle swaps with the
    /// suffix.
    [[nodiscard]] digit_type logical_prefix() const
    {
        return reversed ? rev_mirror_reversed(suffix) : prefix;
    }
    /// The suffix in the orientation the handle presents.
    [[nodiscard]] digit_type logical_suffix() const
    {
        return reversed ? rev_mirror_reversed(prefix) : suffix;
    }
    /// The middle in the orientation the handle presents.
    [[nodiscard]] tree_type logical_middle() const
    {
        return reversed ? middle.mirror() : middle;
    }
    /// The tree in the opposite order.
    [[nodiscard]] tree_type mirror() const override
    {
        return tree_type{std::make_shared<const deep_rev_tree_rep>(
            prefix,
            middle,
            suffix,
            total_size,
            !reversed)};
    }
    /// A tree with the element added at the front.
    [[nodiscard]] tree_type cons(element_type value) const override
    {
        auto logical = logical_prefix();
        if (logical.size() < 3) {
            logical.insert(logical.begin(), std::move(value));
            return rev_make_deep(std::move(logical), logical_middle(), logical_suffix());
        }

        auto pushed = element_type::node2(logical[1], logical[2]);
        auto next_prefix = digit_type{};
        next_prefix.reserve(2);
        next_prefix.push_back(std::move(value));
        next_prefix.push_back(logical[0]);
        return rev_make_deep(std::move(next_prefix), logical_middle().cons(std::move(pushed)), logical_suffix());
    }
    /// A tree with the element added at the back.
    [[nodiscard]] tree_type snoc(element_type value) const override
    {
        auto logical = logical_suffix();
        if (logical.size() < 3) {
            logical.push_back(std::move(value));
            return rev_make_deep(logical_prefix(), logical_middle(), std::move(logical));
        }

        auto pushed = element_type::node2(logical[0], logical[1]);
        auto next_suffix = digit_type{};
        next_suffix.reserve(2);
        next_suffix.push_back(logical[2]);
        next_suffix.push_back(std::move(value));
        return rev_make_deep(logical_prefix(), logical_middle().snoc(std::move(pushed)), std::move(next_suffix));
    }
    /// Views the leftmost element together with the rest, or nothing when empty.
    [[nodiscard]] std::optional<rev_view_result<T>> try_view_left() const override
    {
        auto logical = logical_prefix();
        auto head = logical.front();
        logical.erase(logical.begin());
        auto rest = logical.empty()
            ? rev_deep_left(std::move(logical), logical_middle(), logical_suffix())
            : rev_make_deep(std::move(logical), logical_middle(), logical_suffix());
        return rev_view_result<T>{std::move(head), std::move(rest)};
    }
    /// Views the rightmost element together with the rest, or nothing when empty.
    [[nodiscard]] std::optional<rev_view_result<T>> try_view_right() const override
    {
        auto logical = logical_suffix();
        auto last = logical.back();
        logical.pop_back();
        auto rest = logical.empty()
            ? rev_deep_right(logical_prefix(), logical_middle(), std::move(logical))
            : rev_make_deep(logical_prefix(), logical_middle(), std::move(logical));
        return rev_view_result<T>{std::move(last), std::move(rest)};
    }
    /// The leaf element at the given index.
    [[nodiscard]] T get_leaf(std::size_t index) const override
    {
        auto current_prefix = logical_prefix();
        auto prefix_size = rev_sum_sizes(current_prefix);
        if (index < prefix_size) {
            return get_in_digit(current_prefix, index);
        }

        index -= prefix_size;
        auto current_middle = logical_middle();
        if (index < current_middle.size()) {
            return current_middle.get_leaf(index);
        }

        index -= current_middle.size();
        return get_in_digit(logical_suffix(), index);
    }
    /// A copy with the leaf element at the given index replaced.
    [[nodiscard]] tree_type set_leaf(std::size_t index, T value) const override
    {
        auto current_prefix = logical_prefix();
        auto prefix_size = rev_sum_sizes(current_prefix);
        if (index < prefix_size) {
            return rev_make_deep(set_in_digit(current_prefix, index, std::move(value)), logical_middle(), logical_suffix());
        }

        index -= prefix_size;
        auto current_middle = logical_middle();
        if (index < current_middle.size()) {
            return rev_make_deep(current_prefix, current_middle.set_leaf(index, std::move(value)), logical_suffix());
        }

        index -= current_middle.size();
        return rev_make_deep(current_prefix, current_middle, set_in_digit(logical_suffix(), index, std::move(value)));
    }
    /// Splits at the first point where the accumulated measure satisfies the predicate.
    [[nodiscard]] rev_split_result<T> split_tree(std::size_t index) const override
    {
        auto current_prefix = logical_prefix();
        auto prefix_size = rev_sum_sizes(current_prefix);
        if (index < prefix_size) {
            auto split = split_digit(current_prefix, index);
            return rev_split_result<T>{
                rev_from_digit(split.before),
                split.hit,
                split.index_in_hit,
                rev_deep_left(split.after, logical_middle(), logical_suffix())};
        }

        index -= prefix_size;
        auto current_middle = logical_middle();
        if (index < current_middle.size()) {
            auto middle_split = current_middle.split_tree(index);
            auto children = middle_split.hit.node_value().logical_children();
            auto child_split = split_digit(children, middle_split.index_in_hit);
            return rev_split_result<T>{
                rev_deep_right(logical_prefix(), middle_split.left, child_split.before),
                child_split.hit,
                child_split.index_in_hit,
                rev_deep_left(child_split.after, middle_split.right, logical_suffix())};
        }

        index -= current_middle.size();
        auto current_suffix = logical_suffix();
        auto split = split_digit(current_suffix, index);
        return rev_split_result<T>{
            rev_deep_right(logical_prefix(), current_middle, split.before),
            split.hit,
            split.index_in_hit,
            rev_from_digit(split.after)};
    }
    void copy_leaves(std::vector<T>& sink) const override
    {
        for (const auto& element : logical_prefix()) {
            element.copy_leaves(sink);
        }

        logical_middle().copy_leaves(sink);

        for (const auto& element : logical_suffix()) {
            element.copy_leaves(sink);
        }
    }
    [[nodiscard]] std::size_t validate_and_count() const override
    {
        auto computed = std::size_t{0};
        for (const auto& element : logical_prefix()) {
            computed = checked_add(computed, element.validate_and_count());
        }

        computed = checked_add(computed, logical_middle().validate_and_count());

        for (const auto& element : logical_suffix()) {
            computed = checked_add(computed, element.validate_and_count());
        }

        if (computed != total_size) {
            throw std::logic_error("reversible deep tree cached size mismatch");
        }

        return computed;
    }
    [[nodiscard]] std::size_t depth() const noexcept override
    {
        return 1 + middle.depth();
    }

    digit_type prefix;
    tree_type middle;
    digit_type suffix;
    std::size_t total_size;
    bool reversed = false;

private:
    struct digit_split final {
        digit_type before;
        element_type hit;
        std::size_t index_in_hit;
        digit_type after;
    };

    void validate_digit_lengths() const
    {
        if (prefix.empty() || prefix.size() > 3 || suffix.empty() || suffix.size() > 3) {
            throw std::logic_error("reversible deep tree digits must hold one through three elements");
        }
    }

    [[nodiscard]] static T get_in_digit(const digit_type& elements, std::size_t index)
    {
        for (const auto& element : elements) {
            if (index < element.size()) {
                return element.get_leaf(index);
            }

            index -= element.size();
        }

        throw std::logic_error("reversible digit index is out of range");
    }

    [[nodiscard]] static digit_type set_in_digit(digit_type elements, std::size_t index, T value)
    {
        for (auto& element : elements) {
            if (index < element.size()) {
                element = element.set_leaf(index, std::move(value));
                return elements;
            }

            index -= element.size();
        }

        throw std::logic_error("reversible digit set index is out of range");
    }

    [[nodiscard]] static digit_split split_digit(const digit_type& elements, std::size_t index)
    {
        for (std::size_t element_index = 0; element_index + 1 < elements.size(); ++element_index) {
            if (index < elements[element_index].size()) {
                return digit_split{
                    digit_type{elements.begin(), elements.begin() + static_cast<std::ptrdiff_t>(element_index)},
                    elements[element_index],
                    index,
                    digit_type{
                        elements.begin() + static_cast<std::ptrdiff_t>(element_index + 1),
                        elements.end()}};
            }

            index -= elements[element_index].size();
        }

        return digit_split{
            digit_type{elements.begin(), elements.end() - 1},
            elements.back(),
            index,
            digit_type{}};
    }
};

template <class T>
[[nodiscard]] std::shared_ptr<const rev_tree_rep<T>> empty_rev_tree_rep_instance()
{
    static const auto instance = std::shared_ptr<const rev_tree_rep<T>>{std::make_shared<const empty_rev_tree_rep<T>>()};
    return instance;
}

template <class T>
rev_tree<T>::rev_tree()
    : rep_(empty_rev_tree_rep_instance<T>())
{
}

template <class T>
rev_tree<T> rev_tree<T>::empty()
{
    return rev_tree{};
}

template <class T>
rev_tree<T> rev_tree<T>::single(element_type element)
{
    return rev_tree{std::make_shared<const single_rev_tree_rep<T>>(std::move(element))};
}

template <class T>
rev_tree<T> rev_tree<T>::deep(digit_type prefix, rev_tree middle, digit_type suffix)
{
    return rev_tree{std::make_shared<const deep_rev_tree_rep<T>>(
        std::move(prefix),
        std::move(middle),
        std::move(suffix))};
}

template <class T>
bool rev_tree<T>::empty_state() const noexcept
{
    return rep_->empty_state();
}

template <class T>
std::size_t rev_tree<T>::size() const noexcept
{
    return rep_->size();
}

template <class T>
const T& rev_tree<T>::first_leaf() const
{
    return rep_->first_leaf();
}

template <class T>
const T& rev_tree<T>::last_leaf() const
{
    return rep_->last_leaf();
}

template <class T>
rev_tree<T> rev_tree<T>::mirror() const
{
    return rep_->mirror();
}

template <class T>
rev_tree<T> rev_tree<T>::cons(element_type value) const
{
    return rep_->cons(std::move(value));
}

template <class T>
rev_tree<T> rev_tree<T>::snoc(element_type value) const
{
    return rep_->snoc(std::move(value));
}

template <class T>
std::optional<rev_view_result<T>> rev_tree<T>::try_view_left() const
{
    return rep_->try_view_left();
}

template <class T>
std::optional<rev_view_result<T>> rev_tree<T>::try_view_right() const
{
    return rep_->try_view_right();
}

template <class T>
T rev_tree<T>::get_leaf(const std::size_t index) const
{
    return rep_->get_leaf(index);
}

template <class T>
rev_tree<T> rev_tree<T>::set_leaf(const std::size_t index, T value) const
{
    return rep_->set_leaf(index, std::move(value));
}

template <class T>
rev_split_result<T> rev_tree<T>::split_tree(const std::size_t index) const
{
    return rep_->split_tree(index);
}

template <class T>
void rev_tree<T>::copy_leaves(std::vector<T>& sink) const
{
    rep_->copy_leaves(sink);
}

template <class T>
std::size_t rev_tree<T>::validate_and_count() const
{
    return rep_->validate_and_count();
}

template <class T>
std::size_t rev_tree<T>::depth() const noexcept
{
    return rep_->depth();
}

/* A retained logical-order cursor over the physical reversible tree. Each
 * task carries the orientation inherited from its parent instead of calling
 * mirror(), so increment neither allocates mirrored nodes nor repeats an
 * indexed root-to-leaf descent. The reserved task stack is O(log n), and a
 * complete traversal visits every tree/node/leaf block once. */
template <class T>
class rev_tree_cursor final {
public:
    /// An unpositioned cursor, holding no version.
    rev_tree_cursor() = default;

    /// A cursor over the given tree version at the given position.
    explicit rev_tree_cursor(rev_tree<T> root)
        : root_(std::move(root))
    {
        tasks_.reserve(6 * (root_.depth() + 1) + 8);
        tasks_.push_back(task::tree(&root_, false));
        advance();
    }

    /// Whether the traversal has finished.
    [[nodiscard]] bool at_end() const noexcept
    {
        return current_ == nullptr;
    }

    /// The value at the current position.
    [[nodiscard]] const T& current() const noexcept
    {
        return *current_;
    }

    /// Advances to the next position.
    void advance()
    {
        current_ = nullptr;
        while (!tasks_.empty()) {
            const auto next = tasks_.back();
            tasks_.pop_back();
            switch (next.kind) {
            case task_kind::tree:
                expand_tree(*next.tree_value, next.reversed);
                break;
            case task_kind::digit:
                expand_digit(*next.digit_value, next.reversed);
                break;
            case task_kind::element:
                if (expand_element(*next.element_value, next.reversed)) {
                    return;
                }
                break;
            }
        }
    }

private:
    enum class task_kind {
        tree,
        digit,
        element
    };

    struct task final {
        /// The underlying tree.
        [[nodiscard]] static task tree(const rev_tree<T>* value, const bool reversed)
        {
            auto result = task{};
            result.kind = task_kind::tree;
            result.tree_value = value;
            result.reversed = reversed;
            return result;
        }

        /// The digit this value holds.
        [[nodiscard]] static task digit(const rev_digit<T>* value, const bool reversed)
        {
            auto result = task{};
            result.kind = task_kind::digit;
            result.digit_value = value;
            result.reversed = reversed;
            return result;
        }

        /// The stored element.
        [[nodiscard]] static task element(const rev_element<T>* value, const bool reversed)
        {
            auto result = task{};
            result.kind = task_kind::element;
            result.element_value = value;
            result.reversed = reversed;
            return result;
        }

        task_kind kind = task_kind::tree;
        const rev_tree<T>* tree_value = nullptr;
        const rev_digit<T>* digit_value = nullptr;
        const rev_element<T>* element_value = nullptr;
        bool reversed = false;
    };

    void expand_tree(const rev_tree<T>& tree, const bool inherited_reversal)
    {
        switch (tree.rep_->kind()) {
        case rev_tree_kind::empty:
            return;
        case rev_tree_kind::single: {
            const auto& single = static_cast<const single_rev_tree_rep<T>&>(*tree.rep_);
            tasks_.push_back(task::element(&single.element, inherited_reversal));
            return;
        }
        case rev_tree_kind::deep: {
            const auto& deep = static_cast<const deep_rev_tree_rep<T>&>(*tree.rep_);
            const bool reversed = inherited_reversal != deep.reversed;
            if (reversed) {
                tasks_.push_back(task::digit(&deep.prefix, true));
                tasks_.push_back(task::tree(&deep.middle, true));
                tasks_.push_back(task::digit(&deep.suffix, true));
            } else {
                tasks_.push_back(task::digit(&deep.suffix, false));
                tasks_.push_back(task::tree(&deep.middle, false));
                tasks_.push_back(task::digit(&deep.prefix, false));
            }
            return;
        }
        }
    }

    void expand_digit(const rev_digit<T>& digit, const bool reversed)
    {
        if (reversed) {
            for (const auto& element : digit) {
                tasks_.push_back(task::element(&element, true));
            }
        } else {
            for (auto index = digit.size(); index != 0; --index) {
                tasks_.push_back(task::element(&digit[index - 1], false));
            }
        }
    }

    [[nodiscard]] bool expand_element(const rev_element<T>& element, const bool inherited_reversal)
    {
        if (element.is_leaf()) {
            current_ = &element.leaf_value();
            return true;
        }

        const auto& node = element.node_value();
        tasks_.push_back(task::digit(&node.children_, inherited_reversal != node.reversed_));
        return false;
    }

    rev_tree<T> root_;
    std::vector<task> tasks_;
    const T* current_ = nullptr;
};

template <class T>
std::size_t rev_sum_sizes(const rev_digit<T>& elements)
{
    auto total = std::size_t{0};
    for (const auto& element : elements) {
        total = checked_add(total, element.size());
    }

    return total;
}

template <class T>
rev_digit<T> rev_mirror_reversed(const rev_digit<T>& elements)
{
    auto result = rev_digit<T>{};
    result.reserve(elements.size());
    for (auto index = elements.size(); index != 0; --index) {
        result.push_back(elements[index - 1].mirror());
    }

    return result;
}

template <class T>
rev_tree<T> rev_make_deep(rev_digit<T> prefix, rev_tree<T> middle, rev_digit<T> suffix)
{
    return rev_tree<T>::deep(std::move(prefix), std::move(middle), std::move(suffix));
}

template <class T>
rev_tree<T> rev_from_digit(const rev_digit<T>& elements)
{
    switch (elements.size()) {
    case 0:
        return rev_tree<T>::empty();
    case 1:
        return rev_tree<T>::single(elements.front());
    default:
        return rev_make_deep(
            rev_digit<T>{elements.begin(), elements.begin() + 1},
            rev_tree<T>::empty(),
            rev_digit<T>{elements.begin() + 1, elements.end()});
    }
}

template <class T>
rev_tree<T> rev_deep_left(rev_digit<T> prefix, rev_tree<T> middle, rev_digit<T> suffix)
{
    if (!prefix.empty()) {
        return rev_make_deep(std::move(prefix), std::move(middle), std::move(suffix));
    }

    if (auto view = middle.try_view_left()) {
        return rev_make_deep(view->value.node_value().logical_children(), view->rest, std::move(suffix));
    }

    return rev_from_digit(suffix);
}

template <class T>
rev_tree<T> rev_deep_right(rev_digit<T> prefix, rev_tree<T> middle, rev_digit<T> suffix)
{
    if (!suffix.empty()) {
        return rev_make_deep(std::move(prefix), std::move(middle), std::move(suffix));
    }

    if (auto view = middle.try_view_right()) {
        return rev_make_deep(std::move(prefix), view->rest, view->value.node_value().logical_children());
    }

    return rev_from_digit(prefix);
}

template <class T>
rev_digit<T> rev_nodes(const rev_digit<T>& elements)
{
    auto nodes = rev_digit<T>{};
    auto index = std::size_t{0};
    while (elements.size() - index > 4) {
        nodes.push_back(rev_element<T>::node3(elements[index], elements[index + 1], elements[index + 2]));
        index += 3;
    }

    switch (elements.size() - index) {
    case 2:
        nodes.push_back(rev_element<T>::node2(elements[index], elements[index + 1]));
        break;
    case 3:
        nodes.push_back(rev_element<T>::node3(elements[index], elements[index + 1], elements[index + 2]));
        break;
    case 4:
        nodes.push_back(rev_element<T>::node2(elements[index], elements[index + 1]));
        nodes.push_back(rev_element<T>::node2(elements[index + 2], elements[index + 3]));
        break;
    default:
        throw std::logic_error("unexpected reversible node grouping remainder");
    }

    return nodes;
}

template <class T>
rev_tree<T> rev_concat(rev_tree<T> left, rev_tree<T> right)
{
    return rev_concat_with_middle(std::move(left), rev_digit<T>{}, std::move(right));
}

template <class T>
rev_tree<T> rev_concat_with_middle(rev_tree<T> left, const rev_digit<T>& middle, rev_tree<T> right)
{
    if (left.empty_state()) {
        for (auto index = middle.size(); index != 0; --index) {
            right = right.cons(middle[index - 1]);
        }

        return right;
    }

    if (right.empty_state()) {
        for (const auto& element : middle) {
            left = left.snoc(element);
        }

        return left;
    }

    if (left.rep_->kind() == rev_tree_kind::single) {
        const auto& left_single = static_cast<const single_rev_tree_rep<T>&>(*left.rep_);
        for (auto index = middle.size(); index != 0; --index) {
            right = right.cons(middle[index - 1]);
        }

        return right.cons(left_single.element);
    }

    if (right.rep_->kind() == rev_tree_kind::single) {
        const auto& right_single = static_cast<const single_rev_tree_rep<T>&>(*right.rep_);
        for (const auto& element : middle) {
            left = left.snoc(element);
        }

        return left.snoc(right_single.element);
    }

    const auto& left_deep = static_cast<const deep_rev_tree_rep<T>&>(*left.rep_);
    const auto& right_deep = static_cast<const deep_rev_tree_rep<T>&>(*right.rep_);

    auto combined = left_deep.logical_suffix();
    combined.insert(combined.end(), middle.begin(), middle.end());
    auto right_prefix = right_deep.logical_prefix();
    combined.insert(combined.end(), right_prefix.begin(), right_prefix.end());

    return rev_make_deep(
        left_deep.logical_prefix(),
        rev_concat_with_middle(left_deep.logical_middle(), rev_nodes(combined), right_deep.logical_middle()),
        right_deep.logical_suffix());
}

} // namespace durable7::finger_tree::detail
