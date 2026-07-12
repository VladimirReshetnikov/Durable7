#include <Tools/DataStructures/Hamt/hamt.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

int main()
{
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

    return populated.at(42) == std::optional<std::string>{"forty-two"}
            && statistics.count == 1
            && populated.root_hash().to_hex()
                == "1b464818e8934692ad28f35f520fa0c834634e2200f9e5873d0327e6524bcc94"
        ? 0
        : 1;
}
