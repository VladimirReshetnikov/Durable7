using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Numerics;
using Durable7.Hamt;

namespace Durable7.Hamt.Experimental;

/// <summary>
/// Represents a fully branching persistent insertion-only connectivity forest that can report the
/// first ancestor version in which two vertices became connected.
/// </summary>
/// <remarks>
/// <para>
/// Vertices are the fixed integer universe <c>0 .. VertexCount - 1</c>. Each successful
/// <see cref="Link"/> performs union by size without path compression and labels the one new
/// root-parent edge with the child <see cref="AncestralConnectionVersion"/>. Failed (redundant)
/// unions still create a child history version, but share the complete connectivity index.
/// </para>
/// <para>
/// The parent cells live in a persistent CHAMP map. An absent cell denotes a singleton root, so
/// <see cref="Create"/> is O(1) rather than O(<c>VertexCount</c>). Union by size limits every parent
/// path to O(log n) cells. <see cref="FirstConnected"/> compares the two current parent paths and
/// takes the latest edge-version below their forest LCA, requiring O(w log n) time, where w is the
/// bounded CHAMP path cost. It does not search the version history.
/// </para>
/// <para>
/// Instances and version tokens are immutable snapshots safe for concurrent readers. Deletion,
/// confluent merging, retroactive updates, and path compression are deliberately outside this
/// type's contract.
/// </para>
/// </remarks>
[DebuggerDisplay("VertexCount = {VertexCount}, ComponentCount = {ComponentCount}, Depth = {Version.Depth}")]
public sealed class PersistentAncestralConnectionForest
{
    private readonly PersistentHashMap<int, Cell> _cells;

    private PersistentAncestralConnectionForest(
        int vertexCount,
        int componentCount,
        PersistentHashMap<int, Cell> cells,
        AncestralConnectionVersion version)
    {
        Debug.Assert(vertexCount >= 0);
        Debug.Assert(componentCount >= 0 && componentCount <= vertexCount);
        VertexCount = vertexCount;
        ComponentCount = componentCount;
        _cells = cells;
        Version = version;
    }

    /// <summary>Gets the fixed number of vertices in this forest.</summary>
    public int VertexCount { get; }

    /// <summary>Gets the number of connected components in this version. O(1).</summary>
    public int ComponentCount { get; }

    /// <summary>Gets the identity token for this history version. O(1).</summary>
    public AncestralConnectionVersion Version { get; }

    /// <summary>Creates an edgeless root version over vertices <c>0 .. vertexCount - 1</c>. O(1).</summary>
    /// <param name="vertexCount">The fixed vertex-universe size.</param>
    /// <returns>A new history root whose vertices are all singleton components.</returns>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="vertexCount"/> is negative.</exception>
    public static PersistentAncestralConnectionForest Create(int vertexCount)
    {
        if (vertexCount < 0)
            throw new ArgumentOutOfRangeException(nameof(vertexCount), vertexCount, "Vertex count cannot be negative.");

        return new(
            vertexCount,
            vertexCount,
            PersistentHashMap<int, Cell>.Empty,
            new AncestralConnectionVersion(parent: null));
    }

    /// <summary>Returns the current union-find representative of a vertex. O(w log n) worst-case.</summary>
    /// <param name="vertex">A vertex in <c>0 .. VertexCount - 1</c>.</param>
    /// <returns>The root representative selected by deterministic union-by-size history.</returns>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="vertex"/> is outside the universe.</exception>
    public int Find(int vertex)
    {
        CheckVertex(vertex, nameof(vertex));
        return FindRoot(vertex);
    }

    /// <summary>Determines whether two vertices are connected. O(w log n) worst-case.</summary>
    /// <param name="left">The first vertex.</param>
    /// <param name="right">The second vertex.</param>
    /// <returns><see langword="true"/> exactly when both vertices have the same current root.</returns>
    /// <exception cref="ArgumentOutOfRangeException">Either vertex is outside the fixed universe.</exception>
    public bool Connected(int left, int right)
    {
        CheckVertices(left, right);
        return FindRoot(left) == FindRoot(right);
    }

