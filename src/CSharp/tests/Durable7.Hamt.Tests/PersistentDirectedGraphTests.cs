// Tests for the persistent directed graph.

using Xunit;

namespace Durable7.Hamt.Tests;

/// <summary>Verifies explicit vertices, bidirectional adjacency, reversal, and persistence.</summary>
public sealed class PersistentDirectedGraphTests
{
    /// <summary>Verifies factory identity and retained comparer policy.</summary>
    [Fact]
    public void Create_RetainsVertexComparer()
    {
        Assert.Same(PersistentDirectedGraph<string>.Empty, PersistentDirectedGraph<string>.Create());
        var comparer = StringComparer.OrdinalIgnoreCase;
        var graph = PersistentDirectedGraph<string>.Create(comparer);

        Assert.True(graph.IsEmpty);
        Assert.Same(comparer, graph.VertexComparer);
        Assert.Same(graph, graph.Clear());
        Assert.Same(comparer, graph.GetSuccessors("missing").Comparer);
    }

    /// <summary>Verifies edges add endpoints and adjacency is indexed in both directions.</summary>
    [Fact]
    public void AddEdge_AddsEndpointsAndAdjacency()
    {
        var graph = PersistentDirectedGraph<string>.Empty
            .AddEdge("a", "b")
            .AddEdge("a", "c")
            .AddEdge("c", "a")
            .AddEdge("c", "c");

        Assert.Equal(3, graph.VertexCount);
        Assert.Equal(4, graph.EdgeCount);
        Assert.Equal(["b", "c"], graph.GetSuccessors("a").Order());
        Assert.Equal(["a", "c"], graph.GetPredecessors("c").Order());
        Assert.Equal(2, graph.OutDegree("c"));
        Assert.Equal(2, graph.InDegree("c"));
        Assert.True(graph.ContainsEdge("c", "c"));
        graph.ValidateInvariants();
    }

    /// <summary>Verifies duplicate vertices and edges are identity-preserving no-ops.</summary>
    [Fact]
    public void DuplicateUpdates_PreserveIdentity()
    {
        var graph = PersistentDirectedGraph<string>
            .Create(StringComparer.OrdinalIgnoreCase)
            .AddEdge("Source", "Target");

        Assert.Same(graph, graph.AddVertex("SOURCE"));
        Assert.False(graph.TryAddVertex("target", out var sameVertex));
        Assert.Same(graph, sameVertex);
        Assert.Same(graph, graph.AddEdge("SOURCE", "TARGET"));
        Assert.False(graph.TryAddEdge("source", "target", out var sameEdge));
        Assert.Same(graph, sameEdge);
    }

    /// <summary>Verifies edge endpoints use the vertex set's first retained representatives.</summary>
    [Fact]
    public void AddEdge_NormalizesEndpointRepresentatives()
    {
        var source = new Vertex("source", "stored-source");
        var target = new Vertex("target", "stored-target");
        var graph = PersistentDirectedGraph<Vertex>.Create(VertexComparer.Instance)
            .AddVertex(source)
            .AddVertex(target)
            .AddEdge(new("SOURCE", "caller-source"), new("TARGET", "caller-target"));

        var edge = Assert.Single(graph.Edges);
        Assert.Same(source, edge.Key);
        Assert.Same(target, edge.Value);
        Assert.True(graph.TryGetVertex(new("source", "probe"), out var actual));
        Assert.Same(source, actual);
    }

    /// <summary>Verifies edge removal retains vertices while vertex removal deletes all incident edges.</summary>
    [Fact]
    public void Removal_UsesDirectedAndIncidentSemantics()
    {
        var source = PersistentDirectedGraph<int>.Empty
            .AddVertex(9)
            .AddEdge(1, 2)
            .AddEdge(2, 1)
            .AddEdge(1, 1)
            .AddEdge(3, 1);
        var withoutEdge = source.RemoveEdge(1, 2);
        var withoutOne = source.RemoveVertex(1);

        Assert.True(withoutEdge.ContainsVertex(1));
        Assert.True(withoutEdge.ContainsVertex(2));
        Assert.False(withoutEdge.ContainsEdge(1, 2));
        Assert.Equal([2, 3, 9], withoutOne.Vertices.Order());
        Assert.Equal(0, withoutOne.EdgeCount);
        Assert.Equal(4, source.EdgeCount);
        Assert.Same(source, source.RemoveEdge(99, 100));
        Assert.Same(source, source.RemoveVertex(100));
    }

    /// <summary>Verifies reversed graphs are cached involutive views with unchanged isolated vertices.</summary>
    [Fact]
    public void Reversed_IsCachedInvolutiveView()
    {
        var graph = PersistentDirectedGraph<int>.Empty
            .AddVertex(9)
            .AddEdge(1, 2)
            .AddEdge(3, 2);
        var reversed = graph.Reversed;

        Assert.Same(reversed, graph.Reversed);
        Assert.Same(graph, reversed.Reversed);
        Assert.True(reversed.ContainsEdge(2, 1));
        Assert.True(reversed.ContainsEdge(2, 3));
        Assert.True(reversed.ContainsVertex(9));
        Assert.Equal(graph.VertexCount, reversed.VertexCount);
        Assert.Equal(graph.EdgeCount, reversed.EdgeCount);
    }

    /// <summary>Verifies range construction and retained histories branch independently.</summary>
    [Fact]
    public void CreateRangeAndBranches_AreIndependent()
    {
        var root = PersistentDirectedGraph<int>.CreateRange(
            [0],
            [KeyValuePair.Create(1, 2), KeyValuePair.Create(2, 3)]);
        var left = root.AddEdge(3, 1);
        var right = root.RemoveVertex(2);

        Assert.Equal(4, root.VertexCount);
        Assert.Equal(2, root.EdgeCount);
        Assert.Equal(3, left.EdgeCount);
        Assert.Equal(0, right.EdgeCount);
        Assert.True(root.ContainsVertex(2));
        Assert.False(right.ContainsVertex(2));
        Assert.False(root.ContainsEdge(3, 1));
        root.ValidateInvariants();
        left.ValidateInvariants();
        right.ValidateInvariants();
    }

    private sealed record Vertex(string Class, string Representation);

    private sealed class VertexComparer : IEqualityComparer<Vertex>
    {
        internal static VertexComparer Instance { get; } = new();

        public bool Equals(Vertex? x, Vertex? y) =>
            StringComparer.OrdinalIgnoreCase.Equals(x?.Class, y?.Class);

        public int GetHashCode(Vertex obj) => StringComparer.OrdinalIgnoreCase.GetHashCode(obj.Class);
    }
}
