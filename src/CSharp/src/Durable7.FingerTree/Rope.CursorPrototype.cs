// The cursor prototype of the rope.

namespace Durable7.FingerTree;

public sealed partial class Rope<T>
{
    /// <summary>Gets the tree for cursor prototype.</summary>
    internal FingerTree<Chunk<T>, int, ChunkLengthMeasure<T>> TreeForCursorPrototype => _tree;

    /// <summary>Returns the class cursor prototype.</summary>
    internal RopeCursorPrototype<T> GetClassCursorPrototype(
        int position,
        int focusCapacity,
        int flushChunkSize) =>
        RopeCursorPrototype<T>.Create(this, position, focusCapacity, flushChunkSize);

    /// <summary>Returns the struct cursor prototype.</summary>
    internal RopeCursorStructPrototype<T> GetStructCursorPrototype(
        int position,
        int focusCapacity,
        int flushChunkSize) =>
        RopeCursorStructPrototype<T>.Create(this, position, focusCapacity, flushChunkSize);

    /// <summary>Returns the mutable cursor prototype.</summary>
    internal RopeMutableCursorPrototype<T> GetMutableCursorPrototype(
        int position,
        int focusCapacity,
        int flushChunkSize) =>
        RopeMutableCursorPrototype<T>.Create(this, position, focusCapacity, flushChunkSize);

    /// <summary>
    /// Builds a rope from a prototype cursor's pieces, so the prototypes can be compared against the real rope.
    /// </summary>
    internal static Rope<T> FromCursorPrototypeParts(
        FingerTree<Chunk<T>, int, ChunkLengthMeasure<T>> left,
        ReadOnlySpan<T> middle,
        FingerTree<Chunk<T>, int, ChunkLengthMeasure<T>> right)
    {
        var result = Wrap(left);
        if (!middle.IsEmpty)
            result = result.Concat(Create(middle));
        return result.Concat(Wrap(right));
    }
}
