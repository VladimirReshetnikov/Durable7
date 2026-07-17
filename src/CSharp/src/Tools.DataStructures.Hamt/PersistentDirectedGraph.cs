using System.Collections;
using System.Diagnostics;
using System.Threading;

namespace Tools.DataStructures.Hamt;

/// <summary>
/// Represents an immutable simple directed graph with explicit vertices and degree-local adjacency indexes.
/// </summary>
/// <typeparam name="TVertex">The vertex type.</typeparam>
/// <remarks>
/// Vertices are stored in a persistent hash set and edges in a bidirectionally indexed
/// <see cref="PersistentRelation{TLeft, TRight}"/>. Edges are unique under the retained vertex
/// comparer, self-loops are allowed, and adding an edge also adds either missing endpoint. Removing
/// a vertex removes all incoming and outgoing edges. Every edge endpoint is normalized to the
/// vertex set's first stored representative.
/// </remarks>
[DebuggerDisplay("VertexCount = {VertexCount}, EdgeCount = {EdgeCount}")]
public sealed class PersistentDirectedGraph<TVertex> :
    IEnumerable<KeyValuePair<TVertex, TVertex>>
{
    private static readonly PersistentDirectedGraph<TVertex> EmptyInstance = new(
        PersistentHashSet<TVertex>.Empty,
        PersistentRelation<TVertex, TVertex>.Empty);

    private readonly PersistentHashSet<TVertex> _vertices;
    private readonly PersistentRelation<TVertex, TVertex> _edges;
    private PersistentDirectedGraph<TVertex>? _reversed;

    private PersistentDirectedGraph(
        PersistentHashSet<TVertex> vertices,
        PersistentRelation<TVertex, TVertex> edges)
    {
        Debug.Assert(ReferenceEquals(vertices.Comparer, edges.LeftComparer));
        Debug.Assert(ReferenceEquals(vertices.Comparer, edges.RightComparer));
        _vertices = vertices;
        _edges = edges;
    }

    /// <summary>Gets the shared empty graph using the default vertex comparer.</summary>
    public static PersistentDirectedGraph<TVertex> Empty => EmptyInstance;

    /// <summary>Gets the number of explicit vertices, including isolated vertices.</summary>
    public int VertexCount => _vertices.Count;

    /// <summary>Gets the number of directed edges.</summary>
    public long EdgeCount => _edges.PairCount;

    /// <summary>Gets whether the graph contains no vertices.</summary>
    public bool IsEmpty => _vertices.IsEmpty;

    /// <summary>Gets the equality comparer defining the vertex domain.</summary>
    public IEqualityComparer<TVertex> VertexComparer => _vertices.Comparer;

    /// <summary>Gets the stored vertex representatives in stable-for-this-version trie order.</summary>
    public IEnumerable<TVertex> Vertices => _vertices;

    /// <summary>Gets the directed edges in stable-for-this-version relation order.</summary>
    public IEnumerable<KeyValuePair<TVertex, TVertex>> Edges => _edges;

    /// <summary>Gets a cached O(1) reversed-edge view over the same immutable roots.</summary>
    public PersistentDirectedGraph<TVertex> Reversed
    {
        get
        {
            var current = Volatile.Read(ref _reversed);
            if (current is not null)
                return current;

            var candidate = new PersistentDirectedGraph<TVertex>(_vertices, _edges.Inverse)
            {
                _reversed = this,
            };
            return Interlocked.CompareExchange(ref _reversed, candidate, comparand: null) ?? candidate;
        }
    }

    /// <summary>Creates an empty graph retaining a vertex comparer.</summary>
    /// <param name="vertexComparer">The vertex comparer, or <see langword="null"/> for the default.</param>
    /// <returns>An empty graph with the selected vertex policy.</returns>
    public static PersistentDirectedGraph<TVertex> Create(
        IEqualityComparer<TVertex>? vertexComparer = null)
    {
        vertexComparer ??= EqualityComparer<TVertex>.Default;
        if (ReferenceEquals(vertexComparer, EqualityComparer<TVertex>.Default))
            return Empty;

        return new(
            PersistentHashSet<TVertex>.Create(vertexComparer),
            PersistentRelation<TVertex, TVertex>.Create(vertexComparer, vertexComparer));
    }

    /// <summary>Creates a graph from explicit vertices and directed edges.</summary>
    /// <param name="vertices">The vertices to add before the edges.</param>
    /// <param name="edges">The directed source/target pairs to add.</param>
    /// <param name="vertexComparer">The vertex comparer, or <see langword="null"/> for the default.</param>
    /// <returns>A graph containing the distinct vertices and edges.</returns>
    /// <exception cref="ArgumentNullException">
    /// <paramref name="vertices"/> or <paramref name="edges"/> is <see langword="null"/>.
    /// </exception>
    public static PersistentDirectedGraph<TVertex> CreateRange(
        IEnumerable<TVertex> vertices,
        IEnumerable<KeyValuePair<TVertex, TVertex>> edges,
        IEqualityComparer<TVertex>? vertexComparer = null)
    {
        ArgumentNullException.ThrowIfNull(vertices);
        ArgumentNullException.ThrowIfNull(edges);
        var result = Create(vertexComparer);
        foreach (var vertex in vertices)
            result = result.AddVertex(vertex);
        foreach (var (source, target) in edges)
            result = result.AddEdge(source, target);
        return result;
    }

    /// <summary>Determines whether an equivalent explicit vertex exists.</summary>
    /// <param name="vertex">The vertex to find.</param>
    /// <returns><see langword="true"/> when the vertex exists.</returns>
    public bool ContainsVertex(TVertex vertex) => _vertices.Contains(vertex);

    /// <summary>Tries to recover the vertex set's first stored representative.</summary>
    /// <param name="equalVertex">A vertex equivalent to the stored vertex.</param>
    /// <param name="actualVertex">The stored representative when found.</param>
    /// <returns><see langword="true"/> when the vertex exists.</returns>
    public bool TryGetVertex(TVertex equalVertex, out TVertex actualVertex) =>
        _vertices.TryGetValue(equalVertex, out actualVertex!);

    /// <summary>Determines whether an equivalent directed edge exists.</summary>
    /// <param name="source">The source vertex.</param>
    /// <param name="target">The target vertex.</param>
    /// <returns><see langword="true"/> when the edge exists.</returns>
    public bool ContainsEdge(TVertex source, TVertex target) => _edges.Contains(source, target);

    /// <summary>Gets the persistent set of immediate successors for an equivalent vertex.</summary>
    /// <param name="vertex">The source vertex.</param>
    /// <returns>A set retaining <see cref="VertexComparer"/>; empty when no outgoing edge exists.</returns>
    public PersistentHashSet<TVertex> GetSuccessors(TVertex vertex) => _edges.GetRights(vertex);

    /// <summary>Gets the persistent set of immediate predecessors for an equivalent vertex.</summary>
    /// <param name="vertex">The target vertex.</param>
    /// <returns>A set retaining <see cref="VertexComparer"/>; empty when no incoming edge exists.</returns>
    public PersistentHashSet<TVertex> GetPredecessors(TVertex vertex) => _edges.GetLefts(vertex);

    /// <summary>Gets the outgoing degree of an equivalent vertex.</summary>
    /// <param name="vertex">The source vertex.</param>
    /// <returns>The number of outgoing edges.</returns>
    public int OutDegree(TVertex vertex) => _edges.CountRights(vertex);

    /// <summary>Gets the incoming degree of an equivalent vertex.</summary>
    /// <param name="vertex">The target vertex.</param>
    /// <returns>The number of incoming edges.</returns>
    public int InDegree(TVertex vertex) => _edges.CountLefts(vertex);

    /// <summary>Adds an explicit vertex, preserving the first representative of its equivalence class.</summary>
    /// <param name="vertex">The vertex to add.</param>
    /// <returns>This graph when equivalent vertex exists; otherwise, the enlarged graph.</returns>
    public PersistentDirectedGraph<TVertex> AddVertex(TVertex vertex)
    {
        var vertices = _vertices.Add(vertex);
        return ReferenceEquals(vertices, _vertices) ? this : new(vertices, _edges);
    }

    /// <summary>Attempts to add an explicit vertex.</summary>
    /// <param name="vertex">The vertex to add.</param>
    /// <param name="result">The resulting graph, or this graph when the vertex already exists.</param>
    /// <returns><see langword="true"/> when a vertex was added.</returns>
    public bool TryAddVertex(
        TVertex vertex,
        out PersistentDirectedGraph<TVertex> result)
    {
        var vertices = _vertices.Add(vertex);
        if (ReferenceEquals(vertices, _vertices))
        {
            result = this;
            return false;
        }

        result = new(vertices, _edges);
        return true;
    }

    /// <summary>Adds a directed edge and either missing endpoint.</summary>
    /// <param name="source">The source vertex.</param>
    /// <param name="target">The target vertex.</param>
    /// <returns>This graph when the edge exists; otherwise, the enlarged graph.</returns>
    public PersistentDirectedGraph<TVertex> AddEdge(TVertex source, TVertex target)
    {
        var vertices = _vertices.Add(source).Add(target);
        vertices.TryGetValue(source, out var actualSource);
        vertices.TryGetValue(target, out var actualTarget);
        var edges = _edges.Add(actualSource, actualTarget);
        return ReferenceEquals(vertices, _vertices) && ReferenceEquals(edges, _edges)
            ? this
            : new(vertices, edges);
    }

    /// <summary>Attempts to add a directed edge and either missing endpoint.</summary>
    /// <param name="source">The source vertex.</param>
    /// <param name="target">The target vertex.</param>
    /// <param name="result">The resulting graph, or this graph when the edge already exists.</param>
    /// <returns><see langword="true"/> when an edge was added.</returns>
    public bool TryAddEdge(
        TVertex source,
        TVertex target,
        out PersistentDirectedGraph<TVertex> result)
    {
        result = AddEdge(source, target);
        return result.EdgeCount != EdgeCount;
    }

    /// <summary>Removes a directed edge while retaining both endpoint vertices.</summary>
    /// <param name="source">The source vertex.</param>
    /// <param name="target">The target vertex.</param>
    /// <returns>This graph when absent; otherwise, the graph without the edge.</returns>
    public PersistentDirectedGraph<TVertex> RemoveEdge(TVertex source, TVertex target)
    {
        var edges = _edges.Remove(source, target);
        return ReferenceEquals(edges, _edges) ? this : new(_vertices, edges);
    }

    /// <summary>Removes a vertex and every incident edge.</summary>
    /// <param name="vertex">The vertex to remove.</param>
    /// <returns>This graph when absent; otherwise, the graph without the vertex and incident edges.</returns>
    public PersistentDirectedGraph<TVertex> RemoveVertex(TVertex vertex)
    {
        if (!_vertices.TryGetValue(vertex, out var actualVertex))
            return this;

        var edges = _edges.RemoveLeft(actualVertex).RemoveRight(actualVertex);
        return new(_vertices.Remove(actualVertex), edges);
    }

    /// <summary>Returns an empty graph retaining the current vertex policy.</summary>
    /// <returns>This graph when already empty; otherwise, an empty graph.</returns>
    public PersistentDirectedGraph<TVertex> Clear() =>
        IsEmpty ? this : Create(VertexComparer);

    /// <summary>Materializes the directed edges in stable-for-this-version relation order.</summary>
    /// <returns>An array containing every directed edge.</returns>
    public KeyValuePair<TVertex, TVertex>[] ToArray() => _edges.ToArray();

    /// <summary>Returns an enumerator over directed edges.</summary>
    /// <returns>An enumerator over the immutable graph snapshot.</returns>
    public IEnumerator<KeyValuePair<TVertex, TVertex>> GetEnumerator() => _edges.GetEnumerator();

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

    internal void ValidateInvariants()
    {
        _edges.ValidateInvariants();
        if (!ReferenceEquals(_vertices.Comparer, _edges.LeftComparer)
            || !ReferenceEquals(_vertices.Comparer, _edges.RightComparer))
        {
            throw new InvalidOperationException("The graph vertex and adjacency policies disagree.");
        }

        foreach (var (source, target) in _edges)
        {
            if (!_vertices.TryGetValue(source, out var actualSource)
                || !_vertices.TryGetValue(target, out var actualTarget))
            {
                throw new InvalidOperationException("A graph edge has an endpoint absent from the vertex set.");
            }

            if (!typeof(TVertex).IsValueType
                && (!ReferenceEquals(source, actualSource) || !ReferenceEquals(target, actualTarget)))
            {
                throw new InvalidOperationException("A graph edge does not retain the vertex set's representatives.");
            }
        }
    }
}
