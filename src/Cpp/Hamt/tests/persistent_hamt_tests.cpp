#include <Tools/DataStructures/Hamt/persistent_hash_map.hpp>
#include <Tools/DataStructures/Hamt/persistent_hash_set.hpp>
#include <tools/data_structures/test_support/headless_test_process.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <exception>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <mutex>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using tools::data_structures::hamt::persistent_hamt_node_kind;
using tools::data_structures::hamt::persistent_hash_map;
using tools::data_structures::hamt::persistent_hash_set;

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

struct few_buckets_hash {
    std::size_t operator()(int value) const noexcept {
        return static_cast<std::uint32_t>(value) & 3u;
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

    CHECK_EQ(std::string("a"), map.at(a));
    CHECK_EQ(std::string("b"), map.at(b));
    CHECK_EQ(std::string("c"), map.at(c));
    CHECK_EQ(std::string("d"), map.at(d));
    CHECK_EQ(std::vector<std::string>({"a", "b", "c", "d"}), sorted(map.values()));

    const auto reduced = map.remove(b).remove(c).remove(d);
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

    CHECK_EQ(std::size_t{2}, before_children.size());
    CHECK_EQ(std::size_t{2}, after_children.size());
    CHECK(before_children[0] != after_children[0]);
    CHECK(before_children[1] == after_children[1]);
    CHECK(map.set_item(a, "a").shares_root_with(map));
    CHECK(map.remove(explicit_hash_key{9, 0x09}).shares_root_with(map));
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

        const auto persistent = persistent_hash_set<int>::create_range(left);
        const std::unordered_set<int> left_model(left.begin(), left.end());
        const std::unordered_set<int> right_model(right.begin(), right.end());

        auto union_model = left_model;
        union_model.insert(right_model.begin(), right_model.end());
        assert_equal_set(union_model, persistent.union_with(right));

        std::unordered_set<int> intersection_model;
        for (const auto item : left_model) {
            if (right_model.find(item) != right_model.end()) {
                intersection_model.insert(item);
            }
        }
        assert_equal_set(intersection_model, persistent.intersect_with(right));

        auto except_model = left_model;
        for (const auto item : right_model) {
            except_model.erase(item);
        }
        assert_equal_set(except_model, persistent.except_with(right));

        auto symmetric_model = left_model;
        for (const auto item : right_model) {
            if (symmetric_model.find(item) != symmetric_model.end()) {
                symmetric_model.erase(item);
            } else {
                symmetric_model.insert(item);
            }
        }
        assert_equal_set(symmetric_model, persistent.symmetric_except_with(right));

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
    }
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
