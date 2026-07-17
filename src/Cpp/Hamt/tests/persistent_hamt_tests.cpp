#include <Tools/DataStructures/Hamt/persistent_bi_map.hpp>
#include <Tools/DataStructures/Hamt/persistent_hash_map.hpp>
#include <Tools/DataStructures/Hamt/persistent_hash_bag.hpp>
#include <Tools/DataStructures/Hamt/persistent_hash_multimap.hpp>
#include <Tools/DataStructures/Hamt/persistent_hash_set.hpp>
#include <Tools/DataStructures/Hamt/persistent_directed_graph.hpp>
#include <Tools/DataStructures/Hamt/persistent_indexed_map.hpp>
#include <Tools/DataStructures/Hamt/persistent_map_patch.hpp>
#include <Tools/DataStructures/Hamt/persistent_relation.hpp>
#include <Tools/DataStructures/Hamt/persistent_int_map.hpp>
#include <tools/data_structures/test_support/headless_test_process.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <exception>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <map>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using tools::data_structures::hamt::persistent_hamt_node_kind;
using tools::data_structures::hamt::map_difference_kind;
using tools::data_structures::hamt::bimap_conflict;
using tools::data_structures::hamt::bimap_conflict_error;
using tools::data_structures::hamt::persistent_bi_map;
using tools::data_structures::hamt::persistent_hash_bag;
using tools::data_structures::hamt::persistent_hash_map;
using tools::data_structures::hamt::persistent_hash_multimap;
using tools::data_structures::hamt::persistent_int_map;
using tools::data_structures::hamt::persistent_int_set;
using tools::data_structures::hamt::persistent_long_map;
using tools::data_structures::hamt::persistent_hash_set;
using tools::data_structures::hamt::persistent_relation;
using tools::data_structures::hamt::map_patch_entry;
using tools::data_structures::hamt::persistent_directed_graph;
using tools::data_structures::hamt::persistent_indexed_map;
using tools::data_structures::hamt::persistent_map_patch;

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

struct case_insensitive_hash {
    std::size_t operator()(const std::string& value) const noexcept {
        std::size_t hash = 1469598103934665603ull;
        for (unsigned char ch : value) {
            hash ^= static_cast<unsigned char>(std::tolower(ch));
            hash *= 1099511628211ull;
        }

        return hash;
    }
};

struct case_insensitive_equal {
    bool operator()(const std::string& left, const std::string& right) const noexcept {
        if (left.size() != right.size()) {
            return false;
        }

        for (std::size_t i = 0; i < left.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(left[i])) !=
                std::tolower(static_cast<unsigned char>(right[i]))) {
                return false;
            }
        }

        return true;
    }
};

struct constant_string_hash {
    std::size_t operator()(const std::string&) const noexcept {
        return 0x77;
    }
};

struct collision_key {
    int id;

    friend bool operator==(collision_key left, collision_key right) noexcept {
        return left.id == right.id;
    }

    friend bool operator<(collision_key left, collision_key right) noexcept {
        return left.id < right.id;
    }
};

struct collision_key_hash {
    std::size_t operator()(collision_key) const noexcept {
        return 0x12345;
    }
};

struct collision_key_equal {
    bool operator()(collision_key left, collision_key right) const noexcept {
        return left.id == right.id;
    }
};

struct explicit_hash_key {
    int id;
    std::uint32_t hash;

    friend bool operator==(explicit_hash_key left, explicit_hash_key right) noexcept {
        return left.id == right.id;
    }

    friend bool operator<(explicit_hash_key left, explicit_hash_key right) noexcept {
        return left.id < right.id;
    }
};

struct explicit_hash {
    std::size_t operator()(explicit_hash_key key) const noexcept {
        return key.hash;
    }
};

struct explicit_equal {
    bool operator()(explicit_hash_key left, explicit_hash_key right) const noexcept {
        return left.id == right.id;
    }
};

struct salted_int_hash {
    std::uint32_t salt;

    std::size_t operator()(int value) const noexcept {
        return static_cast<std::uint32_t>(std::hash<int>{}(value)) ^ salt;
    }
};

explicit_hash_key spreading_champ_key(int id) noexcept {
    return explicit_hash_key{
        id,
        id % 9 == 0 ? 17u : static_cast<std::uint32_t>(id) * 0x01010101u,
    };
}

struct counting_hash {
    std::shared_ptr<std::atomic<std::size_t>> calls;

    std::size_t operator()(int value) const noexcept {
        calls->fetch_add(1, std::memory_order_relaxed);
        return std::hash<int>{}(value);
    }
};

struct champ_pruning_counts {
    std::atomic<std::size_t> hash_calls{0};
    std::atomic<std::size_t> key_equal_calls{0};
    std::atomic<std::size_t> value_equal_calls{0};

    void reset() noexcept {
        hash_calls.store(0, std::memory_order_relaxed);
        key_equal_calls.store(0, std::memory_order_relaxed);
        value_equal_calls.store(0, std::memory_order_relaxed);
    }
};

struct champ_counting_hash {
    std::shared_ptr<champ_pruning_counts> counts;

    std::size_t operator()(explicit_hash_key key) const noexcept {
        counts->hash_calls.fetch_add(1, std::memory_order_relaxed);
        return key.hash;
    }
};

struct champ_counting_key_equal {
    std::shared_ptr<champ_pruning_counts> counts;

    bool operator()(explicit_hash_key left, explicit_hash_key right) const noexcept {
        counts->key_equal_calls.fetch_add(1, std::memory_order_relaxed);
        return left.id == right.id;
    }
};

struct champ_counting_value_equal {
    std::shared_ptr<champ_pruning_counts> counts;

    bool operator()(int left, int right) const noexcept {
        counts->value_equal_calls.fetch_add(1, std::memory_order_relaxed);
        return left == right;
    }
};

struct counting_string_equal {
    std::shared_ptr<std::atomic<std::size_t>> calls;

    bool operator()(const std::string& left, const std::string& right) const noexcept {
        calls->fetch_add(1, std::memory_order_relaxed);
        return left == right;
    }
};

struct few_buckets_hash {
    std::size_t operator()(int value) const noexcept {
        return static_cast<std::uint32_t>(value) & 3u;
    }
};

struct controlled_throw_hash {
    std::shared_ptr<bool> should_throw;

    std::size_t operator()(int value) const {
        if (*should_throw) {
            throw std::runtime_error("injected transient hash failure");
        }
        return std::hash<int>{}(value);
    }
};

struct controlled_throw_equal {
    std::shared_ptr<bool> should_throw;

    bool operator()(int left, int right) const {
        if (*should_throw) {
            throw std::runtime_error("injected key-equality failure");
        }
        return left == right;
    }
};

struct controlled_throw_value_equal {
    std::shared_ptr<bool> should_throw;

    bool operator()(int left, int right) const {
        if (*should_throw) {
            throw std::runtime_error("injected value-equality failure");
        }
        return left == right;
    }
};

struct throwing_move_policy_control {
    bool throw_on_move_construction = false;
    bool throw_on_move_assignment = false;
};

struct throwing_move_hash {
    std::shared_ptr<throwing_move_policy_control> control =
        std::make_shared<throwing_move_policy_control>();

    throwing_move_hash() = default;

    explicit throwing_move_hash(std::shared_ptr<throwing_move_policy_control> move_control)
        : control(std::move(move_control)) {
    }

    throwing_move_hash(const throwing_move_hash&) = default;
    throwing_move_hash& operator=(const throwing_move_hash&) = default;

    throwing_move_hash(throwing_move_hash&& other)
        : control(other.control) {
        if (control && control->throw_on_move_construction) {
            throw std::runtime_error("injected policy move-construction failure");
        }
    }

    throwing_move_hash& operator=(throwing_move_hash&& other) {
        control = other.control;
        if (control && control->throw_on_move_assignment) {
            throw std::runtime_error("injected policy move-assignment failure");
        }
        return *this;
    }

    std::size_t operator()(int value) const noexcept {
        return std::hash<int>{}(value);
    }
};

// Distinguishes stored-value retention from replacement: values equal mod 10
// compare equal under the policy while staying observably different.
struct mod_ten_equal {
    bool operator()(int left, int right) const noexcept {
        return left % 10 == right % 10;
    }
};

struct configurable_string_hash {
    bool ignore_case = false;

    std::size_t operator()(const std::string& value) const noexcept {
        if (!ignore_case) {
            return std::hash<std::string>{}(value);
        }
        return case_insensitive_hash{}(value);
    }
};

struct configurable_string_equal {
    bool ignore_case = false;

    bool operator()(const std::string& left, const std::string& right) const noexcept {
        return ignore_case
            ? case_insensitive_equal{}(left, right)
            : left == right;
    }
};

template <class T>
std::vector<T> sorted(std::vector<T> items) {
    std::sort(items.begin(), items.end());
    return items;
}

template <class Set>
std::vector<int> sorted_set_values(const Set& set) {
    return sorted(std::vector<int>(set.begin(), set.end()));
}

template <class Key, class Value, class Hash, class Equal, class Map>
void assert_matches(
    const std::unordered_map<Key, Value, Hash, Equal>& model,
    const Map& map) {
    CHECK_EQ(model.size(), map.count());

    for (const auto& [key, expected] : model) {
        const auto* actual = map.try_get(key);
        CHECK(actual != nullptr);
        CHECK_EQ(expected, *actual);
    }

    std::size_t enumerated = 0;
    for (const auto& [key, value] : map) {
        const auto model_item = model.find(key);
        CHECK(model_item != model.end());
        CHECK_EQ(model_item->second, value);
        ++enumerated;
    }

    CHECK_EQ(model.size(), enumerated);
}

void assert_equal_set(std::initializer_list<int> expected, const persistent_hash_set<int>& actual) {
    CHECK_EQ(sorted(std::vector<int>(expected)), sorted(actual.to_vector()));
}

void assert_equal_set(const std::unordered_set<int>& expected, const persistent_hash_set<int>& actual) {
    CHECK_EQ(sorted(std::vector<int>(expected.begin(), expected.end())), sorted(actual.to_vector()));
}

TEST(EmptyMap_HasNoEntries) {
    using map_type = persistent_hash_map<int, std::string>;

    const auto empty = map_type::empty();

    CHECK(empty.is_empty());
    CHECK_EQ(std::size_t{0}, empty.count());
    CHECK(!empty.contains_key(1));
    CHECK(empty.try_get(1) == nullptr);
    CHECK(empty.remove(1).shares_root_with(empty));
    CHECK(empty.clear().shares_root_with(empty));
    CHECK_THROWS_AS(empty.at(1), std::out_of_range);
}

TEST(SameValueReferenceBypassesValuePolicy) {
    const auto calls = std::make_shared<std::atomic<std::size_t>>(0);
    using map_type = persistent_hash_map<
        int,
        std::string,
        std::hash<int>,
        std::equal_to<int>,
        counting_string_equal>;
    const auto map = map_type::create(
        std::hash<int>{}, std::equal_to<int>{}, counting_string_equal{calls})
        .set_item(1, "one");
    const auto* stored = map.try_get(1);
    CHECK(stored != nullptr);
    calls->store(0, std::memory_order_relaxed);

    const auto same = map.set_item(1, *stored);

    CHECK(same.shares_root_with(map));
    CHECK_EQ(std::size_t{0}, calls->load(std::memory_order_relaxed));
}

TEST(SetItem_AddsReplacesAndPreservesOldVersions) {
    using map_type = persistent_hash_map<int, std::string>;

    const auto empty = map_type::empty();
    const auto one = empty.set_item(1, "one");
    const auto two = one.set_item(2, "two");
    const auto replaced = two.set_item(1, "uno");

    CHECK(empty.is_empty());
    CHECK_EQ(std::string("one"), one.at(1));
    CHECK_EQ(std::string("one"), two.at(1));
    CHECK_EQ(std::string("two"), two.at(2));
    CHECK_EQ(std::string("uno"), replaced.at(1));
    CHECK_EQ(std::string("two"), replaced.at(2));
    CHECK(replaced.set_item(1, "uno").shares_root_with(replaced));
}

TEST(AddAndTryAdd_RejectDuplicates) {
    using map_type = persistent_hash_map<int, std::string>;

    const auto map = map_type::empty().add(1, "one");

    CHECK_THROWS_AS(map.add(1, "duplicate"), std::invalid_argument);

    const auto [same, duplicate_added] = map.try_add(1, "duplicate");
    CHECK(!duplicate_added);
    CHECK(same.shares_root_with(map));

    const auto [added, was_added] = map.try_add(2, "two");
    CHECK(was_added);
    CHECK_EQ(std::string("two"), added.at(2));
    CHECK(!map.contains_key(2));
}

TEST(RemoveAndTryRemove_DeletePresentKeys) {
    using map_type = persistent_hash_map<int, std::string>;

    const auto map = map_type::empty().set_item(1, "one").set_item(2, "two");
    const auto [removed, did_remove, value] = map.try_remove(1);

    CHECK(did_remove);
    CHECK(value.has_value());
    CHECK_EQ(std::string("one"), *value);
    CHECK(!removed.contains_key(1));
    CHECK(map.contains_key(1));

    const auto [same, missing_removed, missing_value] = removed.try_remove(9);
    CHECK(!missing_removed);
    CHECK(!missing_value.has_value());
    CHECK(same.shares_root_with(removed));
    CHECK(removed.remove(9).shares_root_with(removed));
}

TEST(CreateRange_LastWinsAndRetainsFirstEquivalentKey) {
    using map_type = persistent_hash_map<std::string, int, case_insensitive_hash, case_insensitive_equal>;

    const std::vector<std::pair<std::string, int>> items = {
        {"Alpha", 1},
        {"beta", 2},
        {"ALPHA", 3},
    };

    const auto map = map_type::create_range(items, case_insensitive_hash{}, case_insensitive_equal{});

    CHECK_EQ(std::size_t{2}, map.count());
    CHECK_EQ(3, map.at("alpha"));
    const auto* actual_key = map.try_get_key("ALPHA");
    CHECK(actual_key != nullptr);
    CHECK_EQ(std::string("Alpha"), *actual_key);
    CHECK(map.contains_key("BETA"));
}

TEST(KeysAndValues_AlignWithPairEnumeration) {
    using map_type = persistent_hash_map<int, std::string>;

    const auto map = map_type::empty()
        .set_item(5, "five")
        .set_item(2, "two")
        .set_item(7, "seven");

    std::vector<int> enumerated_keys;
    std::vector<std::string> enumerated_values;
    for (const auto& [key, value] : map) {
        enumerated_keys.push_back(key);
        enumerated_values.push_back(value);
    }

    CHECK_EQ(enumerated_keys, map.keys());
    CHECK_EQ(enumerated_values, map.values());
}

TEST(HelperKeyComparisons_AreExercisedForPortableWarningBuilds) {
    CHECK(collision_key{1} == collision_key{1});
    CHECK(!(collision_key{1} == collision_key{2}));
    CHECK(collision_key{1} < collision_key{2});
    CHECK(!(collision_key{2} < collision_key{1}));

    CHECK((explicit_hash_key{1, 0x10} == explicit_hash_key{1, 0x20}));
    CHECK(!(explicit_hash_key{1, 0x10} == explicit_hash_key{2, 0x10}));
    CHECK((explicit_hash_key{1, 0x20} < explicit_hash_key{2, 0x10}));
    CHECK(!(explicit_hash_key{2, 0x10} < explicit_hash_key{1, 0x20}));
}

