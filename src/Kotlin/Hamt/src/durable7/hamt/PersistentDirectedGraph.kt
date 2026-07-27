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
        /** An empty graph. One policy governs vertex identity in both directions. */
        public fun <V> empty(policy: HashPolicy<V> = defaultHashPolicy()): PersistentDirectedGraph<V> =
            PersistentDirectedGraph(PersistentHashSet.empty(policy), PersistentRelation.empty(policy, policy))

        /**
         * A graph with every vertex and edge. An edge naming a vertex not in [vertices] adds that vertex, so the
         * result is always well formed.
         */
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

    /** The hash policy governing vertex identity. */
    public val policy: HashPolicy<V> get() = storedVertices.policy
    /**
     * Number of vertices, including isolated ones. Vertices exist independently of edges here, which is what lets
     * the graph represent an isolated node at all.
     */
    public val vertexCount: Int get() = storedVertices.size
    /** Number of directed edges. */
    public val edgeCount: Long get() = storedEdges.pairCount
    /** Whether the graph has no vertices. A graph with vertices but no edges is not empty. */
    public val isEmpty: Boolean get() = storedVertices.isEmpty
    /** The vertices, in the vertex set's iteration order. */
    public val vertices: Sequence<V> get() = storedVertices.asSequence()
    /** The directed edges, each as a source-target pair. */
    public val edges: Sequence<DirectedEdge<V>> get() = storedEdges.asSequence().map { DirectedEdge(it.first, it.second) }
    /**
     * The graph with every edge reversed. O(1): the two directions of the underlying relation are already indexed,
     * so this swaps them rather than rebuilding.
     */
    public val reversed: PersistentDirectedGraph<V> get() = PersistentDirectedGraph(storedVertices, storedEdges.inverse())

    /** Whether [vertex] is present. */
    public fun containsVertex(vertex: V): Boolean = storedVertices.contains(vertex)
    /**
     * Whether a directed edge runs from [source] to [target]. Direction matters: this says nothing about the
     * reverse edge.
     */
    public fun containsEdge(source: V, target: V): Boolean = storedEdges.contains(source, target)
    /** The *stored* vertex representative equal to [vertex], or `null` when absent. */
    public fun actualVertex(vertex: V): V? = storedVertices.get(vertex)
    /**
     * The vertices reachable by one outgoing edge from [vertex]. An empty set both when the vertex has no
     * successors and when it is not in the graph.
     */
    public fun successors(vertex: V): PersistentHashSet<V> =
        storedEdges.rightsFor(vertex) ?: PersistentHashSet.empty(policy)
    /**
     * The vertices with one incoming edge into [vertex]. Costs the same as [successors], since both directions are
     * indexed.
     */
    public fun predecessors(vertex: V): PersistentHashSet<V> =
        storedEdges.leftsFor(vertex) ?: PersistentHashSet.empty(policy)
    /** How many edges leave [vertex]. */
    public fun outDegree(vertex: V): Int = successors(vertex).size
    /** How many edges enter [vertex]. */
    public fun inDegree(vertex: V): Int = predecessors(vertex).size

    /**
     * A graph containing [vertex]. Returns the receiver unchanged when it is already present, keeping the stored
     * representative.
     */
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

    /**
     * A graph without the edge from [source] to [target]. Both endpoints remain as vertices. Returns the receiver
     * unchanged when the edge is absent.
     */
    public fun removeEdge(source: V, target: V): PersistentDirectedGraph<V> {
        val nextEdges = storedEdges.remove(source, target)
        return if (nextEdges === storedEdges) this else PersistentDirectedGraph(storedVertices, nextEdges)
    }

    /**
     * A graph without [vertex] and every edge touching it, in either direction. Returns the receiver unchanged when
     * the vertex is absent.
     */
    public fun removeVertex(vertex: V): PersistentDirectedGraph<V> {
        if (!storedVertices.contains(vertex)) return this
        return PersistentDirectedGraph(
            storedVertices.remove(vertex),
            storedEdges.removeLeft(vertex).removeRight(vertex),
        )
    }

    /** An empty graph retaining the same policy. */
    public fun clear(): PersistentDirectedGraph<V> = if (isEmpty) this else empty(policy)

    /**
     * Walk the whole graph and check its invariants - both directions of the edge relation agreeing, and every edge
     * endpoint present as a vertex - returning its shape measurements. A defensive audit for tests, not a routine
     * operation.
     *
     * @throws IllegalStateException on the first violation found.
     */
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
