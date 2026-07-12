#include <Tools/DataStructures/Hamt/hamt.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

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

    return populated.at(42) == std::optional<std::string>{"forty-two"}
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
