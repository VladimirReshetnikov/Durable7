#pragma once

#include <durable7/finger_tree/measures.hpp>

#include <array>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace durable7::finger_tree {

/// Describes a validated DABA Lite chunk and incremental-reversal state.
struct daba_lite_statistics final {
    std::size_t count = 0;
    std::size_t front_length = 0;
    std::size_t back_length = 0;
    std::size_t left_length = 0;
    std::size_t right_length = 0;
    std::size_t accumulator_length = 0;
    std::size_t block_count = 0;
    std::size_t allocated_slot_capacity = 0;
    std::size_t slack_slot_count = 0;

    [[nodiscard]] constexpr bool operator==(const daba_lite_statistics&) const = default;
};

/// A monoid policy usable by `daba_lite<T, MonoidPolicy>`. The policy's
/// measure is both the stored window value and the aggregate value.
template <class Policy, class T>
concept daba_lite_monoid_policy = monoid_policy<Policy> && std::same_as<typename Policy::measure_type, T>;

/// A value whose publication can be committed without throwing. Copies may
/// throw because DABA Lite performs every copy while building a private plan;
/// moves must not throw because the final slot/aggregate swaps are the commit.
template <class T>
concept daba_lite_value = std::copyable<T> && std::is_nothrow_move_constructible_v<T>
    && std::is_nothrow_move_assignable_v<T>;

/// Maintains a FIFO sliding-window aggregate with worst-case constant
/// scheduling work per insertion, eviction, and query.
///
/// This is Tangwongsan, Hirzel, and Schneider's six-cursor DABA Lite
/// schedule. It is deliberately mutable and does not support overlapping
/// access to one instance; callers must provide external synchronization.
/// `insert`, `evict`/`try_evict`, and `aggregate` invoke `combine` at most
/// three, two, and one times respectively. Policy callbacks are completed
/// before the corresponding mutation is published. All admissible values
/// have nonthrowing moves, making the final publication step nonthrowing;
/// policy, allocation, and value-copy failures therefore leave the window
/// unchanged. Side effects performed by a callback itself cannot be rolled
/// back.
///
/// Per-slide work is worst-case O(1) when the policy operations and value
/// copies are O(1). The queue uses fixed 64-slot blocks and promptly destroys
/// a block when the front crosses its boundary. Unlike the tracing-GC C#
/// reference, `clear` deterministically destroys the retired values and
/// blocks; it therefore takes O(n + c) native reclamation work for n live
/// positions in c blocks rather than claiming a false O(1) bound.
template <daba_lite_value T, class MonoidPolicy>
    requires daba_lite_monoid_policy<MonoidPolicy, T>
class daba_lite final {
private:
    struct block;

    struct cursor final {
        block* owner = nullptr;
        std::size_t index = 0;

        [[nodiscard]] constexpr bool operator==(const cursor&) const = default;
    };

    struct block final {
        static constexpr std::size_t capacity = 64;

        std::array<std::optional<T>, capacity> items{};
        block* previous = nullptr;
        std::unique_ptr<block> next;
    };

    struct end_advance final {
        cursor next;
        bool created_successor = false;
    };

    class chunked_queue final {
    public:
        chunked_queue() : first_(std::make_unique<block>()) {}

        chunked_queue(const chunked_queue&) = delete;
        chunked_queue& operator=(const chunked_queue&) = delete;
        chunked_queue(chunked_queue&&) = delete;
        chunked_queue& operator=(chunked_queue&&) = delete;

        ~chunked_queue() { destroy_chain(std::move(first_)); }

        [[nodiscard]] cursor begin() const noexcept { return cursor{first_.get(), 0}; }

        [[nodiscard]] block* first_block() const noexcept { return first_.get(); }

        [[nodiscard]] const T& read(const cursor position) const
        {
            const auto& slot = position.owner->items[position.index];
            if (!slot.has_value()) {
                throw std::logic_error("a DABA Lite work cursor reached an unoccupied slot");
            }
            return *slot;
        }

