#include <tools/data_structures/tungsten/tungsten.hpp>

#include "../../FingerTree/tests/test_support/test_runner.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace wf = tools::data_structures::tungsten;
using namespace tools::data_structures::finger_tree::tests;

namespace {

class deterministic_rng final {
public:
    explicit deterministic_rng(const std::uint64_t seed)
        : state_(seed)
    {
    }

    [[nodiscard]] std::uint32_t next()
    {
        state_ = state_ * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<std::uint32_t>(state_ >> 32);
    }

    [[nodiscard]] std::size_t next_index(const std::size_t exclusive)
    {
        return exclusive == 0 ? 0 : static_cast<std::size_t>(next()) % exclusive;
    }

    [[nodiscard]] int next_int(const int low, const int high)
    {
        return low + static_cast<int>(next_index(static_cast<std::size_t>(high - low + 1)));
    }

private:
    std::uint64_t state_;
};

template <class T>
std::string describe_vector(const std::vector<T>& values)
{
    auto stream = std::ostringstream{};
    stream << '[';
    for (auto index = std::size_t{0}; index != values.size(); ++index) {
        if (index != 0) {
            stream << ", ";
        }

        stream << values[index];
    }

    stream << ']';
    return stream.str();
}

template <class K, class V>
std::string describe_vector(const std::vector<std::pair<K, V>>& values)
{
    auto stream = std::ostringstream{};
    stream << '[';
    for (auto index = std::size_t{0}; index != values.size(); ++index) {
        if (index != 0) {
            stream << ", ";
        }

        stream << values[index].first << " -> " << values[index].second;
    }

    stream << ']';
    return stream.str();
}

template <class T>
void require_vector_equal(
    const std::vector<T>& actual,
    const std::vector<T>& expected,
    const std::source_location location = std::source_location::current())
{
    if (actual == expected) {
        return;
    }

    auto message = std::ostringstream{};
    message << location.file_name() << ':' << location.line() << ": vector mismatch. Actual "
            << describe_vector(actual) << ", expected " << describe_vector(expected);
    throw test_failure(message.str());
}

template <class K, class V>
void require_association_equal(
    const wf::persistent_association<K, V>& actual,
    const std::vector<std::pair<K, V>>& expected,
    const std::source_location location = std::source_location::current())
{
    require_vector_equal(actual.to_vector(), expected, location);
    FT_REQUIRE_EQUAL(actual.size(), expected.size());

    auto expected_keys = std::vector<K>{};
    auto expected_values = std::vector<V>{};
    expected_keys.reserve(expected.size());
    expected_values.reserve(expected.size());
    for (const auto& [key, value] : expected) {
        expected_keys.push_back(key);
        expected_values.push_back(value);
    }

    require_vector_equal(actual.keys(), expected_keys, location);
    require_vector_equal(actual.values(), expected_values, location);

    for (auto index = std::size_t{0}; index != expected.size(); ++index) {
        FT_REQUIRE(actual.get_at(index) == expected[index]);
        FT_REQUIRE_EQUAL(actual.index_of_key(expected[index].first), static_cast<std::ptrdiff_t>(index));
        const auto* value = actual.try_get(expected[index].first);
        FT_REQUIRE(value != nullptr);
        FT_REQUIRE_EQUAL(*value, expected[index].second);
    }
}

template <class K, class V>
std::ptrdiff_t find_key(const std::vector<std::pair<K, V>>& model, const K& key)
{
    const auto found = std::ranges::find_if(model, [&](const auto& pair) {
        return pair.first == key;
    });
    return found == model.end() ? -1 : static_cast<std::ptrdiff_t>(found - model.begin());
}

struct case_insensitive_hash final {
    [[nodiscard]] std::size_t operator()(const std::string& value) const
    {
        auto hash = std::size_t{1469598103934665603ULL};
        for (const auto ch : value) {
            hash ^= static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(ch)));
            hash *= std::size_t{1099511628211ULL};
        }

        return hash;
    }
};

struct case_insensitive_equal final {
    [[nodiscard]] bool operator()(const std::string& left, const std::string& right) const
    {
        if (left.size() != right.size()) {
            return false;
        }

        for (auto index = std::size_t{0}; index != left.size(); ++index) {
            if (std::tolower(static_cast<unsigned char>(left[index]))
                != std::tolower(static_cast<unsigned char>(right[index]))) {
                return false;
            }
        }

        return true;
    }
};