    /// <summary>
    /// Creates a child history version containing an undirected connection between two vertices.
    /// </summary>
    /// <param name="left">The first endpoint.</param>
    /// <param name="right">The second endpoint.</param>
    /// <returns>
    /// A distinct child version. When the endpoints were already connected, its connectivity cells
    /// are shared unchanged; otherwise, one union-by-size root edge is added.
    /// </returns>
    /// <exception cref="ArgumentOutOfRangeException">Either endpoint is outside the fixed universe.</exception>
    /// <exception cref="OverflowException">The history depth would exceed <see cref="long.MaxValue"/>.</exception>
    /// <remarks>O(w log n) time and O(w) new CHAMP nodes for a successful union; O(w log n) time and O(1) space when redundant.</remarks>
    public PersistentAncestralConnectionForest Link(int left, int right)
    {
        CheckVertices(left, right);
        var leftRoot = FindRoot(left);
        var rightRoot = FindRoot(right);
        var childVersion = new AncestralConnectionVersion(Version);

        if (leftRoot == rightRoot)
            return new(VertexCount, ComponentCount, _cells, childVersion);

        var leftCell = GetCell(leftRoot);
        var rightCell = GetCell(rightRoot);
        if (leftCell.Size < rightCell.Size)
        {
            (leftRoot, rightRoot) = (rightRoot, leftRoot);
            (leftCell, rightCell) = (rightCell, leftCell);
        }

        // On a size tie the first endpoint's root remains the representative, making the result
        // deterministic without changing the logarithmic-height proof.
        var combinedSize = checked(leftCell.Size + rightCell.Size);
        var cells = _cells
            .SetItem(leftRoot, Cell.Root(leftRoot, combinedSize))
            .SetItem(rightRoot, Cell.Child(leftRoot, childVersion));
        return new(VertexCount, ComponentCount - 1, cells, childVersion);
    }

    /// <summary>
    /// Returns the earliest version on this version's root-to-current history in which two vertices
    /// were connected.
    /// </summary>
    /// <param name="left">The first vertex.</param>
    /// <param name="right">The second vertex.</param>
    /// <returns>
    /// The unique ancestor version where the pair first became connected, or <see langword="null"/>
    /// when they are disconnected in this version. A vertex is connected to itself at
    /// <see cref="AncestralConnectionVersion.Root"/>.
    /// </returns>
    /// <exception cref="ArgumentOutOfRangeException">Either vertex is outside the fixed universe.</exception>
    /// <remarks>O(w log n) worst-case time and O(log n) temporary path space; independent of history depth.</remarks>
    public AncestralConnectionVersion? FirstConnected(int left, int right)
    {
        CheckVertices(left, right);
        if (left == right)
            return Version.Root;

        var leftPath = BuildPath(left);
        var rightPath = BuildPath(right);
        if (leftPath[^1].Vertex != rightPath[^1].Vertex)
            return null;

        // Delete the common rootward suffix. The remaining steps are precisely the two paths
        // strictly below the forest LCA, and every one of those steps owns a successful-union tag.
        var leftLast = leftPath.Count - 1;
        var rightLast = rightPath.Count - 1;
        while (leftLast >= 0
               && rightLast >= 0
               && leftPath[leftLast].Vertex == rightPath[rightLast].Vertex)
        {
            leftLast--;
            rightLast--;
        }

        AncestralConnectionVersion? latest = null;
        for (var index = 0; index <= leftLast; index++)
            latest = Later(latest, leftPath[index].JoinedAt);
        for (var index = 0; index <= rightLast; index++)
            latest = Later(latest, rightPath[index].JoinedAt);

        // Distinct connected vertices have at least one edge strictly below their LCA.
        return latest ?? throw new InvalidOperationException("A connected pair has no union-edge witness.");
    }

    /// <summary>Attempts to find the first ancestor version in which two vertices were connected.</summary>
    /// <param name="left">The first vertex.</param>
    /// <param name="right">The second vertex.</param>
    /// <param name="version">The first connection version on success; otherwise, <see langword="null"/>.</param>
    /// <returns><see langword="true"/> when the pair is connected in this version.</returns>
    public bool TryGetFirstConnected(
        int left,
        int right,
        [NotNullWhen(true)] out AncestralConnectionVersion? version)
    {
        version = FirstConnected(left, right);
        return version is not null;
    }