        void construct(const cursor position, T value)
        {
            auto& slot = position.owner->items[position.index];
            if (slot.has_value()) {
                throw std::logic_error("the DABA Lite end cursor does not identify a free slot");
            }
            slot.emplace(std::move(value));
        }

        void write(const cursor position, T value) noexcept
        {
            auto& slot = position.owner->items[position.index];
            assert(slot.has_value());
            slot.emplace(std::move(value));
        }

        void clear_slot(const cursor position) noexcept { position.owner->items[position.index].reset(); }

        [[nodiscard]] cursor advance(const cursor position) const
        {
            if (position.index + 1 < block::capacity) {
                return cursor{position.owner, position.index + 1};
            }
            if (position.owner->next == nullptr) {
                throw std::logic_error("a DABA Lite cursor cannot advance beyond the active block chain");
            }
            return cursor{position.owner->next.get(), 0};
        }

        [[nodiscard]] end_advance advance_end(const cursor position)
        {
            if (position.index + 1 < block::capacity) {
                return end_advance{cursor{position.owner, position.index + 1}, false};
            }

            auto created = false;
            if (position.owner->next == nullptr) {
                auto successor = std::make_unique<block>();
                successor->previous = position.owner;
                position.owner->next = std::move(successor);
                created = true;
            }
            return end_advance{cursor{position.owner->next.get(), 0}, created};
        }

        [[nodiscard]] cursor retreat(const cursor position) const
        {
            if (position.index != 0) {
                return cursor{position.owner, position.index - 1};
            }
            if (position.owner->previous == nullptr) {
                throw std::logic_error("a DABA Lite cursor cannot retreat before the active block chain");
            }
            return cursor{position.owner->previous, block::capacity - 1};
        }

        void remove_created_successor(block* const predecessor) noexcept
        {
            auto discarded = std::move(predecessor->next);
            if (discarded != nullptr) {
                discarded->previous = nullptr;
                destroy_chain(std::move(discarded));
            }
        }

        void trim_before(const cursor front) noexcept
        {
            while (first_.get() != front.owner) {
                auto retired = std::move(first_);
                first_ = std::move(retired->next);
                retired->next.reset();
                retired.reset();
            }
            first_->previous = nullptr;
        }

        void swap(chunked_queue& other) noexcept { first_.swap(other.first_); }

    private:
        static void destroy_chain(std::unique_ptr<block> chain) noexcept
        {
            while (chain != nullptr) {
                auto next = std::move(chain->next);
                chain.reset();
                chain = std::move(next);
            }
        }

        std::unique_ptr<block> first_;
    };

    struct pending_write final {
        cursor position;
        T value;
    };

    struct fixup_plan final {
        cursor l;
        cursor r;
        cursor a;
        cursor b;
        T aggregate_ra;
        T aggregate_b;
        std::optional<pending_write> first_write;
        std::optional<pending_write> second_write;
    };

    static_assert(std::is_nothrow_move_constructible_v<fixup_plan>);

public:
    /// Initializes an empty aggregator.
    daba_lite()
        : f_(queue_.begin()), l_(f_), r_(f_), a_(f_), b_(f_), e_(f_), aggregate_ra_(MonoidPolicy::empty()),
          aggregate_b_(MonoidPolicy::empty())
    {
    }

    daba_lite(const daba_lite&) = delete;
    daba_lite& operator=(const daba_lite&) = delete;
    daba_lite(daba_lite&&) = delete;
    daba_lite& operator=(daba_lite&&) = delete;

    /// Gets the number of values in the current window.
    [[nodiscard]] std::size_t size() const noexcept { return size_; }

