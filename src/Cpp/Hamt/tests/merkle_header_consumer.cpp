#include <Tools/DataStructures/Hamt/hamt.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

int main()
{
#if defined(_MSC_VER)
    // The persistence implementation used to expose a top-level access bridge. Keep the
    // aggregate-header consumer honest: implementation access belongs exclusively to the
    // explicitly unsupported detail namespace.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wlanguage-extension-token"
#endif
    __if_exists(tools::data_structures::hamt::merkle_persistence_access) {
        static_assert(false, "the legacy public Merkle persistence access bridge must not exist");
    }
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
#endif

    using namespace tools::data_structures::hamt;

    const auto champ_source = persistent_hash_map<std::int32_t, std::string>::empty()
        .set_item(1, "one");
    const auto [champ_factory, champ_selected] = champ_source.add_or_update(
        1,
        [](const std::int32_t&) { return std::string("missing"); },
        [](const std::int32_t&, const std::string& stored) { return stored + "!"; });
    auto champ_edit = champ_source.to_transient();
    champ_edit.set_item(2, "two");
    const auto champ_published = std::move(champ_edit).persist();
    auto set_edit = persistent_hash_set<std::int32_t>::create_transient();
    const auto set_added = set_edit.add(7);
    const auto set_published = std::move(set_edit).persist();
    const auto bag = persistent_hash_bag<std::int32_t>::create_range({1, 1, 2})
        .sum_with(persistent_hash_bag<std::int32_t>::create_range({2, 3}));
    const auto bimap = persistent_bi_map<std::int32_t, std::string>::empty()
        .add(1, "one")
        .set_item(1, "uno");
    const auto inverse_bimap = bimap.inverse();

    const auto policy = merkle_search_tree_policy<
        std::int32_t,
        std::optional<std::string>>::natural(
        "golden-int-string-v1",
        std::make_shared<int32_merkle_codec>(),
        std::make_shared<nullable_utf8_merkle_codec>());
    const auto empty = merkle_search_tree<
        std::int32_t,
        std::optional<std::string>>::create(policy);
    const auto populated = empty.set_item(42, std::optional<std::string>{"forty-two"});
    const auto statistics = populated.validate_structure();
    const auto pack = export_merkle_pack(populated);
    auto store = in_memory_merkle_block_store{};
    const auto saved = save_merkle_tree(populated, store);
    const auto loaded = load_merkle_tree(populated.root_hash(), policy, store);
    const auto proof = create_merkle_proof(populated, std::int32_t{42});
    const auto verified = verify_merkle_proof(proof, policy);
    const auto merged = merge_merkle_trees(empty, populated, empty);

    return champ_source.count() == 1
            && champ_published.count() == 2
            && champ_published.at(1) == "one"
            && champ_published.at(2) == "two"
            && champ_factory.at(1) == "one!"
            && champ_selected == "one!"
            && set_added
            && set_published.contains(7)
            && bag.distinct_count() == 3
            && bag.total_count() == 5
            && bag.count_of(1) == 2
            && bag.count_of(2) == 2
            && bimap.at(1) == "uno"
            && inverse_bimap.at("uno") == 1
            && inverse_bimap.inverse().shares_roots_with(bimap)
            && populated.at(42) == std::optional<std::string>{"forty-two"}
            && statistics.count == 1
            && pack.block_count() == 1
            && saved == 1
            && loaded.content_equals(populated)
            && verified.valid()
            && merged.success()
            && merged.merged_tree()->content_equals(populated)
            && populated.root_hash().to_hex()
                == "1b464818e8934692ad28f35f520fa0c834634e2200f9e5873d0327e6524bcc94"
        ? 0
        : 1;
}
