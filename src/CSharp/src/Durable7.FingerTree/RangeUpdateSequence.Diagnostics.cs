// The structural diagnostics for the range update sequence, used by the tests to assert on shape
// and sharing.

namespace Durable7.FingerTree;

public sealed partial class RangeUpdateSequence<TElement, TMeasure, TTag, TOps>
    where TOps : IRangeUpdateAlgebra<TElement, TMeasure, TTag>
{
    internal object? RootIdentityForDiagnostics => _root;

    internal RangeUpdateSequenceStructureDiagnostics GetStructureDiagnostics()
    {
        if (_root is null)
        {
            return new(
                Count: 0,
                NodeCount: 0,
                Height: 0,
                MaximumAbsoluteBalanceFactor: 0,
                PendingTagNodeCount: 0,
                MaximumPendingTagDepth: 0);
        }

        var nodes = 0;
        var maximumBalance = 0;
        var pendingTags = 0;
        var maximumPendingDepth = 0;
        var stack = new Stack<(Node Node, int PendingDepth)>();
        stack.Push((_root, 0));
        while (stack.TryPop(out var frame))
        {
            var node = frame.Node;
            nodes = checked(nodes + 1);
            maximumBalance = Math.Max(
                maximumBalance,
                Math.Abs(HeightOf(node.Left) - HeightOf(node.Right)));
            var pendingDepth = frame.PendingDepth;
            if (node.HasPendingTag)
            {
                pendingTags = checked(pendingTags + 1);
                pendingDepth = checked(pendingDepth + 1);
                maximumPendingDepth = Math.Max(maximumPendingDepth, pendingDepth);
            }
            if (node.Right is not null)
                stack.Push((node.Right, pendingDepth));
            if (node.Left is not null)
                stack.Push((node.Left, pendingDepth));
        }

        return new(
            Count,
            nodes,
            _root.Height,
            maximumBalance,
            pendingTags,
            maximumPendingDepth);
    }

    internal object[] GetNodeIdentitiesForDiagnostics()
    {
        if (_root is null)
            return [];

        var result = new object[Count];
        var offset = 0;
        var stack = new Stack<Node>();
        stack.Push(_root);
        while (stack.TryPop(out var node))
        {
            result[offset++] = node;
            if (node.Right is not null)
                stack.Push(node.Right);
            if (node.Left is not null)
                stack.Push(node.Left);
        }

        return result;
    }

    internal int CountSharedNodesForDiagnostics(
        RangeUpdateSequence<TElement, TMeasure, TTag, TOps> other)
    {
        ArgumentNullException.ThrowIfNull(other);
        if (_root is null || other._root is null)
            return 0;

        var mine = new HashSet<object>(ReferenceEqualityComparer.Instance);
        var stack = new Stack<Node>();
        stack.Push(_root);
        while (stack.TryPop(out var node))
        {
            mine.Add(node);
            if (node.Left is not null)
                stack.Push(node.Left);
            if (node.Right is not null)
                stack.Push(node.Right);
        }

        var shared = 0;
        stack.Push(other._root);
        while (stack.TryPop(out var node))
        {
            if (mine.Contains(node))
                shared = checked(shared + 1);
            if (node.Left is not null)
                stack.Push(node.Left);
            if (node.Right is not null)
                stack.Push(node.Right);
        }

        return shared;
    }

    internal RangeUpdateSequenceStructureDiagnostics ValidateInvariants() =>
        ValidateInvariants(static (left, right) => EqualityComparer<TMeasure>.Default.Equals(left, right));

    internal RangeUpdateSequenceStructureDiagnostics ValidateInvariants(
        Func<TMeasure, TMeasure, bool> measureEquals)
    {
        ArgumentNullException.ThrowIfNull(measureEquals);
        if (_root is null)
            return GetStructureDiagnostics();

        var validated = ValidateNode(_root, measureEquals);
        if (validated.Count != Count)
        {
            throw new InvalidOperationException(
                $"Root count {Count} disagrees with recomputed count {validated.Count}.");
        }
        if (validated.Height != _root.Height)
        {
            throw new InvalidOperationException(
                $"Root height {_root.Height} disagrees with recomputed height {validated.Height}.");
        }

        var report = GetStructureDiagnostics();
        if (report.NodeCount != Count)
        {
            throw new InvalidOperationException(
                $"Reachable node count {report.NodeCount} disagrees with root count {Count}.");
        }

        return report;
    }

    private static ValidationResult ValidateNode(
        Node node,
        Func<TMeasure, TMeasure, bool> measureEquals)
    {
        RangeUpdateSequenceDiagnostics.RecordNodeVisit();

        var left = node.Left is null
            ? default
            : ValidateNode(node.Left, measureEquals);
        var right = node.Right is null
            ? default
            : ValidateNode(node.Right, measureEquals);

        var expectedHeight = checked(1 + Math.Max(left.Height, right.Height));
        var expectedCount = checked(checked(left.Count + 1) + right.Count);
        var balance = left.Height - right.Height;
        if (Math.Abs(balance) > 1)
        {
            throw new InvalidOperationException(
                $"AVL balance factor {balance} is outside [-1, 1] at a node of count {node.Count}.");
        }
        if (node.Height != expectedHeight)
        {
            throw new InvalidOperationException(
                $"Cached height {node.Height} disagrees with recomputed height {expectedHeight}.");
        }
        if (node.Count != expectedCount)
        {
            throw new InvalidOperationException(
                $"Cached count {node.Count} disagrees with recomputed count {expectedCount}.");
        }
        if (node.HasPendingTag && IsIdentityTag(node.PendingTag))
        {
            throw new InvalidOperationException(
                "A node retains a pending tag that the algebra recognizes as identity.");
        }

        var expectedMeasure = MeasureElement(node.Value);
        if (node.Left is not null)
        {
            var leftMeasure = node.HasPendingTag
                ? ApplyMeasureTag(node.PendingTag, node.Left.Measure, node.Left.Count)
                : node.Left.Measure;
            expectedMeasure = CombineMeasures(leftMeasure, expectedMeasure);
        }
        if (node.Right is not null)
        {
            var rightMeasure = node.HasPendingTag
                ? ApplyMeasureTag(node.PendingTag, node.Right.Measure, node.Right.Count)
                : node.Right.Measure;
            expectedMeasure = CombineMeasures(expectedMeasure, rightMeasure);
        }

        if (!measureEquals(node.Measure, expectedMeasure))
        {
            throw new InvalidOperationException(
                $"Cached logical measure disagrees with the recomputed measure at a node of count {node.Count}.");
        }

        return new ValidationResult(expectedCount, expectedHeight);
    }

    private readonly record struct ValidationResult(int Count, int Height);
}

/// <summary>Shape and pending-tag measurements from a structural audit.</summary>
internal readonly record struct RangeUpdateSequenceStructureDiagnostics(
    int Count,
    int NodeCount,
    int Height,
    int MaximumAbsoluteBalanceFactor,
    int PendingTagNodeCount,
    int MaximumPendingTagDepth);