    /// Gets whether the current window is empty.
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }

    /// Gets the aggregate in FIFO order. An empty query invokes `empty` and
    /// no `combine`; a nonempty query invokes `combine` exactly once.
    [[nodiscard]] T aggregate() const
    {
        return empty() ? MonoidPolicy::empty() : MonoidPolicy::combine(queue_.read(f_), aggregate_b_);
    }

    /// Inserts a value at the back of the window. Invokes `combine` at most
    /// three times. Any exception leaves this object unchanged.
    void insert(T value)
    {
        if (size_ == (std::numeric_limits<std::size_t>::max)()) {
            throw std::overflow_error("the DABA Lite window cannot grow beyond size_t capacity");
        }

        auto next_aggregate_b = MonoidPolicy::combine(aggregate_b_, value);
        const auto old_end = e_;
        const auto advanced = queue_.advance_end(old_end);

        try {
            queue_.construct(old_end, std::move(value));
        } catch (...) {
            if (advanced.created_successor) {
                queue_.remove_created_successor(old_end.owner);
            }
            throw;
        }

        auto plan = std::optional<fixup_plan>{};
        try {
            plan.emplace(compute_fixup(f_, advanced.next, aggregate_ra_, next_aggregate_b));
        } catch (...) {
            queue_.clear_slot(old_end);
            if (advanced.created_successor) {
                queue_.remove_created_successor(old_end.owner);
            }
            throw;
        }

        apply_fixup(std::move(*plan));
        e_ = advanced.next;
        ++size_;
    }

    /// Evicts the oldest value. Invokes `combine` at most twice.
    /// Throws `std::out_of_range` when the window is empty.
    void evict()
    {
        if (!try_evict()) {
            throw std::out_of_range("cannot evict from an empty DABA Lite window");
        }
    }

    /// Attempts to evict the oldest value. Any exception leaves this object
    /// unchanged.
    [[nodiscard]] bool try_evict()
    {
        if (empty()) {
            return false;
        }

        const auto old_front = f_;
        const auto next_front = queue_.advance(old_front);
        auto plan = compute_fixup(next_front, e_, aggregate_ra_, aggregate_b_);
        apply_fixup(std::move(plan));
        f_ = next_front;
        --size_;

        queue_.clear_slot(old_front);
        queue_.trim_before(f_);
        return true;
    }

    /// Evicts every value and leaves one reusable empty block. `empty` is
    /// invoked exactly once for a nonempty window and no `combine` callback is
    /// invoked. Any identity, copy, or allocation exception leaves this
    /// object unchanged.
    ///
    /// C++ deterministic destruction makes this O(n + c), not O(1): every
    /// retired value and each of the c blocks is reclaimed before return.
    void clear()
    {
        if (empty()) {
            return;
        }

        auto identity = MonoidPolicy::empty();
        auto next_aggregate_ra = identity;
        auto next_aggregate_b = identity;
        auto replacement = chunked_queue{};
        const auto begin = replacement.begin();

        queue_.swap(replacement);
        f_ = l_ = r_ = a_ = b_ = e_ = begin;
        aggregate_ra_ = std::move(next_aggregate_ra);
        aggregate_b_ = std::move(next_aggregate_b);
        size_ = 0;
    }

    /// Validates block links, slot ownership, cursor order, and the DABA Lite
    /// size equations without invoking the monoid. Runs in O(c) auxiliary
    /// space and O(n + c) time for n occupied slots in c active blocks.
    [[nodiscard]] daba_lite_statistics validate_structure() const
    {
        validate_cursor_index(f_, "F");
        validate_cursor_index(l_, "L");
        validate_cursor_index(r_, "R");
        validate_cursor_index(a_, "A");
        validate_cursor_index(b_, "B");
        validate_cursor_index(e_, "E");

        const auto first = queue_.first_block();
        if (first == nullptr) {
            throw std::logic_error("the DABA Lite block chain is empty");
        }
        if (first->previous != nullptr) {
            throw std::logic_error("the first DABA Lite block has a predecessor");
        }
        if (first != f_.owner) {
            throw std::logic_error("the DABA Lite front cursor is not in the first active block");
        }

        auto seen = std::unordered_set<const block*>{};
        const block* previous = nullptr;
        const block* current = first;
        auto block_base = std::size_t{0};
        auto block_count = std::size_t{0};
        auto occupied_count = std::size_t{0};
        auto f = std::optional<std::size_t>{};
        auto l = std::optional<std::size_t>{};
        auto r = std::optional<std::size_t>{};
        auto a = std::optional<std::size_t>{};
        auto b = std::optional<std::size_t>{};
        auto e = std::optional<std::size_t>{};

        while (true) {
            if (!seen.insert(current).second) {
                throw std::logic_error("the DABA Lite block chain contains a cycle");
            }
            if (current->previous != previous) {
                throw std::logic_error("a DABA Lite block has an invalid predecessor link");
            }
            if (previous != nullptr && previous->next.get() != current) {
                throw std::logic_error("a DABA Lite block has an invalid successor link");
            }

            record_cursor_position(current, block_base, f_, f);
            record_cursor_position(current, block_base, l_, l);
            record_cursor_position(current, block_base, r_, r);
            record_cursor_position(current, block_base, a_, a);
            record_cursor_position(current, block_base, b_, b);
            record_cursor_position(current, block_base, e_, e);
            for (const auto& item : current->items) {
                if (item.has_value()) {
                    occupied_count = checked_add(occupied_count, std::size_t{1});
                }
            }
            block_count = checked_add(block_count, std::size_t{1});

            if (current == e_.owner) {
                if (current->next != nullptr) {
                    throw std::logic_error("the DABA Lite end cursor is not in the last active block");
                }
                break;
            }

            previous = current;
            current = current->next.get();
            if (current == nullptr) {
                throw std::logic_error("the DABA Lite end cursor is not reachable from the front");
            }
            block_base = checked_add(block_base, block::capacity);
        }

        if (!f.has_value() || !l.has_value() || !r.has_value() || !a.has_value() || !b.has_value() || !e.has_value()) {
            throw std::logic_error("a DABA Lite cursor is outside the active block chain");
        }
        if (!(*f <= *l && *l <= *r && *r <= *a && *a <= *b && *b <= *e)) {
            throw std::logic_error("DABA Lite cursors do not satisfy F <= L <= R <= A <= B <= E");
        }
        if (*e - *f != size_) {
            throw std::logic_error("the DABA Lite size disagrees with the F-to-E cursor distance");
        }
        if (occupied_count != size_) {
            throw std::logic_error("the DABA Lite occupied-slot count disagrees with the window size");
        }
        if (e_.owner->items[e_.index].has_value()) {
            throw std::logic_error("the DABA Lite end cursor does not identify a free slot");
        }

        const auto front_count = *b - *f;
        const auto back_count = *e - *b;
        const auto left_count = *r - *l;
        const auto right_count = *a - *r;
        const auto accumulator_count = *b - *a;
        if (empty()) {
            if (!(f_ == l_ && l_ == r_ && r_ == a_ && a_ == b_ && b_ == e_)) {
                throw std::logic_error("an empty DABA Lite window does not have coincident cursors");
            }
        } else {
            if (left_count != right_count) {
                throw std::logic_error("the DABA Lite left and right work regions have different sizes");
            }
            if (front_count < back_count) {
                throw std::logic_error("the DABA Lite front region is shorter than the back region");
            }
            auto reversal_count = checked_add(left_count, right_count);
            reversal_count = checked_add(reversal_count, accumulator_count);
            reversal_count = checked_add(reversal_count, std::size_t{1});
            if (reversal_count != front_count - back_count) {
                throw std::logic_error("the DABA Lite incremental-reversal size equation is invalid");
            }
            if (*l - *f != checked_add(back_count, std::size_t{1})) {
                throw std::logic_error("the DABA Lite processed-prefix distance is invalid");
            }
        }

        const auto front_plus_size = checked_add(f_.index, size_);
        const auto expected_block_count = checked_add(front_plus_size / block::capacity, std::size_t{1});
        if (block_count != expected_block_count) {
            throw std::logic_error("the DABA Lite active-block count is invalid");
        }
        if (block_count > (std::numeric_limits<std::size_t>::max)() / block::capacity) {
            throw std::overflow_error("DABA Lite allocated-slot capacity overflow");
        }
        const auto allocated_slot_capacity = block_count * block::capacity;
        const auto slack_slot_count = allocated_slot_capacity - size_;
        if (slack_slot_count < 1 || slack_slot_count > 2 * block::capacity - 1) {
            throw std::logic_error("DABA Lite block slack exceeds its two-block bound");
        }

        return daba_lite_statistics{size_,           front_count,       back_count,  left_count,
                                    right_count,     accumulator_count, block_count, allocated_slot_capacity,
                                    slack_slot_count};
    }

