/// Tests for the persistent ancestral connection forest.

#include <durable7/hamt/persistent_ancestral_connection_forest.hpp>
#include <durable7/test_support/headless_test_process.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using durable7::hamt::ancestral_connection_forest_statistics;
using durable7::hamt::ancestral_connection_version;
using durable7::hamt::ancestral_connection_vertex_hash;
using durable7::hamt::persistent_ancestral_connection_forest;

namespace {

class test_failure final : public std::runtime_error {
public:
    explicit test_failure(const std::string& message)
        : std::runtime_error(message) {
    }
};

[[noreturn]] void fail(const char* file, int line, const char* expression) {
    std::ostringstream message;
    message << file << ':' << line << ": check failed: " << expression;
    throw test_failure(message.str());
}

[[noreturn]] void fail_message(const char* file, int line, const std::string& message) {
    std::ostringstream full_message;
    full_message << file << ':' << line << ": " << message;
    throw test_failure(full_message.str());
}

#define CHECK(expression) \
    do { \
        if (!(expression)) { \
            fail(__FILE__, __LINE__, #expression); \
        } \
    } while (false)

#define CHECK_EQ(expected, actual) \
    do { \
        if (!((expected) == (actual))) { \
            fail(__FILE__, __LINE__, #expected " == " #actual); \
        } \
    } while (false)

#define CHECK_THROWS_AS(expression, exception_type) \
    do { \
        bool threw_expected_exception = false; \
        try { \
            (void)(expression); \
        } catch (const exception_type&) { \
            threw_expected_exception = true; \
        } catch (const std::exception& ex) { \
            fail_message(__FILE__, __LINE__, std::string("wrong exception type: ") + ex.what()); \
        } catch (...) { \
            fail_message(__FILE__, __LINE__, "wrong non-standard exception type"); \
        } \
        if (!threw_expected_exception) { \
            fail(__FILE__, __LINE__, #expression " throws " #exception_type); \
        } \
    } while (false)

struct test_case {
    const char* name;
    void (*run)();
};

std::vector<test_case>& registry() {
    static std::vector<test_case> tests;
    return tests;
}

struct registrar {
    registrar(const char* name, void (*run)()) {
        registry().push_back(test_case{name, run});
    }
};

#define CONCAT_INNER(left, right) left##right
#define CONCAT(left, right) CONCAT_INNER(left, right)
#define TEST(name) \
    void name(); \
    registrar CONCAT(registrar_, __LINE__)(#name, &name); \
    void name()

using forest_type = persistent_ancestral_connection_forest;
using version_type = ancestral_connection_version;

[[nodiscard]] forest_type forest(std::int32_t vertex_count) {
    return forest_type::create(vertex_count);
}

/// Whether an optional witness is present and denotes exactly the expected version.
[[nodiscard]] bool is_version(
    const std::optional<version_type>& actual,
    const version_type& expected) {
    return actual.has_value() && actual->is_same_version(expected);
}

[[nodiscard]] bool same_witness(
    const std::optional<version_type>& left,
    const std::optional<version_type>& right) {
    if (left.has_value() != right.has_value()) {
        return false;
    }
    return !left.has_value() || left->is_same_version(*right);
}

/// Sums one cached size per distinct current root, which must recover the whole universe.
[[nodiscard]] std::int64_t total_component_size(const forest_type& value) {
    std::unordered_map<std::int32_t, std::int32_t> roots;
    for (std::int32_t vertex = 0; vertex < value.vertex_count(); ++vertex) {
        roots[value.find(vertex)] = value.component_size(vertex);
    }

    CHECK_EQ(static_cast<std::size_t>(value.component_count()), roots.size());
    std::int64_t total = 0;
    for (const auto& entry : roots) {
        total += entry.second;
    }
    return total;
}

TEST(Create_ProducesSingletonComponentsAndRootHistory) {
    const auto root = forest(5);

    CHECK_EQ(std::int32_t{5}, root.vertex_count());
    CHECK_EQ(std::int32_t{5}, root.component_count());
    CHECK_EQ(std::uint64_t{0}, root.version().depth());
    CHECK(!root.version().parent().has_value());
    CHECK(root.version().is_same_version(root.version().root()));
    for (std::int32_t vertex = 0; vertex < root.vertex_count(); ++vertex) {
        CHECK_EQ(vertex, root.find(vertex));
        CHECK(root.connected(vertex, vertex));
        CHECK(is_version(root.first_connected(vertex, vertex), root.version()));
        CHECK_EQ(std::int32_t{1}, root.component_size(vertex));
    }

    CHECK(!root.connected(0, 1));
    CHECK(!root.first_connected(0, 1).has_value());

    const auto statistics = root.validate_structure();
    CHECK_EQ(std::size_t{0}, statistics.stored_cell_count);
    CHECK_EQ(std::int32_t{5}, statistics.component_count);
    CHECK_EQ(std::size_t{0}, statistics.maximum_parent_path_length);
    CHECK_EQ(std::uint64_t{0}, statistics.history_depth);

    const auto empty = forest(0);
    CHECK_EQ(std::int32_t{0}, empty.vertex_count());
    CHECK_EQ(std::int32_t{0}, empty.component_count());
    (void)empty.validate_structure();

    CHECK_THROWS_AS(forest_type::create(-1), std::invalid_argument);
}

TEST(Link_RecordsTheFirstConnectionVersion) {
    const auto root = forest(6);
    const auto first = root.link(0, 1);
    const auto second = first.link(2, 3);
    const auto bridge = second.link(0, 2);
    const auto attach = bridge.link(4, 0);

    CHECK_EQ(std::int32_t{2}, attach.component_count());
    CHECK(is_version(attach.first_connected(0, 1), first.version()));
    CHECK(is_version(attach.first_connected(2, 3), second.version()));
    CHECK(is_version(attach.first_connected(0, 3), bridge.version()));
    CHECK(is_version(attach.first_connected(1, 2), bridge.version()));
    CHECK(is_version(attach.first_connected(4, 1), attach.version()));
    CHECK(is_version(attach.first_connected(4, 4), root.version()));
    CHECK(!attach.first_connected(0, 5).has_value());

    CHECK_EQ(std::int32_t{0}, attach.find(0));
    CHECK_EQ(std::int32_t{0}, attach.find(4));

    // Every retained version keeps its own connectivity, untouched by later descendants.
    CHECK_EQ(std::int32_t{6}, root.component_count());
    CHECK(!root.connected(0, 1));
    CHECK(!first.connected(0, 2));
    CHECK_EQ(std::int32_t{5}, first.component_count());
    CHECK(!first.first_connected(0, 2).has_value());
    CHECK(is_version(first.first_connected(0, 1), first.version()));

    (void)root.validate_structure();
    (void)first.validate_structure();
    (void)second.validate_structure();
    (void)bridge.validate_structure();
    (void)attach.validate_structure();
}

TEST(RedundantLinksCreateVersionsAndShareConnectivityState) {
    const auto root = forest(4);
    const auto linked = root.link(0, 1);
    const auto reverse_duplicate = linked.link(1, 0);
    const auto sibling_duplicate = linked.link(1, 0);
    const auto self_duplicate = reverse_duplicate.link(0, 0);

    CHECK_EQ(std::uint64_t{1}, linked.version().depth());
    CHECK_EQ(std::uint64_t{2}, reverse_duplicate.version().depth());
    CHECK_EQ(std::uint64_t{3}, self_duplicate.version().depth());
    CHECK(is_version(reverse_duplicate.version().parent(), linked.version()));
    CHECK(is_version(sibling_duplicate.version().parent(), linked.version()));
    CHECK(is_version(self_duplicate.version().parent(), reverse_duplicate.version()));
    CHECK(!(linked.version() == reverse_duplicate.version()));
    CHECK(!(reverse_duplicate.version() == sibling_duplicate.version()));
    CHECK_EQ(linked.component_count(), reverse_duplicate.component_count());
    CHECK_EQ(linked.component_count(), self_duplicate.component_count());
    CHECK(linked.shares_connectivity_root_with(reverse_duplicate));
    CHECK(reverse_duplicate.shares_connectivity_root_with(self_duplicate));
    CHECK(!root.shares_connectivity_root_with(linked));
    CHECK(is_version(self_duplicate.first_connected(0, 1), linked.version()));
    CHECK(is_version(self_duplicate.first_connected(2, 2), root.version()));
    (void)self_duplicate.validate_structure();
    (void)sibling_duplicate.validate_structure();
}

TEST(SiblingBranchesKeepEqualDepthVersionTagsDistinct) {
    const auto root = forest(5);
    const auto common = root.link(0, 1);
    const auto left = common.link(0, 2);
    const auto right = common.link(0, 3);
    const auto left_later = left.link(2, 4);
    const auto right_later = right.link(3, 4);

    CHECK_EQ(left.version().depth(), right.version().depth());
    CHECK(!(left.version() == right.version()));
    CHECK(!left.version().is_same_version(right.version()));
    CHECK(is_version(left.first_connected(0, 1), common.version()));
    CHECK(is_version(right.first_connected(0, 1), common.version()));
    CHECK(is_version(left_later.first_connected(1, 2), left.version()));
    CHECK(is_version(right_later.first_connected(1, 3), right.version()));
    CHECK(is_version(left_later.first_connected(0, 4), left_later.version()));
    CHECK(is_version(right_later.first_connected(0, 4), right_later.version()));
    CHECK(!left_later.connected(0, 3));
    CHECK(!right_later.connected(0, 2));
    CHECK(left_later.version().root().is_same_version(root.version()));
    CHECK(right_later.version().root().is_same_version(root.version()));

    // Identity, not structure: equal depth plus equal history root is not equality, and the hash
    // specialization agrees with the equality it is paired with.
    std::unordered_set<version_type> versions;
    versions.insert(left.version());
    versions.insert(right.version());
    CHECK_EQ(std::size_t{2}, versions.size());
    versions.insert(left.version().root());
    versions.insert(right.version().root());
    CHECK_EQ(std::size_t{3}, versions.size());
    CHECK(versions.find(common.version()) == versions.end());

    (void)left_later.validate_structure();
    (void)right_later.validate_structure();
}

TEST(FirstConnectedUsesPairLcaRatherThanLatestComponentBirth) {
    const auto root = forest(8);
    const auto ab = root.link(0, 1);
    const auto cd = ab.link(2, 3);
    const auto abcd = cd.link(1, 3);
    const auto ef = abcd.link(4, 5);
    const auto gh = ef.link(6, 7);
    const auto efgh = gh.link(4, 6);
    const auto all = efgh.link(0, 4);

    CHECK(is_version(all.first_connected(0, 1), ab.version()));
    CHECK(is_version(all.first_connected(2, 3), cd.version()));
    CHECK(is_version(all.first_connected(0, 2), abcd.version()));
    CHECK(is_version(all.first_connected(4, 5), ef.version()));
    CHECK(is_version(all.first_connected(6, 7), gh.version()));
    CHECK(is_version(all.first_connected(5, 7), efgh.version()));
    CHECK(is_version(all.first_connected(1, 6), all.version()));
    (void)all.validate_structure();
}

TEST(FirstConnectedIsSymmetricInItsEndpoints) {
    // The witness fold keeps the left candidate on an equal-depth tie. A tie is unreachable (one
    // link labels at most one edge, so no two distinct edges of a lineage share a depth), and the
    // observable consequence of the rule being well defined is that swapping the endpoints — which
    // swaps which parent path the fold visits first — cannot change the answer.
    const auto root = forest(10);
    auto current = root;
    const std::pair<std::int32_t, std::int32_t> edges[] = {
        {0, 1}, {2, 3}, {0, 2}, {4, 5}, {6, 7}, {4, 6}, {8, 9}, {0, 8}, {5, 9}, {1, 7},
    };
    for (const auto& edge : edges) {
        current = current.link(edge.first, edge.second);
        for (std::int32_t left = 0; left < current.vertex_count(); ++left) {
            for (std::int32_t right = 0; right < current.vertex_count(); ++right) {
                CHECK(same_witness(
                    current.first_connected(left, right),
                    current.first_connected(right, left)));
            }
        }
    }

    CHECK_EQ(std::int32_t{1}, current.component_count());
    (void)current.validate_structure();
}

TEST(FirstConnectedHandlesOneEndpointAtLcaAfterThatLcaBecomesAChild) {
    const auto root = forest(5);
    const auto pair = root.link(0, 1);        // 1 -> 0
    const auto other_pair = pair.link(2, 3);  // 3 -> 2
    const auto larger = other_pair.link(2, 4);
    const auto merged = larger.link(2, 0);    // the component rooted at 0 becomes a child of 2

    CHECK_EQ(std::int32_t{2}, merged.find(0));
    CHECK(is_version(merged.first_connected(0, 1), pair.version()));
    CHECK(is_version(merged.first_connected(0, 2), merged.version()));
    CHECK(is_version(merged.first_connected(1, 4), merged.version()));
    (void)merged.validate_structure();
}

TEST(BalancedUnionsRespectUnionBySizeHeightAndRetainWitnesses) {
    constexpr std::int32_t count = 1024;
    const auto root = forest(count);
    auto current = root;
    std::optional<version_type> first_pair_version;
    std::optional<version_type> final_bridge_version;

    for (std::int32_t width = 1; width < count; width *= 2) {
        for (std::int32_t start = 0; start < count; start += width * 2) {
            current = current.link(start, start + width);
            if (start == 0 && width == 1) {
                first_pair_version = current.version();
            }
            if (start == 0 && width == count / 2) {
                final_bridge_version = current.version();
            }
        }
    }

    CHECK(first_pair_version.has_value());
    CHECK(final_bridge_version.has_value());
    CHECK_EQ(std::int32_t{1}, current.component_count());
    CHECK_EQ(std::int32_t{0}, current.find(count - 1));
    CHECK(same_witness(current.first_connected(0, 1), first_pair_version));
    CHECK(same_witness(current.first_connected(0, count - 1), final_bridge_version));
    CHECK_EQ(count, root.component_count());

    const auto statistics = current.validate_structure();
    CHECK_EQ(static_cast<std::size_t>(count), statistics.stored_cell_count);
    CHECK_EQ(std::int32_t{1}, statistics.component_count);
    CHECK_EQ(std::size_t{10}, statistics.maximum_parent_path_length);
    CHECK_EQ(static_cast<std::uint64_t>(count) - 1, statistics.history_depth);
}

struct model_version final {
    forest_type actual;
    std::vector<std::int32_t> classes;
    std::size_t parent;
    bool has_parent;
};

[[nodiscard]] std::vector<std::size_t> lineage_of(
    const std::vector<model_version>& states,
    std::size_t index) {
    std::vector<std::size_t> lineage;
    auto current = index;
    while (true) {
        lineage.push_back(current);
        if (!states[current].has_parent) {
            break;
        }
        current = states[current].parent;
    }

    std::reverse(lineage.begin(), lineage.end());
    return lineage;
}

/// The naive reference: the earliest ancestor version, scanning the retained history from the
/// history root forward, whose equivalence classes already agree on the pair.
[[nodiscard]] std::optional<version_type> first_connected_by_scan(
    const std::vector<model_version>& states,
    const std::vector<std::size_t>& lineage,
    std::int32_t left,
    std::int32_t right) {
    for (const auto position : lineage) {
        const auto& candidate = states[position];
        if (candidate.classes[static_cast<std::size_t>(left)]
            == candidate.classes[static_cast<std::size_t>(right)]) {
            return candidate.actual.version();
        }
    }

    return std::nullopt;
}

TEST(RandomBranchingHistoriesMatchNaiveAncestorScan) {
    constexpr std::int32_t vertex_count = 12;
    constexpr int operation_count = 220;
    std::uint64_t random_state = 0x5EED'2026ull;
    const auto next_random = [&random_state]() -> std::size_t {
        random_state = random_state * 6'364'136'223'846'793'005ull
            + 1'442'695'040'888'963'407ull;
        return static_cast<std::size_t>(random_state >> 33);
    };

    std::vector<model_version> states;
    {
        std::vector<std::int32_t> classes;
        for (std::int32_t vertex = 0; vertex < vertex_count; ++vertex) {
            classes.push_back(vertex);
        }
        states.push_back(model_version{forest(vertex_count), std::move(classes), 0, false});
    }

    for (int operation = 0; operation < operation_count; ++operation) {
        const auto parent_index = next_random() % states.size();
        const auto left = static_cast<std::int32_t>(
            next_random() % static_cast<std::size_t>(vertex_count));
        const auto right = static_cast<std::int32_t>(
            next_random() % static_cast<std::size_t>(vertex_count));

        auto classes = states[parent_index].classes;
        const auto left_class = classes[static_cast<std::size_t>(left)];
        const auto right_class = classes[static_cast<std::size_t>(right)];
        if (left_class != right_class) {
            for (auto& value : classes) {
                if (value == right_class) {
                    value = left_class;
                }
            }
        }

        auto actual = states[parent_index].actual.link(left, right);
        CHECK(is_version(actual.version().parent(), states[parent_index].actual.version()));
        CHECK_EQ(states[parent_index].actual.version().depth() + 1, actual.version().depth());
        CHECK(actual.version().root().is_same_version(states[0].actual.version()));

        std::unordered_set<std::int32_t> distinct(classes.begin(), classes.end());
        CHECK_EQ(static_cast<std::size_t>(actual.component_count()), distinct.size());

        const auto index = states.size();
        states.push_back(model_version{std::move(actual), std::move(classes), parent_index, true});

        const auto lineage = lineage_of(states, index);
        for (std::int32_t query_left = 0; query_left < vertex_count; ++query_left) {
            for (std::int32_t query_right = 0; query_right < vertex_count; ++query_right) {
                const auto& state = states[index];
                const auto expected_connected =
                    state.classes[static_cast<std::size_t>(query_left)]
                    == state.classes[static_cast<std::size_t>(query_right)];
                CHECK_EQ(expected_connected, state.actual.connected(query_left, query_right));

                const auto witness = state.actual.first_connected(query_left, query_right);
                CHECK(same_witness(
                    witness,
                    first_connected_by_scan(states, lineage, query_left, query_right)));
                CHECK(same_witness(
                    witness,
                    state.actual.first_connected(query_right, query_left)));
                CHECK_EQ(expected_connected, witness.has_value());
            }
        }

        if (operation % 25 == 0) {
            (void)states[index].actual.validate_structure();
        }
    }

    // Recheck retained versions after every sibling branch has been constructed.
    for (int probe = 0; probe < 120; ++probe) {
        const auto index = next_random() % states.size();
        const auto left = static_cast<std::int32_t>(
            next_random() % static_cast<std::size_t>(vertex_count));
        const auto right = static_cast<std::int32_t>(
            next_random() % static_cast<std::size_t>(vertex_count));
        const auto lineage = lineage_of(states, index);
        CHECK(same_witness(
            states[index].actual.first_connected(left, right),
            first_connected_by_scan(states, lineage, left, right)));
        (void)states[index].actual.validate_structure();
    }
}

TEST(LongRedundantHistoryDoesNotReplaceTheSuccessfulUnionWitness) {
    const auto root = forest(3);
    const auto first = root.link(0, 1);
    auto current = first;
    for (int index = 0; index < 20'000; ++index) {
        current = current.link(2, 2);
    }

    CHECK_EQ(std::uint64_t{20'001}, current.version().depth());
    CHECK(is_version(current.first_connected(0, 1), first.version()));
    CHECK(!current.first_connected(0, 2).has_value());
    CHECK(first.shares_connectivity_root_with(current));
    CHECK(current.version().root().is_same_version(root.version()));

    const auto statistics = current.validate_structure();
    CHECK_EQ(std::uint64_t{20'001}, statistics.history_depth);
    CHECK_EQ(std::size_t{2}, statistics.stored_cell_count);
}

TEST(DeepHistoryChainsAreReleasedWithoutRecursion) {
    // A version chain is a linked list of shared nodes. Recursive release would overflow the stack
    // well before this depth, so reaching the assertions after the scope ends is the test.
    constexpr int depth = 150'000;
    version_type retained_root = forest(2).version();
    {
        auto current = forest(2);
        retained_root = current.version();
        for (int index = 0; index < depth; ++index) {
            current = current.link(0, 0);
        }

        CHECK_EQ(static_cast<std::uint64_t>(depth), current.version().depth());
        CHECK(current.version().root().is_same_version(retained_root));

        // Walking the chain is iterative too, and stays independent of the query cost.
        std::uint64_t walked = 0;
        for (auto step = std::optional<version_type>(current.version());
             step.has_value();
             step = step->parent()) {
            ++walked;
        }
        CHECK_EQ(static_cast<std::uint64_t>(depth) + 1, walked);
    }

    // The root outlives the released chain and stays a well-formed history root.
    CHECK_EQ(std::uint64_t{0}, retained_root.depth());
    CHECK(!retained_root.parent().has_value());
    CHECK(retained_root.root().is_same_version(retained_root));

    // A branching chain releases correctly too: the retained middle keeps its own suffix alive.
    auto middle = forest(2);
    {
        auto current = forest(2);
        for (int index = 0; index < 60'000; ++index) {
            current = current.link(0, 0);
            if (index == 30'000) {
                middle = current;
            }
        }
    }
    CHECK_EQ(std::uint64_t{30'001}, middle.version().depth());
    (void)middle.validate_structure();
}

TEST(HugeSparseUniverseStoresOnlyVerticesTouchedByAUnion) {
    constexpr auto maximum = (std::numeric_limits<std::int32_t>::max)();
    const auto root = forest(maximum);
    const auto root_statistics = root.validate_structure();
    CHECK_EQ(std::size_t{0}, root_statistics.stored_cell_count);
    CHECK_EQ(maximum, root_statistics.component_count);

    const auto linked = root.link(0, maximum - 1);
    CHECK_EQ(maximum - 1, linked.component_count());
    CHECK(linked.connected(0, maximum - 1));
    CHECK(!linked.connected(0, maximum - 2));
    CHECK_EQ(maximum - 2, linked.find(maximum - 2));
    CHECK(is_version(linked.first_connected(0, maximum - 1), linked.version()));
    CHECK(!linked.first_connected(0, maximum - 2).has_value());

    const auto statistics = linked.validate_structure();
    CHECK_EQ(std::size_t{2}, statistics.stored_cell_count);
    CHECK_EQ(std::size_t{1}, statistics.maximum_parent_path_length);
}

TEST(OperationsRejectVerticesOutsideTheUniverse) {
    const auto root = forest(3);

    CHECK_THROWS_AS(root.find(-1), std::out_of_range);
    CHECK_THROWS_AS(root.find(3), std::out_of_range);
    CHECK_THROWS_AS(root.connected(-1, 0), std::out_of_range);
    CHECK_THROWS_AS(root.connected(0, 3), std::out_of_range);
    CHECK_THROWS_AS(root.link(-1, 0), std::out_of_range);
    CHECK_THROWS_AS(root.link(0, 3), std::out_of_range);
    CHECK_THROWS_AS(root.first_connected(-1, 0), std::out_of_range);
    CHECK_THROWS_AS(root.first_connected(0, 3), std::out_of_range);
    CHECK_THROWS_AS(root.component_size(-1), std::out_of_range);
    CHECK_THROWS_AS(root.component_size(3), std::out_of_range);
    CHECK_THROWS_AS(
        root.first_connected((std::numeric_limits<std::int32_t>::min)(), 0),
        std::out_of_range);

    // A rejected operation publishes nothing: no child version, no component change, no cells.
    CHECK(root.version().is_same_version(root.version().root()));
    CHECK_EQ(std::uint64_t{0}, root.version().depth());
    CHECK_EQ(std::int32_t{3}, root.component_count());
    const auto statistics = root.validate_structure();
    CHECK_EQ(std::size_t{0}, statistics.stored_cell_count);

    // A disconnected pair is an absent result, not an error: the two failure modes stay distinct.
    const auto linked = root.link(0, 1);
    CHECK(!linked.first_connected(0, 2).has_value());
    CHECK(is_version(linked.first_connected(0, 1), linked.version()));
}

TEST(ComponentSizeReportsTheCachedUnionSize) {
    const auto root = forest(6);
    for (std::int32_t vertex = 0; vertex < root.vertex_count(); ++vertex) {
        CHECK_EQ(std::int32_t{1}, root.component_size(vertex));
    }
    CHECK_EQ(std::int64_t{6}, total_component_size(root));

    const auto pair = root.link(0, 1);
    const auto triple = pair.link(1, 2);
    const auto quad = triple.link(2, 3);

    CHECK_EQ(std::int32_t{2}, pair.component_size(0));
    CHECK_EQ(std::int32_t{2}, pair.component_size(1));
    CHECK_EQ(std::int32_t{1}, pair.component_size(5));
    CHECK_EQ(std::int32_t{3}, triple.component_size(2));
    for (std::int32_t vertex = 0; vertex < 4; ++vertex) {
        CHECK_EQ(std::int32_t{4}, quad.component_size(vertex));
    }
    CHECK_EQ(std::int32_t{1}, quad.component_size(4));
    CHECK_EQ(std::int64_t{6}, total_component_size(quad));

    // A redundant link changes nothing observable about sizes.
    const auto redundant = quad.link(3, 0);
    for (std::int32_t vertex = 0; vertex < quad.vertex_count(); ++vertex) {
        CHECK_EQ(quad.component_size(vertex), redundant.component_size(vertex));
    }
    CHECK_EQ(std::int64_t{6}, total_component_size(redundant));

    // Retained versions keep their own sizes across a branching history.
    const auto branch = quad.link(4, 5);
    CHECK_EQ(std::int32_t{2}, branch.component_size(4));
    CHECK_EQ(std::int32_t{1}, quad.component_size(4));
    CHECK_EQ(std::int32_t{1}, pair.component_size(2));
    CHECK_EQ(std::int32_t{1}, root.component_size(0));
    CHECK_EQ(std::int64_t{6}, total_component_size(branch));
    CHECK_EQ(std::int64_t{6}, total_component_size(pair));

    const auto merged = branch.link(5, 0);
    CHECK_EQ(std::int32_t{1}, merged.component_count());
    for (std::int32_t vertex = 0; vertex < merged.vertex_count(); ++vertex) {
        CHECK_EQ(std::int32_t{6}, merged.component_size(vertex));
    }
    CHECK_EQ(std::int64_t{6}, total_component_size(merged));
    (void)merged.validate_structure();

    // A huge sparse universe still reports singletons without storing them.
    constexpr auto maximum = (std::numeric_limits<std::int32_t>::max)();
    const auto sparse = forest(maximum).link(0, maximum - 1);
    CHECK_EQ(std::int32_t{2}, sparse.component_size(0));
    CHECK_EQ(std::int32_t{2}, sparse.component_size(maximum - 1));
    CHECK_EQ(std::int32_t{1}, sparse.component_size(maximum - 2));
}

TEST(SuccessfulUnionRebuildsOnlyTheTouchedCellsAndTiesKeepTheFirstEndpoint) {
    const auto root = forest(16);
    const auto first = root.link(0, 1);
    const auto second = first.link(2, 3);

    // A successful union publishes a new connectivity index; a redundant one does not.
    CHECK(!first.shares_connectivity_root_with(second));
    CHECK(second.shares_connectivity_root_with(second.link(0, 1)));
    CHECK(second.shares_connectivity_root_with(second.link(3, 2)));
    CHECK(second.shares_connectivity_root_with(second.link(2, 2)));

    // Union by size: on a tie the first endpoint's root stays the representative.
    CHECK_EQ(std::int32_t{0}, second.find(1));
    CHECK_EQ(std::int32_t{2}, second.find(3));
    const auto merged = second.link(3, 1);
    CHECK_EQ(std::int32_t{2}, merged.find(0));
    CHECK_EQ(std::int32_t{13}, merged.component_count());
    const auto mirrored = second.link(1, 3);
    CHECK_EQ(std::int32_t{0}, mirrored.find(2));

    // The smaller component always attaches to the larger one, tie or not.
    const auto larger = merged.link(4, 5).link(4, 6);
    CHECK_EQ(std::int32_t{3}, larger.component_size(4));
    const auto absorbed = larger.link(4, 8);
    CHECK_EQ(std::int32_t{4}, absorbed.find(8));
    (void)merged.validate_structure();
    (void)mirrored.validate_structure();
    (void)absorbed.validate_structure();
}

TEST(SeparateForestsHaveSeparateVersionTrees) {
    const auto left_root = forest(2);
    const auto right_root = forest(2);
    const auto left = left_root.link(0, 1);
    const auto right = right_root.link(0, 1);

    CHECK(!(left_root.version() == right_root.version()));
    CHECK(!(left.version() == right.version()));
    CHECK_EQ(left.version().depth(), right.version().depth());
    CHECK(is_version(left.first_connected(0, 1), left.version()));
    CHECK(is_version(right.first_connected(0, 1), right.version()));
    CHECK(!same_witness(left.first_connected(0, 1), right.first_connected(0, 1)));
}

TEST(ValueSemanticsShareStructureAndSurviveAssignment) {
    const auto root = forest(6);
    const auto linked = root.link(0, 1).link(2, 3);

    const auto copy = linked;
    CHECK(copy.shares_connectivity_root_with(linked));
    CHECK(copy.version().is_same_version(linked.version()));
    CHECK_EQ(linked.component_count(), copy.component_count());

    auto assigned = root;
    assigned = linked;
    CHECK(assigned.shares_connectivity_root_with(linked));
    CHECK(assigned.version().is_same_version(linked.version()));

    auto moved_source = linked;
    const auto moved = std::move(moved_source);
    CHECK(moved.shares_connectivity_root_with(linked));
    CHECK(moved.version().is_same_version(linked.version()));

    // The source of a copy keeps its own answers; branching from either handle is independent.
    const auto from_copy = copy.link(0, 2);
    CHECK(!linked.connected(0, 2));
    CHECK(from_copy.connected(0, 2));
    CHECK(is_version(from_copy.first_connected(0, 2), from_copy.version()));
    (void)from_copy.validate_structure();

    auto version_copy = linked.version();
    const auto version_assigned = version_copy;
    CHECK(version_assigned.is_same_version(linked.version()));
    version_copy = root.version();
    CHECK(version_copy.is_same_version(root.version()));
    CHECK(version_assigned.is_same_version(linked.version()));
}

[[nodiscard]] std::uint32_t inverse_xor_shift(std::uint32_t value, int shift) {
    auto result = value;
    for (int applied = shift; applied < 32; applied *= 2) {
        result = static_cast<std::uint32_t>(result ^ (result >> applied));
    }
    return result;
}

/// The multiplicative inverse of an odd word modulo 2^32, by Newton iteration.
[[nodiscard]] std::uint32_t modular_inverse(std::uint32_t value) {
    auto inverse = value;
    for (int step = 0; step < 5; ++step) {
        inverse = static_cast<std::uint32_t>(
            inverse * static_cast<std::uint32_t>(2u - value * inverse));
    }
    return inverse;
}

/// The explicit left inverse of the pinned vertex hash. Its existence is what makes the CHAMP
/// path factor a worst-case rather than an expected constant.
[[nodiscard]] std::uint32_t unmix_vertex_hash(std::uint32_t hash) {
    auto value = inverse_xor_shift(hash, 16);
    value = static_cast<std::uint32_t>(value * modular_inverse(0xc2b2'ae35u));
    value = inverse_xor_shift(value, 13);
    value = static_cast<std::uint32_t>(value * modular_inverse(0x85eb'ca6bu));
    value = inverse_xor_shift(value, 16);
    return value;
}

TEST(VertexHashIsInjectiveSoTheChampPathIsWorstCaseBounded) {
    const ancestral_connection_vertex_hash hash;
    CHECK_EQ(std::uint32_t{1}, static_cast<std::uint32_t>(0x85eb'ca6bu & 1u));
    CHECK_EQ(std::uint32_t{1}, static_cast<std::uint32_t>(0xc2b2'ae35u & 1u));
    CHECK_EQ(std::uint32_t{1}, static_cast<std::uint32_t>(
        modular_inverse(0x85eb'ca6bu) * 0x85eb'ca6bu));
    CHECK_EQ(std::uint32_t{1}, static_cast<std::uint32_t>(
        modular_inverse(0xc2b2'ae35u) * 0xc2b2'ae35u));

    constexpr auto maximum = (std::numeric_limits<std::int32_t>::max)();
    constexpr auto minimum = (std::numeric_limits<std::int32_t>::min)();
    std::vector<std::int32_t> probes;
    for (std::uint32_t step = 0; step < 4'096u; ++step) {
        const auto vertex = static_cast<std::int32_t>(step);
        probes.push_back(vertex);
        probes.push_back(maximum - vertex);
        probes.push_back(minimum + vertex);
        // Vertices that agree on every low-order trie level, which is where an injective hash
        // matters most: they are discriminated only at the deepest bitmap level.
        probes.push_back(static_cast<std::int32_t>(step * 1'048'576u));
    }

    std::unordered_set<std::uint32_t> seen;
    for (const auto vertex : probes) {
        const auto value = hash(vertex);
        // The map truncates the policy result to 32 bits, so injectivity must hold after truncation.
        const auto truncated = static_cast<std::uint32_t>(value);
        CHECK_EQ(static_cast<std::uint32_t>(vertex), unmix_vertex_hash(truncated));
        seen.insert(truncated);
    }
    CHECK_EQ(std::unordered_set<std::int32_t>(probes.begin(), probes.end()).size(), seen.size());

    // A forest over hash-adjacent and widely spread vertices still validates, and every parent path
    // stays inside the union-by-size ceiling.
    auto current = forest(maximum);
    for (std::size_t index = 1; index < 64; ++index) {
        current = current.link(
            static_cast<std::int32_t>(index - 1) * 16'777'216,
            static_cast<std::int32_t>(index) * 16'777'216);
    }
    const auto statistics = current.validate_structure();
    CHECK_EQ(std::size_t{64}, statistics.stored_cell_count);
    // The exact depth, not the height ceiling: `validate_structure` already enforces the ceiling
    // before returning, so a `<=` assertion here could never fail. Union-by-size makes every later
    // vertex a direct child of root 0, so the true parent-path length is 1.
    CHECK_EQ(std::size_t{1}, statistics.maximum_parent_path_length);
    CHECK_EQ(std::int32_t{64}, current.component_size(0));
}

TEST(StatisticsCompareByValue) {
    const auto root = forest(4);
    const auto linked = root.link(0, 1);
    CHECK(root.validate_structure() == root.validate_structure());
    CHECK(!(root.validate_structure() == linked.validate_structure()));

    const auto expected = ancestral_connection_forest_statistics{4, 3, 2, 1, 1};
    CHECK(expected == linked.validate_structure());
}

} // namespace

int main() {
    if (!d7_enter_headless_test_process()) {
        return EXIT_FAILURE;
    }

    int failed = 0;
    for (const auto& test : registry()) {
        try {
            test.run();
            std::cout << "[PASS] " << test.name << '\n';
        } catch (const std::exception& ex) {
            ++failed;
            std::cerr << "[FAIL] " << test.name << ": " << ex.what() << '\n';
        } catch (...) {
            ++failed;
            std::cerr << "[FAIL] " << test.name << ": non-standard exception\n";
        }
    }

    if (failed != 0) {
        std::cerr << failed << " test(s) failed\n";
        return 1;
    }

    std::cout << registry().size() << " test(s) passed\n";
    return 0;
}
