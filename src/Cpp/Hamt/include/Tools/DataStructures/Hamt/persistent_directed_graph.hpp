#pragma once

#include "persistent_hash_set.hpp"
#include "persistent_relation.hpp"

#include <concepts>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace tools::data_structures::hamt {

/// Immutable simple directed graph with explicit vertices and bidirectional adjacency indexes.
template <class Vertex, class Hash = std::hash<Vertex>, class KeyEqual = std::equal_to<Vertex>>
    requires std::copyable<Vertex> && std::copyable<Hash> && std::copyable<KeyEqual>
class persistent_directed_graph final {
public:
    using vertex_type = Vertex;
    using value_type = std::pair<Vertex, Vertex>;
    using vertex_set = persistent_hash_set<Vertex, Hash, KeyEqual>;
    using relation_type = persistent_relation<Vertex, Vertex, Hash, KeyEqual, Hash, KeyEqual>;
    using size_type = std::size_t;

    persistent_directed_graph() = default;

    [[nodiscard]] static persistent_directed_graph empty() { return {}; }

    [[nodiscard]] static persistent_directed_graph create(Hash hash = {}, KeyEqual equal = {})
    {
        return persistent_directed_graph{
            vertex_set::create(hash, equal),
            relation_type::create(hash, equal, hash, equal)};
    }

    template <class VertexRange, class EdgeRange>
    [[nodiscard]] static persistent_directed_graph create_range(
        const VertexRange& vertices,
        const EdgeRange& edges,
        Hash hash = {},
        KeyEqual equal = {})
    {
        auto result = create(std::move(hash), std::move(equal));
        for (const auto& vertex : vertices) {
            result = result.add_vertex(vertex);
        }
        for (const auto& [source, target] : edges) {
            result = result.add_edge(source, target);
        }
        return result;
    }

    [[nodiscard]] size_type vertex_count() const noexcept { return vertices_.count(); }
    [[nodiscard]] std::int64_t edge_count() const noexcept { return edges_.pair_count(); }
    [[nodiscard]] bool is_empty() const noexcept { return vertices_.is_empty(); }
    [[nodiscard]] const Hash& hash_function() const noexcept { return vertices_.hash_function(); }
    [[nodiscard]] const KeyEqual& key_eq() const noexcept { return vertices_.key_eq(); }

    [[nodiscard]] bool contains_vertex(const Vertex& vertex) const { return vertices_.contains(vertex); }
    [[nodiscard]] const Vertex* try_get_vertex(const Vertex& equal_vertex) const
    {
        return vertices_.try_get_value(equal_vertex);
    }
    [[nodiscard]] bool contains_edge(const Vertex& source, const Vertex& target) const
    {
        return edges_.contains(source, target);
    }
    [[nodiscard]] vertex_set successors_or_empty(const Vertex& vertex) const
    {
        return edges_.rights_or_empty(vertex);
    }
    [[nodiscard]] vertex_set predecessors_or_empty(const Vertex& vertex) const
    {
        return edges_.lefts_or_empty(vertex);
    }
    [[nodiscard]] size_type out_degree(const Vertex& vertex) const
    {
        return successors_or_empty(vertex).count();
    }
    [[nodiscard]] size_type in_degree(const Vertex& vertex) const
    {
        return predecessors_or_empty(vertex).count();
    }

    [[nodiscard]] persistent_directed_graph add_vertex(const Vertex& vertex) const
    {
        auto vertices = vertices_.add(vertex);
        return vertices.shares_root_with(vertices_)
            ? *this
            : persistent_directed_graph{std::move(vertices), edges_};
    }

    [[nodiscard]] std::pair<persistent_directed_graph, bool> try_add_vertex(
        const Vertex& vertex) const
    {
        auto result = add_vertex(vertex);
        const auto changed = !result.vertices_.shares_root_with(vertices_);
        return {std::move(result), changed};
    }

    [[nodiscard]] persistent_directed_graph add_edge(
        const Vertex& source,
        const Vertex& target) const
    {
        auto vertices = vertices_.add(source).add(target);
        const auto* stored_source = vertices.try_get_value(source);
        const auto* stored_target = vertices.try_get_value(target);
        if (stored_source == nullptr || stored_target == nullptr) {
            throw std::logic_error("persistent_directed_graph endpoint lookup is inconsistent");
        }
        auto edges = edges_.add(*stored_source, *stored_target);
        return vertices.shares_root_with(vertices_) && edges.shares_roots_with(edges_)
            ? *this
            : persistent_directed_graph{std::move(vertices), std::move(edges)};
    }

    [[nodiscard]] std::pair<persistent_directed_graph, bool> try_add_edge(
        const Vertex& source,
        const Vertex& target) const
    {
        auto result = add_edge(source, target);
        const auto changed = result.edge_count() != edge_count();
        return {std::move(result), changed};
    }

    [[nodiscard]] persistent_directed_graph remove_edge(
        const Vertex& source,
        const Vertex& target) const
    {
        auto edges = edges_.remove(source, target);
        return edges.shares_roots_with(edges_)
            ? *this
            : persistent_directed_graph{vertices_, std::move(edges)};
    }

    [[nodiscard]] persistent_directed_graph remove_vertex(const Vertex& vertex) const
    {
        const auto* stored = vertices_.try_get_value(vertex);
        if (stored == nullptr) {
            return *this;
        }
        const auto representative = *stored;
        return persistent_directed_graph{
            vertices_.remove(representative),
            edges_.remove_left(representative).remove_right(representative)};
    }

    [[nodiscard]] persistent_directed_graph clear() const
    {
        return is_empty() ? *this : create(hash_function(), key_eq());
    }

    [[nodiscard]] persistent_directed_graph reversed() const
    {
        return persistent_directed_graph{vertices_, edges_.inverse()};
    }

    template <class Function>
        requires std::invocable<Function&, const Vertex&, const Vertex&>
    void for_each_edge(Function&& function) const
    {
        edges_.for_each_pair(std::forward<Function>(function));
    }

    [[nodiscard]] std::vector<Vertex> vertices_to_vector() const
    {
        return std::vector<Vertex>{vertices_.begin(), vertices_.end()};
    }
    [[nodiscard]] std::vector<value_type> to_vector() const { return edges_.to_vector(); }

    [[nodiscard]] bool shares_roots_with(const persistent_directed_graph& other) const noexcept
    {
        return vertices_.shares_root_with(other.vertices_)
            && edges_.shares_roots_with(other.edges_);
    }

    void validate_invariants() const
    {
        edges_.validate_invariants();
        edges_.for_each_pair([this](const Vertex& source, const Vertex& target) {
            if (!vertices_.contains(source) || !vertices_.contains(target)) {
                throw std::logic_error("persistent_directed_graph edge endpoint is absent");
            }
        });
    }

    [[nodiscard]] bool debug_validate() const noexcept
    {
        try {
            validate_invariants();
            return true;
        } catch (...) {
            return false;
        }
    }

private:
    persistent_directed_graph(vertex_set vertices, relation_type edges)
        : vertices_(std::move(vertices)), edges_(std::move(edges))
    {
    }

    vertex_set vertices_;
    relation_type edges_;
};

} // namespace tools::data_structures::hamt