private:
    [[nodiscard]] fixup_plan compute_fixup(const cursor front, const cursor end, const T& aggregate_ra,
                                           const T& aggregate_b) const
    {
        auto l = l_;
        auto r = r_;
        auto a = a_;
        auto b = b_;
        auto next_aggregate_ra = aggregate_ra;
        auto next_aggregate_b = aggregate_b;
        auto identity = std::optional<T>{};

        if (front == b) {
            identity.emplace(MonoidPolicy::empty());
            return fixup_plan{end, end, end, end, *identity, *identity, std::nullopt, std::nullopt};
        }

        if (l == b) {
            identity.emplace(MonoidPolicy::empty());
            l = front;
            a = end;
            b = end;
            next_aggregate_ra = next_aggregate_b;
            next_aggregate_b = *identity;
        }

        if (l == r) {
            const auto next_l = queue_.advance(l);
            const auto next_r = queue_.advance(r);
            const auto next_a = queue_.advance(a);
            if (!identity.has_value()) {
                identity.emplace(MonoidPolicy::empty());
            }
            return fixup_plan{next_l,       next_r,      next_a, b, *identity, std::move(next_aggregate_b),
                              std::nullopt, std::nullopt};
        }

        const auto next_left = queue_.advance(l);
        const auto before_a = queue_.retreat(a);
        auto new_left_value = MonoidPolicy::combine(queue_.read(l), next_aggregate_ra);
        auto product_a = a == b ? MonoidPolicy::empty() : queue_.read(a);
        auto new_accumulator_value = MonoidPolicy::combine(queue_.read(before_a), product_a);

        return fixup_plan{next_left,
                          r,
                          before_a,
                          b,
                          std::move(next_aggregate_ra),
                          std::move(next_aggregate_b),
                          pending_write{l, std::move(new_left_value)},
                          pending_write{before_a, std::move(new_accumulator_value)}};
    }

    void apply_fixup(fixup_plan plan) noexcept
    {
        if (plan.first_write.has_value()) {
            queue_.write(plan.first_write->position, std::move(plan.first_write->value));
        }
        if (plan.second_write.has_value()) {
            queue_.write(plan.second_write->position, std::move(plan.second_write->value));
        }
        l_ = plan.l;
        r_ = plan.r;
        a_ = plan.a;
        b_ = plan.b;
        aggregate_ra_ = std::move(plan.aggregate_ra);
        aggregate_b_ = std::move(plan.aggregate_b);
    }

    static void validate_cursor_index(const cursor value, const std::string& name)
    {
        if (value.owner == nullptr || value.index >= block::capacity) {
            throw std::logic_error("the DABA Lite " + name + " cursor has an invalid block index");
        }
    }

    static void record_cursor_position(const block* const current, const std::size_t block_base, const cursor value,
                                       std::optional<std::size_t>& position)
    {
        if (current == value.owner) {
            position = checked_add(block_base, value.index);
        }
    }

    chunked_queue queue_;
    cursor f_;
    cursor l_;
    cursor r_;
    cursor a_;
    cursor b_;
    cursor e_;
    T aggregate_ra_;
    T aggregate_b_;
    std::size_t size_ = 0;
};

} // namespace durable7::finger_tree
