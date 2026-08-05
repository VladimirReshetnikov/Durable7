/// Tests for the contextual event-rank sequence.
///
/// The interesting property is that the cached summary is an *ordered* composition: swapping two
/// subtrees feeds a different state into the other summary, so the tests check the composition law
/// and its non-commutativity directly, not just the operations built on top of it.

#include <durable7/finger_tree/contextual_rank_sequence.hpp>

#include "test_support/command_model.hpp"
#include "test_support/test_runner.hpp"

#include <algorithm>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

using namespace durable7::finger_tree;
using namespace durable7::finger_tree::tests;

namespace {

/// Two states: outside quotes (0) and inside quotes (1). A comma outside quotes is an event.
struct quoted_comma_machine final {
    static constexpr std::size_t state_count = 2;

    [[nodiscard]] static contextual_event_transition transition(
        const std::size_t state,
        const char element)
    {
        if (element == '"') {
            return contextual_event_transition{std::size_t{1} - state, 0};
        }

        if (element == ',' && state == 0) {
            return contextual_event_transition{state, 1};
        }

        return contextual_event_transition{state, 0};
    }
};

/// Three states, with transitions that emit zero, one, or two events.
struct ternary_machine final {
    static constexpr std::size_t state_count = 3;

    [[nodiscard]] static contextual_event_transition transition(
        const std::size_t state,
        const int element)
    {
        const auto magnitude = static_cast<std::size_t>(
            element < 0 ? -static_cast<std::int64_t>(element) : static_cast<std::int64_t>(element));
        const auto next = (state + magnitude) % 3;
        return contextual_event_transition{next, static_cast<std::int64_t>(next)};
    }
};

/// Three states whose reset and shift transitions do not commute, in either component.
struct reset_shift_machine final {
    static constexpr std::size_t state_count = 3;

    [[nodiscard]] static contextual_event_transition transition(
        const std::size_t state,
        const char element)
    {
        if (element == 'r') {
            return contextual_event_transition{0, 0};
        }

        if (element == 's') {
            return contextual_event_transition{(state + 1) % 3, state == 2 ? 1 : 0};
        }

        return contextual_event_transition{state, 0};
    }
};

std::atomic<std::uint64_t> counting_calls{0};

/// Counts every transition so tests can prove cached queries do not rescan the input.
struct counting_machine final {
    static constexpr std::size_t state_count = 2;

    [[nodiscard]] static contextual_event_transition transition(
        const std::size_t state,
        const int element)
    {
        counting_calls.fetch_add(1, std::memory_order_relaxed);
        const auto value = static_cast<std::size_t>(element);
        return contextual_event_transition{
            (state + (value & std::size_t{1})) & std::size_t{1},
            ((value ^ state) & std::size_t{3}) == 0 ? 1 : 0};
    }
};

/// Declares one state but leaves it, violating the machine contract.
struct invalid_state_machine final {
    static constexpr std::size_t state_count = 1;

    [[nodiscard]] static contextual_event_transition transition(std::size_t, int)
    {
        return contextual_event_transition{1, 0};
    }
};

/// Emits a negative event count, violating the machine contract.
struct negative_event_machine final {
    static constexpr std::size_t state_count = 1;

    [[nodiscard]] static contextual_event_transition transition(std::size_t, int)
    {
        return contextual_event_transition{0, -1};
    }
};

/// Emits the largest representable event count, so two elements overflow.
struct huge_event_machine final {
    static constexpr std::size_t state_count = 1;

    [[nodiscard]] static contextual_event_transition transition(std::size_t, int)
    {
        return contextual_event_transition{0, (std::numeric_limits<std::int64_t>::max)()};
    }
};

/// Declares no states at all, which the concept rejects.
struct stateless_machine final {
    static constexpr std::size_t state_count = 0;

    [[nodiscard]] static contextual_event_transition transition(const std::size_t state, int)
    {
        return contextual_event_transition{state, 0};
    }
};

/// Reports something other than a transition, which the concept rejects.
struct wrong_result_machine final {
    static constexpr std::size_t state_count = 2;

