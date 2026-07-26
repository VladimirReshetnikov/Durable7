/**
 * Tests for the directed graph: isolated vertices, self loops, endpoint representative reuse, and
 * the distinct semantics of edge versus vertex removal.
 */
import { describe, expect, it } from "vitest";

import { createHashPolicy, PersistentDirectedGraph } from "../../src/hamt/index.js";

interface Vertex { readonly id: number; readonly name: string }
const vertices = createHashPolicy<Vertex>((vertex) => vertex.id, (left, right) => left.id === right.id);

describe("PersistentDirectedGraph", () => {
    it("retains explicit isolated vertices", () => {
        const graph = PersistentDirectedGraph.empty<string>().addVertex("isolated");
        expect(graph.vertexCount).toBe(1);
        expect(graph.edgeCount).toBe(0);
        expect([...graph.vertices()]).toEqual(["isolated"]);
    });

    it("adds edge endpoints and supports self-loops without parallel edges", () => {
        const graph = PersistentDirectedGraph.empty<string>().addEdge("a", "b").addEdge("a", "a");
        expect(graph.vertexCount).toBe(2);
        expect(graph.edgeCount).toBe(2);
        expect(graph.addEdge("a", "b")).toBe(graph);
        expect(graph.outgoing("a").setEquals(["a", "b"])).toBe(true);
        expect(graph.incoming("a").setEquals(["a"])).toBe(true);
    });

    it("normalizes endpoints to the vertex-set representatives", () => {
        const first = { id: 1, name: "first" };
        const second = { id: 2, name: "second" };
        const graph = PersistentDirectedGraph.empty<Vertex>(vertices)
            .addVertex(first)
            .addEdge({ id: 1, name: "later" }, second);
        const edge = [...graph][0]!;
        expect(edge.from).toBe(first);
        expect(edge.to).toBe(second);
        expect(graph.validateStructure()).toEqual({ vertexCount: 2, edgeCount: 1 });
    });

    it("distinguishes edge removal from incident vertex removal", () => {
        const source = PersistentDirectedGraph.from([], [["a", "b"], ["b", "c"], ["c", "a"]]);
        const edgeRemoved = source.removeEdge("a", "b");
        const vertexRemoved = source.removeVertex("b");
        expect(edgeRemoved.containsVertex("a") && edgeRemoved.containsVertex("b")).toBe(true);
        expect(vertexRemoved.containsVertex("b")).toBe(false);
        expect([...vertexRemoved].some((edge) => edge.from === "b" || edge.to === "b")).toBe(false);
        expect(source.edgeCount).toBe(3);
    });

    it("exposes an O(1) involutive reversed facade", () => {
        const graph = PersistentDirectedGraph.from([], [["a", "b"], ["a", "c"]]);
        expect(graph.reversed.reversed).toBe(graph);
        expect(graph.reversed.containsEdge("b", "a")).toBe(true);
        expect(graph.reversed).toBe(graph.reversed);
    });

    it("preserves receiver identity for misses and retained branches", () => {
        const source = PersistentDirectedGraph.from(["isolated"], [["a", "b"]]);
        expect(source.addVertex("a")).toBe(source);
        expect(source.removeEdge("b", "a")).toBe(source);
        expect(source.removeVertex("missing")).toBe(source);
        const branch = source.addEdge("b", "a");
        expect(source.containsEdge("b", "a")).toBe(false);
        expect(branch.validateStructure()).toEqual({ vertexCount: 3, edgeCount: 2 });
    });
});
