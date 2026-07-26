// The core representation and operations of the range update sequence.

using System.Diagnostics;

namespace Durable7.FingerTree;

public sealed partial class RangeUpdateSequence<TElement, TMeasure, TTag, TOps>
    where TOps : IRangeUpdateAlgebra<TElement, TMeasure, TTag>
{
    private static Node BuildBalanced(ReadOnlySpan<TElement> items)
    {
        var middle = items.Length / 2;
        var left = middle == 0 ? null : BuildBalanced(items[..middle]);
        var right = middle == items.Length - 1 ? null : BuildBalanced(items[(middle + 1)..]);
        return MakeNode(items[middle], left, right);
    }

    private static TElement GetElement(Node node, int index)
    {
        var hasInheritedTag = false;
        var inheritedTag = default(TTag)!;

        while (true)
        {
            RangeUpdateSequenceDiagnostics.RecordNodeVisit();
            var leftCount = CountOf(node.Left);
            if (index == leftCount)
            {
                return hasInheritedTag
                    ? ApplyElementTag(inheritedTag, node.Value)
                    : node.Value;
            }

            GetChildInheritance(
                node,
                hasInheritedTag,
                inheritedTag,
                out hasInheritedTag,
                out inheritedTag);

            if (index < leftCount)
            {
                node = node.Left!;
            }
            else
            {
                index -= leftCount + 1;
                node = node.Right!;
            }
        }
    }

    private static Node InsertCore(Node? node, int index, TElement item)
    {
        if (node is null)
            return MakeNode(item, left: null, right: null);

        RangeUpdateSequenceDiagnostics.RecordNodeVisit();
        node = Push(node);
        var leftCount = CountOf(node.Left);
        if (index <= leftCount)
        {
            var left = InsertCore(node.Left, index, item);
            return Balance(MakeNode(node.Value, left, node.Right));
        }

        var right = InsertCore(node.Right, index - leftCount - 1, item);
        return Balance(MakeNode(node.Value, node.Left, right));
    }

    private static Node SetCore(Node node, int index, TElement item)
    {
        RangeUpdateSequenceDiagnostics.RecordNodeVisit();
        node = Push(node);
        var leftCount = CountOf(node.Left);
        if (index < leftCount)
        {
            var left = SetCore(node.Left!, index, item);
            return MakeNode(node.Value, left, node.Right);
        }
        if (index > leftCount)
        {
            var right = SetCore(node.Right!, index - leftCount - 1, item);
            return MakeNode(node.Value, node.Left, right);
        }

        return MakeNode(item, node.Left, node.Right);
    }

    private static Node? RemoveCore(Node node, int index)
    {
        RangeUpdateSequenceDiagnostics.RecordNodeVisit();
        node = Push(node);
        var leftCount = CountOf(node.Left);
        if (index < leftCount)
        {
            var left = RemoveCore(node.Left!, index);
            return Balance(MakeNode(node.Value, left, node.Right));
        }
        if (index > leftCount)
        {
            var right = RemoveCore(node.Right!, index - leftCount - 1);
            return Balance(MakeNode(node.Value, node.Left, right));
        }

        if (node.Left is null)
            return node.Right;
        if (node.Right is null)
            return node.Left;

        var (successor, rightWithoutSuccessor) = ExtractMinimum(node.Right);
        return Balance(MakeNode(successor, node.Left, rightWithoutSuccessor));
    }

    private static (TElement Minimum, Node? Remainder) ExtractMinimum(Node node)
    {
        RangeUpdateSequenceDiagnostics.RecordNodeVisit();
        node = Push(node);
        if (node.Left is null)
            return (node.Value, node.Right);

        var (minimum, left) = ExtractMinimum(node.Left);
        return (minimum, Balance(MakeNode(node.Value, left, node.Right)));
    }

    private static (Node? Left, Node? Right) Split(Node? node, int index)
    {
        if (node is null)
            return (null, null);
        if (index == 0)
            return (null, node);
        if (index == node.Count)
            return (node, null);

        RangeUpdateSequenceDiagnostics.RecordNodeVisit();
        node = Push(node);
        var leftCount = CountOf(node.Left);
        if (index <= leftCount)
        {
            var (left, middle) = Split(node.Left, index);
            return (left, Join(middle, node.Value, node.Right));
        }

        var (middleRight, right) = Split(node.Right, index - leftCount - 1);
        return (Join(node.Left, node.Value, middleRight), right);
    }

    private static Node Join(Node? left, TElement pivot, Node? right)
    {
        var leftHeight = HeightOf(left);
        var rightHeight = HeightOf(right);
        if (leftHeight <= rightHeight + 1 && rightHeight <= leftHeight + 1)
            return MakeNode(pivot, left, right);

        RangeUpdateSequenceDiagnostics.RecordNodeVisit();
        if (leftHeight > rightHeight)
        {
            left = Push(left!);
            var boundary = Join(left.Right, pivot, right);
            return Balance(MakeNode(left.Value, left.Left, boundary));
        }

        right = Push(right!);
        var leading = Join(left, pivot, right.Left);
        return Balance(MakeNode(right.Value, leading, right.Right));
    }

    private static Node? JoinConcatenated(Node? left, Node? right)
    {
        if (left is null)
            return right;
        if (right is null)
            return left;

        var (pivot, remainder) = ExtractMinimum(right);
        return Join(left, pivot, remainder);
    }

    private static Node Balance(Node node)
    {
        var factor = HeightOf(node.Left) - HeightOf(node.Right);
        if (factor > 1)
        {
            if (HeightOf(node.Left!.Left) < HeightOf(node.Left.Right))
            {
                node = Push(node);
                var left = RotateLeft(node.Left!);
                node = MakeNode(node.Value, left, node.Right);
            }

            return RotateRight(node);
        }

        if (factor < -1)
        {
            if (HeightOf(node.Right!.Right) < HeightOf(node.Right.Left))
            {
                node = Push(node);
                var right = RotateRight(node.Right!);
                node = MakeNode(node.Value, node.Left, right);
            }

            return RotateLeft(node);
        }

        return node;
    }

    private static Node RotateLeft(Node node)
    {
        RangeUpdateSequenceDiagnostics.RecordRotation();
        node = Push(node);
        var pivot = Push(node.Right!);
        var lower = MakeNode(node.Value, node.Left, pivot.Left);
        return MakeNode(pivot.Value, lower, pivot.Right);
    }

    private static Node RotateRight(Node node)
    {
        RangeUpdateSequenceDiagnostics.RecordRotation();
        node = Push(node);
        var pivot = Push(node.Left!);
        var lower = MakeNode(node.Value, pivot.Right, node.Right);
        return MakeNode(pivot.Value, pivot.Left, lower);
    }

    private static Node ApplySubtree(Node node, TTag newerTag)
    {
        RangeUpdateSequenceDiagnostics.RecordNodeVisit();
        RangeUpdateSequenceDiagnostics.RecordSubtreeApplication();

        var value = ApplyElementTag(newerTag, node.Value);
        var measure = ApplyMeasureTag(newerTag, node.Measure, node.Count);

        var hasPendingTag = true;
        var pendingTag = newerTag;
        if (node.HasPendingTag)
        {
            pendingTag = ComposeTags(newerTag, node.PendingTag);
            if (IsIdentityTag(pendingTag))
            {
                hasPendingTag = false;
                pendingTag = default!;
            }
        }

        return NewRawNode(
            value,
            node.Left,
            node.Right,
            node.Height,
            node.Count,
            measure,
            hasPendingTag,
            pendingTag);
    }

    private static Node Push(Node node)
    {
        if (!node.HasPendingTag)
            return node;

        RangeUpdateSequenceDiagnostics.RecordPush();
        var left = node.Left is null ? null : ApplySubtree(node.Left, node.PendingTag);
        var right = node.Right is null ? null : ApplySubtree(node.Right, node.PendingTag);
        return NewRawNode(
            node.Value,
            left,
            right,
            node.Height,
            node.Count,
            node.Measure,
            hasPendingTag: false,
            pendingTag: default!);
    }

    private static TMeasure MeasureRangeCore(
        Node node,
        int index,
        int count,
        bool hasInheritedTag,
        TTag inheritedTag)
    {
        RangeUpdateSequenceDiagnostics.RecordNodeVisit();
        if (index == 0 && count == node.Count)
        {
            return hasInheritedTag
                ? ApplyMeasureTag(inheritedTag, node.Measure, node.Count)
                : node.Measure;
        }

        var end = checked(index + count);
        var leftCount = CountOf(node.Left);
        var hasResult = false;
        var result = default(TMeasure)!;

        var childTagComputed = false;
        var hasChildTag = false;
        var childTag = default(TTag)!;

        if (index < leftCount)
        {
            GetChildInheritance(
                node,
                hasInheritedTag,
                inheritedTag,
                out hasChildTag,
                out childTag);
            childTagComputed = true;

            var childCount = Math.Min(count, leftCount - index);
            result = MeasureRangeCore(node.Left!, index, childCount, hasChildTag, childTag);
            hasResult = true;
        }

        if (index <= leftCount && end > leftCount)
        {
            var singleton = MeasureElement(node.Value);
            if (hasInheritedTag)
                singleton = ApplyMeasureTag(inheritedTag, singleton, count: 1);
            result = hasResult ? CombineMeasures(result, singleton) : singleton;
            hasResult = true;
        }

        var rightStartAtRoot = leftCount + 1;
        if (end > rightStartAtRoot)
        {
            if (!childTagComputed)
            {
                GetChildInheritance(
                    node,
                    hasInheritedTag,
                    inheritedTag,
                    out hasChildTag,
                    out childTag);
            }

            var localStart = Math.Max(0, index - rightStartAtRoot);
            var localEnd = end - rightStartAtRoot;
            var rightMeasure = MeasureRangeCore(
                node.Right!,
                localStart,
                localEnd - localStart,
                hasChildTag,
                childTag);
            result = hasResult ? CombineMeasures(result, rightMeasure) : rightMeasure;
            hasResult = true;
        }

        Debug.Assert(hasResult);
        return result;
    }

    private static void GetChildInheritance(
        Node node,
        bool hasInheritedTag,
        TTag inheritedTag,
        out bool hasChildTag,
        out TTag childTag)
    {
        if (!node.HasPendingTag)
        {
            hasChildTag = hasInheritedTag;
            childTag = inheritedTag;
            return;
        }

        if (!hasInheritedTag)
        {
            hasChildTag = true;
            childTag = node.PendingTag;
            return;
        }

        // Ancestor tags are newer than this node's pending tag. Compose therefore receives the
        // inherited tag first and the node-local tag second. The composed tag is identity-tested
        // and erased from the read frame when possible; both policy calls remain visible through
        // the deterministic diagnostic callback counters.
        childTag = ComposeTags(inheritedTag, node.PendingTag);
        hasChildTag = !IsIdentityTag(childTag);
        if (!hasChildTag)
            childTag = default!;
    }

    private static Node MakeNode(TElement value, Node? left, Node? right)
    {
        // Validate all structural metadata before invoking user policy. This gives count overflow a
        // deterministic precedence and ensures no callbacks run for a structurally impossible node.
        var height = checked(1 + Math.Max(HeightOf(left), HeightOf(right)));
        var count = checked(checked(CountOf(left) + 1) + CountOf(right));

        var measure = MeasureElement(value);
        if (left is not null)
            measure = CombineMeasures(left.Measure, measure);
        if (right is not null)
            measure = CombineMeasures(measure, right.Measure);

        return NewRawNode(
            value,
            left,
            right,
            height,
            count,
            measure,
            hasPendingTag: false,
            pendingTag: default!);
    }

    private static Node NewRawNode(
        TElement value,
        Node? left,
        Node? right,
        int height,
        int count,
        TMeasure measure,
        bool hasPendingTag,
        TTag pendingTag)
    {
        RangeUpdateSequenceDiagnostics.RecordNodeAllocation();
        return new Node(
            value,
            left,
            right,
            height,
            count,
            measure,
            hasPendingTag,
            pendingTag);
    }

    private static TMeasure MeasureElement(TElement element)
    {
        RangeUpdateSequenceDiagnostics.RecordElementMeasureCallback();
        return TOps.Measure(element);
    }

    private static TMeasure CombineMeasures(TMeasure left, TMeasure right)
    {
        RangeUpdateSequenceDiagnostics.RecordMeasureCombineCallback();
        return TOps.Combine(left, right);
    }

    private static bool IsIdentityTag(TTag tag)
    {
        RangeUpdateSequenceDiagnostics.RecordIdentityTestCallback();
        return TOps.IsIdentity(tag);
    }

    private static TTag ComposeTags(TTag newer, TTag older)
    {
        RangeUpdateSequenceDiagnostics.RecordTagComposeCallback();
        return TOps.Compose(newer, older);
    }

    private static TElement ApplyElementTag(TTag tag, TElement element)
    {
        RangeUpdateSequenceDiagnostics.RecordElementApplyCallback();
        return TOps.ApplyElement(tag, element);
    }

    private static TMeasure ApplyMeasureTag(TTag tag, TMeasure measure, int count)
    {
        RangeUpdateSequenceDiagnostics.RecordMeasureApplyCallback();
        return TOps.ApplyMeasure(tag, measure, count);
    }

    private static int HeightOf(Node? node) => node?.Height ?? 0;

    private static int CountOf(Node? node) => node?.Count ?? 0;

    private sealed class Node(
        TElement value,
        Node? left,
        Node? right,
        int height,
        int count,
        TMeasure measure,
        bool hasPendingTag,
        TTag pendingTag)
    {
        internal TElement Value { get; } = value;
        internal Node? Left { get; } = left;
        internal Node? Right { get; } = right;
        internal int Height { get; } = height;
        internal int Count { get; } = count;
        internal TMeasure Measure { get; } = measure;
        internal bool HasPendingTag { get; } = hasPendingTag;
        internal TTag PendingTag { get; } = pendingTag;
    }
}