    [[nodiscard]] static int transition(std::size_t, int)
    {
        return 0;
    }
};

/// Carries no state count at all, which the concept rejects.
struct not_a_machine final {
};

using quoted_sequence = contextual_rank_sequence<char, quoted_comma_machine>;
using ternary_sequence = contextual_rank_sequence<int, ternary_machine>;
using reset_shift_sequence = contextual_rank_sequence<char, reset_shift_machine>;

[[nodiscard]] std::vector<char> characters(const std::string_view text)
{
    return std::vector<char>{text.begin(), text.end()};
}

// Brace initializers cannot appear inside the assertion macros, whose arguments are separated by
// the preprocessor's own comma scan, so the expected aggregates are built through these factories.

[[nodiscard]] contextual_prefix_summary prefix_summary(
    const std::size_t final_state,
    const std::int64_t event_count)
{
    return contextual_prefix_summary{final_state, event_count};
}

[[nodiscard]] contextual_event_location event_location(
    const std::size_t element_index,
    const std::int64_t event_index_in_element,
    const std::size_t state_before,
    const std::size_t state_after,
    const std::int64_t element_event_count)
{
    return contextual_event_location{
        element_index,
        event_index_in_element,
        state_before,
        state_after,
        element_event_count};
}

[[nodiscard]] contextual_rank_validation_statistics validation_statistics(
    const std::size_t count,
    const std::size_t state_count,
    const std::int64_t maximum_total_event_count)
{
    return contextual_rank_validation_statistics{count, state_count, maximum_total_event_count};
}

/// Checks a sequence against an independent list plus a direct machine scan, for every state, every
/// prefix boundary, and every event.
template <class Element, class MachineType>
void require_matches_model(
    const contextual_rank_sequence<Element, MachineType>& sequence,
    const std::vector<Element>& model)
{
    FT_REQUIRE(sequence.to_vector() == model);
    FT_REQUIRE_EQUAL(sequence.size(), model.size());
    FT_REQUIRE_EQUAL(sequence.empty(), model.empty());

    const auto statistics = sequence.validate_structure();
    FT_REQUIRE(statistics.has_value());
    FT_REQUIRE_EQUAL(statistics->count, model.size());
    FT_REQUIRE_EQUAL(statistics->state_count, MachineType::state_count);

    auto maximum_events = std::int64_t{0};
    for (auto initial_state = std::size_t{0}; initial_state != MachineType::state_count;
         ++initial_state) {
        auto state = initial_state;
        auto events = std::int64_t{0};
        auto expected_locations = std::vector<contextual_event_location>{};
        FT_REQUIRE(
            sequence.evaluate_prefix(0, initial_state) == prefix_summary(state, events));

        for (auto index = std::size_t{0}; index != model.size(); ++index) {
            const auto step = MachineType::transition(state, model[index]);
            for (auto ordinal = std::int64_t{0}; ordinal != step.event_count; ++ordinal) {
                expected_locations.push_back(
                    event_location(index, ordinal, state, step.next_state, step.event_count));
            }

            state = step.next_state;
            events += step.event_count;
            FT_REQUIRE(
                sequence.evaluate_prefix(index + 1, initial_state)
                == prefix_summary(state, events));
            FT_REQUIRE_EQUAL(sequence.event_rank(index + 1, initial_state), events);
            FT_REQUIRE(sequence.at(index) == model[index]);
            FT_REQUIRE(sequence[index] == model[index]);
        }

        FT_REQUIRE(sequence.evaluate(initial_state) == prefix_summary(state, events));
        maximum_events = (std::max)(maximum_events, events);

        for (auto rank = std::size_t{0}; rank != expected_locations.size(); ++rank) {
            const auto located = sequence.try_select_event(
                static_cast<std::int64_t>(rank),
                initial_state);
            FT_REQUIRE(located.has_value());
            FT_REQUIRE(*located == expected_locations[rank]);
        }

        FT_REQUIRE(!sequence
                        .try_select_event(
                            static_cast<std::int64_t>(expected_locations.size()),
                            initial_state)
                        .has_value());
    }

    FT_REQUIRE_EQUAL(statistics->maximum_total_event_count, maximum_events);
}

/// Checks the ordered composition law through the public surface: the concatenation's summary must
/// feed the left operand's outgoing state into the right operand's summary.
template <class Element, class MachineType>
void require_ordered_composition(
    const contextual_rank_sequence<Element, MachineType>& left,
    const contextual_rank_sequence<Element, MachineType>& right)
{
    const auto joined = left.concat(right);
    FT_REQUIRE_EQUAL(joined.size(), left.size() + right.size());
    for (auto state = std::size_t{0}; state != MachineType::state_count; ++state) {
        const auto first = left.evaluate(state);
        const auto second = right.evaluate(first.final_state);
        FT_REQUIRE(
            joined.evaluate(state)
            == prefix_summary(second.final_state, first.event_count + second.event_count));
    }
}

} // namespace

