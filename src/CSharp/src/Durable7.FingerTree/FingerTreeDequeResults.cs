namespace Durable7.FingerTree;

/// <summary>
/// Represents the result of splitting a deque into a prefix and suffix.
/// </summary>
/// <typeparam name="T">Element type stored in the deque.</typeparam>
/// <param name="Left">Elements before the split point.</param>
/// <param name="Right">Elements at and after the split point.</param>
public readonly record struct FingerTreeDequeSplit<T>(
    FingerTreeDeque<T> Left,
    FingerTreeDeque<T> Right);

/// <summary>
/// Represents the result of splitting a deque around a single indexed item.
/// </summary>
/// <typeparam name="T">Element type stored in the deque.</typeparam>
/// <param name="Left">Elements before the indexed item.</param>
/// <param name="Item">The indexed item.</param>
/// <param name="Right">Elements after the indexed item.</param>
public readonly record struct FingerTreeDequeItemSplit<T>(
    FingerTreeDeque<T> Left,
    T Item,
    FingerTreeDeque<T> Right);

/// <summary>
/// Represents the result of splitting a deque around a contiguous range.
/// </summary>
/// <typeparam name="T">Element type stored in the deque.</typeparam>
/// <param name="Before">Elements before the range.</param>
/// <param name="Range">Elements in the selected range.</param>
/// <param name="After">Elements after the range.</param>
public readonly record struct FingerTreeDequeRangeSplit<T>(
    FingerTreeDeque<T> Before,
    FingerTreeDeque<T> Range,
    FingerTreeDeque<T> After);

/// <summary>
/// Represents the result of popping one endpoint from a deque.
/// </summary>
/// <typeparam name="T">Element type stored in the deque.</typeparam>
/// <param name="Value">Removed endpoint value.</param>
/// <param name="Rest">Remaining deque after removal.</param>
public readonly record struct FingerTreeDequePop<T>(
    T Value,
    FingerTreeDeque<T> Rest);
