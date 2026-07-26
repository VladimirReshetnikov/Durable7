/**
 * Persistent directed graph with explicit vertices and both adjacency directions.
 *
 * Keeping the vertex set explicit makes an isolated vertex representable, and removing a vertex
 * removes its incident edges rather than leaving the edge relation naming a vertex that no longer
 * exists. Both successors and predecessors are available directly.
 */
import { defaultHashPolicy, type HashPolicy } from "./hash-policy.js";
import { PersistentHashSet } from "./persistent-hamt.js";
import { PersistentRelation } from "./persistent-relation.js";

/** One stored-representative directed edge. */
export interface DirectedEdge<V> {
    /** Build a collection from the given elements. */
    readonly from: V;
    /** The edge's target vertex. */
    readonly to: V;
}

/** Immutable directed graph with explicit isolated vertices and set-valued edges. */
export class PersistentDirectedGraph<V> implements Iterable<DirectedEdge<V>> {
    readonly #vertices: PersistentHashSet<V>;
    readonly #edges: PersistentRelation<V, V>;
    #reverseView: PersistentDirectedGraph<V> | undefined;

    private constructor(vertices: PersistentHashSet<V>, edges: PersistentRelation<V, V>) {
        this.#vertices = vertices;
        this.#edges = edges;
    }

    /** The empty graph, retaining the supplied policy objects. */
    public static empty<V>(policy: HashPolicy<V> = defaultHashPolicy<V>()): PersistentDirectedGraph<V> {
        return new PersistentDirectedGraph(PersistentHashSet.empty(policy), PersistentRelation.empty(policy, policy));
    }

    /** Build a graph from the given vertices. */
    public static from<V>(
        vertices: Iterable<V>,
        edges: Iterable<readonly [V, V]>,
        policy: HashPolicy<V> = defaultHashPolicy<V>(),
    ): PersistentDirectedGraph<V> {
        if (vertices === null || vertices === undefined) throw new TypeError("vertices must be iterable.");
        if (edges === null || edges === undefined) throw new TypeError("edges must be iterable.");
        let result = PersistentDirectedGraph.empty(policy);
        for (const vertex of vertices) result = result.addVertex(vertex);
        for (const [from, to] of edges) result = result.addEdge(from, to);
        return result;
    }

    /** Number of vertices, including isolated ones. */
    public get vertexCount(): number { return this.#vertices.size; }
    /** Number of directed edges. */
    public get edgeCount(): number { return this.#edges.pairCount; }
    /** Whether the graph holds no vertices. */
    public get isEmpty(): boolean { return this.#vertices.isEmpty; }
    /** The retained policy defining equivalence. */
    public get policy(): HashPolicy<V> { return this.#vertices.policy; }

    /**
     * This graph with every edge direction flipped, sharing the vertex set and exchanging the
     * adjacency indexes.
     */
    public get reversed(): PersistentDirectedGraph<V> {
        if (this.#reverseView !== undefined) return this.#reverseView;
        const reversed = new PersistentDirectedGraph(this.#vertices, this.#edges.inverse);
        reversed.#reverseView = this;
        this.#reverseView = reversed;
        return reversed;
    }

    /** Whether the vertex is present, isolated or not. */
    public containsVertex(vertex: V): boolean { return this.#vertices.contains(vertex); }
    /** Whether that directed edge is present. */
    public containsEdge(from: V, to: V): boolean { return this.#edges.contains(from, to); }
    /**
     * The stored vertex representative. Edge endpoints reuse it, so a graph never holds two
     * equivalent vertices.
     */
    public getStoredVertex(vertex: V): V | undefined { return this.#vertices.get(vertex); }
    /** The vertices this one points to. */
    public outgoing(vertex: V): PersistentHashSet<V> { return this.#edges.getRights(vertex); }
    /**
     * The vertices pointing to this one. As cheap as `outgoing`, because both directions are
     * materialized.
     */
    public incoming(vertex: V): PersistentHashSet<V> { return this.#edges.getLefts(vertex); }
    /** Iterate the vertices, isolated ones included. */
    public vertices(): IterableIterator<V> { return this.#vertices[Symbol.iterator](); }

    /**
     * A graph containing that vertex, isolated; returns the receiver when it is already present.
     */
    public addVertex(vertex: V): PersistentDirectedGraph<V> {
        const vertices = this.#vertices.put(vertex);
        return vertices === this.#vertices ? this : new PersistentDirectedGraph(vertices, this.#edges);
    }

    /** Adds an edge and implicitly adds both endpoints, including for a self-loop. */
    public addEdge(from: V, to: V): PersistentDirectedGraph<V> {
        let vertices = this.#vertices.put(from);
        vertices = vertices.put(to);
        const storedFrom = vertices.get(from) as V;
        const storedTo = vertices.get(to) as V;
        const edges = this.#edges.add(storedFrom, storedTo);
        return vertices === this.#vertices && edges === this.#edges
            ? this
            : new PersistentDirectedGraph(vertices, edges);
    }

    /** A graph without that directed edge, leaving both vertices in place. */
    public removeEdge(from: V, to: V): PersistentDirectedGraph<V> {
        const edges = this.#edges.remove(from, to);
        return edges === this.#edges ? this : new PersistentDirectedGraph(this.#vertices, edges);
    }

    /** Removes a vertex and every incoming and outgoing edge incident to it. */
    public removeVertex(vertex: V): PersistentDirectedGraph<V> {
        if (!this.#vertices.contains(vertex)) return this;
        const stored = this.#vertices.get(vertex) as V;
        const edges = this.#edges.removeLeft(stored).removeRight(stored);
        return new PersistentDirectedGraph(this.#vertices.remove(stored), edges);
    }

    /** An empty graph retaining the same policies; returns the receiver when already empty. */
    public clear(): PersistentDirectedGraph<V> {
        return this.isEmpty ? this : PersistentDirectedGraph.empty(this.policy);
    }

    public *[Symbol.iterator](): IterableIterator<DirectedEdge<V>> {
        for (const edge of this.#edges) yield { from: edge.left, to: edge.right };
    }

    /**
     * Whether both graphs share the vertex set and edge relation. A representation test, not an
     * equality test.
     */
    public sharesRootsWith(other: PersistentDirectedGraph<V>): boolean {
        return this.#vertices.sharesRootWith(other.#vertices) && this.#edges.sharesRootsWith(other.#edges);
    }

    /**
     * Walk the whole graph and check its invariants, throwing on the first violation. A defensive
     * audit, not part of normal use.
     */
    public validateStructure(): { readonly vertexCount: number; readonly edgeCount: number } {
        if (!this.#edges.validateStructure()) throw new Error("The directed graph's edge relation is invalid.");
        for (const edge of this.#edges) {
            if (!this.#vertices.contains(edge.left) || !this.#vertices.contains(edge.right)) {
                throw new Error("A directed edge has an endpoint outside the explicit vertex set.");
            }
            if (!Object.is(this.#vertices.get(edge.left), edge.left)
                || !Object.is(this.#vertices.get(edge.right), edge.right)) {
                throw new Error("A directed edge retains a noncanonical vertex representative.");
            }
        }
        return { vertexCount: this.vertexCount, edgeCount: this.edgeCount };
    }
}
