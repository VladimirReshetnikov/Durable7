/*
 * Persistent directed graph with explicit vertices and both adjacency directions.
 *
 * Keeping the vertex set explicit makes an isolated vertex representable, and removing a vertex
 * removes its incident edges rather than leaving the edge relation naming a vertex that no longer
 * exists.
 */
package durable7.hamt

/** One directed edge, from a source vertex to a target vertex. */
public data class DirectedEdge<V>(public val source: V, public val target: V)

/** Vertex and edge counts returned by a successful structural audit. */
public data class PersistentDirectedGraphStatistics(
    public val vertexCount: Int,
    public val edgeCount: Long,
)

/** Immutable directed graph with explicit vertices and bidirectionally indexed edges. */
public class PersistentDirectedGraph<V> private constructor(
    private val storedVertices: PersistentHashSet<V>,
    private val storedEdges: PersistentRelation<V, V>,
) : Iterable<V> {
    public companion object {
        public fun <V> empty(policy: HashPolicy<V> = defaultHashPolicy()): PersistentDirectedGraph<V> =
            PersistentDirectedGraph(PersistentHashSet.empty(policy), PersistentRelation.empty(policy, policy))

        public fun <V> from(
            vertices: Iterable<V>,
            edges: Iterable<Pair<V, V>>,
            policy: HashPolicy<V> = defaultHashPolicy(),
        ): PersistentDirectedGraph<V> {
            var result = empty<V>(policy)
            for (vertex in vertices) result = result.addVertex(vertex)
            for ((source, target) in edges) result = result.addEdge(source, target)
            return result
        }
    }

    public val policy: HashPolicy<V> get() = storedVertices.policy
    public val vertexCount: Int get() = storedVertices.size
    public val edgeCount: Long get() = storedEdges.pairCount
    public val isEmpty: Boolean get() = storedVertices.isEmpty
    public val vertices: Sequence<V> get() = storedVertices.asSequence()
    public val edges: Sequence<DirectedEdge<V>> get() = storedEdges.asSequence().map { DirectedEdge(it.first, it.second) }
    public val reversed: PersistentDirectedGraph<V> get() = PersistentDirectedGraph(storedVertices, storedEdges.inverse())

    public fun containsVertex(vertex: V): Boolean = storedVertices.contains(vertex)
    public fun containsEdge(source: V, target: V): Boolean = storedEdges.contains(source, target)
    public fun actualVertex(vertex: V): V? = storedVertices.get(vertex)
    public fun successors(vertex: V): PersistentHashSet<V> =
        storedEdges.rightsFor(vertex) ?: PersistentHashSet.empty(policy)
    public fun predecessors(vertex: V): PersistentHashSet<V> =
        storedEdges.leftsFor(vertex) ?: PersistentHashSet.empty(policy)
    public fun outDegree(vertex: V): Int = successors(vertex).size
    public fun inDegree(vertex: V): Int = predecessors(vertex).size

    public fun addVertex(vertex: V): PersistentDirectedGraph<V> {
        val updated = storedVertices.put(vertex)
        return if (updated === storedVertices) this else PersistentDirectedGraph(updated, storedEdges)
    }

    /** Adds an edge and implicitly adds missing endpoint vertices. */
    public fun addEdge(source: V, target: V): PersistentDirectedGraph<V> {
        if (storedEdges.contains(source, target)) return this
        val nextVertices = storedVertices.put(source).put(target)
        val actualSource = checkNotNull(nextVertices.get(source))
        val actualTarget = checkNotNull(nextVertices.get(target))
        return PersistentDirectedGraph(nextVertices, storedEdges.add(actualSource, actualTarget))
    }

    public fun removeEdge(source: V, target: V): PersistentDirectedGraph<V> {
        val nextEdges = storedEdges.remove(source, target)
        return if (nextEdges === storedEdges) this else PersistentDirectedGraph(storedVertices, nextEdges)
    }

    public fun removeVertex(vertex: V): PersistentDirectedGraph<V> {
        if (!storedVertices.contains(vertex)) return this
        return PersistentDirectedGraph(
            storedVertices.remove(vertex),
            storedEdges.removeLeft(vertex).removeRight(vertex),
        )
    }

    public fun clear(): PersistentDirectedGraph<V> = if (isEmpty) this else empty(policy)

    public fun validateStructure(): PersistentDirectedGraphStatistics {
        storedEdges.validateStructure()
        for (edge in storedEdges) {
            check(storedVertices.contains(edge.first) && storedVertices.contains(edge.second)) {
                "PersistentDirectedGraph edge endpoint is absent from the vertex set."
            }
        }
        return PersistentDirectedGraphStatistics(vertexCount, edgeCount)
    }

    override fun iterator(): Iterator<V> = storedVertices.iterator()
}