void add_contextual_rank_sequence_tests(suite& tests)
{
    static_assert(contextual_event_machine<quoted_comma_machine, char>);
    static_assert(contextual_event_machine<ternary_machine, int>);
    static_assert(contextual_event_machine<reset_shift_machine, char>);
    static_assert(!contextual_event_machine<stateless_machine, int>);
    static_assert(!contextual_event_machine<wrong_result_machine, int>);
    static_assert(!contextual_event_machine<not_a_machine, int>);
    static_assert(quoted_sequence::state_count == 2);
    static_assert(ternary_sequence::state_count == 3);
    static_assert(std::forward_iterator<quoted_sequence::const_iterator>);
    static_assert(std::ranges::forward_range<quoted_sequence>);

    tests.add("Contextual ranks and selects depend on the prefix context, not the element", [] {
        const auto model = characters("\"a,b\",c,d");
        const auto text = quoted_sequence::from_range(model);

        FT_REQUIRE(text.evaluate(0) == prefix_summary(0, 2));
        FT_REQUIRE(text.evaluate(1) == prefix_summary(1, 1));
        FT_REQUIRE_EQUAL(text.event_rank(5, 0), std::int64_t{0});
        FT_REQUIRE_EQUAL(text.event_rank(6, 0), std::int64_t{1});

        const auto first = text.try_select_event(0, 0);
        FT_REQUIRE(first.has_value());
        FT_REQUIRE(*first == event_location(5, 0, 0, 0, 1));
        const auto second = text.try_select_event(1, 0);
        FT_REQUIRE(second.has_value());
        FT_REQUIRE_EQUAL(second->element_index, std::size_t{7});
        FT_REQUIRE(!text.try_select_event(2, 0).has_value());

        // Starting inside a quoted region makes the leading quote a close rather than an open, so
        // the comma at index 2 is now outside quotes and is the first event.
        const auto inside = text.try_select_event(0, 1);
        FT_REQUIRE(inside.has_value());
        FT_REQUIRE(*inside == event_location(2, 0, 0, 0, 1));
        FT_REQUIRE(text.evaluate_prefix(1, 1) == prefix_summary(0, 0));
        require_matches_model(text, model);

        // Editing does not disturb the retained version.
        const auto edited = text.insert_at(0, '"');
        FT_REQUIRE(text.evaluate(0) == prefix_summary(0, 2));
        FT_REQUIRE(edited.evaluate(0) == prefix_summary(1, 1));
        require_matches_model(text, model);
        require_matches_model(edited, characters("\"\"a,b\",c,d"));
    });

    tests.add("Contextual composition is associative but not commutative", [] {
        const auto reset = reset_shift_sequence{'r'};
        const auto shift = reset_shift_sequence{'s'};
        const auto reset_then_shift = reset.concat(shift);
        const auto shift_then_reset = shift.concat(reset);

        FT_REQUIRE(reset_then_shift.to_vector() == characters("rs"));
        FT_REQUIRE(shift_then_reset.to_vector() == characters("sr"));

        // From state 2 the two orders disagree in both summary components.
        FT_REQUIRE(reset_then_shift.evaluate(2) == prefix_summary(1, 0));
        FT_REQUIRE(shift_then_reset.evaluate(2) == prefix_summary(0, 1));
        FT_REQUIRE(!(reset_then_shift.evaluate(2) == shift_then_reset.evaluate(2)));

        // The quoted machine disagrees only in the event count, which is the subtler case.
        const auto quote = quoted_sequence{'"'};
        const auto comma = quoted_sequence{','};
        FT_REQUIRE(quote.concat(comma).evaluate(0) == prefix_summary(1, 0));
        FT_REQUIRE(comma.concat(quote).evaluate(0) == prefix_summary(1, 1));

        const auto left = reset_shift_sequence::from_range(characters("ssr"));
        const auto middle = reset_shift_sequence::from_range(characters("rss"));
        const auto right = reset_shift_sequence::from_range(characters("sss"));
        require_ordered_composition(left, middle);
        require_ordered_composition(middle, left);
        require_ordered_composition(left.concat(middle), right);
        require_ordered_composition(left, middle.concat(right));

        for (auto state = std::size_t{0}; state != reset_shift_machine::state_count; ++state) {
            FT_REQUIRE(
                left.concat(middle).concat(right).evaluate(state)
                == left.concat(middle.concat(right)).evaluate(state));
        }

        require_matches_model(left.concat(middle).concat(right), characters("ssrrsssss"));
    });

    tests.add("Contextual exhaustive small words match an independent scanner", [] {
        const auto alphabet = characters("\",x");
        for (auto length = std::size_t{0}; length != 8; ++length) {
            auto words = std::size_t{1};
            for (auto power = std::size_t{0}; power != length; ++power) {
                words *= alphabet.size();
            }

            for (auto code = std::size_t{0}; code != words; ++code) {
                auto remaining = code;
                auto word = std::vector<char>{};
                word.reserve(length);
                for (auto position = std::size_t{0}; position != length; ++position) {
                    word.push_back(alphabet[remaining % alphabet.size()]);
                    remaining /= alphabet.size();
                }

                require_matches_model(quoted_sequence::from_range(word), word);
            }
        }
    });

    tests.add("Contextual retained branches match a list-and-machine model", [] {
        deterministic_rng random{0x434f4e5445585455ULL};

        struct version final {
            ternary_sequence sequence;
            std::vector<int> model;
        };

        auto versions = std::vector<version>{version{ternary_sequence{}, {}}};
        for (auto step = 0; step != 600; ++step) {
            const auto parent_index = random.next_index(versions.size());
            const auto parent = versions[parent_index].sequence;
            auto model = versions[parent_index].model;

            auto next = parent;
            switch (random.next_index(8)) {
            case 0: {
                const auto item = random.next_int(-9, 9);
                model.insert(model.begin(), item);
                next = parent.push_front(item);
                break;
            }
            case 1: {
                const auto item = random.next_int(-9, 9);
                model.push_back(item);
                next = parent.push_back(item);
                break;
            }
            case 2: {
                const auto item = random.next_int(-9, 9);
                const auto index = random.next_index(model.size() + 1);
                model.insert(model.begin() + static_cast<std::ptrdiff_t>(index), item);
                next = parent.insert_at(index, item);
                break;
            }
            case 3:
                if (!model.empty()) {
                    const auto index = random.next_index(model.size());
                    const auto item = random.next_int(-9, 9);
                    model[index] = item;
                    next = parent.set_item(index, item);
                }
                break;
            case 4:
                if (!model.empty()) {
                    const auto index = random.next_index(model.size());
                    model.erase(model.begin() + static_cast<std::ptrdiff_t>(index));
                    next = parent.remove_at(index);
                }
                break;
            case 5: {
                const auto index = random.next_index(model.size() + 1);
                const auto count = random.next_index(model.size() - index + 1);
                next = parent.get_range(index, count);
                model = std::vector<int>{
                    model.begin() + static_cast<std::ptrdiff_t>(index),
                    model.begin() + static_cast<std::ptrdiff_t>(index + count)};
                break;
            }
            case 6: {
                const auto boundary = random.next_index(model.size() + 1);
                const auto parts = parent.split_at(boundary);
                FT_REQUIRE_EQUAL(parts.left.size() + parts.right.size(), model.size());
                next = parts.left.concat(parts.right);
                break;
            }
            default: {
                const auto suffix_index = random.next_index(versions.size());
                const auto& suffix = versions[suffix_index];
                if (model.size() + suffix.model.size() <= 96) {
                    model.insert(model.end(), suffix.model.begin(), suffix.model.end());
                    next = parent.concat(suffix.sequence);
                }
                break;
            }
            }

            require_matches_model(next, model);
            versions.push_back(version{next, model});
            if (versions.size() > 48) {
                versions.erase(
                    versions.begin()
                    + static_cast<std::ptrdiff_t>(1 + random.next_index(versions.size() - 2)));
            }
        }

        // Every retained branch is still exactly what it was when it was recorded.
        for (const auto& retained : versions) {
            require_matches_model(retained.sequence, retained.model);
        }
    });

    tests.add("Contextual boundaries, multi-event transitions, and errors are exact", [] {
        const auto model = std::vector<int>{2, -3, 4, 0};
        const auto sequence = ternary_sequence::create(std::span<const int>{model});
        const auto empty = ternary_sequence{};
        require_matches_model(sequence, model);

        for (auto index = std::size_t{0}; index <= sequence.size(); ++index) {
            const auto parts = sequence.split_at(index);
            FT_REQUIRE(parts.left.concat(parts.right).to_vector() == model);
            FT_REQUIRE_EQUAL(parts.left.size() + parts.right.size(), sequence.size());
        }

        FT_REQUIRE(sequence.shares_root_with(sequence.concat(empty)));
        FT_REQUIRE(sequence.shares_root_with(empty.concat(sequence)));
        FT_REQUIRE(sequence.shares_root_with(sequence.get_range(0, sequence.size())));
        FT_REQUIRE(sequence.shares_root_with(sequence.split_at(0).right));
        FT_REQUIRE(sequence.shares_root_with(sequence.split_at(sequence.size()).left));
        FT_REQUIRE(ternary_sequence::from_range(sequence).shares_root_with(sequence));

        // The ternary machine must actually exercise a multi-event transition.
        auto multi_event = false;
        for (auto index = std::size_t{0}; index != sequence.size(); ++index) {
            multi_event = multi_event
                || sequence.event_rank(index + 1, 0) - sequence.event_rank(index, 0) > 1;
        }
        FT_REQUIRE(multi_event);

        const auto located = sequence.try_select_event(0, 0);
        FT_REQUIRE(located.has_value());
        FT_REQUIRE(located->event_index_in_element < located->element_event_count);

        FT_REQUIRE_THROWS(std::out_of_range, sequence.evaluate(3));
        FT_REQUIRE_THROWS(std::out_of_range, sequence.evaluate_prefix(0, 3));
        FT_REQUIRE_THROWS(std::out_of_range, sequence.evaluate_prefix(sequence.size() + 1, 0));
        FT_REQUIRE_THROWS(std::out_of_range, sequence.event_rank(sequence.size() + 1, 0));
        FT_REQUIRE_THROWS(std::out_of_range, sequence.at(sequence.size()));
        FT_REQUIRE_THROWS(std::out_of_range, sequence.insert_at(sequence.size() + 1, 0));
        FT_REQUIRE_THROWS(std::out_of_range, sequence.set_item(sequence.size(), 0));
        FT_REQUIRE_THROWS(std::out_of_range, sequence.remove_at(sequence.size()));
        FT_REQUIRE_THROWS(std::out_of_range, sequence.split_at(sequence.size() + 1));
        FT_REQUIRE_THROWS(std::out_of_range, sequence.get_range(1, sequence.size()));
        FT_REQUIRE_THROWS(
            std::out_of_range,
            sequence.get_range(0, (std::numeric_limits<std::size_t>::max)()));
        FT_REQUIRE_THROWS(
            std::out_of_range,
            sequence.get_range((std::numeric_limits<std::size_t>::max)(), 1));

        // An out-of-range state or rank is an ordinary miss for select, unlike every other member.
        FT_REQUIRE(!sequence.try_select_event(0, 3).has_value());
        FT_REQUIRE(!sequence.try_select_event(-1, 0).has_value());
        FT_REQUIRE(!sequence.try_select_event(sequence.evaluate(0).event_count, 0).has_value());

        FT_REQUIRE(empty.empty());
        FT_REQUIRE_EQUAL(empty.size(), std::size_t{0});
        FT_REQUIRE(empty.evaluate(2) == prefix_summary(2, 0));
        FT_REQUIRE(empty.evaluate_prefix(0, 1) == prefix_summary(1, 0));
        FT_REQUIRE(!empty.try_select_event(0, 0).has_value());
        FT_REQUIRE(empty.get_range(0, 0).empty());
        FT_REQUIRE(empty.validate_structure().has_value());
        FT_REQUIRE_EQUAL(empty.validate_structure()->count, std::size_t{0});
        FT_REQUIRE(empty.begin() == empty.end());

        const auto singleton = empty.push_back(5);
        FT_REQUIRE_EQUAL(singleton.size(), std::size_t{1});
        FT_REQUIRE_EQUAL(singleton.at(0), 5);
        FT_REQUIRE(singleton.remove_at(0).empty());
        require_matches_model(singleton, std::vector<int>{5});
        require_matches_model(
            ternary_sequence{1, 2, 3},
            std::vector<int>{1, 2, 3});
        require_matches_model(
            ternary_sequence::empty_sequence().append(1).prepend(2).set_at(0, 7),
            std::vector<int>{7, 1});

        auto streamed = std::vector<int>{};
        for (auto cursor = sequence.cbegin(); cursor != sequence.cend(); ++cursor) {
            streamed.push_back(*cursor);
        }
        FT_REQUIRE(streamed == model);
    });

    tests.add("Contextual cached rank and select do not rescan the input", [] {
        using counting_sequence = contextual_rank_sequence<int, counting_machine>;
        auto values = std::vector<int>{};
        values.reserve(4'096);
        for (auto value = 0; value != 4'096; ++value) {
            values.push_back(value);
        }

        const auto sequence = counting_sequence::from_range(values);
        counting_calls.store(0, std::memory_order_relaxed);

        for (auto boundary = std::size_t{0}; boundary <= sequence.size(); boundary += 17) {
            (void)sequence.event_rank(boundary, boundary & std::size_t{1});
        }

        (void)sequence.evaluate(0);
        (void)sequence.size();
        (void)sequence.at(sequence.size() / 2);
        FT_REQUIRE_EQUAL(counting_calls.load(std::memory_order_relaxed), std::uint64_t{0});

        const auto total = sequence.evaluate(0).event_count;
        FT_REQUIRE(total > 0);
        auto successful = std::uint64_t{0};
        for (auto rank = std::int64_t{0}; rank < total; rank += 31) {
            FT_REQUIRE(sequence.try_select_event(rank, 0).has_value());
            ++successful;
        }

        // Each successful select runs exactly one transition: one measure-directed descent
        // recovers both the boundary element and the prefix summary at it.
        FT_REQUIRE_EQUAL(counting_calls.load(std::memory_order_relaxed), successful);

        FT_REQUIRE(!sequence.try_select_event(total, 0).has_value());
        FT_REQUIRE_EQUAL(counting_calls.load(std::memory_order_relaxed), successful);
    });

    tests.add("Contextual machine-contract violations and overflow are failure atomic", [] {
        using invalid_sequence = contextual_rank_sequence<int, invalid_state_machine>;
        const auto invalid = invalid_sequence{};
        FT_REQUIRE_THROWS(std::logic_error, invalid.append(1));
        FT_REQUIRE(invalid.empty());

        using negative_sequence = contextual_rank_sequence<int, negative_event_machine>;
        const auto negative = negative_sequence{};
        FT_REQUIRE_THROWS(std::logic_error, negative.append(1));
        FT_REQUIRE(negative.empty());

        using huge_sequence = contextual_rank_sequence<int, huge_event_machine>;
        const auto source = huge_sequence{}.push_back(1);
        FT_REQUIRE(
            source.evaluate(0)
            == prefix_summary(0, (std::numeric_limits<std::int64_t>::max)()));
        FT_REQUIRE_THROWS(std::overflow_error, source.push_back(2));
        FT_REQUIRE_THROWS(std::overflow_error, source.push_front(2));
        FT_REQUIRE_THROWS(std::overflow_error, source.concat(source));
        FT_REQUIRE_THROWS(std::overflow_error, source.insert_at(0, 2));

        // The rejected successors published nothing; the retained version is untouched.
        FT_REQUIRE(source.to_vector() == std::vector<int>{1});
        FT_REQUIRE(
            source.evaluate(0)
            == prefix_summary(0, (std::numeric_limits<std::int64_t>::max)()));
        FT_REQUIRE(source.validate_structure().has_value());
        FT_REQUIRE_EQUAL(
            source.validate_structure()->maximum_total_event_count,
            (std::numeric_limits<std::int64_t>::max)());
    });

    tests.add("Contextual validate_structure reports recomputed statistics", [] {
        const auto model = characters("\"a,b\",c,d");
        const auto sequence = quoted_sequence::from_range(model);
        const auto statistics = sequence.validate_structure();

        FT_REQUIRE(statistics.has_value());
        FT_REQUIRE_EQUAL(statistics->count, std::size_t{9});
        FT_REQUIRE_EQUAL(statistics->state_count, std::size_t{2});
        FT_REQUIRE_EQUAL(statistics->maximum_total_event_count, std::int64_t{2});
        FT_REQUIRE(*statistics == validation_statistics(9, 2, 2));
        const auto empty_statistics = quoted_sequence{}.validate_structure();
        FT_REQUIRE(empty_statistics.has_value());
        FT_REQUIRE(*empty_statistics == validation_statistics(0, 2, 0));
    });

    tests.add("Contextual independent readers observe retained snapshots concurrently", [] {
        auto values = std::vector<int>{};
        values.reserve(513);
        for (auto value = -256; value <= 256; ++value) {
            values.push_back(value);
        }

        const auto source = ternary_sequence::from_range(values);
        auto expected_summaries = std::vector<contextual_prefix_summary>{};
        for (auto state = std::size_t{0}; state != ternary_sequence::state_count; ++state) {
            expected_summaries.push_back(source.evaluate(state));
        }

        auto failed = std::atomic<bool>{false};
        {
            auto readers = std::vector<std::jthread>{};
            for (auto worker = std::size_t{0}; worker != 4; ++worker) {
                readers.emplace_back([&, worker] {
                    try {
                        auto branch = source;
                        for (auto iteration = std::size_t{0}; iteration != 120; ++iteration) {
                            const auto state = (worker + iteration) % expected_summaries.size();
                            if (!(source.evaluate(state) == expected_summaries[state])) {
                                throw std::logic_error("contextual reader summary mismatch");
                            }

                            const auto boundary =
                                (worker * 31 + iteration * 17) % (source.size() + 1);
                            (void)source.event_rank(boundary, state);
                            const auto total = expected_summaries[state].event_count;
                            if (total > 0
                                && !source
                                        .try_select_event(
                                            static_cast<std::int64_t>(worker + iteration)
                                                % total,
                                            state)
                                        .has_value()) {
                                throw std::logic_error("contextual reader select mismatch");
                            }

                            branch = branch.set_item(
                                iteration % branch.size(),
                                static_cast<int>(worker) - static_cast<int>(iteration));
                        }

                        if (branch.size() != source.size()) {
                            throw std::logic_error("contextual branch length mismatch");
                        }
                    } catch (...) {
                        failed.store(true, std::memory_order_relaxed);
                    }
                });
            }
        }

        FT_REQUIRE(!failed.load(std::memory_order_relaxed));
        require_matches_model(source, values);
    });
}