TEST(EqualHashCollisionBucket_PreservesEveryKey) {
    using map_type = persistent_hash_map<collision_key, int, collision_key_hash, collision_key_equal>;
    using model_type = std::unordered_map<collision_key, int, collision_key_hash, collision_key_equal>;

    auto map = map_type::create(collision_key_hash{}, collision_key_equal{});
    model_type model(0, collision_key_hash{}, collision_key_equal{});

    for (int i = 0; i < 100; ++i) {
        const collision_key key{i};
        map = map.set_item(key, i * 10);
        model[key] = i * 10;
    }

    for (int i = 0; i < 100; i += 3) {
        const collision_key key{i};
        map = map.remove(key);
        model.erase(key);
    }

    for (int i = 1; i < 100; i += 4) {
        const collision_key key{i};
        map = map.set_item(key, -i);
        model[key] = -i;
    }

    assert_matches(model, map);
}

TEST(DeepSharedHashPrefixes_LookupAndRemoveCorrectly) {
    using map_type = persistent_hash_map<explicit_hash_key, std::string, explicit_hash, explicit_equal>;

    const explicit_hash_key a{1, 0};
    const explicit_hash_key b{2, 1u << 30};
    const explicit_hash_key c{3, 0x80000000u};
    const explicit_hash_key d{4, 0xC0000000u};

    const auto map = map_type::create(explicit_hash{}, explicit_equal{})
        .set_item(a, "a")
        .set_item(b, "b")
        .set_item(c, "c")
        .set_item(d, "d");

    CHECK(map.debug_validate_canonical());
    CHECK_EQ(std::string("a"), map.at(a));
    CHECK_EQ(std::string("b"), map.at(b));
    CHECK_EQ(std::string("c"), map.at(c));
    CHECK_EQ(std::string("d"), map.at(d));
    CHECK_EQ(std::vector<std::string>({"a", "b", "c", "d"}), sorted(map.values()));

    const auto reduced = map.remove(b).remove(c).remove(d);
    CHECK(reduced.debug_validate_canonical());
    CHECK_EQ(std::size_t{1}, reduced.count());
    CHECK_EQ(persistent_hamt_node_kind::leaf, reduced.debug_root_kind());
    CHECK_EQ(std::string("a"), reduced.at(a));
    CHECK(!reduced.contains_key(b));
    CHECK_EQ(std::string("b"), map.at(b));
}

TEST(CollisionBucket_SplitsWhenDifferentHashKeyArrives) {
    using map_type = persistent_hash_map<explicit_hash_key, std::string, explicit_hash, explicit_equal>;

    const explicit_hash_key a{1, 0x10};
    const explicit_hash_key b{2, 0x10};
    const explicit_hash_key c{3, 0x30};

    const auto map = map_type::create(explicit_hash{}, explicit_equal{})
        .set_item(a, "a")
        .set_item(b, "b")
        .set_item(c, "c");

    CHECK_EQ(std::size_t{3}, map.count());
    CHECK_EQ(std::string("a"), map.at(a));
    CHECK_EQ(std::string("b"), map.at(b));
    CHECK_EQ(std::string("c"), map.at(c));
    CHECK_EQ(std::vector<std::string>({"a", "b", "c"}), sorted(map.values()));
}

TEST(CollisionBucket_HashMismatchProbesMissAndSplitDeeply) {
    using map_type = persistent_hash_map<explicit_hash_key, std::string, explicit_hash, explicit_equal>;

    const explicit_hash_key a{1, 0x10};
    const explicit_hash_key b{2, 0x10};
    const explicit_hash_key probe{9, 0x410};

    const auto map = map_type::create(explicit_hash{}, explicit_equal{})
        .set_item(a, "a")
        .set_item(b, "b");

    CHECK(!map.contains_key(probe));
    CHECK(map.remove(probe).shares_root_with(map));

    const auto expanded = map.set_item(probe, "p");
    CHECK_EQ(std::size_t{3}, expanded.count());
    CHECK_EQ(std::string("p"), expanded.at(probe));
    CHECK_EQ(std::string("a"), expanded.at(a));
    CHECK_EQ(std::string("b"), expanded.at(b));
    CHECK_EQ(std::vector<std::string>({"a", "b", "p"}), sorted(expanded.values()));

    const auto reduced = expanded.remove(probe);
    CHECK_EQ(std::size_t{2}, reduced.count());
    CHECK_EQ(std::string("a"), reduced.at(a));
    CHECK_EQ(std::string("b"), reduced.at(b));
}

TEST(CollisionBucket_EqualValueKeepsRootAndReplaceKeepsKey) {
    using map_type = persistent_hash_map<std::string, int, constant_string_hash, case_insensitive_equal>;

    const auto map = map_type::create(constant_string_hash{}, case_insensitive_equal{})
        .set_item("Alpha", 1)
        .set_item("beta", 2);

    CHECK(map.set_item("ALPHA", 1).shares_root_with(map));

    const auto replaced = map.set_item("ALPHA", 3);
    CHECK_EQ(3, replaced.at("alpha"));
    const auto* actual_key = replaced.try_get_key("alpha");
    CHECK(actual_key != nullptr);
    CHECK_EQ(std::string("Alpha"), *actual_key);
}

TEST(Structure_RootShapeTracksContentsAndCollapse) {
    using map_type = persistent_hash_map<explicit_hash_key, std::string, explicit_hash, explicit_equal>;

    const auto empty = map_type::create(explicit_hash{}, explicit_equal{});
    CHECK_EQ(persistent_hamt_node_kind::empty, empty.debug_root_kind());

    const auto single = empty.set_item(explicit_hash_key{1, 0x10}, "a");
    CHECK_EQ(persistent_hamt_node_kind::leaf, single.debug_root_kind());

    const explicit_hash_key a{1, 0x10};
    const explicit_hash_key b{2, 0x10};
    const auto bucket = empty.set_item(a, "a").set_item(b, "b");
    CHECK_EQ(persistent_hamt_node_kind::collision, bucket.debug_root_kind());
    CHECK_EQ(persistent_hamt_node_kind::leaf, bucket.remove(b).debug_root_kind());

    const auto branching = bucket.set_item(explicit_hash_key{3, 0x11}, "c");
    CHECK_EQ(persistent_hamt_node_kind::bitmap_indexed, branching.debug_root_kind());

    const explicit_hash_key deep_a{3, 0};
    const explicit_hash_key deep_b{4, 1u << 30};
    const auto deep = empty.set_item(deep_a, "a").set_item(deep_b, "b");
    CHECK_EQ(persistent_hamt_node_kind::bitmap_indexed, deep.debug_root_kind());
    CHECK_EQ(persistent_hamt_node_kind::leaf, deep.remove(deep_b).debug_root_kind());
}

TEST(Structure_UpdateSharesUntouchedSiblingSubtrees) {
    using map_type = persistent_hash_map<explicit_hash_key, std::string, explicit_hash, explicit_equal>;

    const explicit_hash_key a{1, 0x00};
    const explicit_hash_key b{2, 0x01};
    const explicit_hash_key c{3, 0x21};

    const auto map = map_type::create(explicit_hash{}, explicit_equal{})
        .set_item(a, "a")
        .set_item(b, "b")
        .set_item(c, "c");
    const auto updated = map.set_item(a, "a2");

    const auto before_children = map.debug_root_child_identities();
    const auto after_children = updated.debug_root_child_identities();

    CHECK_EQ(std::size_t{1}, before_children.size());
    CHECK_EQ(std::size_t{1}, after_children.size());
    CHECK(before_children[0] == after_children[0]);
    CHECK(map.set_item(a, "a").shares_root_with(map));
    CHECK(map.remove(explicit_hash_key{9, 0x09}).shares_root_with(map));
}