void add_persistent_list_tests(suite& tests)
{
    tests.add("persistent list constructs and preserves Tungsten-style order", [] {
        const auto empty = wf::persistent_list<int>::empty_list();
        FT_REQUIRE(empty.is_empty());
        FT_REQUIRE_EQUAL(empty.size(), std::size_t{0});

        const auto list = wf::persistent_list<int>{1, 2, 3};
        FT_REQUIRE_EQUAL(list.size(), std::size_t{3});
        FT_REQUIRE_EQUAL(list.front(), 1);
        FT_REQUIRE_EQUAL(list.back(), 3);
        FT_REQUIRE_EQUAL(list[1], 2);
        require_vector_equal(list.to_vector(), std::vector<int>{1, 2, 3});
    });

    tests.add("persistent list updates leave source versions readable", [] {
        const auto source = wf::persistent_list<int>{2, 3};
        const auto appended = source.append(4);
        const auto prepended = source.prepend(1);
        const auto joined = prepended.join(appended);

        require_vector_equal(source.to_vector(), std::vector<int>{2, 3});
        require_vector_equal(appended.to_vector(), std::vector<int>{2, 3, 4});
        require_vector_equal(prepended.to_vector(), std::vector<int>{1, 2, 3});
        require_vector_equal(joined.to_vector(), std::vector<int>{1, 2, 3, 2, 3, 4});
    });

    tests.add("persistent list positional operations match vector behavior", [] {
        auto list = wf::persistent_list<int>{1, 4};
        list = list.insert(1, 2).insert(2, 3).append(5);
        require_vector_equal(list.to_vector(), std::vector<int>{1, 2, 3, 4, 5});

        require_vector_equal(list.remove_at(2).to_vector(), std::vector<int>{1, 2, 4, 5});
        require_vector_equal(list.remove_range(1, 2).to_vector(), std::vector<int>{1, 4, 5});
        require_vector_equal(list.set_item(1, 20).to_vector(), std::vector<int>{1, 20, 3, 4, 5});
        require_vector_equal(list.update_at(1, [](const int value) { return value * 10; }).to_vector(),
            std::vector<int>{1, 20, 3, 4, 5});
        require_vector_equal(list.take(3).to_vector(), std::vector<int>{1, 2, 3});
        require_vector_equal(list.take_last(2).to_vector(), std::vector<int>{4, 5});
        require_vector_equal(list.drop(2).to_vector(), std::vector<int>{3, 4, 5});
        require_vector_equal(list.drop_last(2).to_vector(), std::vector<int>{1, 2, 3});

        const auto split = list.split_at(2);
        require_vector_equal(split.left.to_vector(), std::vector<int>{1, 2});
        require_vector_equal(split.right.to_vector(), std::vector<int>{3, 4, 5});
    });

    tests.add("persistent list reverse map and search expose List vocabulary", [] {
        const auto list = wf::persistent_list<std::string>{"a", "B", "c"};
        require_vector_equal(list.reverse().to_vector(), std::vector<std::string>{"c", "B", "a"});
        require_vector_equal(list.map([](const std::string& value) { return value + "!"; }).to_vector(),
            std::vector<std::string>{"a!", "B!", "c!"});
        FT_REQUIRE_EQUAL(list.index_of(std::string{"B"}), std::ptrdiff_t{1});
        FT_REQUIRE_EQUAL(list.index_of(std::string{"b"}), std::ptrdiff_t{-1});
        FT_REQUIRE_EQUAL(list.index_of(std::string{"b"}, case_insensitive_equal{}), std::ptrdiff_t{1});
        FT_REQUIRE(list.contains(std::string{"c"}));
    });

    tests.add("persistent list generated histories match a vector model and retained snapshots", [] {
        auto rng = deterministic_rng{0x51A7E57U};
        auto list = wf::persistent_list<int>{};
        auto model = std::vector<int>{};
        auto snapshots = std::vector<std::pair<wf::persistent_list<int>, std::vector<int>>>{};

        for (auto step = 0; step != 500; ++step) {
            switch (rng.next_index(10)) {
            case 0: {
                const auto value = rng.next_int(-1000, 1000);
                list = list.append(value);
                model.push_back(value);
                break;
            }
            case 1: {
                const auto value = rng.next_int(-1000, 1000);
                list = list.prepend(value);
                model.insert(model.begin(), value);
                break;
            }
            case 2: {
                const auto index = rng.next_index(model.size() + 1);
                const auto value = rng.next_int(-1000, 1000);
                list = list.insert(index, value);
                model.insert(model.begin() + static_cast<std::ptrdiff_t>(index), value);
                break;
            }
            case 3:
                if (!model.empty()) {
                    const auto index = rng.next_index(model.size());
                    list = list.remove_at(index);
                    model.erase(model.begin() + static_cast<std::ptrdiff_t>(index));
                }
                break;
            case 4:
                if (!model.empty()) {
                    const auto index = rng.next_index(model.size());
                    const auto value = rng.next_int(-1000, 1000);
                    list = list.set_item(index, value);
                    model[index] = value;
                }
                break;
            case 5: {
                const auto count = rng.next_index(model.size() + 1);
                list = list.take(count);
                model.erase(model.begin() + static_cast<std::ptrdiff_t>(count), model.end());
                break;
            }
            case 6: {
                const auto count = rng.next_index(model.size() + 1);
                list = list.drop(count);
                model.erase(model.begin(), model.begin() + static_cast<std::ptrdiff_t>(count));
                break;
            }
            case 7:
                list = list.reverse();
                std::ranges::reverse(model);
                break;
            case 8: {
                const auto value = rng.next_int(-1000, 1000);
                const auto suffix = wf::persistent_list<int>{value, value + 1};
                list = list.join(suffix);
                model.push_back(value);
                model.push_back(value + 1);
                break;
            }
            default:
                snapshots.push_back({list, model});
                if (snapshots.size() > 32) {
                    snapshots.erase(snapshots.begin());
                }
                break;
            }

            require_vector_equal(list.to_vector(), model);
        }

        for (const auto& [snapshot, expected] : snapshots) {
            require_vector_equal(snapshot.to_vector(), expected);
        }
    });
}

