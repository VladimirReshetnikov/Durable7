/// A persistent directed graph over hashed vertices.
///
/// Successors and predecessors are both indexed, so neither direction is a scan. Every operation
/// returns a new version and leaves its inputs valid, sharing unchanged structure, so an edit
/// copies a path rather than the whole collection.

#pragma once

#include "persistent_hash_set.hpp"
#include "persistent_relation.hpp"

#include <concepts>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace durable7::hamt {

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

    /// An empty graph.
    persistent_directed_graph() = default;

    /// Whether the graph holds no vertices.
    [[nodiscard]] static persistent_directed_graph empty() { return {}; }

    /// An empty graph using the supplied policies, which it retains.
    [[nodiscard]] static persistent_directed_graph create(Hash hash = {}, KeyEqual equal = {})
    {
        return persistent_directed_graph{
            vertex_set::create(hash, equal),
            relation_type::create(hash, equal, hash, equal)};
    }

    /// A graph holding a range's vertices, built in bulk rather than by repeated insertion.
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

    /// The retained key equivalence policy.
    /// The retained hashing policy. Keys the policy treats as equivalent must hash identically.
    /// Whether the graph holds no vertices.
    /// Number of directed edges.
    /// Number of vertices.
    [[nodiscard]] size_type vertex_count() const noexcept { return vertices_.count(); }
    [[nodiscard]] std::int64_t edge_count() const noexcept { return edges_.pair_count(); }
    [[nodiscard]] bool is_empty() const noexcept { return vertices_.is_empty(); }
    [[nodiscard]] const Hash& hash_function() const noexcept { return vertices_.hash_function(); }
    [[nodiscard]] const KeyEqual& key_eq() const noexcept { return vertices_.key_eq(); }

    /// Reads the stored vertex representative, or nothing when absent.
    /// Whether the vertex is present.
    [[nodiscard]] bool contains_vertex(const Vertex& vertex) const { return vertices_.contains(vertex); }
    [[nodiscard]] const Vertex* try_get_vertex(const Vertex& equal_vertex) const
    {
        return vertices_.try_get_value(equal_vertex);
    }
    /// Whether the directed edge is present.
    [[nodiscard]] bool contains_edge(const Vertex& source, const Vertex& target) const
    {
        return edges_.contains(source, target);
    }
    /// The vertices the given vertex points at, empty when it is absent.
    [[nodiscard]] vertex_set successors_or_empty(const Vertex& vertex) const
    {
        return edges_.rights_or_empty(vertex);
    }
    /// The vertices pointing at the given vertex, empty when it is absent. Maintained as its own
    /// index, so this is a lookup rather than a scan.
    [[nodiscard]] vertex_set predecessors_or_empty(const Vertex& vertex) const
    {
        return edges_.lefts_or_empty(vertex);
    }
    /// How many edges leave the vertex.
    [[nodiscard]] size_type out_degree(const Vertex& vertex) const
    {
        return successors_or_empty(vertex).count();
    }
    /// How many edges enter the vertex.
    [[nodiscard]] size_type in_degree(const Vertex& vertex) const
    {
        return predecessors_or_empty(vertex).count();
    }

    /// A graph containing the vertex, with no edges added.
    [[nodiscard]] persistent_directed_graph add_vertex(const Vertex& vertex) const
    {
        auto vertices = vertices_.add(vertex);
        return vertices.shares_root_with(vertices_)
            ? *this
            : persistent_directed_graph{std::move(vertices), edges_};
    }

    /// Adds the vertex unless it is present, reporting which happened.
    [[nodiscard]] std::pair<persistent_directed_graph, bool> try_add_vertex(
        const Vertex& vertex) const
    {
        auto result = add_vertex(vertex);
        const auto changed = !result.vertices_.shares_root_with(vertices_);
        return {std::move(result), changed};
    }

    /// A graph containing the directed edge, adding either endpoint that is missing.
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

    /// Adds the edge unless it is present, reporting which happened.
    [[nodiscard]] std::pair<persistent_directed_graph, bool> try_add_edge(
        const Vertex& source,
        const Vertex& target) const
    {
        auto result = add_edge(source, target);
        const auto changed = result.edge_count() != edge_count();
        return {std::move(result), changed};
    }

    /// A graph without that directed edge, leaving both endpoints in place.
    [[nodiscard]] persistent_directed_graph remove_edge(
        const Vertex& source,
        const Vertex& target) const
    {
        auto edges = edges_.remove(source, target);
        return edges.shares_roots_with(edges_)
            ? *this
            : persistent_directed_graph{vertices_, std::move(edges)};
    }

    /// A graph without the vertex and without any edge touching it.
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

    /// An empty graph retaining the same policies; returns the receiver when already empty.
    [[nodiscard]] persistent_directed_graph clear() const
    {
        return is_empty() ? *this : create(hash_function(), key_eq());
    }

    /// The graph with every edge direction flipped, reusing both existing indexes rather than
    /// rebuilding.
    [[nodiscard]] persistent_directed_graph reversed() const
    {
        return persistent_directed_graph{vertices_, edges_.inverse()};
    }

    /// Calls the function once per edge.
    template <class Function>
        requires std::invocable<Function&, const Vertex&, const Vertex&>
    void for_each_edge(Function&& function) const
    {
        edges_.for_each_pair(std::forward<Function>(function));
    }

    /// Copies the vertices out into a vector.
    [[nodiscard]] std::vector<Vertex> vertices_to_vector() const
    {
        return std::vector<Vertex>{vertices_.begin(), vertices_.end()};
    }
    /// Copies the vertices out into a vector, in the graph's own order.
    [[nodiscard]] std::vector<value_type> to_vector() const { return edges_.to_vector(); }

    /// Whether both handles reference the same roots.
    [[nodiscard]] bool shares_roots_with(const persistent_directed_graph& other) const noexcept
    {
        return vertices_.shares_root_with(other.vertices_)
            && edges_.shares_roots_with(other.edges_);
    }

    /// Checks the graph's structural invariants. For tests and diagnostics.
    void validate_invariants() const
    {
        edges_.validate_invariants();
        edges_.for_each_pair([this](const Vertex& source, const Vertex& target) {
            if (!vertices_.contains(source) || !vertices_.contains(target)) {
                throw std::logic_error("persistent_directed_graph edge endpoint is absent");
            }
        });
    }

    /// Checks the graph's structural invariants. For tests and diagnostics.
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

} // namespace durable7::hamt