TEST(Champ_IndependentHistoriesAndTypedDiff) {
    using map_type = persistent_hash_map<explicit_hash_key, int, explicit_hash, explicit_equal>;
    const auto empty = map_type::create(explicit_hash{}, explicit_equal{});
    auto ascending = empty;
    auto descending = empty;
    for (int key = 0; key < 512; ++key) {
        ascending = ascending.set_item(spreading_champ_key(key), key);
        descending = descending.set_item(spreading_champ_key(511 - key), 511 - key);
    }

    CHECK(ascending.map_equals(descending));
    CHECK(ascending.diff(descending).empty());
    CHECK(ascending.debug_validate_canonical());
    CHECK(descending.debug_validate_canonical());
    CHECK(ascending.debug_topology_equal(descending));

    auto churned = ascending;
    for (int key = 0; key < 512; key += 3) {
        churned = churned.remove(spreading_champ_key(key));
    }
    for (int key = 510; key >= 0; key -= 3) {
        churned = churned.set_item(spreading_champ_key(key), key);
    }
    CHECK(churned.debug_validate_canonical());
    CHECK(ascending.debug_topology_equal(churned));
    const auto changed = descending.remove(spreading_champ_key(7))
        .set_item(spreading_champ_key(9), -9)
        .set_item(spreading_champ_key(1000), 1000);
    const auto differences = ascending.diff(changed);
    CHECK_EQ(std::size_t{3}, differences.size());
    CHECK(std::ranges::any_of(differences, [](const auto& item) {
        return item.kind == map_difference_kind::removed && item.key.id == 7;
    }));
    CHECK(std::ranges::any_of(differences, [](const auto& item) {
        return item.kind == map_difference_kind::changed && item.key.id == 9;
    }));
    CHECK(std::ranges::any_of(differences, [](const auto& item) {
        return item.kind == map_difference_kind::added && item.key.id == 1000;
    }));

    // Exact non-vacuity fixture: two keys share the first ten hash bits and diverge
    // below a unary bridge, while the third key diverges at the root. Removing one
    // deep key must inline the survivor and recover the direct-build topology.
    const auto deep_a = explicit_hash_key{10'000, 1u << 10};
    const auto deep_b = explicit_hash_key{10'001, 2u << 10};
    const auto root_sibling = explicit_hash_key{10'002, 1u};
    const auto deep = map_type::create(explicit_hash{}, explicit_equal{})
        .set_item(deep_a, 1)
        .set_item(deep_b, 2)
        .set_item(root_sibling, 3);
    const auto collapsed = deep.remove(deep_b);
    const auto direct = map_type::create(explicit_hash{}, explicit_equal{})
        .set_item(deep_a, 1)
        .set_item(root_sibling, 3);
    CHECK(deep.debug_validate_canonical());
    CHECK(collapsed.debug_validate_canonical());
    CHECK(collapsed.debug_topology_equal(direct));
}

TEST(Champ_IndependentPolicyHashStatesUseSemanticEqualityAndDiff) {
    using map_type = persistent_hash_map<int, int, salted_int_hash>;
    const auto left = map_type::create(salted_int_hash{0u})
        .set_item(1, 10)
        .set_item(2, 20)
        .set_item(3, 30);
    const auto equal_right = map_type::create(salted_int_hash{0x9e3779b9u})
        .set_item(3, 30)
        .set_item(2, 20)
        .set_item(1, 10);

    CHECK(!left.shares_policy_with(equal_right));
    CHECK(left.map_equals(equal_right));
    CHECK(left.diff(equal_right).empty());

    const auto changed_right = equal_right.remove(3).set_item(2, -20).set_item(4, 40);
    const auto differences = left.diff(changed_right);
    CHECK_EQ(std::size_t{3}, differences.size());
    CHECK(std::ranges::any_of(differences, [](const auto& item) {
        return item.kind == map_difference_kind::removed && item.key == 3;
    }));
    CHECK(std::ranges::any_of(differences, [](const auto& item) {
        return item.kind == map_difference_kind::changed && item.key == 2
            && item.before == 20 && item.after == -20;
    }));
    CHECK(std::ranges::any_of(differences, [](const auto& item) {
        return item.kind == map_difference_kind::added && item.key == 4;
    }));
}

TEST(Champ_EqualityAndDiffPruneSharedDescendants) {
    const auto callback_counts = std::make_shared<champ_pruning_counts>();
    using map_type = persistent_hash_map<
        explicit_hash_key,
        int,
        champ_counting_hash,
        champ_counting_key_equal,
        champ_counting_value_equal>;
    auto basis = map_type::create(
        champ_counting_hash{callback_counts},
        champ_counting_key_equal{callback_counts},
        champ_counting_value_equal{callback_counts});
    for (int key = 0; key < 512; ++key) {
        basis = basis.set_item(spreading_champ_key(key), key);
    }

    const auto changed = basis.set_item(spreading_champ_key(42), -42);
    const auto restored = changed.set_item(spreading_champ_key(42), 42);
    CHECK(!basis.shares_root_with(restored));

    callback_counts->reset();
    CHECK(basis.map_equals(restored));
    CHECK_EQ(std::size_t{0}, callback_counts->hash_calls.load(std::memory_order_relaxed));
    const auto equality_key_calls = callback_counts->key_equal_calls.load(std::memory_order_relaxed);
    const auto equality_value_calls = callback_counts->value_equal_calls.load(std::memory_order_relaxed);
    CHECK(equality_key_calls > 0 && equality_key_calls < basis.count());
    CHECK(equality_value_calls > 0 && equality_value_calls < basis.count());

    callback_counts->reset();
    CHECK(basis.diff(restored).empty());
    CHECK_EQ(std::size_t{0}, callback_counts->hash_calls.load(std::memory_order_relaxed));
    const auto equal_diff_key_calls = callback_counts->key_equal_calls.load(std::memory_order_relaxed);
    const auto equal_diff_value_calls = callback_counts->value_equal_calls.load(std::memory_order_relaxed);
    CHECK(equal_diff_key_calls > 0 && equal_diff_key_calls < basis.count());
    CHECK(equal_diff_value_calls > 0 && equal_diff_value_calls < basis.count());

    callback_counts->reset();
    const auto differences = basis.diff(changed);
    CHECK_EQ(std::size_t{1}, differences.size());
    CHECK_EQ(map_difference_kind::changed, differences[0].kind);
    CHECK_EQ(42, differences[0].key.id);
    CHECK_EQ(std::size_t{0}, callback_counts->hash_calls.load(std::memory_order_relaxed));
    const auto changed_diff_key_calls = callback_counts->key_equal_calls.load(std::memory_order_relaxed);
    const auto changed_diff_value_calls = callback_counts->value_equal_calls.load(std::memory_order_relaxed);
    CHECK(changed_diff_key_calls > 0 && changed_diff_key_calls < basis.count());
    CHECK(changed_diff_value_calls > 0 && changed_diff_value_calls < basis.count());
}

TEST(Champ_TopologyComparatorRejectsDifferentCollisionKeys) {
    using map_type = persistent_hash_map<collision_key, int, collision_key_hash, collision_key_equal>;
    const auto left = map_type::create(collision_key_hash{}, collision_key_equal{})
        .set_item(collision_key{1}, 10)
        .set_item(collision_key{2}, 20);
    const auto same_reversed = map_type::create(collision_key_hash{}, collision_key_equal{})
        .set_item(collision_key{2}, 20)
        .set_item(collision_key{1}, 10);
    const auto different = map_type::create(collision_key_hash{}, collision_key_equal{})
        .set_item(collision_key{1}, 10)
        .set_item(collision_key{3}, 30);

    CHECK(left.debug_topology_equal(same_reversed));
    CHECK(!left.debug_topology_equal(different));
}

TEST(Patricia_SignedOrderingHistoriesAndStructuralAlgebra) {
    auto ints = persistent_int_map<std::string>{};
    for (const auto key : {INT32_MAX, 1, 0, -1, INT32_MIN}) {
        ints = ints.set_item(key, std::to_string(key));
    }
    const auto entries = ints.to_vector();
    CHECK_EQ(INT32_MIN, entries.front().first);
    CHECK_EQ(INT32_MAX, entries.back().first);

    auto longs = persistent_long_map<std::int64_t>{};
    for (const auto key : {INT64_MAX, std::int64_t{1}, std::int64_t{0}, std::int64_t{-1}, INT64_MIN}) {
        longs = longs.set_item(key, key);
    }
    CHECK_EQ(INT64_MIN, longs.to_vector().front().first);
    CHECK_EQ(INT64_MAX, longs.to_vector().back().first);

    auto actual = persistent_int_map<std::uint32_t>{};
    std::map<std::int32_t, std::uint32_t> expected;
    std::uint32_t state = 0x1234abcdu;
    for (int operation = 0; operation < 10000; ++operation) {
        state = state * 1664525u + 1013904223u;
        const auto key = static_cast<std::int32_t>((state >> 8) % 401u) - 200;
        if ((state & 3u) == 0) { actual = actual.remove(key); expected.erase(key); }
        else { actual = actual.set_item(key, state); expected[key] = state; }
    }
    CHECK_EQ((std::vector<std::pair<std::int32_t, std::uint32_t>>(expected.begin(), expected.end())), actual.to_vector());
    CHECK(actual.remove(10000).shares_root_with(actual));

    const auto left_map = persistent_int_map<std::string>{}.set_item(1, "left").set_item(2, "two");
    const auto right_map = persistent_int_map<std::string>{}.set_item(1, "right").set_item(3, "three");
    CHECK_EQ(std::string("right"), *left_map.union_with(right_map).try_get(1));
    CHECK_EQ(std::string("left"), *left_map.intersect_with(right_map).try_get(1));
    const auto append_values = [](std::int32_t, const std::string& left_value, const std::string& right_value) {
        return left_value + "+" + right_value;
    };
    const auto combined_union = left_map.union_with(right_map, append_values);
    const auto combined_intersection = left_map.intersect_with(right_map, append_values);
    CHECK_EQ(std::string("left+right"), *combined_union.try_get(1));
    CHECK_EQ(std::string("left+right"), *combined_intersection.try_get(1));
    CHECK_EQ(std::size_t{3}, combined_union.size());
    CHECK_EQ(std::size_t{1}, combined_intersection.size());
    const auto choose_left = [](
        std::int32_t, const std::string& left_value, const std::string&) { return left_value; };
    CHECK(left_map.union_with(left_map, choose_left).shares_root_with(left_map));
    CHECK(left_map.intersect_with(left_map, choose_left).shares_root_with(left_map));

    const auto left = persistent_int_set{}.add(-3).add(-1).add(1).add(3);
    const auto right = persistent_int_set{}.add(-1).add(0).add(1);
    CHECK_EQ((std::vector<std::int32_t>{-3, -1, 0, 1, 3}), left.union_with(right).to_vector());
    CHECK_EQ((std::vector<std::int32_t>{-1, 1}), left.intersect_with(right).to_vector());
    CHECK_EQ((std::vector<std::int32_t>{-3, 3}), left.except_with(right).to_vector());
}

TEST(Enumerator_CopiedIteratorAdvancesIndependently) {
    using map_type = persistent_hash_map<int, std::string>;

    const auto map = map_type::empty()
        .set_item(0, "zero")
        .set_item(1, "one")
        .set_item(33, "thirty-three");

    const auto expected = map.keys();

    auto original = map.begin();
    CHECK(original != map.end());
    auto copy = original;

    std::vector<int> from_original;
    for (; original != map.end(); ++original) {
        from_original.push_back(original->first);
    }

    std::vector<int> from_copy;
    for (; copy != map.end(); ++copy) {
        from_copy.push_back(copy->first);
    }

    CHECK_EQ(expected, from_original);
    CHECK_EQ(expected, from_copy);
}

TEST(Enumerator_RetainsTheTrieBeyondTheSourceMapValue) {
    using map_type = persistent_hash_map<int, std::string>;

    // The iterator must own the trie root: obtaining it from a map value that
    // is destroyed before iteration finishes must remain valid.
    auto iterator = [] {
        const auto local = map_type::empty()
            .set_item(0, "zero")
            .set_item(1, "one")
            .set_item(33, "thirty-three");
        return local.begin();
    }();

    std::vector<int> keys;
    for (; iterator != std::default_sentinel; ++iterator) {
        keys.push_back(iterator->first);
    }

    std::sort(keys.begin(), keys.end());
    CHECK_EQ((std::vector<int>{0, 1, 33}), keys);
}

TEST(RandomHistory_MatchesUnorderedMapAndPreservesSnapshots) {
    using map_type = persistent_hash_map<int, int>;
    using model_type = std::unordered_map<int, int>;

    std::mt19937 rng(0xC0FFEEu);
    std::uniform_int_distribution<int> op_dist(0, 4);
    std::uniform_int_distribution<int> key_dist(-40, 40);
    std::uniform_int_distribution<int> value_dist(-1000, 1000);

    for (int iteration = 0; iteration < 300; ++iteration) {
        auto map = map_type::empty();
        model_type model;
        std::vector<std::pair<map_type, model_type>> snapshots;

        for (int step = 0; step < 200; ++step) {
            const int op = op_dist(rng);
            const int key = key_dist(rng);
            const int value = value_dist(rng);

            switch (op) {
            case 0:
                map = map.set_item(key, value);
                model[key] = value;
                break;

            case 1:
                map = map.remove(key);
                model.erase(key);
                break;

            case 2: {
                const bool expected_added = model.find(key) == model.end();
                const auto [added_map, actual_added] = map.try_add(key, value);
                CHECK_EQ(expected_added, actual_added);
                map = added_map;
                if (expected_added) {
                    model[key] = value;
                }
                break;
            }

            case 3: {
                const auto model_item = model.find(key);
                const bool expected_removed = model_item != model.end();
                const int expected_value = expected_removed ? model_item->second : 0;
                const auto [removed_map, actual_removed, actual_value] = map.try_remove(key);
                CHECK_EQ(expected_removed, actual_removed);
                if (expected_removed) {
                    CHECK(actual_value.has_value());
                    CHECK_EQ(expected_value, *actual_value);
                    model.erase(model_item);
                }

                map = removed_map;
                break;
            }

            default:
                snapshots.push_back({map, model});
                break;
            }

            assert_matches(model, map);
            const auto snapshot_start = snapshots.size() > 5 ? snapshots.size() - 5 : 0;
            for (std::size_t i = snapshot_start; i < snapshots.size(); ++i) {
                assert_matches(snapshots[i].second, snapshots[i].first);
            }
        }
    }
}

TEST(ScriptedCollisionSnapshotStory_MatchesModel) {
    using map_type = persistent_hash_map<explicit_hash_key, int, explicit_hash, explicit_equal>;
    using model_type = std::unordered_map<explicit_hash_key, int, explicit_hash, explicit_equal>;

    auto keys = std::vector<explicit_hash_key>{};
    keys.reserve(96);
    for (int id = 0; id != 96; ++id) {
        auto hash = std::uint32_t{};
        if (id < 32) {
            hash = 0x00ABCDEFu;
        } else if (id < 64) {
            hash = static_cast<std::uint32_t>(id - 32) << 25;
        } else {
            hash = 0x00000410u |
                (static_cast<std::uint32_t>(id & 7) << 15) |
                (static_cast<std::uint32_t>(id & 3) << 5);
        }

        keys.push_back(explicit_hash_key{id, hash});
    }

    auto map = map_type::create(explicit_hash{}, explicit_equal{});
    auto model = model_type(0, explicit_hash{}, explicit_equal{});
    auto snapshots = std::vector<std::pair<map_type, model_type>>{};

    for (int step = 0; step != 96; ++step) {
        const auto id = (step * 37) % 96;
        const auto value = id - 500;
        map = map.set_item(keys[static_cast<std::size_t>(id)], value);
        model[keys[static_cast<std::size_t>(id)]] = value;

        if (step == 23 || step == 47 || step == 71) {
            snapshots.push_back({map, model});
        }
    }

    assert_matches(model, map);

    for (int id = 5; id < 96; id += 11) {
        CHECK(map.set_item(keys[static_cast<std::size_t>(id)], model.at(keys[static_cast<std::size_t>(id)]))
                  .shares_root_with(map));
    }

    for (int id = 2; id < 96; id += 5) {
        const auto value = 400 - id;
        map = map.set_item(keys[static_cast<std::size_t>(id)], value);
        model[keys[static_cast<std::size_t>(id)]] = value;
    }

    snapshots.push_back({map, model});

    for (int id = 0; id < 96; ++id) {
        if (id % 7 == 0 || id % 13 == 0) {
            map = map.remove(keys[static_cast<std::size_t>(id)]);
            model.erase(keys[static_cast<std::size_t>(id)]);
        }
    }

    for (int id = 0; id < 96; id += 9) {
        const auto key = keys[static_cast<std::size_t>(id)];
        const auto value = 700 - id;
        const auto expected_added = model.find(key) == model.end();
        const auto [next, added] = map.try_add(key, value);
        CHECK_EQ(expected_added, added);
        map = next;
        if (added) {
            model[key] = value;
        }
    }

    for (int id = 1; id < 96; id += 10) {
        const auto key = keys[static_cast<std::size_t>(id)];
        if (model.find(key) != model.end()) {
            const auto [same, added] = map.try_add(key, -900);
            CHECK(!added);
            CHECK(same.shares_root_with(map));
        }
    }

    assert_matches(model, map);
    for (const auto& [snapshot, snapshot_model] : snapshots) {
        assert_matches(snapshot_model, snapshot);
    }

    const auto cleared = map.clear();
    CHECK(cleared.is_empty());
    CHECK(!map.is_empty());
}

TEST(RandomHistory_WithCollidingHashes_MatchesUnorderedMap) {
    using map_type = persistent_hash_map<int, int, few_buckets_hash>;
    using model_type = std::unordered_map<int, int, few_buckets_hash>;

    std::mt19937 rng(0xBAD5EEDu);
    std::uniform_int_distribution<int> op_dist(0, 4);
    std::uniform_int_distribution<int> key_dist(-40, 40);
    std::uniform_int_distribution<int> value_dist(-1000, 1000);

    for (int iteration = 0; iteration < 300; ++iteration) {
        auto map = map_type::create(few_buckets_hash{});
        model_type model(0, few_buckets_hash{});

        for (int step = 0; step < 200; ++step) {
            const int op = op_dist(rng);
            const int key = key_dist(rng);
            const int value = value_dist(rng);

            switch (op) {
            case 0:
            case 4:
                map = map.set_item(key, value);
                model[key] = value;
                break;

            case 1:
                map = map.remove(key);
                model.erase(key);
                break;

            case 2: {
                const bool expected_added = model.find(key) == model.end();
                const auto [added_map, actual_added] = map.try_add(key, value);
                CHECK_EQ(expected_added, actual_added);
                map = added_map;
                if (expected_added) {
                    model[key] = value;
                }
                break;
            }

            default: {
                const auto model_item = model.find(key);
                const bool expected_removed = model_item != model.end();
                const int expected_value = expected_removed ? model_item->second : 0;
                const auto [removed_map, actual_removed, actual_value] = map.try_remove(key);
                CHECK_EQ(expected_removed, actual_removed);
                if (expected_removed) {
                    CHECK(actual_value.has_value());
                    CHECK_EQ(expected_value, *actual_value);
                    model.erase(model_item);
                }

                map = removed_map;
                break;
            }
            }
        }

        assert_matches(model, map);
    }
}

TEST(ConcurrentReaders_ObserveConsistentRetainedSnapshots) {
    using map_type = persistent_hash_map<int, int>;
    using set_type = persistent_hash_set<int>;

    auto map = map_type::empty();
    auto set = set_type::empty();
    for (int value = 0; value != 256; ++value) {
        map = map.set_item(value, value * 3 - 100);
        set = set.add(value);
    }

    std::atomic<int> failures{0};
    std::mutex failure_mutex;
    std::vector<std::string> failure_messages;
    std::vector<std::thread> threads;
    threads.reserve(8);
    for (int worker = 0; worker != 8; ++worker) {
        threads.emplace_back([&] {
            try {
                for (int pass = 0; pass != 256; ++pass) {
                    CHECK_EQ(std::size_t{256}, map.count());
                    CHECK_EQ(std::size_t{256}, set.count());

                    for (int value = 0; value < 256; value += 11) {
                        const auto* actual = map.try_get(value);
                        CHECK(actual != nullptr);
                        CHECK_EQ(value * 3 - 100, *actual);
                        CHECK(set.contains(value));
                    }

                    std::size_t enumerated = 0;
                    for (const auto& [key, value] : map) {
                        CHECK(key >= 0);
                        CHECK(key < 256);
                        CHECK_EQ(key * 3 - 100, value);
                        ++enumerated;
                    }

                    CHECK_EQ(std::size_t{256}, enumerated);
                    const auto sorted_values = sorted(set.to_vector());
                    CHECK_EQ(std::size_t{256}, sorted_values.size());
                    CHECK_EQ(std::vector<int>({0, 1, 2, 3, 4}), std::vector<int>(
                        sorted_values.begin(),
                        sorted_values.begin() + 5));
                }
            } catch (const std::exception& ex) {
                {
                    const std::lock_guard<std::mutex> lock(failure_mutex);
                    failure_messages.push_back(ex.what());
                }

                failures.fetch_add(1, std::memory_order_relaxed);
            } catch (...) {
                {
                    const std::lock_guard<std::mutex> lock(failure_mutex);
                    failure_messages.push_back("non-standard exception");
                }

                failures.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    if (failures.load(std::memory_order_relaxed) != 0) {
        const std::lock_guard<std::mutex> lock(failure_mutex);
        fail_message(__FILE__, __LINE__, failure_messages.empty() ? "worker failed" : failure_messages.front());
    }
}

TEST(Set_AddRemoveContainsAndPersistence) {
    using set_type = persistent_hash_set<int>;

    const auto empty = set_type::empty();
    const auto one = empty.add(1);
    const auto two = one.add(2);
    const auto removed = two.remove(1);

    CHECK(empty.is_empty());
    CHECK(one.contains(1));
    CHECK(!one.contains(2));
    CHECK(two.contains(1));
    CHECK(two.contains(2));
    CHECK(!removed.contains(1));
    CHECK(removed.contains(2));
    CHECK(one.add(1).shares_root_with(one));
    CHECK(one.remove(9).shares_root_with(one));
}

TEST(Set_TryAddAndTryRemove_ReportWhetherMembershipChanged) {
    using set_type = persistent_hash_set<int>;

    const auto set = set_type::empty().add(1);

    const auto [same, duplicate_added] = set.try_add(1);
    CHECK(!duplicate_added);
    CHECK(same.shares_root_with(set));

    const auto [added, was_added] = set.try_add(2);
    CHECK(was_added);
    CHECK(added.contains(2));

    const auto [removed, was_removed] = added.try_remove(1);
    CHECK(was_removed);
    CHECK(!removed.contains(1));

    const auto [still_removed, removed_again] = removed.try_remove(1);
    CHECK(!removed_again);
    CHECK(still_removed.shares_root_with(removed));
}

TEST(Set_CustomComparerDefinesEqualityAndRetainsFirstItem) {
    using set_type = persistent_hash_set<std::string, case_insensitive_hash, case_insensitive_equal>;

    const std::vector<std::string> items = {"Alpha", "ALPHA", "beta"};
    const auto set = set_type::create_range(items, case_insensitive_hash{}, case_insensitive_equal{});

    CHECK_EQ(std::size_t{2}, set.count());
    CHECK(set.contains("alpha"));
    const auto stored_items = set.to_vector();
    CHECK(std::find(stored_items.begin(), stored_items.end(), "ALPHA") == stored_items.end());

    const auto* stored = set.try_get_value("ALPHA");
    CHECK(stored != nullptr);
    CHECK_EQ(std::string("Alpha"), *stored);
}

TEST(Set_AlgebraMatchesUnorderedSet) {
    std::mt19937 rng(0x51A7E5u);
    std::uniform_int_distribution<int> length_dist(0, 120);
    std::uniform_int_distribution<int> value_dist(-30, 30);

    for (int iteration = 0; iteration < 300; ++iteration) {
        std::vector<int> left;
        std::vector<int> right;
        left.reserve(120);
        right.reserve(120);

        const int left_length = length_dist(rng);
        const int right_length = length_dist(rng);
        for (int i = 0; i < left_length; ++i) {
            left.push_back(value_dist(rng));
        }

        for (int i = 0; i < right_length; ++i) {
            right.push_back(value_dist(rng));
        }

        const auto policy = persistent_hash_set<int>::empty();
        const auto persistent = policy.union_with(left);
        const auto persistent_right = policy.union_with(right);
        const std::unordered_set<int> left_model(left.begin(), left.end());
        const std::unordered_set<int> right_model(right.begin(), right.end());

        auto union_model = left_model;
        union_model.insert(right_model.begin(), right_model.end());
        assert_equal_set(union_model, persistent.union_with(right));
        assert_equal_set(union_model, persistent.union_with(persistent_right));

        std::unordered_set<int> intersection_model;
        for (const auto item : left_model) {
            if (right_model.find(item) != right_model.end()) {
                intersection_model.insert(item);
            }
        }
        assert_equal_set(intersection_model, persistent.intersect_with(right));
        assert_equal_set(intersection_model, persistent.intersect_with(persistent_right));

        auto except_model = left_model;
        for (const auto item : right_model) {
            except_model.erase(item);
        }
        assert_equal_set(except_model, persistent.except_with(right));
        assert_equal_set(except_model, persistent.except_with(persistent_right));

        auto symmetric_model = left_model;
        for (const auto item : right_model) {
            if (symmetric_model.find(item) != symmetric_model.end()) {
                symmetric_model.erase(item);
            } else {
                symmetric_model.insert(item);
            }
        }
        assert_equal_set(symmetric_model, persistent.symmetric_except_with(right));
        assert_equal_set(symmetric_model, persistent.symmetric_except_with(persistent_right));

        const bool subset = std::all_of(left_model.begin(), left_model.end(), [&](int item) {
            return right_model.find(item) != right_model.end();
        });
        const bool superset = std::all_of(right_model.begin(), right_model.end(), [&](int item) {
            return left_model.find(item) != left_model.end();
        });
        bool overlaps = false;
        for (const auto item : right_model) {
            overlaps = overlaps || left_model.find(item) != left_model.end();
        }

        CHECK_EQ(subset, persistent.is_subset_of(right));
        CHECK_EQ(subset && left_model.size() < right_model.size(), persistent.is_proper_subset_of(right));
        CHECK_EQ(superset, persistent.is_superset_of(right));
        CHECK_EQ(superset && left_model.size() > right_model.size(), persistent.is_proper_superset_of(right));
        CHECK_EQ(overlaps, persistent.overlaps(right));
        CHECK_EQ(left_model == right_model, persistent.set_equals(right));
        CHECK_EQ(subset, persistent.is_subset_of(persistent_right));
        CHECK_EQ(superset, persistent.is_superset_of(persistent_right));
        CHECK_EQ(overlaps, persistent.overlaps(persistent_right));
        CHECK_EQ(left_model == right_model, persistent.set_equals(persistent_right));
    }
}

TEST(Champ_SamePolicyAlgebraPrunesSharedNodesWithoutRehashing) {
    const auto calls = std::make_shared<std::atomic<std::size_t>>(0);
    using set_type = persistent_hash_set<int, counting_hash>;
    auto basis = set_type::create(counting_hash{calls});
    for (auto value = 0; value < 256; ++value) {
        basis = basis.add(value);
    }
    const auto left = basis.add(1000);
    const auto right = basis.add(2000);

    calls->store(0, std::memory_order_relaxed);
    const auto self_union = left.union_with(left);
    const auto self_intersection = left.intersect_with(left);
    const auto self_except = left.except_with(left);
    const auto self_symmetric = left.symmetric_except_with(left);
    const auto united = left.union_with(right);
    const auto intersected = left.intersect_with(right);
    const auto excepted = left.except_with(right);
    const auto symmetric = left.symmetric_except_with(right);
    CHECK(left.is_subset_of(united));
    CHECK(united.is_superset_of(right));
    CHECK(left.overlaps(right));
    CHECK(left.set_equals(left));
    const auto structural_hashes = calls->load(std::memory_order_relaxed);

    CHECK_EQ(std::size_t{0}, structural_hashes);
    CHECK(self_union.shares_root_with(left));
    CHECK(self_intersection.shares_root_with(left));
    CHECK(self_except.is_empty());
    CHECK(self_symmetric.is_empty());
    CHECK_EQ(std::size_t{258}, united.count());
    CHECK_EQ(std::size_t{256}, intersected.count());
    CHECK_EQ(std::size_t{1}, excepted.count());
    CHECK_EQ(std::size_t{2}, symmetric.count());

    const auto independent = set_type::create(counting_hash{calls}).union_with(
        std::vector<int>{2000, 3000});
    calls->store(0, std::memory_order_relaxed);
    const auto fallback = left.union_with(independent);
    CHECK(calls->load(std::memory_order_relaxed) > 0);
    CHECK_EQ(std::size_t{259}, fallback.count());
}

TEST(Champ_MapAlgebraKeepsLeftKeyRepresentativeAndRightUnionValue) {
    using map_type = persistent_hash_map<
        std::string,
        int,
        case_insensitive_hash,
        case_insensitive_equal>;
    const auto policy = map_type::create(
        case_insensitive_hash{}, case_insensitive_equal{});
    const auto left = policy.set_item("Alpha", 1).set_item("left", 10);
    const auto right = policy.set_item("ALPHA", 2).set_item("right", 20);

    const auto united = left.union_with(right);
    CHECK_EQ(std::size_t{3}, united.count());
    CHECK_EQ(std::string("Alpha"), *united.try_get_key("alpha"));
    CHECK_EQ(2, united.at("alpha"));
    CHECK_EQ(std::size_t{1}, left.intersect_with(right).count());
    CHECK_EQ(std::size_t{1}, left.except_with(right).count());
    CHECK_EQ(std::size_t{2}, left.symmetric_except_with(right).count());
    CHECK(left.union_with(left).shares_root_with(left));
    CHECK(left.intersect_with(left).shares_root_with(left));
}

TEST(Set_AlgebraHonorsCustomComparer) {
    using set_type = persistent_hash_set<std::string, case_insensitive_hash, case_insensitive_equal>;

    const auto set = set_type::create_range(
        std::vector<std::string>{"Alpha", "Beta"},
        case_insensitive_hash{},
        case_insensitive_equal{});

    const auto intersection = set.intersect_with(std::vector<std::string>{"ALPHA"});
    CHECK_EQ(std::size_t{1}, intersection.count());
    CHECK_EQ(std::string("Alpha"), *intersection.try_get_value("alpha"));

    const auto unioned = set.union_with(std::vector<std::string>{"ALPHA", "gamma"});
    CHECK_EQ(std::size_t{3}, unioned.count());
    CHECK(unioned.contains("GAMMA"));
    CHECK_EQ(std::string("Alpha"), *unioned.try_get_value("alpha"));

    const auto symmetric = set.symmetric_except_with(std::vector<std::string>{"BETA", "gamma"});
    CHECK_EQ(std::size_t{2}, symmetric.count());
    CHECK(symmetric.contains("Alpha"));
    CHECK(symmetric.contains("gamma"));
    CHECK(!symmetric.contains("Beta"));

    CHECK(set.is_subset_of(std::vector<std::string>{"ALPHA", "BETA", "x"}));
    CHECK(set.is_proper_subset_of(std::vector<std::string>{"ALPHA", "BETA", "x"}));
    CHECK(!set.is_proper_subset_of(std::vector<std::string>{"ALPHA", "BETA"}));
    CHECK(set.is_superset_of(std::vector<std::string>{"ALPHA"}));
    CHECK(set.is_proper_superset_of(std::vector<std::string>{"ALPHA"}));
    CHECK(!set.is_proper_superset_of(std::vector<std::string>{"ALPHA", "BETA"}));
    CHECK(set.set_equals(std::vector<std::string>{"ALPHA", "beta"}));
    CHECK(set.overlaps(std::vector<std::string>{"ALPHA"}));
}

TEST(Set_SymmetricExceptTreatsInputDuplicatesAsOneItem) {
    const auto set = persistent_hash_set<int>::empty().add(1).add(2);

    assert_equal_set({2, 3}, set.symmetric_except_with(std::vector<int>{1, 1, 3, 3}));
}

TEST(Map_MovedFromMapReadsAsEmpty) {
    auto source = persistent_hash_map<int, int>::empty().set_item(1, 10).set_item(2, 20);

    const auto moved = std::move(source);
    CHECK_EQ(std::size_t{2}, moved.count());
    CHECK(source.is_empty());
    CHECK_EQ(std::size_t{0}, source.count());
    CHECK(source.begin() == source.end());

    auto assign_source = persistent_hash_map<int, int>::empty().set_item(3, 30);
    auto target = persistent_hash_map<int, int>::empty();
    target = std::move(assign_source);
    CHECK_EQ(std::size_t{1}, target.count());
    CHECK(assign_source.is_empty());
    CHECK_EQ(std::size_t{0}, assign_source.count());
}

TEST(Set_MovedFromSetReadsAsEmpty) {
    auto source = persistent_hash_set<int>::empty().add(1).add(2);

    const auto moved = std::move(source);
    CHECK_EQ(std::size_t{2}, moved.count());
    CHECK(source.is_empty());
    CHECK_EQ(std::size_t{0}, source.count());
    CHECK(source.begin() == source.end());
}

TEST(BulkBuilder_FrozenSnapshotsRemainImmutableAcrossBuilderMutations) {
    using map_type = persistent_hash_map<explicit_hash_key, std::string, explicit_hash, explicit_equal>;

    const auto first = explicit_hash_key{1, 0x10};
    const auto collision = explicit_hash_key{2, 0x10};
    const auto branch = explicit_hash_key{3, 0x11};
    auto builder = map_type::create_bulk_builder();

    builder.set_item(first, "first");
    const auto leaf_snapshot = builder.to_immutable();
    builder.set_item(collision, "collision");
    const auto collision_snapshot = builder.to_immutable();
    builder.set_item(branch, "branch");
    builder.set_item(first, "updated");
    const auto branch_snapshot = builder.to_immutable();

    CHECK_EQ(std::size_t{1}, leaf_snapshot.count());
    CHECK_EQ(std::string("first"), leaf_snapshot.at(first));
    CHECK(!leaf_snapshot.contains_key(collision));
    CHECK_EQ(std::size_t{2}, collision_snapshot.count());
    CHECK_EQ(std::string("first"), collision_snapshot.at(first));
    CHECK_EQ(std::string("collision"), collision_snapshot.at(collision));
    CHECK(!collision_snapshot.contains_key(branch));
    CHECK_EQ(std::size_t{3}, branch_snapshot.count());
    CHECK_EQ(std::string("updated"), branch_snapshot.at(first));
    CHECK_EQ(std::string("collision"), branch_snapshot.at(collision));
    CHECK_EQ(std::string("branch"), branch_snapshot.at(branch));
    CHECK(!leaf_snapshot.shares_root_with(collision_snapshot));
    CHECK(!collision_snapshot.shares_root_with(branch_snapshot));
}

TEST(BulkBuilder_EquivalentKeysRetainFirstKeyAndEqualValue) {
    using map_type = persistent_hash_map<
        std::string,
        int,
        case_insensitive_hash,
        case_insensitive_equal,
        mod_ten_equal>;

    auto builder = map_type::create_bulk_builder();
    builder.set_item("Alpha", 12);
    builder.set_item("ALPHA", 22);
    const auto equal_snapshot = builder.to_immutable();
    builder.set_item("alpha", 7);
    const auto replaced_snapshot = builder.to_immutable();

    CHECK_EQ(std::size_t{1}, builder.count());
    const auto* stored_key = equal_snapshot.try_get_key("alpha");
    CHECK(stored_key != nullptr);
    CHECK_EQ(std::string("Alpha"), *stored_key);
    CHECK_EQ(12, equal_snapshot.at("ALPHA"));
    const auto* replaced_key = replaced_snapshot.try_get_key("ALPHA");
    CHECK(replaced_key != nullptr);
    CHECK_EQ(std::string("Alpha"), *replaced_key);
    CHECK_EQ(7, replaced_snapshot.at("alpha"));
}

TEST(BulkBuilder_DeepPrefixKeysBranchAtFinalHashLevel) {
    using map_type = persistent_hash_map<explicit_hash_key, int, explicit_hash, explicit_equal>;

    const auto low = explicit_hash_key{1, 0};
    const auto high = explicit_hash_key{2, 1u << 30};
    auto builder = map_type::create_bulk_builder();
    builder.set_item(low, 10);
    builder.set_item(high, 20);
    const auto map = builder.to_immutable();

    CHECK_EQ(std::size_t{2}, map.count());
    CHECK_EQ(10, map.at(low));
    CHECK_EQ(20, map.at(high));
}

TEST(BulkBuilder_RandomizedBuildMatchesPersistentUpdates) {
    using map_type = persistent_hash_map<explicit_hash_key, int, explicit_hash, explicit_equal>;

    auto builder = map_type::create_bulk_builder();
    auto persistent = map_type::create();
    std::mt19937 random(20260710u);

    for (int i = 0; i < 10000; ++i) {
        const auto id = static_cast<int>(random() % 2000u);
        const auto hash = id % 4 == 0
            ? static_cast<std::uint32_t>(id) & 31u
            : static_cast<std::uint32_t>(id) * 0x01010101u;
        const auto key = explicit_hash_key{id, hash};
        const auto value = static_cast<int>(random() % 100000u);
        builder.set_item(key, value);
        persistent = persistent.set_item(key, value);
    }

    const auto built = builder.to_immutable();
    CHECK_EQ(persistent.count(), built.count());
    CHECK(persistent.to_vector() == built.to_vector());
}

TEST(BulkBuilder_CreateRangeAndIntersectionUseBuilderSemantics) {
    using map_type = persistent_hash_map<
        std::string,
        int,
        case_insensitive_hash,
        case_insensitive_equal,
        mod_ten_equal>;

    const auto map = map_type::create_range(
        std::vector<std::pair<std::string, int>>{{"Alpha", 12}, {"ALPHA", 22}, {"beta", 3}, {"alpha", 5}},
        case_insensitive_hash{},
        case_insensitive_equal{},
        mod_ten_equal{});
    CHECK_EQ(std::size_t{2}, map.count());
    const auto* range_key = map.try_get_key("alpha");
    CHECK(range_key != nullptr);
    CHECK_EQ(std::string("Alpha"), *range_key);
    CHECK_EQ(5, map.at("ALPHA"));
    CHECK_EQ(3, map.at("BETA"));

    using set_type = persistent_hash_set<std::string, case_insensitive_hash, case_insensitive_equal>;
    const auto set = set_type::create_range(
        std::vector<std::string>{"Alpha", "beta", "Gamma"},
        case_insensitive_hash{},
        case_insensitive_equal{});
    const auto intersection = set.intersect_with(std::vector<std::string>{"ALPHA", "GAMMA"});
    CHECK_EQ(std::size_t{2}, intersection.count());
    const auto* intersected = intersection.try_get_value("alpha");
    CHECK(intersected != nullptr);
    CHECK_EQ(std::string("Alpha"), *intersected);
    CHECK(intersection.contains("gamma"));
    CHECK(!intersection.contains("beta"));
}

TEST(TransientMap_CleanAndLogicalNoOpPublicationRetainSourceIdentity) {
    using map_type = persistent_hash_map<
        std::string,
        int,
        case_insensitive_hash,
        case_insensitive_equal,
        mod_ten_equal>;

    static_assert(std::is_move_constructible_v<map_type::transient>);
    static_assert(std::is_move_assignable_v<map_type::transient>);
    static_assert(!std::is_default_constructible_v<map_type::transient>);
    static_assert(!std::is_copy_constructible_v<map_type::transient>);
    static_assert(!std::is_copy_assignable_v<map_type::transient>);

    const auto source = map_type::create(
        case_insensitive_hash{}, case_insensitive_equal{}, mod_ten_equal{})
        .set_item("Alpha", 12);
    auto session = source.to_transient();
    auto iterator = session.begin();

    CHECK_EQ(std::size_t{1}, session.count());
    CHECK(!session.is_empty());
    CHECK(session.contains_key("ALPHA"));
    CHECK_EQ(12, session.at("alpha"));
    CHECK_EQ(std::string("Alpha"), *session.try_get_key("ALPHA"));
    CHECK_EQ(source.hash_function()("Alpha"), session.hash_function()("Alpha"));
    CHECK(session.key_eq()("Alpha", "ALPHA"));
    CHECK(session.value_eq()(12, 22));

    session.set_item("ALPHA", 22);
    CHECK(!session.try_add("alpha", 99));
    CHECK(!session.remove("missing"));
    CHECK(iterator != session.end());
    CHECK_EQ(std::string("Alpha"), iterator->first);
    CHECK_EQ(12, iterator->second);
    CHECK_EQ(12, session.at("alpha"));
    CHECK_EQ(std::size_t{1}, session.to_vector().size());
    CHECK_EQ(std::vector<std::string>{"Alpha"}, session.keys());
    CHECK_EQ(std::vector<int>{12}, session.values());
    CHECK(session.debug_validate_canonical());

    const auto published = std::move(session).persist();
    CHECK(published.shares_root_with(source));
    CHECK(published.shares_policy_with(source));
    CHECK_EQ(std::string("Alpha"), *published.try_get_key("alpha"));
    CHECK_EQ(12, published.at("ALPHA"));

    CHECK_THROWS_AS(session.count(), std::logic_error);
    CHECK_THROWS_AS(session.contains_key("alpha"), std::logic_error);
    CHECK_THROWS_AS(session.begin(), std::logic_error);
    CHECK_THROWS_AS(std::move(session).persist(), std::logic_error);
    CHECK_THROWS_AS(*iterator, std::logic_error);
}

TEST(TransientMap_PointEditsPreserveRepresentativesAndVersionBoundIteration) {
    using map_type = persistent_hash_map<
        std::string,
        int,
        case_insensitive_hash,
        case_insensitive_equal,
        mod_ten_equal>;

    const auto source = map_type::create(
        case_insensitive_hash{}, case_insensitive_equal{}, mod_ten_equal{})
        .set_item("Alpha", 1);
    auto session = source.to_transient();
    auto stale = session.begin();

    session.set_item("BETA", 3);
    CHECK_THROWS_AS(*stale, std::logic_error);
    CHECK_EQ(std::size_t{2}, session.count());
    CHECK_EQ(std::string("BETA"), *session.try_get_key("beta"));

    auto copied_iterator = session.begin();
    auto copied_peer = copied_iterator;
    const auto copied_key = copied_iterator->first;
    const auto copied_value = copied_iterator->second;
    ++copied_iterator;
    CHECK_EQ(copied_key, copied_peer->first);
    CHECK_EQ(copied_value, copied_peer->second);

    auto duplicate_guard = session.begin();
    CHECK_THROWS_AS(session.add("beta", 99), std::invalid_argument);
    CHECK(duplicate_guard != session.end());

    auto equal_replacement_guard = session.begin();
    session.set_item("beta", 13);
    CHECK(equal_replacement_guard != session.end());
    CHECK_EQ(3, session.at("BETA"));

    session.set_item("beta", 4);
    CHECK_THROWS_AS(*equal_replacement_guard, std::logic_error);
    CHECK_EQ(4, session.at("beta"));

    auto absent_remove_guard = session.begin();
    CHECK(!session.remove("missing"));
    CHECK(absent_remove_guard != session.end());
    CHECK(session.remove("ALPHA"));
    CHECK_THROWS_AS(*absent_remove_guard, std::logic_error);
    CHECK(!session.contains_key("alpha"));
    CHECK(source.contains_key("alpha"));
    CHECK(!source.contains_key("beta"));

    auto before_clear = session.begin();
    session.clear();
    CHECK(session.is_empty());
    CHECK_THROWS_AS(before_clear == session.end(), std::logic_error);

    auto empty_guard = session.begin();
    CHECK(empty_guard == session.end());
    session.clear();
    CHECK(empty_guard == session.end());

    const auto published = std::move(session).persist();
    CHECK(published.is_empty());
    CHECK(published.shares_policy_with(source));
    CHECK_EQ(std::size_t{1}, source.count());
}

TEST(TransientMap_MoveTransferAndOverwriteHaveDeterministicLifecycles) {
    using map_type = persistent_hash_map<int, std::string>;

    auto rvalue_source = map_type::empty().set_item(7, "seven");
    auto rvalue_session = std::move(rvalue_source).to_transient();
    CHECK(rvalue_source.is_empty());
    const auto rvalue_publication = std::move(rvalue_session).persist();
    CHECK_EQ(std::string("seven"), rvalue_publication.at(7));

    auto original = map_type::empty().set_item(1, "one").to_transient();
    auto transferred_iterator = original.begin();
    auto transferred = std::move(original);

    CHECK_THROWS_AS(original.count(), std::logic_error);
    CHECK_THROWS_AS(original.begin(), std::logic_error);
    CHECK_EQ(1, transferred_iterator->first);
    CHECK_EQ(std::string("one"), transferred_iterator->second);

    auto target = map_type::create_transient();
    auto overwritten_iterator = target.begin();
    target = std::move(transferred);

    CHECK_THROWS_AS(transferred.count(), std::logic_error);
    CHECK_THROWS_AS(overwritten_iterator == target.end(), std::logic_error);
    CHECK_EQ(std::size_t{1}, target.count());
    CHECK_EQ(std::string("one"), target.at(1));
    CHECK_EQ(1, transferred_iterator->first);

    const auto published = std::move(target).persist();
    CHECK_EQ(std::string("one"), published.at(1));
    CHECK_THROWS_AS(target.count(), std::logic_error);
    CHECK_THROWS_AS(*transferred_iterator, std::logic_error);

    auto destroyed_iterator = map_type::transient::const_iterator{};
    {
        auto doomed = map_type::empty().set_item(9, "nine").to_transient();
        destroyed_iterator = doomed.begin();
    }
    CHECK_THROWS_AS(*destroyed_iterator, std::logic_error);
}

TEST(TransientMoveFailuresInvalidateBothSessionsAndTheirIterators) {
    using map_type = persistent_hash_map<int, int, throwing_move_hash>;

    const auto construction_control = std::make_shared<throwing_move_policy_control>();
    const auto construction_source_map =
        map_type::create(throwing_move_hash{construction_control}).set_item(1, 10);
    auto construction_source = construction_source_map.to_transient();
    auto construction_iterator = construction_source.begin();

    construction_control->throw_on_move_construction = true;
    bool construction_threw = false;
    try {
        auto destination = std::move(construction_source);
        (void)destination;
    } catch (const std::runtime_error&) {
        construction_threw = true;
    }
    construction_control->throw_on_move_construction = false;

    CHECK(construction_threw);
    CHECK_THROWS_AS(construction_source.count(), std::logic_error);
    CHECK_THROWS_AS(*construction_iterator, std::logic_error);

    const auto assignment_control = std::make_shared<throwing_move_policy_control>();
    const auto assignment_source_map =
        map_type::create(throwing_move_hash{assignment_control}).set_item(2, 20);
    const auto assignment_target_map =
        map_type::create(throwing_move_hash{assignment_control}).set_item(3, 30);
    auto assignment_source = assignment_source_map.to_transient();
    auto assignment_target = assignment_target_map.to_transient();
    auto assignment_source_iterator = assignment_source.begin();
    auto assignment_target_iterator = assignment_target.begin();

    assignment_control->throw_on_move_assignment = true;
    CHECK_THROWS_AS(assignment_target = std::move(assignment_source), std::runtime_error);
    assignment_control->throw_on_move_assignment = false;

    CHECK_THROWS_AS(assignment_source.count(), std::logic_error);
    CHECK_THROWS_AS(assignment_target.count(), std::logic_error);
    CHECK_THROWS_AS(*assignment_source_iterator, std::logic_error);
    CHECK_THROWS_AS(*assignment_target_iterator, std::logic_error);

    using set_type = persistent_hash_set<int, throwing_move_hash>;
    const auto set_control = std::make_shared<throwing_move_policy_control>();
    auto set_source = set_type::create(throwing_move_hash{set_control}).add(4).to_transient();
    auto set_target = set_type::create(throwing_move_hash{set_control}).add(5).to_transient();
    auto set_source_iterator = set_source.begin();
    auto set_target_iterator = set_target.begin();

    set_control->throw_on_move_assignment = true;
    CHECK_THROWS_AS(set_target = std::move(set_source), std::runtime_error);
    set_control->throw_on_move_assignment = false;

    CHECK_THROWS_AS(set_source.count(), std::logic_error);
    CHECK_THROWS_AS(set_target.count(), std::logic_error);
    CHECK_THROWS_AS(*set_source_iterator, std::logic_error);
    CHECK_THROWS_AS(*set_target_iterator, std::logic_error);
}

TEST(TransientMap_HashFailureLeavesContentsAndIteratorsUnchanged) {
    using map_type = persistent_hash_map<int, int, controlled_throw_hash>;

    const auto should_throw = std::make_shared<bool>(false);
    const auto source = map_type::create(controlled_throw_hash{should_throw}).set_item(1, 10);
    auto session = source.to_transient();
    auto iterator = session.begin();

    *should_throw = true;
    CHECK_THROWS_AS(session.set_item(2, 20), std::runtime_error);
    *should_throw = false;

    CHECK_EQ(std::size_t{1}, session.count());
    CHECK_EQ(10, session.at(1));
    CHECK(!session.contains_key(2));
    CHECK(iterator != session.end());
    CHECK_EQ(1, iterator->first);
    CHECK(session.debug_validate_canonical());

    const auto published = std::move(session).persist();
    CHECK(published.shares_root_with(source));
    CHECK_EQ(std::size_t{1}, published.count());
}

TEST(TransientMap_RandomizedPathCopySessionMatchesModelAndIsolatesSource) {
    using map_type = persistent_hash_map<int, int, few_buckets_hash>;
    using model_type = std::unordered_map<int, int, few_buckets_hash>;

    auto source = map_type::create(few_buckets_hash{});
    auto initial_model = model_type{};
    for (int key = 0; key != 32; ++key) {
        source = source.set_item(key, key * 10);
        initial_model.emplace(key, key * 10);
    }

    auto model = initial_model;
    auto session = source.to_transient();
    auto random = std::mt19937{20260713u};
    for (int step = 0; step != 5000; ++step) {
        const auto key = static_cast<int>(random() % 257u);
        const auto value = static_cast<int>(random() % 100000u);
        switch (random() % 4u) {
        case 0:
            session.set_item(key, value);
            model[key] = value;
            break;
        case 1: {
            const auto expected = model.emplace(key, value).second;
            CHECK_EQ(expected, session.try_add(key, value));
            break;
        }
        case 2: {
            const auto expected = model.erase(key) != 0;
            CHECK_EQ(expected, session.remove(key));
            break;
        }
        default:
            if (step % 997 == 0) {
                session.clear();
                model.clear();
            } else {
                session.set_item(key, value);
                model[key] = value;
            }
            break;
        }

        if (step % 73 == 0) {
            assert_matches(model, session);
            CHECK(session.debug_validate_canonical());
        }
    }

    assert_matches(initial_model, source);
    const auto published = std::move(session).persist();
    assert_matches(model, published);
    CHECK(published.debug_validate_canonical());
}

TEST(TransientSet_DelegatesLifecycleRepresentativesNoOpsAndIteration) {
    using set_type = persistent_hash_set<
        std::string,
        case_insensitive_hash,
        case_insensitive_equal>;

    static_assert(std::is_move_constructible_v<set_type::transient>);
    static_assert(std::is_move_assignable_v<set_type::transient>);
    static_assert(!std::is_default_constructible_v<set_type::transient>);
    static_assert(!std::is_copy_constructible_v<set_type::transient>);
    static_assert(!std::is_copy_assignable_v<set_type::transient>);

    const auto source = set_type::create_range(
        std::vector<std::string>{"Alpha", "Beta"},
        case_insensitive_hash{},
        case_insensitive_equal{});

    auto clean = source.to_transient();
    const auto clean_publication = std::move(clean).persist();
    CHECK(clean_publication.shares_root_with(source));

    auto session = source.to_transient();
    auto no_op_guard = session.begin();
    CHECK(!session.add("ALPHA"));
    CHECK(!session.remove("missing"));
    CHECK(no_op_guard != session.end());
    CHECK_EQ(std::string("Alpha"), *session.try_get_value("alpha"));
    CHECK_EQ(source.hash_function()("Alpha"), session.hash_function()("Alpha"));
    CHECK(session.key_eq()("Alpha", "ALPHA"));

    CHECK(session.add("gamma"));
    CHECK_THROWS_AS(*no_op_guard, std::logic_error);
    CHECK(session.contains("GAMMA"));
    CHECK(session.remove("BETA"));
    CHECK(!session.remove("beta"));
    CHECK_EQ(std::size_t{2}, session.count());
    CHECK_EQ(std::size_t{2}, session.to_vector().size());
    CHECK(session.debug_validate_canonical());

    auto before_clear = session.begin();
    session.clear();
    CHECK(session.is_empty());
    CHECK_THROWS_AS(*before_clear, std::logic_error);
    auto empty_guard = session.begin();
    session.clear();
    CHECK(empty_guard == session.end());

    const auto published = std::move(session).persist();
    CHECK(published.is_empty());
    CHECK_EQ(std::size_t{2}, source.count());
    CHECK(source.contains("alpha"));
    CHECK(source.contains("beta"));
    CHECK_THROWS_AS(session.count(), std::logic_error);
    CHECK_THROWS_AS(std::move(session).persist(), std::logic_error);

    auto fresh = set_type::create_transient(
        case_insensitive_hash{}, case_insensitive_equal{});
    CHECK(fresh.add("Delta"));
    const auto fresh_publication = std::move(fresh).persist();
    CHECK_EQ(std::string("Delta"), *fresh_publication.try_get_value("delta"));
}

TEST(TransientSet_RelationsUseReceiverPolicyAndRequireActiveSession) {
    using set_type = persistent_hash_set<
        std::string,
        case_insensitive_hash,
        case_insensitive_equal>;

    const auto source = set_type::create_range(
        std::vector<std::string>{"Alpha", "Beta"},
        case_insensitive_hash{},
        case_insensitive_equal{});
    const auto equal_set = set_type::create_range(
        std::vector<std::string>{"alpha", "BETA"},
        case_insensitive_hash{},
        case_insensitive_equal{});
    const auto proper_super = std::vector<std::string>{"ALPHA", "beta", "Gamma", "gamma"};
    const auto proper_sub = std::vector<std::string>{"ALPHA", "alpha"};
    const auto disjoint = std::vector<std::string>{"gamma", "DELTA"};
    const auto empty = std::vector<std::string>{};

    auto session = source.to_transient();
    auto iterator = session.begin();
    CHECK(session.is_subset_of(proper_super));
    CHECK(session.is_proper_subset_of(proper_super));
    CHECK(session.is_superset_of(proper_sub));
    CHECK(session.is_proper_superset_of(proper_sub));
    CHECK(session.overlaps(proper_super));
    CHECK(!session.overlaps(disjoint));
    CHECK(session.set_equals(equal_set));
    CHECK(session.set_equals(std::vector<std::string>{"ALPHA", "beta", "alpha"}));
    CHECK(!session.is_subset_of(proper_sub));
    CHECK(!session.set_equals(proper_super));
    CHECK_EQ(std::string("Alpha"), *session.try_get_value("ALPHA"));
    CHECK(iterator != session.end());

    const auto published = std::move(session).persist();
    CHECK(published.set_equals(source));
    CHECK_THROWS_AS(session.is_subset_of(empty), std::logic_error);
    CHECK_THROWS_AS(session.is_proper_subset_of(empty), std::logic_error);
    CHECK_THROWS_AS(session.is_superset_of(empty), std::logic_error);
    CHECK_THROWS_AS(session.is_proper_superset_of(empty), std::logic_error);
    CHECK_THROWS_AS(session.overlaps(empty), std::logic_error);
    CHECK_THROWS_AS(session.set_equals(empty), std::logic_error);
}

TEST(PersistentMap_FactoryUpdatesValidateBeforeHashAndSelectExactlyOneBranch) {
    const auto hash_calls = std::make_shared<std::atomic<std::size_t>>(0);
    using map_type = persistent_hash_map<int, int, counting_hash>;
    const auto source = map_type::create(counting_hash{hash_calls}).set_item(7, 70);
    hash_calls->store(0, std::memory_order_relaxed);

    auto empty_add = std::function<int(const int&)>{};
    CHECK_THROWS_AS(source.get_or_add(7, empty_add), std::invalid_argument);
    CHECK_EQ(std::size_t{0}, hash_calls->load(std::memory_order_relaxed));
    auto* null_add = static_cast<int (*)(const int&)>(nullptr);
    CHECK_THROWS_AS(source.get_or_add(7, null_add), std::invalid_argument);
    CHECK_EQ(std::size_t{0}, hash_calls->load(std::memory_order_relaxed));

    auto empty_update = std::function<int(const int&, const int&)>{};
    CHECK_THROWS_AS(
        source.add_or_update(7, [](const int&) { return 1; }, empty_update),
        std::invalid_argument);
    CHECK_EQ(std::size_t{0}, hash_calls->load(std::memory_order_relaxed));

    auto add_calls = 0;
    auto update_calls = 0;
    auto [hit, hit_value] = source.add_or_update(
        7,
        [&add_calls](const int&) {
            ++add_calls;
            return -1;
        },
        [&update_calls](const int& lookup_key, const int& stored) {
            ++update_calls;
            CHECK_EQ(7, lookup_key);
            CHECK_EQ(70, stored);
            return 71;
        });
    CHECK_EQ(0, add_calls);
    CHECK_EQ(1, update_calls);
    CHECK_EQ(71, hit_value);
    CHECK_EQ(std::size_t{1}, hash_calls->load(std::memory_order_relaxed));
    CHECK_EQ(71, hit.at(7));

    hash_calls->store(0, std::memory_order_relaxed);
    auto [miss, miss_value] = source.add_or_update(
        8,
        [&add_calls](const int& lookup_key) {
            ++add_calls;
            CHECK_EQ(8, lookup_key);
            return 80;
        },
        [&update_calls](const int&, const int&) {
            ++update_calls;
            return -1;
        });
    CHECK_EQ(1, add_calls);
    CHECK_EQ(1, update_calls);
    CHECK_EQ(80, miss_value);
    CHECK_EQ(std::size_t{1}, hash_calls->load(std::memory_order_relaxed));
    CHECK_EQ(80, miss.at(8));
    CHECK_EQ(std::size_t{1}, source.count());

    hash_calls->store(0, std::memory_order_relaxed);
    auto get_add_calls = 0;
    auto [same, stored] = source.get_or_add(7, [&get_add_calls](const int&) {
        ++get_add_calls;
        return -1;
    });
    CHECK_EQ(0, get_add_calls);
    CHECK_EQ(70, stored);
    CHECK(same.shares_root_with(source));
    CHECK_EQ(std::size_t{1}, hash_calls->load(std::memory_order_relaxed));

    hash_calls->store(0, std::memory_order_relaxed);
    auto [get_miss, get_miss_value] = source.get_or_add(
        9,
        [&get_add_calls](const int& lookup_key) {
            ++get_add_calls;
            CHECK_EQ(9, lookup_key);
            return 90;
        });
    CHECK_EQ(1, get_add_calls);
    CHECK_EQ(90, get_miss_value);
    CHECK_EQ(std::size_t{1}, hash_calls->load(std::memory_order_relaxed));
    CHECK_EQ(90, get_miss.at(9));
}

TEST(PersistentMap_FactoryUpdatesRetainStoredKeyAndValueRepresentatives) {
    using map_type = persistent_hash_map<
        std::string,
        int,
        case_insensitive_hash,
        case_insensitive_equal,
        mod_ten_equal>;
    const auto source = map_type::create(
        case_insensitive_hash{}, case_insensitive_equal{}, mod_ten_equal{})
        .set_item("Alpha", 11);

    auto [equal_update, selected_equal] = source.add_or_update(
        "ALPHA",
        [](const std::string&) { return -1; },
        [](const std::string& lookup_key, const int& stored) {
            CHECK_EQ(std::string("ALPHA"), lookup_key);
            CHECK_EQ(11, stored);
            return 21;
        });
    CHECK(equal_update.shares_root_with(source));
    CHECK_EQ(11, selected_equal);
    CHECK_EQ(std::string("Alpha"), *equal_update.try_get_key("alpha"));
    CHECK_EQ(11, equal_update.at("alpha"));

    auto [changed, selected_changed] = source.add_or_update(
        "ALPHA",
        [](const std::string&) { return -1; },
        [](const std::string&, const int&) { return 22; });
    CHECK(!changed.shares_root_with(source));
    CHECK_EQ(22, selected_changed);
    CHECK_EQ(std::string("Alpha"), *changed.try_get_key("alpha"));
    CHECK_EQ(22, changed.at("alpha"));
}

TEST(PersistentMap_FactoryUpdatesScanOneCollisionPathAndAreFailureAtomic) {
    const auto counts = std::make_shared<champ_pruning_counts>();
    using map_type = persistent_hash_map<
        explicit_hash_key,
        int,
        champ_counting_hash,
        champ_counting_key_equal,
        champ_counting_value_equal>;
    auto source = map_type::create(
        champ_counting_hash{counts},
        champ_counting_key_equal{counts},
        champ_counting_value_equal{counts});
    for (auto id = 1; id <= 3; ++id) {
        source = source.set_item(explicit_hash_key{id, 7}, id * 10);
    }

    counts->reset();
    auto update_calls = 0;
    auto [updated, selected] = source.add_or_update(
        explicit_hash_key{3, 7},
        [](const explicit_hash_key&) { return -1; },
        [&update_calls](const explicit_hash_key& lookup_key, const int& stored) {
            ++update_calls;
            CHECK_EQ(3, lookup_key.id);
            CHECK_EQ(30, stored);
            return 31;
        });
    CHECK_EQ(1, update_calls);
    CHECK_EQ(31, selected);
    CHECK_EQ(std::size_t{1}, counts->hash_calls.load(std::memory_order_relaxed));
    CHECK_EQ(std::size_t{3}, counts->key_equal_calls.load(std::memory_order_relaxed));
    CHECK(updated.debug_validate_canonical());

    const auto source_root = source.debug_root_identity();
    CHECK_THROWS_AS(
        source.add_or_update(
            explicit_hash_key{2, 7},
            [](const explicit_hash_key&) { return -1; },
            [](const explicit_hash_key&, const int&) -> int {
                throw std::runtime_error("factory");
            }),
        std::runtime_error);
    CHECK_EQ(source_root, source.debug_root_identity());
    CHECK_EQ(20, source.at(explicit_hash_key{2, 7}));

    CHECK_THROWS_AS(
        source.get_or_add(
            explicit_hash_key{4, 7},
            [](const explicit_hash_key&) -> int {
                throw std::runtime_error("factory");
            }),
        std::runtime_error);
    CHECK_EQ(source_root, source.debug_root_identity());
    CHECK_EQ(std::size_t{3}, source.count());

    const auto key_equal_failure = std::make_shared<bool>(false);
    using key_throw_map = persistent_hash_map<
        int,
        int,
        std::hash<int>,
        controlled_throw_equal>;
    const auto key_source = key_throw_map::create(
        std::hash<int>{}, controlled_throw_equal{key_equal_failure})
        .set_item(1, 10);
    const auto key_root = key_source.debug_root_identity();
    *key_equal_failure = true;
    CHECK_THROWS_AS(
        key_source.add_or_update(
            1,
            [](const int&) { return -1; },
            [](const int&, const int& stored) { return stored + 1; }),
        std::runtime_error);
    *key_equal_failure = false;
    CHECK_EQ(key_root, key_source.debug_root_identity());
    CHECK_EQ(10, key_source.at(1));

    const auto value_equal_failure = std::make_shared<bool>(false);
    using value_throw_map = persistent_hash_map<
        int,
        int,
        std::hash<int>,
        std::equal_to<int>,
        controlled_throw_value_equal>;
    const auto value_source = value_throw_map::create(
        std::hash<int>{},
        std::equal_to<int>{},
        controlled_throw_value_equal{value_equal_failure})
        .set_item(1, 10);
    const auto value_root = value_source.debug_root_identity();
    *value_equal_failure = true;
    CHECK_THROWS_AS(
        value_source.add_or_update(
            1,
            [](const int&) { return -1; },
            [](const int&, const int& stored) { return stored + 1; }),
        std::runtime_error);
    *value_equal_failure = false;
    CHECK_EQ(value_root, value_source.debug_root_identity());
    CHECK_EQ(10, value_source.at(1));
}

TEST(PersistentMap_BulkBuilderCombinesInOnePathAndKeepsDetachedSnapshots) {
    using map_type = persistent_hash_map<
        std::string,
        int,
        case_insensitive_hash,
        case_insensitive_equal,
        mod_ten_equal>;
    auto builder = map_type::create_bulk_builder(
        case_insensitive_hash{}, case_insensitive_equal{}, mod_ten_equal{});

    CHECK_EQ(11, builder.add_or_update(
        "Alpha", 11, [](const int& stored, const int& incoming) {
            return stored + incoming;
        }));
    CHECK_EQ(11, builder.add_or_update(
        "ALPHA", 10, [](const int& stored, const int& incoming) {
            return stored + incoming;
        }));
    const auto first = builder.to_immutable();
    CHECK_EQ(std::size_t{1}, first.count());
    CHECK_EQ(std::string("Alpha"), *first.try_get_key("alpha"));
    CHECK_EQ(11, first.at("alpha"));

    CHECK_EQ(22, builder.add_or_update(
        "alpha", 1, [](const int& stored, const int& incoming) {
            return stored + incoming + 10;
        }));
    CHECK_EQ(5, builder.add_or_update(
        "Beta", 5, [](const int& stored, const int& incoming) {
            return stored + incoming;
        }));
    const auto second = builder.to_immutable();
    CHECK_EQ(11, first.at("alpha"));
    CHECK_EQ(std::size_t{1}, first.count());
    CHECK_EQ(22, second.at("alpha"));
    CHECK_EQ(5, second.at("beta"));
    CHECK_EQ(std::size_t{2}, second.count());
}

TEST(PersistentMap_BulkBuilderValidatesBeforeOneHashAndSelectsOneBranch) {
    const auto hash_calls = std::make_shared<std::atomic<std::size_t>>(0);
    using map_type = persistent_hash_map<int, int, counting_hash>;
    auto builder = map_type::create_bulk_builder(counting_hash{hash_calls});

    auto empty_update = std::function<int(const int&, const int&)>{};
    CHECK_THROWS_AS(builder.add_or_update(1, 10, empty_update), std::invalid_argument);
    CHECK_EQ(std::size_t{0}, hash_calls->load(std::memory_order_relaxed));

    auto update_calls = 0;
    CHECK_EQ(10, builder.add_or_update(
        1,
        10,
        [&update_calls](const int&, const int&) {
            ++update_calls;
            return -1;
        }));
    CHECK_EQ(0, update_calls);
    CHECK_EQ(std::size_t{1}, hash_calls->load(std::memory_order_relaxed));

    hash_calls->store(0, std::memory_order_relaxed);
    CHECK_EQ(11, builder.add_or_update(
        1,
        1,
        [&update_calls](const int& stored, const int& incoming) {
            ++update_calls;
            return stored + incoming;
        }));
    CHECK_EQ(1, update_calls);
    CHECK_EQ(std::size_t{1}, hash_calls->load(std::memory_order_relaxed));

    hash_calls->store(0, std::memory_order_relaxed);
    CHECK_EQ(20, builder.add_or_update(
        2,
        20,
        [&update_calls](const int&, const int&) {
            ++update_calls;
            return -1;
        }));
    CHECK_EQ(1, update_calls);
    CHECK_EQ(std::size_t{1}, hash_calls->load(std::memory_order_relaxed));
    const auto snapshot = builder.to_immutable();
    CHECK_EQ(11, snapshot.at(1));
    CHECK_EQ(20, snapshot.at(2));
}

TEST(PersistentMap_BulkBuilderCallbackAndComparerFailuresRetainState) {
    const auto should_throw = std::make_shared<bool>(false);
    using map_type = persistent_hash_map<int, int, std::hash<int>, controlled_throw_equal>;
    auto builder = map_type::create_bulk_builder(
        std::hash<int>{}, controlled_throw_equal{should_throw});
    CHECK_EQ(10, builder.add_or_update(
        1, 10, [](const int& stored, const int& incoming) {
            return stored + incoming;
        }));
    const auto before = builder.to_immutable();

    CHECK_THROWS_AS(
        builder.add_or_update(
            1,
            1,
            [](const int&, const int&) -> int {
                throw std::runtime_error("factory");
            }),
        std::runtime_error);
    auto after_factory = builder.to_immutable();
    CHECK_EQ(10, after_factory.at(1));
    CHECK_EQ(std::size_t{1}, after_factory.count());

    *should_throw = true;
    CHECK_THROWS_AS(
        builder.add_or_update(
            1,
            1,
            [](const int& stored, const int& incoming) {
                return stored + incoming;
            }),
        std::runtime_error);
    *should_throw = false;
    auto after_equal = builder.to_immutable();
    CHECK_EQ(10, after_equal.at(1));
    CHECK_EQ(std::size_t{1}, after_equal.count());
    CHECK(!after_equal.shares_root_with(before));

    CHECK_EQ(11, builder.add_or_update(
        1, 1, [](const int& stored, const int& incoming) {
            return stored + incoming;
        }));
    CHECK_EQ(11, builder.to_immutable().at(1));

    const auto value_should_throw = std::make_shared<bool>(false);
    using value_map_type = persistent_hash_map<
        int,
        int,
        std::hash<int>,
        std::equal_to<int>,
        controlled_throw_value_equal>;
    auto value_builder = value_map_type::create_bulk_builder(
        std::hash<int>{},
        std::equal_to<int>{},
        controlled_throw_value_equal{value_should_throw});
    CHECK_EQ(10, value_builder.add_or_update(
        1, 10, [](const int& stored, const int& incoming) {
            return stored + incoming;
        }));
    *value_should_throw = true;
    CHECK_THROWS_AS(
        value_builder.add_or_update(
            1,
            1,
            [](const int& stored, const int& incoming) {
                return stored + incoming;
            }),
        std::runtime_error);
    *value_should_throw = false;
    CHECK_EQ(10, value_builder.to_immutable().at(1));
}

TEST(PersistentHashBag_AggregatesCountsRetainsRepresentativesAndEnumeratesViews) {
    using bag_type = persistent_hash_bag<
        std::string,
        case_insensitive_hash,
        case_insensitive_equal>;
    const auto bag = bag_type::create_range(
        std::vector<std::string>{"Alpha", "ALPHA", "Beta"},
        case_insensitive_hash{},
        case_insensitive_equal{});

    CHECK_EQ(std::size_t{2}, bag.distinct_count());
    CHECK_EQ(std::int64_t{3}, bag.total_count());
    CHECK_EQ(2, bag.count_of("alpha"));
    CHECK_EQ(1, bag.count_of("BETA"));
    CHECK_EQ(0, bag.count_of("missing"));
    CHECK_EQ(std::string("Alpha"), *bag.try_get_value("ALPHA"));
    CHECK_EQ(
        sorted(std::vector<std::string>{"Alpha", "Alpha", "Beta"}),
        sorted(bag.to_vector()));

    auto distinct = std::vector<std::string>{};
    for (const auto& item : bag.distinct_items()) {
        distinct.push_back(item);
    }
    CHECK_EQ(
        sorted(std::vector<std::string>{"Alpha", "Beta"}),
        sorted(std::move(distinct)));

    auto entry_total = std::int64_t{0};
    auto entry_count = std::size_t{0};
    for (const auto& [item, multiplicity] : bag.entries()) {
        CHECK(bag.key_eq()(item, "alpha") || bag.key_eq()(item, "beta"));
        entry_total += multiplicity;
        ++entry_count;
    }
    CHECK_EQ(std::size_t{2}, entry_count);
    CHECK_EQ(std::int64_t{3}, entry_total);
    CHECK(bag.debug_validate_canonical());
}

TEST(PersistentHashBag_PointEditsValidateAndPreserveNoOpIdentity) {
    using bag_type = persistent_hash_bag<int>;
    const auto source = bag_type::empty().add_copies(1, 3).add(2);
    CHECK_EQ(std::int64_t{4}, source.total_count());

    CHECK(source.add_copies(1, 0).shares_root_with(source));
    CHECK(source.remove_copies(1, 0).shares_root_with(source));
    CHECK(source.remove(99).shares_root_with(source));
    CHECK(source.remove_all(99).shares_root_with(source));
    CHECK_THROWS_AS(source.add_copies(1, -1), std::out_of_range);
    CHECK_THROWS_AS(
        source.add_copies(
            1,
            static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max()) + 1),
        std::out_of_range);

    const auto reduced = source.remove_copies(1, 2);
    CHECK_EQ(1, reduced.count_of(1));
    CHECK_EQ(std::int64_t{2}, reduced.total_count());
    const auto saturated = reduced.remove_copies(1, 100);
    CHECK(!saturated.contains(1));
    CHECK_EQ(std::int64_t{1}, saturated.total_count());

    const auto maxed = bag_type::empty().add_copies(
        7, std::numeric_limits<std::int32_t>::max());
    const auto maxed_root = maxed.debug_root_identity();
    CHECK_THROWS_AS(maxed.add(7), std::overflow_error);
    CHECK_EQ(maxed_root, maxed.debug_root_identity());
    CHECK_EQ(std::numeric_limits<std::int32_t>::max(), maxed.count_of(7));

    auto moved_from = source;
    const auto moved_to = std::move(moved_from);
    CHECK(moved_from.is_empty());
    CHECK_EQ(std::int64_t{0}, moved_from.total_count());
    CHECK_EQ(std::int64_t{4}, moved_to.total_count());
}

TEST(PersistentHashBag_AlgebraUsesMultisetCountsAndSharesIdentities) {
    using bag_type = persistent_hash_bag<int>;
    const auto left = bag_type::empty().add_copies(1, 2).add(2);
    const auto right = bag_type::empty().add(1).add_copies(2, 3).add(3);

    const auto united = left.union_with(right);
    CHECK_EQ(2, united.count_of(1));
    CHECK_EQ(3, united.count_of(2));
    CHECK_EQ(1, united.count_of(3));
    CHECK_EQ(std::int64_t{6}, united.total_count());

    const auto intersected = left.intersect_with(right);
    CHECK_EQ(1, intersected.count_of(1));
    CHECK_EQ(1, intersected.count_of(2));
    CHECK_EQ(0, intersected.count_of(3));
    CHECK_EQ(std::int64_t{2}, intersected.total_count());

    const auto subtracted = left.except_with(right);
    CHECK_EQ(1, subtracted.count_of(1));
    CHECK_EQ(0, subtracted.count_of(2));
    CHECK_EQ(std::int64_t{1}, subtracted.total_count());

    const auto summed = left.sum_with(right);
    CHECK_EQ(3, summed.count_of(1));
    CHECK_EQ(4, summed.count_of(2));
    CHECK_EQ(1, summed.count_of(3));
    CHECK_EQ(std::int64_t{8}, summed.total_count());

    CHECK(left.union_with(left).shares_root_with(left));
    CHECK(left.intersect_with(left).shares_root_with(left));
    CHECK(left.except_with(left).is_empty());
    CHECK(left.union_with(bag_type::empty()).shares_root_with(left));
    CHECK(left.sum_with(bag_type::empty()).shares_root_with(left));
}

TEST(PersistentHashBag_NormalizesArgumentToReceiverPolicyAndKeepsPrecedence) {
    using bag_type = persistent_hash_bag<
        std::string,
        configurable_string_hash,
        configurable_string_equal>;
    const auto receiver = bag_type::create(
        configurable_string_hash{true}, configurable_string_equal{true})
        .add_copies("Alpha", 2);
    const auto argument = bag_type::create(
        configurable_string_hash{false}, configurable_string_equal{false})
        .add("ALPHA")
        .add_copies("alpha", 2)
        .add("Beta")
        .add("BETA");

    auto first_argument_beta = std::string{};
    for (const auto& item : argument.distinct_items()) {
        if (case_insensitive_equal{}(item, "beta")) {
            first_argument_beta = item;
            break;
        }
    }
    CHECK(!first_argument_beta.empty());

    const auto united = receiver.union_with(argument);
    CHECK_EQ(3, united.count_of("alpha"));
    CHECK_EQ(2, united.count_of("beta"));
    CHECK_EQ(std::string("Alpha"), *united.try_get_value("ALPHA"));
    CHECK_EQ(first_argument_beta, *united.try_get_value("beta"));
    CHECK_EQ(std::int64_t{5}, united.total_count());

    const auto intersected = receiver.intersect_with(argument);
    CHECK(intersected.shares_root_with(receiver));
    const auto subtracted = receiver.except_with(argument);
    CHECK(subtracted.is_empty());
    const auto summed = receiver.sum_with(argument);
    CHECK_EQ(5, summed.count_of("alpha"));
    CHECK_EQ(2, summed.count_of("beta"));
    CHECK_EQ(std::string("Alpha"), *summed.try_get_value("alpha"));
    CHECK_EQ(std::int64_t{7}, summed.total_count());
}

TEST(PersistentHashBag_CheckedSumFailureLeavesBothOperandsUnchanged) {
    using bag_type = persistent_hash_bag<int>;
    const auto left = bag_type::empty().add_copies(
        1, std::numeric_limits<std::int32_t>::max());
    const auto right = bag_type::empty().add(1);
    const auto left_root = left.debug_root_identity();
    const auto right_root = right.debug_root_identity();

    CHECK_THROWS_AS(left.sum_with(right), std::overflow_error);
    CHECK_EQ(left_root, left.debug_root_identity());
    CHECK_EQ(right_root, right.debug_root_identity());
    CHECK_EQ(std::numeric_limits<std::int32_t>::max(), left.count_of(1));
    CHECK_EQ(1, right.count_of(1));
}

TEST(PersistentBiMap_StrictAddsRejectEitherRepresentedDomain) {
    const auto source = persistent_bi_map<int, std::string>::empty().add(1, "one");
    CHECK_EQ(std::size_t{1}, source.count());
    CHECK_EQ(std::string("one"), source.at(1));
    CHECK_EQ(1, *source.try_get_key("one"));

    const auto [key_conflict_map, key_added, key_conflict] = source.try_add(1, "uno");
    CHECK(!key_added);
    CHECK_EQ(bimap_conflict::key, key_conflict);
    CHECK(key_conflict_map.shares_roots_with(source));

    const auto [value_conflict_map, value_added, value_conflict] = source.try_add(2, "one");
    CHECK(!value_added);
    CHECK_EQ(bimap_conflict::value, value_conflict);
    CHECK(value_conflict_map.shares_roots_with(source));

    const auto [same_pair_map, same_pair_added, same_pair_conflict] = source.try_add(1, "one");
    CHECK(!same_pair_added);
    CHECK_EQ(bimap_conflict::key, same_pair_conflict);
    CHECK(same_pair_map.shares_roots_with(source));

    CHECK_THROWS_AS(source.add(1, "one"), bimap_conflict_error);
    CHECK(source.validate_structure());
}

TEST(PersistentBiMap_IndependentPoliciesRetainRepresentativesAndGovernReplacement) {
    using map_type = persistent_bi_map<
        std::string,
        std::string,
        configurable_string_hash,
        configurable_string_equal,
        configurable_string_hash,
        configurable_string_equal>;
    const auto source = map_type::create(
        configurable_string_hash{true},
        configurable_string_equal{true},
        configurable_string_hash{true},
        configurable_string_equal{true})
        .add("Alpha", "One")
        .add("Beta", "Two");

    CHECK(source.key_hash_function().ignore_case);
    CHECK(source.key_eq().ignore_case);
    CHECK(source.value_hash_function().ignore_case);
    CHECK(source.value_eq().ignore_case);
    CHECK_EQ(std::string("One"), source.at("ALPHA"));
    CHECK_EQ(std::string("Alpha"), *source.try_get_key("ONE"));

    const auto equivalent = source.set_item("ALPHA", "ONE");
    CHECK(equivalent.shares_roots_with(source));
    CHECK_EQ(std::string("One"), equivalent.at("alpha"));

    const auto replaced = source.set_item("ALPHA", "Three");
    CHECK_EQ(std::string("Three"), replaced.at("alpha"));
    CHECK_EQ(std::string("Alpha"), *replaced.try_get_key("THREE"));
    CHECK(!replaced.contains_value("one"));
    CHECK_THROWS_AS(source.set_item("alpha", "TWO"), bimap_conflict_error);
    CHECK(source.validate_structure());
    CHECK(replaced.validate_structure());
}

TEST(PersistentBiMap_SymmetricRemovalAndInverseShareTheSameRoots) {
    const auto source = persistent_bi_map<int, std::string>::empty()
        .add(1, "one")
        .add(2, "two");
    const auto inverse = source.inverse();
    CHECK_EQ(1, inverse.at("one"));
    CHECK(inverse.inverse().shares_roots_with(source));
    CHECK(inverse.validate_structure());

    const auto [after_key, key_removed, removed_value] = source.try_remove_key(1);
    CHECK(key_removed);
    CHECK_EQ(std::optional<std::string>("one"), removed_value);
    CHECK(!after_key.contains_key(1));
    CHECK(!after_key.contains_value("one"));
    CHECK(source.contains_key(1));

    const auto [after_value, value_removed, removed_key] = source.try_remove_value("two");
    CHECK(value_removed);
    CHECK_EQ(std::optional<int>(2), removed_key);
    CHECK(!after_value.contains_key(2));
    CHECK(!after_value.contains_value("two"));

    const auto [key_miss, missed_key, missing_value] = source.try_remove_key(99);
    CHECK(!missed_key);
    CHECK(!missing_value.has_value());
    CHECK(key_miss.shares_roots_with(source));
    const auto [value_miss, missed_value, missing_key] = source.try_remove_value("missing");
    CHECK(!missed_value);
    CHECK(!missing_key.has_value());
    CHECK(value_miss.shares_roots_with(source));
}

TEST(PersistentBiMap_ClearAndEnumerationPreserveForwardContracts) {
    const std::vector<std::pair<int, std::string>> entries{{1, "one"}, {2, "two"}, {3, "three"}};
    const auto source = persistent_bi_map<int, std::string>::create_range(entries);
    CHECK_EQ(std::size_t{3}, source.count());

    std::unordered_map<int, std::string> enumerated;
    for (const auto& [key, value] : source) {
        enumerated.emplace(key, value);
    }
    CHECK_EQ(std::size_t{3}, enumerated.size());
    CHECK_EQ(std::string("two"), enumerated.at(2));

    const auto cleared = source.clear();
    CHECK(cleared.is_empty());
    CHECK(cleared.key_hash_function()(7) == source.key_hash_function()(7));
    CHECK(cleared.value_hash_function()("seven") == source.value_hash_function()("seven"));
    CHECK(cleared.clear().shares_roots_with(cleared));
    CHECK_THROWS_AS(
        (persistent_bi_map<int, std::string>::create_range(
            std::vector<std::pair<int, std::string>>{{1, "one"}, {1, "uno"}})),
        bimap_conflict_error);
}

TEST(PersistentBiMap_RandomHistoryMatchesTwoMapModelAndRetainsSnapshots) {
    using map_type = persistent_bi_map<
        int,
        int,
        few_buckets_hash,
        std::equal_to<int>,
        few_buckets_hash,
        std::equal_to<int>>;
    auto map = map_type::create(few_buckets_hash{}, {}, few_buckets_hash{}, {});
    std::unordered_map<int, int> forward;
    std::unordered_map<int, int> inverse;
    std::vector<map_type> snapshots;
    std::mt19937 random(0xB1A4u);
    std::uniform_int_distribution<int> operation(0, 3);
    std::uniform_int_distribution<int> key_distribution(0, 31);
    std::uniform_int_distribution<int> value_distribution(100, 131);

    for (int step = 0; step < 2'000; ++step) {
        const auto key = key_distribution(random);
        const auto value = value_distribution(random);
        switch (operation(random)) {
        case 0: {
            const auto expected_added = !forward.contains(key) && !inverse.contains(value);
            const auto [candidate, added, conflict] = map.try_add(key, value);
            CHECK_EQ(expected_added, added);
            if (expected_added) {
                CHECK_EQ(bimap_conflict::none, conflict);
                map = candidate;
                forward.emplace(key, value);
                inverse.emplace(value, key);
            } else {
                CHECK_EQ(
                    forward.contains(key) ? bimap_conflict::key : bimap_conflict::value,
                    conflict);
                CHECK(candidate.shares_roots_with(map));
            }
            break;
        }
        case 1: {
            const auto old = forward.find(key);
            const auto claimed = inverse.find(value);
            if (claimed != inverse.end() && (old == forward.end() || claimed->second != key)) {
                const auto before = map;
                CHECK_THROWS_AS(map.set_item(key, value), bimap_conflict_error);
                CHECK(map.shares_roots_with(before));
            } else {
                map = map.set_item(key, value);
                if (old != forward.end()) {
                    inverse.erase(old->second);
                    old->second = value;
                } else {
                    forward.emplace(key, value);
                }
                inverse[value] = key;
            }
            break;
        }
        case 2: {
            const auto old = forward.find(key);
            const auto [candidate, removed, removed_value] = map.try_remove_key(key);
            CHECK_EQ(old != forward.end(), removed);
            if (old != forward.end()) {
                CHECK_EQ(std::optional<int>(old->second), removed_value);
                inverse.erase(old->second);
                forward.erase(old);
                map = candidate;
            } else {
                CHECK(candidate.shares_roots_with(map));
            }
            break;
        }
        default: {
            const auto old = inverse.find(value);
            const auto [candidate, removed, removed_key] = map.try_remove_value(value);
            CHECK_EQ(old != inverse.end(), removed);
            if (old != inverse.end()) {
                CHECK_EQ(std::optional<int>(old->second), removed_key);
                forward.erase(old->second);
                inverse.erase(old);
                map = candidate;
            } else {
                CHECK(candidate.shares_roots_with(map));
            }
            break;
        }
        }

        CHECK_EQ(forward.size(), map.count());
        CHECK(map.validate_structure());
        for (const auto& [expected_key, expected_value] : forward) {
            CHECK_EQ(expected_value, map.at(expected_key));
            CHECK_EQ(expected_key, *map.try_get_key(expected_value));
        }
        if (step % 127 == 0) {
            snapshots.push_back(map);
        }
    }

    for (const auto& snapshot : snapshots) {
        CHECK(snapshot.validate_structure());
    }
}

TEST(PersistentBiMap_PolicyFailureLeavesPublishedSnapshotUnchanged) {
    using map_type = persistent_bi_map<
        int,
        int,
        std::hash<int>,
        std::equal_to<int>,
        controlled_throw_hash,
        std::equal_to<int>>;
    const auto should_throw = std::make_shared<bool>(false);
    const auto source = map_type::create(
        std::hash<int>{},
        std::equal_to<int>{},
        controlled_throw_hash{should_throw},
        std::equal_to<int>{})
        .add(1, 10);
    const auto root_identity = source;

    *should_throw = true;
    CHECK_THROWS_AS(source.add(2, 20), std::runtime_error);
    *should_throw = false;

    CHECK(source.shares_roots_with(root_identity));
    CHECK_EQ(std::size_t{1}, source.count());
    CHECK_EQ(10, source.at(1));
    CHECK(source.validate_structure());
}

TEST(PersistentHashMultimap_RetainsRepresentativesAndContractsGroups) {
    using multimap_type = persistent_hash_multimap<
        std::string,
        std::string,
        case_insensitive_hash,
        case_insensitive_equal,
        case_insensitive_hash,
        case_insensitive_equal>;
    const auto key = std::string{"Alpha"};
    const auto value = std::string{"First"};
    const auto source = multimap_type::create(
        case_insensitive_hash{},
        case_insensitive_equal{},
        case_insensitive_hash{},
        case_insensitive_equal{})
        .add(key, value)
        .add("ALPHA", "FIRST")
        .add("alpha", "Second")
        .add("Beta", "Third");

    CHECK_EQ(std::size_t{2}, source.key_count());
    CHECK_EQ(std::int64_t{3}, source.pair_count());
    CHECK_EQ(key, *source.try_get_key("ALPHA"));
    CHECK_EQ(value, *source.try_get_values("alpha")->try_get_value("FIRST"));
    CHECK(source.debug_validate());

    const auto reduced = source.remove("alpha", "first");
    const auto contracted = reduced.remove("ALPHA", "second");
    CHECK(!contracted.contains_key("alpha"));
    CHECK_EQ(std::size_t{1}, contracted.key_count());
    CHECK_EQ(std::int64_t{1}, contracted.pair_count());
    CHECK(source.contains("Alpha", "First"));
}

TEST(PersistentHashMultimap_NoOpsPoliciesAndWholeKeyRemoval) {
    using multimap_type = persistent_hash_multimap<int, int>;
    const auto source = multimap_type::empty().add(1, 10).add(1, 20).add(2, 20);
    CHECK(source.add(1, 10).shares_root_with(source));
    CHECK(source.remove(1, 99).shares_root_with(source));
    CHECK(source.remove_key(99).shares_root_with(source));

    const auto branch = source.remove_key(1);
    CHECK_EQ(std::size_t{1}, branch.key_count());
    CHECK_EQ(std::int64_t{1}, branch.pair_count());
    CHECK(branch.contains(2, 20));
    CHECK_EQ(std::int64_t{3}, source.pair_count());
    CHECK(branch.debug_validate());
    CHECK(branch.clear().debug_validate());
}

TEST(PersistentRelation_NormalizesGlobalRepresentativesAndSwapsInverseRoots) {
    using relation_type = persistent_relation<
        std::string,
        std::string,
        case_insensitive_hash,
        case_insensitive_equal,
        case_insensitive_hash,
        case_insensitive_equal>;
    const auto relation = relation_type::create(
        case_insensitive_hash{},
        case_insensitive_equal{},
        case_insensitive_hash{},
        case_insensitive_equal{})
        .add("LeftOne", "Right")
        .add("LeftTwo", "RIGHT")
        .add("LEFTONE", "Other");

    CHECK_EQ(std::int64_t{3}, relation.pair_count());
    CHECK_EQ(std::size_t{2}, relation.left_count());
    CHECK_EQ(std::size_t{2}, relation.right_count());
    CHECK_EQ(std::string("Right"), *relation.rights_or_empty("lefttwo").try_get_value("right"));
    CHECK(relation.debug_validate());

    const auto inverse = relation.inverse();
    CHECK(inverse.contains("right", "leftone"));
    CHECK(inverse.inverse().shares_roots_with(relation));
    CHECK(inverse.debug_validate());
}

TEST(PersistentRelation_RemovesPairsAndWholeSidesSymmetrically) {
    using relation_type = persistent_relation<std::string, int>;
    const auto source = relation_type::empty()
        .add("a", 1)
        .add("a", 2)
        .add("b", 2)
        .add("c", 3);
    CHECK(source.add("a", 1).shares_roots_with(source));
    CHECK(source.remove("a", 9).shares_roots_with(source));

    const auto branch = source.remove("a", 2);
    CHECK(!branch.contains("a", 2));
    CHECK(branch.contains("b", 2));
    CHECK(source.contains("a", 2));

    const auto no_a = source.remove_left("a");
    CHECK(!no_a.contains_left("a"));
    CHECK(no_a.contains("b", 2));
    const auto no_two = source.remove_right(2);
    CHECK(!no_two.contains_right(2));
    CHECK(no_two.contains("a", 1));
    CHECK(no_a.debug_validate());
    CHECK(no_two.debug_validate());
}

TEST(PersistentMapPatch_BetweenApplyInvertAndComposeRoundTrip) {
    using map_type = persistent_hash_map<int, std::string>;
    using patch_type = persistent_map_patch<int, std::string>;
    const auto source = map_type{}.set_item(1, "one").set_item(2, "two");
    const auto middle = source.remove(1).set_item(2, "TWO").set_item(3, "three");
    const auto target = middle.set_item(3, "THREE").set_item(4, "four");

    const auto first = patch_type::between(source, middle);
    const auto second = patch_type::between(middle, target);
    const auto composed = first.compose(second);
    CHECK(composed.apply(source).map_equals(target));
    CHECK(composed.invert().apply(target).map_equals(source));
    CHECK(first.debug_validate());
    CHECK(composed.debug_validate());
}

TEST(PersistentMapPatch_IsStrictPresenceSafeAndPreservesNoOpIdentity) {
    using value_type = std::optional<int>;
    using map_type = persistent_hash_map<int, value_type>;
    using patch_type = persistent_map_patch<int, value_type>;
    const auto source = map_type{}.set_item(1, std::nullopt);
    const auto target = source.set_item(2, value_type{7});
    const auto patch = patch_type::between(source, target);
    CHECK_EQ(std::size_t{1}, patch.count());
    CHECK(patch.apply(source).map_equals(target));
    const auto conflicting = source.set_item(2, value_type{9});
    const auto [unchanged, conflict] = patch.try_apply(conflicting);
    CHECK(conflict.has_value());
    CHECK(unchanged.shares_root_with(conflicting));

    const auto no_op = patch.add(map_patch_entry<int, value_type>{
        8, value_type{3}, value_type{3}});
    CHECK(no_op.shares_root_with(patch));
    CHECK_THROWS_AS(
        patch.add(map_patch_entry<int, value_type>{
            2, std::nullopt, value_type{8}}),
        std::invalid_argument);
}

TEST(PersistentMapPatch_ComposeRejectsMismatchedIntermediateState) {
    using patch_type = persistent_map_patch<int, int>;
    const auto first = patch_type{}.add({1, 10, 20});
    const auto next = patch_type{}.add({1, 99, 30});
    CHECK_THROWS_AS(first.compose(next), std::invalid_argument);
    CHECK(first.remove(8).shares_root_with(first));
}

TEST(PersistentDirectedGraph_AddsEndpointsAndMaintainsBothDirections) {
    const auto graph = persistent_directed_graph<std::string>{}
        .add_vertex("isolated")
        .add_edge("a", "b")
        .add_edge("a", "c")
        .add_edge("b", "c");
    CHECK_EQ(std::size_t{4}, graph.vertex_count());
    CHECK_EQ(std::int64_t{3}, graph.edge_count());
    CHECK_EQ(std::size_t{2}, graph.out_degree("a"));
    CHECK_EQ(std::size_t{2}, graph.in_degree("c"));
    CHECK(graph.successors_or_empty("a").contains("b"));
    CHECK(graph.predecessors_or_empty("c").contains("b"));
    CHECK(graph.debug_validate());
}

TEST(PersistentDirectedGraph_ReverseAndRemovalRetainPersistentBranches) {
    const auto source = persistent_directed_graph<int>{}
        .add_edge(1, 2).add_edge(2, 3).add_edge(3, 1).add_vertex(9);
    const auto reversed = source.reversed();
    CHECK(reversed.contains_edge(2, 1));
    CHECK(reversed.reversed().shares_roots_with(source));
    const auto reduced = source.remove_vertex(2);
    CHECK(!reduced.contains_vertex(2));
    CHECK(!reduced.contains_edge(1, 2));
    CHECK(source.contains_edge(1, 2));
    CHECK(source.remove_edge(8, 9).shares_roots_with(source));
    CHECK(reduced.debug_validate());
}

TEST(PersistentDirectedGraph_CustomEqualityRetainsVertexRepresentatives) {
    using graph_type = persistent_directed_graph<
        std::string, case_insensitive_hash, case_insensitive_equal>;
    const auto graph = graph_type::create(
        case_insensitive_hash{}, case_insensitive_equal{})
        .add_edge("Source", "Target")
        .add_edge("SOURCE", "TARGET");
    CHECK_EQ(std::int64_t{1}, graph.edge_count());
    CHECK_EQ(std::string{"Source"}, *graph.try_get_vertex("source"));
    CHECK(graph.debug_validate());
}

TEST(PersistentIndexedMap_MaintainsNonUniqueSecondaryGroups) {
    using selector_type = std::function<char(const int&, const std::string&)>;
    using map_type = persistent_indexed_map<int, std::string, char, selector_type>;
    auto selector = selector_type{[](const int&, const std::string& value) {
        return value.front();
    }};
    const auto map = map_type::create(selector)
        .add(1, "apple").add(2, "apricot").add(3, "banana");
    CHECK_EQ(std::size_t{3}, map.count());
    CHECK_EQ(std::size_t{2}, map.index_key_count());
    CHECK_EQ(std::size_t{2}, map.count_by_index('a'));
    CHECK(map.keys_by_index_or_empty('a').contains(2));
    CHECK(map.debug_validate());
}

TEST(PersistentIndexedMap_UpdatesMoveMembershipAndSuppressEqualValues) {
    using selector_type = std::function<char(const int&, const std::string&)>;
    using map_type = persistent_indexed_map<int, std::string, char, selector_type>;
    auto calls = 0;
    const auto map = map_type::create(selector_type{[&calls](const int&, const std::string& value) {
        ++calls;
        return value.front();
    }}).add(1, "apple");
    const auto calls_after_add = calls;
    const auto unchanged = map.set_item(1, "apple");
    CHECK(unchanged.shares_roots_with(map));
    CHECK_EQ(calls_after_add, calls);
    const auto moved = map.set_item(1, "banana");
    CHECK_EQ(std::size_t{0}, moved.count_by_index('a'));
    CHECK_EQ(std::size_t{1}, moved.count_by_index('b'));
    CHECK_EQ(std::string{"banana"}, moved.at(1));
    CHECK(moved.debug_validate());
}

TEST(PersistentIndexedMap_StrictAddsRemovalAndBranchesArePersistent) {
    using selector_type = std::function<int(const int&, const int&)>;
    using map_type = persistent_indexed_map<int, int, int, selector_type>;
    const auto root = map_type::create(selector_type{
        [](const int&, const int& value) { return value % 2; }})
        .add(1, 10).add(2, 11);
    CHECK_THROWS_AS(root.add(1, 12), std::invalid_argument);
    const auto [same, added] = root.try_add(1, 12);
    CHECK(!added);
    CHECK(same.shares_roots_with(root));
    const auto left = root.set_item(1, 13);
    const auto right = root.remove(2);
    CHECK_EQ(10, root.at(1));
    CHECK_EQ(13, left.at(1));
    CHECK(!right.contains_key(2));
    CHECK(root.remove(9).shares_roots_with(root));
    CHECK(left.debug_validate() && right.debug_validate());
}

TEST(PatriciaMap_CachedCountsAndNoOpAlgebraPreserveRoots) {
    using map_type = tools::data_structures::hamt::persistent_int_map<std::string>;

    const auto left = map_type{}
        .set_item(1, "one")
        .set_item(2, "two")
        .set_item(3, "three");
    const auto subset = map_type{}
        .set_item(1, "one")
        .set_item(2, "two");
    const auto disjoint = map_type{}.set_item(4, "four");

    CHECK_EQ(std::size_t{3}, left.size());
    CHECK(left.union_with(left).shares_root_with(left));
    CHECK(left.intersect_with(left).shares_root_with(left));
    CHECK(left.except_with(map_type{}).shares_root_with(left));
    CHECK(left.union_with(subset).shares_root_with(left));
    CHECK_EQ(std::size_t{4}, left.union_with(disjoint).size());
    CHECK_EQ(std::size_t{2}, left.intersect_with(subset).size());
    CHECK_EQ(std::size_t{1}, left.except_with(subset).size());
}

} // namespace

int main() {
    if (!tds_enter_headless_test_process()) {
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