void add_persistent_association_tests(suite& tests)
{
    tests.add("persistent association construction keeps duplicate first position with last value", [] {
        const auto assoc = wf::persistent_association<std::string, int>::create_range({
            {"a", 1},
            {"b", 2},
            {"a", 3},
        });
        require_association_equal(assoc, {{"a", 3}, {"b", 2}});
    });

    tests.add("persistent association SetItem Append Prepend and Join follow Tungsten ordering rules", [] {
        const auto assoc = wf::persistent_association<std::string, int>{{"a", 1}, {"b", 2}};

        require_association_equal(assoc.set_item("a", 5), {{"a", 5}, {"b", 2}});
        require_association_equal(assoc.set_item("c", 3), {{"a", 1}, {"b", 2}, {"c", 3}});
        require_association_equal(assoc.append("a", 3), {{"b", 2}, {"a", 3}});
        require_association_equal(assoc.prepend("b", 3), {{"b", 3}, {"a", 1}});

        const auto joined = assoc.join(wf::persistent_association<std::string, int>{{"a", 3}, {"c", 4}});
        require_association_equal(joined, {{"a", 3}, {"b", 2}, {"c", 4}});
        require_association_equal(assoc, {{"a", 1}, {"b", 2}});
    });

    tests.add("persistent association Insert existing key wins interpreted source position", [] {
        const auto assoc = wf::persistent_association<std::string, int>{{"a", 1}, {"b", 2}, {"c", 3}};

        require_association_equal(assoc.insert(2, "a", 9), {{"b", 2}, {"a", 9}, {"c", 3}});
        require_association_equal(assoc.insert(0, "c", 9), {{"c", 9}, {"a", 1}, {"b", 2}});
        require_association_equal(assoc.insert(3, "a", 9), {{"b", 2}, {"c", 3}, {"a", 9}});
        require_association_equal(assoc.insert(0, "a", 9), {{"a", 9}, {"b", 2}, {"c", 3}});
    });

    tests.add("persistent association removal slicing reverse and key take stay order aware", [] {
        const auto assoc = wf::persistent_association<std::string, int>{{"a", 1}, {"b", 2}, {"c", 3}};

        require_association_equal(assoc.remove("b"), {{"a", 1}, {"c", 3}});
        require_association_equal(assoc.remove_at(1), {{"a", 1}, {"c", 3}});
        require_association_equal(assoc.remove_first(), {{"b", 2}, {"c", 3}});
        require_association_equal(assoc.remove_last(), {{"a", 1}, {"b", 2}});
        require_association_equal(assoc.take(2), {{"a", 1}, {"b", 2}});
        require_association_equal(assoc.drop(1), {{"b", 2}, {"c", 3}});
        require_association_equal(assoc.get_range(1, 1), {{"b", 2}});
        require_association_equal(assoc.reverse(), {{"c", 3}, {"b", 2}, {"a", 1}});
        require_association_equal(assoc.key_take(std::vector<std::string>{"c", "missing", "a", "c"}),
            {{"c", 3}, {"a", 1}});
        require_association_equal(assoc.remove_range(std::vector<std::string>{"c", "a", "missing"}), {{"b", 2}});
    });

    tests.add("persistent association sort and key_sort are stable ordinary associations", [] {
        const auto assoc = wf::persistent_association<std::string, int>{{"b", 2}, {"c", 0}, {"a", 1}};

        require_association_equal(assoc.key_sort(), {{"a", 1}, {"b", 2}, {"c", 0}});
        require_association_equal(assoc.sort(), {{"c", 0}, {"a", 1}, {"b", 2}});

        const auto ties = wf::persistent_association<std::string, int>{{"b", 7}, {"a", 7}, {"c", 7}};
        require_association_equal(ties.sort(), {{"b", 7}, {"a", 7}, {"c", 7}});
        require_association_equal(assoc.key_sort().set_item("d", 9), {{"a", 1}, {"b", 2}, {"c", 0}, {"d", 9}});
    });

    tests.add("persistent association keyed reads custom equality and stored key recovery work", [] {
        using assoc_type =
            wf::persistent_association<std::string, int, case_insensitive_hash, case_insensitive_equal>;

        const auto assoc = assoc_type::create(case_insensitive_hash{}, case_insensitive_equal{})
                               .set_item("Alpha", 1)
                               .set_item("beta", 2);

        FT_REQUIRE_EQUAL(assoc.at("ALPHA"), 1);
        FT_REQUIRE_EQUAL(assoc.index_of_key("ALPHA"), std::ptrdiff_t{0});
        const auto* stored = assoc.try_get_key("aLpHa");
        FT_REQUIRE(stored != nullptr);
        FT_REQUIRE_EQUAL(*stored, std::string{"Alpha"});

        const auto updated = assoc.set_item("ALPHA", 9);
        FT_REQUIRE_EQUAL(updated.get_at(0).first, std::string{"Alpha"});

        const auto appended = assoc.append("ALPHA", 9);
        FT_REQUIRE_EQUAL(appended.get_at(1).first, std::string{"ALPHA"});

        const auto taken = assoc.key_take(std::vector<std::string>{"ALPHA"});
        FT_REQUIRE_EQUAL(taken.get_at(0).first, std::string{"Alpha"});
        FT_REQUIRE_EQUAL(taken.at("alpha"), 1);
    });

    tests.add("persistent association relabels exhausted stamp gaps without losing lookup positions", [] {
        auto assoc = wf::persistent_association<std::string, int>{{"start", -1}, {"end", -2}};
        for (auto index = 0; index != 100; ++index) {
            assoc = assoc.insert(1, "k" + std::to_string(index), index);
        }

        FT_REQUIRE_EQUAL(assoc.size(), std::size_t{102});
        FT_REQUIRE_EQUAL(assoc.get_at(0).first, std::string{"start"});
        FT_REQUIRE_EQUAL(assoc.get_at(101).first, std::string{"end"});
        for (auto index = 0; index != 100; ++index) {
            const auto key = "k" + std::to_string(index);
            FT_REQUIRE_EQUAL(assoc.at(key), index);
            FT_REQUIRE_EQUAL(assoc.index_of_key(key), static_cast<std::ptrdiff_t>(1 + (99 - index)));
        }
    });

    tests.add("persistent association generated histories match ordered model and retained snapshots", [] {
        auto rng = deterministic_rng{0xA550C1A710BULL};
        auto assoc = wf::persistent_association<int, int>{};
        auto model = std::vector<std::pair<int, int>>{};
        auto snapshots = std::vector<std::pair<wf::persistent_association<int, int>, std::vector<std::pair<int, int>>>>{};

        for (auto step = 0; step != 700; ++step) {
            const auto key = rng.next_int(-20, 20);
            const auto value = rng.next_int(-1000, 1000);
            const auto position = rng.next_index(model.size() + 1);

            switch (rng.next_index(11)) {
            case 0: {
                assoc = assoc.set_item(key, value);
                const auto found = find_key(model, key);
                if (found >= 0) {
                    model[static_cast<std::size_t>(found)] = {key, value};
                } else {
                    model.push_back({key, value});
                }
                break;
            }
            case 1: {
                assoc = assoc.append(key, value);
                const auto found = find_key(model, key);
                if (found >= 0) {
                    model.erase(model.begin() + found);
                }
                model.push_back({key, value});
                break;
            }
            case 2: {
                assoc = assoc.prepend(key, value);
                const auto found = find_key(model, key);
                if (found >= 0) {
                    model.erase(model.begin() + found);
                }
                model.insert(model.begin(), {key, value});
                break;
            }
            case 3: {
                assoc = assoc.remove(key);
                const auto found = find_key(model, key);
                if (found >= 0) {
                    model.erase(model.begin() + found);
                }
                break;
            }
            case 4:
                if (!model.empty()) {
                    const auto index = rng.next_index(model.size());
                    assoc = assoc.remove_at(index);
                    model.erase(model.begin() + static_cast<std::ptrdiff_t>(index));
                }
                break;
            case 5: {
                assoc = assoc.insert(position, key, value);
                auto insert_at = static_cast<std::ptrdiff_t>(position);
                const auto found = find_key(model, key);
                if (found >= 0) {
                    model.erase(model.begin() + found);
                    if (found < insert_at) {
                        --insert_at;
                    }
                }
                model.insert(model.begin() + insert_at, {key, value});
                break;
            }
            case 6: {
                const auto count = rng.next_index(model.size() + 1);
                assoc = assoc.take(count);
                model.erase(model.begin() + static_cast<std::ptrdiff_t>(count), model.end());
                break;
            }
            case 7: {
                const auto count = rng.next_index(model.size() + 1);
                assoc = assoc.drop(count);
                model.erase(model.begin(), model.begin() + static_cast<std::ptrdiff_t>(count));
                break;
            }
            case 8:
                assoc = assoc.reverse();
                std::ranges::reverse(model);
                break;
            case 9: {
                const auto other = wf::persistent_association<int, int>{}.set_item(key, value).set_item(key + 1, value + 1);
                assoc = assoc.join(other);
                for (const auto pair : other) {
                    const auto found = find_key(model, pair.first);
                    if (found >= 0) {
                        model[static_cast<std::size_t>(found)] = pair;
                    } else {
                        model.push_back(pair);
                    }
                }
                break;
            }
            default:
                snapshots.push_back({assoc, model});
                if (snapshots.size() > 32) {
                    snapshots.erase(snapshots.begin());
                }
                break;
            }

            require_association_equal(assoc, model);
            if (!model.empty()) {
                FT_REQUIRE(assoc.first() == model.front());
                FT_REQUIRE(assoc.last() == model.back());
            }
        }

        for (const auto& [snapshot, expected] : snapshots) {
            require_association_equal(snapshot, expected);
        }
    });
}

} // namespace

int main()
{
    suite tests;

    tests.add("aggregate header exposes version metadata", [] {
        FT_REQUIRE_EQUAL(wf::library_name, std::string_view{"Tools.DataStructures.Tungsten.Cpp"});
        FT_REQUIRE_EQUAL(wf::version_major, 0U);
        FT_REQUIRE_EQUAL(wf::version_minor, 1U);
        FT_REQUIRE_EQUAL(wf::version_patch, 0U);
    });

    add_persistent_list_tests(tests);
    add_persistent_association_tests(tests);

    return tests.run();
}
