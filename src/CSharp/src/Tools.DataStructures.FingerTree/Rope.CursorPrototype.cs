namespace Tools.DataStructures.FingerTree;

public sealed partial class Rope<T>
{
    internal FingerTree<Chunk<T>, int, ChunkLengthMeasure<T>> TreeForCursorPrototype => _tree;

    internal RopeCursorPrototype<T> GetClassCursorPrototype(
        int position,
        int focusCapacity,
        int flushChunkSize) =>
        RopeCursorPrototype<T>.Create(this, position, focusCapacity, flushChunkSize);

    internal RopeCursorStructPrototype<T> GetStructCursorPrototype(
        int position,
        int focusCapacity,
        int flushChunkSize) =>
        RopeCursorStructPrototype<T>.Create(this, position, focusCapacity, flushChunkSize);

    internal RopeMutableCursorPrototype<T> GetMutableCursorPrototype(
        int position,
        int focusCapacity,
        int flushChunkSize) =>
        RopeMutableCursorPrototype<T>.Create(this, position, focusCapacity, flushChunkSize);

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