    /// <summary>
    /// Validates sparse-cell canonicality, current root sizes, the resulting height bound,
    /// union-tag ancestry/order, and component count. O(H + m w log n), where H is history depth,
    /// m is the explicit-cell count, and w is bounded CHAMP depth; test-only.
    /// </summary>
    internal void ValidateInvariants()
    {
        if (VertexCount < 0 || ComponentCount < 0 || ComponentCount > VertexCount)
            throw new InvalidOperationException("Connection-forest counts are invalid.");

        var ancestors = new HashSet<AncestralConnectionVersion>(ReferenceEqualityComparer.Instance);
        var expectedDepth = Version.Depth;
        for (AncestralConnectionVersion? version = Version; version is not null; version = version.Parent)
        {
            if (version.Depth != expectedDepth-- || !ReferenceEquals(version.Root, Version.Root))
                throw new InvalidOperationException("The history-token parent chain is inconsistent.");
            ancestors.Add(version);
        }
        if (expectedDepth != -1
            || !ancestors.Contains(Version.Root)
            || Version.Root.Parent is not null
            || Version.Root.Depth != 0)
        {
            throw new InvalidOperationException("The history root is inconsistent.");
        }

        foreach (var (vertex, cell) in _cells)
        {
            if ((uint)vertex >= (uint)VertexCount || (uint)cell.Parent >= (uint)VertexCount)
                throw new InvalidOperationException("A sparse parent cell addresses a vertex outside the universe.");

            if (cell.Parent == vertex)
            {
                if (cell.Size <= 1 || cell.JoinedAt is not null)
                    throw new InvalidOperationException("A stored root cell is not a canonical non-singleton root.");
            }
            else if (cell.Size != 0
                     || cell.JoinedAt is null
                     || !ancestors.Contains(cell.JoinedAt)
                     || !_cells.ContainsKey(cell.Parent))
            {
                throw new InvalidOperationException("A non-root cell has an invalid size or union-version tag.");
            }
        }

        var componentSizes = new Dictionary<int, int>();
        var maximumHeight = VertexCount <= 1 ? 0 : BitOperations.Log2((uint)VertexCount);
        foreach (var (vertex, _) in _cells)
        {
            var current = vertex;
            var height = 0;
            long previousDepth = -1;
            while (true)
            {
                var cell = GetCell(current);
                if (cell.Parent == current)
                    break;
                if (cell.JoinedAt is null
                    || cell.JoinedAt.Depth <= previousDepth
                    || !ancestors.Contains(cell.JoinedAt))
                {
                    throw new InvalidOperationException("Union-edge versions do not strictly increase toward the root.");
                }

                previousDepth = cell.JoinedAt.Depth;
                current = cell.Parent;
                if (++height > maximumHeight)
                    throw new InvalidOperationException("A parent path exceeds the union-by-size height bound.");
            }

            componentSizes.TryGetValue(current, out var size);
            componentSizes[current] = size + 1;
        }

        var absentSingletons = VertexCount - _cells.Count;
        if (absentSingletons < 0 || absentSingletons + componentSizes.Count != ComponentCount)
            throw new InvalidOperationException("The cached component count disagrees with the parent forest.");
        foreach (var (root, size) in componentSizes)
        {
            if (GetCell(root).Size != size)
                throw new InvalidOperationException("A root's cached union size is incorrect.");
        }
    }

    /// <summary>Reports whether two versions share the exact sparse connectivity root. Test-only.</summary>
    internal bool SharesConnectivityRootWith(PersistentAncestralConnectionForest other) =>
        ReferenceEquals(_cells, other._cells);

    private int FindRoot(int vertex)
    {
        while (true)
        {
            var cell = GetCell(vertex);
            if (cell.Parent == vertex)
                return vertex;
            vertex = cell.Parent;
        }
    }

    private List<PathStep> BuildPath(int vertex)
    {
        var path = new List<PathStep>();
        while (true)
        {
            var cell = GetCell(vertex);
            path.Add(new PathStep(vertex, cell.JoinedAt));
            if (cell.Parent == vertex)
                return path;
            vertex = cell.Parent;
        }
    }

    private Cell GetCell(int vertex) =>
        _cells.TryGetValue(vertex, out var cell) ? cell : Cell.Root(vertex, size: 1);

    private static AncestralConnectionVersion? Later(
        AncestralConnectionVersion? left,
        AncestralConnectionVersion? right) =>
        right is not null && (left is null || right.Depth > left.Depth) ? right : left;

    private void CheckVertices(int left, int right)
    {
        CheckVertex(left, nameof(left));
        CheckVertex(right, nameof(right));
    }

    private void CheckVertex(int vertex, string parameterName)
    {
        if ((uint)vertex >= (uint)VertexCount)
        {
            throw new ArgumentOutOfRangeException(
                parameterName,
                vertex,
                "Vertex must lie within the forest's fixed universe.");
        }
    }

    private readonly record struct Cell(
        int Parent,
        int Size,
        AncestralConnectionVersion? JoinedAt)
    {
        public static Cell Root(int vertex, int size) => new(vertex, size, JoinedAt: null);

        public static Cell Child(int parent, AncestralConnectionVersion joinedAt) =>
            new(parent, 0, joinedAt);
    }

    private readonly record struct PathStep(
        int Vertex,
        AncestralConnectionVersion? JoinedAt);
}

/// <summary>
/// Identifies one immutable version in a <see cref="PersistentAncestralConnectionForest"/> history tree.
/// </summary>
/// <remarks>
/// Tokens use reference identity. Equal depths in sibling branches are not equal versions. The parent
/// chain contains history identities, not mutable connectivity state.
/// </remarks>
[DebuggerDisplay("Depth = {Depth}")]
public sealed class AncestralConnectionVersion
{
    internal AncestralConnectionVersion(AncestralConnectionVersion? parent)
    {
        Parent = parent;
        Depth = parent is null ? 0 : checked(parent.Depth + 1);
        Root = parent?.Root ?? this;
    }

    /// <summary>Gets this version's parent, or <see langword="null"/> at the history root.</summary>
    public AncestralConnectionVersion? Parent { get; }

    /// <summary>Gets the number of link events from the history root.</summary>
    public long Depth { get; }

    /// <summary>Gets the root identity of this version tree. O(1).</summary>
    public AncestralConnectionVersion Root { get; }
}
