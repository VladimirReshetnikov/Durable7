using System.Collections;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;

namespace Durable7.FingerTree;

/// <summary>Describes one maximal half-open run of checkpoint-relative changes.</summary>
/// <param name="Start">The zero-based first changed position.</param>
/// <param name="Length">The number of consecutive changed positions.</param>
public readonly record struct PersistentDirtyRun(int Start, int Length)
{
    /// <summary>Gets the exclusive end position.</summary>
    public int EndExclusive => checked(Start + Length);
}

/// <summary>
/// Represents a fixed-length persistent vector with a checkpoint and an exact maximal-run index of
/// positions whose current values differ from that checkpoint.
/// </summary>
/// <typeparam name="T">Element type.</typeparam>
/// <remarks>
/// <para>
/// Current and checkpoint values are immutable RRB vectors. A persistent ordered map stores one
/// <c>start -&gt; endExclusive</c> entry per maximal dirty run. One effective point edit copies one
/// RRB path and changes at most two neighboring run records, so it takes O(log n) amortized time
/// and fresh nodes. Forking, accepting every change, and reverting every change are O(1).
/// </para>
/// <para>
/// If <c>r</c> maximal runs contain <c>k</c> dirty positions, run enumeration is output-optimal
/// Theta(r). This improves worst-case Theta(n) discovery from unindexed current/checkpoint roots;
/// a straightforward linear coalescing scan of point deltas costs Theta(k), but that scan is not a
/// lower bound for every order-statistic point index. Accepting or reverting one whole indexed run
/// takes O(log n) amortized time through structural RRB splicing, independently of that run's
/// length. Indexed current/checkpoint reads are O(log n), dirty membership is O(log(r + 2))
/// amortized, and complete value enumeration is O(n).
/// </para>
/// <para>
/// Instances are immutable. Concurrent reads and independent branch derivation are safe when the
/// configured value comparer is itself safe for concurrent <see cref="IEqualityComparer{T}.Equals(T, T)"/>
/// calls and remains a stable equivalence relation for every retained version's lifetime. Values
/// retained by a version must not mutate state observed by that comparer; replacing such a value
/// through <see cref="SetItem"/> is the supported way to change equality-observable state.
/// </para>
/// </remarks>
[DebuggerDisplay("Count = {Count}, Dirty = {DirtyCount}, Runs = {DirtyRunCount}")]
public sealed class PersistentRunDeltaVector<T> : IReadOnlyList<T>
{
    private static readonly IEqualityComparer<T> DefaultComparer = EqualityComparer<T>.Default;
    private static readonly PersistentRunDeltaVector<T> EmptyInstance = new(
        RrbVector<Cell>.Empty,
        RrbVector<Cell>.Empty,
        SortedDictionary<int, int>.Empty,
        dirtyCount: 0,
        DefaultComparer);

    private readonly RrbVector<Cell> _current;
    private readonly RrbVector<Cell> _checkpoint;
    private readonly SortedDictionary<int, int> _dirtyRuns;

    private PersistentRunDeltaVector(
        RrbVector<Cell> current,
        RrbVector<Cell> checkpoint,
        SortedDictionary<int, int> dirtyRuns,
        int dirtyCount,
        IEqualityComparer<T> valueComparer)
    {
        _current = current;
        _checkpoint = checkpoint;
        _dirtyRuns = dirtyRuns;
        DirtyCount = dirtyCount;
        ValueComparer = valueComparer;
    }

    /// <summary>Gets the clean empty vector using the default value comparer.</summary>
    public static PersistentRunDeltaVector<T> Empty => EmptyInstance;

    /// <summary>Gets the fixed number of positions.</summary>
    public int Count => _current.Count;

    /// <summary>Gets whether the vector has no positions.</summary>
    public bool IsEmpty => Count == 0;

    /// <summary>Gets the comparer defining checkpoint-relative value equality.</summary>
    public IEqualityComparer<T> ValueComparer { get; }

    /// <summary>Gets the number of positions that differ from the checkpoint.</summary>
    public int DirtyCount { get; }

    /// <summary>Gets the number of maximal dirty runs.</summary>
    public int DirtyRunCount => _dirtyRuns.Count;

    /// <summary>Gets whether at least one current value differs from its checkpoint value.</summary>
    public bool HasChanges => DirtyCount != 0;

    /// <summary>Gets the current value at a zero-based position.</summary>
    /// <param name="index">The zero-based position.</param>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="index"/> is outside the vector.</exception>
    public T this[int index] => GetCell(_current, index).Value;

    /// <summary>Creates a clean empty vector under a supplied value comparer.</summary>
    /// <param name="valueComparer">Value equality, or <see langword="null"/> for the default.</param>
    /// <returns>The clean empty vector.</returns>
    public static PersistentRunDeltaVector<T> Create(IEqualityComparer<T>? valueComparer = null)
    {
        var comparer = valueComparer ?? DefaultComparer;
        return ReferenceEquals(comparer, DefaultComparer)
            ? Empty
            : new(
                RrbVector<Cell>.Empty,
                RrbVector<Cell>.Empty,
                SortedDictionary<int, int>.Empty,
                dirtyCount: 0,
                comparer);
    }

    /// <summary>Creates a clean fixed-length vector from one enumeration of a source.</summary>
    /// <param name="items">Values in positional order.</param>
    /// <param name="valueComparer">Value equality, or <see langword="null"/> for the default.</param>
    /// <returns>A clean vector containing the source values.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="items"/> is <see langword="null"/>.</exception>
    /// <exception cref="OverflowException">The source has more than <see cref="int.MaxValue"/> values.</exception>
    public static PersistentRunDeltaVector<T> CreateRange(
        IEnumerable<T> items,
        IEqualityComparer<T>? valueComparer = null)
    {
        ArgumentNullException.ThrowIfNull(items);
        var comparer = valueComparer ?? DefaultComparer;
        var builder = RrbVector<Cell>.CreateBuilder();
        foreach (var item in items)
            builder.Add(new Cell(item));
        var values = builder.ToImmutable();
        if (values.IsEmpty)
            return Create(comparer);
        return new(
            values,
            values,
            SortedDictionary<int, int>.Empty,
            dirtyCount: 0,
            comparer);
    }

    /// <summary>Gets the checkpoint value at a zero-based position.</summary>
    /// <param name="index">The zero-based position.</param>
    /// <returns>The checkpoint value.</returns>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="index"/> is outside the vector.</exception>
    public T GetCheckpointValue(int index) => GetCell(_checkpoint, index).Value;

    /// <summary>Returns a version with one current value replaced.</summary>
    /// <param name="index">The zero-based position.</param>
    /// <param name="value">The replacement value.</param>
    /// <returns>This version for a comparer-equal no-op; otherwise, an edited version.</returns>
    /// <remarks>
    /// Returning to the checkpoint value cancels that position's delta. A comparer-equal replacement
    /// retains the existing current representative; cancellation restores the exact checkpoint cell.
    /// </remarks>
    public PersistentRunDeltaVector<T> SetItem(int index, T value)
    {
        var current = GetCell(_current, index);
        if (ValueComparer.Equals(current.Value, value))
            return this;

        var checkpoint = _checkpoint[index];
        var wasDirty = TryFindDirtyRun(index, out _);
        var remainsDirty = !ValueComparer.Equals(checkpoint.Value, value);
        var replacement = remainsDirty ? new Cell(value) : checkpoint;
        var nextCurrent = _current.SetItem(index, replacement);
        var nextRuns = _dirtyRuns;
        var nextDirtyCount = DirtyCount;

        if (!wasDirty && remainsDirty)
        {
            nextRuns = AddDirtyPosition(nextRuns, index);
            nextDirtyCount = checked(nextDirtyCount + 1);
        }
        else if (wasDirty && !remainsDirty)
        {
            nextRuns = RemoveDirtyPosition(nextRuns, index);
            nextDirtyCount--;
        }

        if (nextRuns.IsEmpty)
            nextCurrent = _checkpoint;
        return new(
            nextCurrent,
            _checkpoint,
            nextRuns,
            nextDirtyCount,
            ValueComparer);
    }

    /// <summary>Returns a version whose current value at one position is reset to its checkpoint value.</summary>
    /// <param name="index">The zero-based position.</param>
    /// <returns>This version when the position is already clean; otherwise, an edited version.</returns>
    public PersistentRunDeltaVector<T> ResetItem(int index) =>
        SetItem(index, GetCell(_checkpoint, index).Value);

    /// <summary>Accepts every current value as a new checkpoint in O(1).</summary>
    /// <returns>This version when clean; otherwise, a clean version sharing the current root.</returns>
    public PersistentRunDeltaVector<T> Checkpoint() => !HasChanges
        ? this
        : new(
            _current,
            _current,
            SortedDictionary<int, int>.Empty,
            dirtyCount: 0,
            ValueComparer);

    /// <summary>Reverts every current value to the checkpoint in O(1).</summary>
    /// <returns>This version when clean; otherwise, a clean version sharing the checkpoint root.</returns>
    public PersistentRunDeltaVector<T> Rollback() => !HasChanges
        ? this
        : new(
            _checkpoint,
            _checkpoint,
            SortedDictionary<int, int>.Empty,
            dirtyCount: 0,
            ValueComparer);

    /// <summary>Gets whether one position differs from its checkpoint value.</summary>
    /// <param name="index">The zero-based position.</param>
    /// <returns><see langword="true"/> when the position is dirty.</returns>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="index"/> is outside the vector.</exception>
    public bool IsDirty(int index)
    {
        ValidateIndex(index);
        return TryFindDirtyRun(index, out _);
    }

    /// <summary>Tries to get the maximal dirty run containing one position.</summary>
    /// <param name="index">The zero-based position.</param>
    /// <param name="run">The containing run on success; otherwise, the default run.</param>
    /// <returns><see langword="true"/> when the position is dirty.</returns>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="index"/> is outside the vector.</exception>
    public bool TryGetDirtyRunContaining(int index, out PersistentDirtyRun run)
    {
        ValidateIndex(index);
        return TryFindDirtyRun(index, out run);
    }

    /// <summary>Gets a maximal dirty run by ascending start-position rank.</summary>
    /// <param name="rank">The zero-based dirty-run rank.</param>
    /// <returns>The selected run.</returns>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="rank"/> is outside the run index.</exception>
    public PersistentDirtyRun GetDirtyRunAt(int rank)
    {
        if ((uint)rank >= (uint)_dirtyRuns.Count)
            throw new ArgumentOutOfRangeException(nameof(rank));
        return ToRun(_dirtyRuns.EntryAt(rank));
    }

    /// <summary>Enumerates maximal dirty runs in ascending position order.</summary>
    /// <returns>The stable checkpoint-relative runs.</returns>
    public IEnumerable<PersistentDirtyRun> EnumerateDirtyRuns()
    {
        foreach (var entry in _dirtyRuns)
            yield return ToRun(entry);
    }

    /// <summary>Accepts one maximal dirty run into the checkpoint by structural splicing.</summary>
    /// <param name="rank">The zero-based dirty-run rank.</param>
    /// <returns>A version in which the selected run is clean and every other run is unchanged.</returns>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="rank"/> is outside the run index.</exception>
    public PersistentRunDeltaVector<T> AcceptDirtyRunAt(int rank) => AcceptRun(GetDirtyRunAt(rank));

    /// <summary>Reverts one maximal dirty run from the checkpoint by structural splicing.</summary>
    /// <param name="rank">The zero-based dirty-run rank.</param>
    /// <returns>A version in which the selected run is clean and every other run is unchanged.</returns>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="rank"/> is outside the run index.</exception>
    public PersistentRunDeltaVector<T> RevertDirtyRunAt(int rank) => RevertRun(GetDirtyRunAt(rank));

    /// <summary>Accepts the maximal dirty run containing one position into the checkpoint by structural splicing.</summary>
    /// <param name="index">The zero-based position, which need not be the run's start.</param>
    /// <returns>
    /// This version when the position is clean; otherwise, a version in which the containing run is
    /// clean and every other run is unchanged.
    /// </returns>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="index"/> is outside the vector.</exception>
    /// <remarks>
    /// The argument is a position, not a dirty-run rank. Locating the containing run costs
    /// O(log(r + 2)) amortized time over <c>r</c> maximal runs, and the splice costs O(log n)
    /// amortized time independently of that run's length.
    /// </remarks>
    public PersistentRunDeltaVector<T> AcceptDirtyRunContaining(int index)
    {
        ValidateIndex(index);
        return TryFindDirtyRun(index, out var run) ? AcceptRun(run) : this;
    }

    /// <summary>Reverts the maximal dirty run containing one position from the checkpoint by structural splicing.</summary>
    /// <param name="index">The zero-based position, which need not be the run's start.</param>
    /// <returns>
    /// This version when the position is clean; otherwise, a version in which the containing run is
    /// clean and every other run is unchanged.
    /// </returns>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="index"/> is outside the vector.</exception>
    /// <remarks>
    /// The argument is a position, not a dirty-run rank. Locating the containing run costs
    /// O(log(r + 2)) amortized time over <c>r</c> maximal runs, and the splice costs O(log n)
    /// amortized time independently of that run's length.
    /// </remarks>
    public PersistentRunDeltaVector<T> RevertDirtyRunContaining(int index)
    {
        ValidateIndex(index);
        return TryFindDirtyRun(index, out var run) ? RevertRun(run) : this;
    }

    /// <summary>Returns an enumerator over current values in positional order.</summary>
    /// <returns>The current-value enumerator.</returns>
    public IEnumerator<T> GetEnumerator()
    {
        foreach (var cell in _current)
            yield return cell.Value;
    }

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

    internal PersistentRunDeltaVectorStatistics ValidateInvariants()
    {
        if (_current.Count != _checkpoint.Count || DirtyCount < 0 || DirtyCount > Count)
            throw new InvalidOperationException("The vector roots or dirty cardinality are inconsistent.");
        if (!_current.ValidateStructure(out _) || !_checkpoint.ValidateStructure(out _))
            throw new InvalidOperationException("A current or checkpoint RRB root is invalid.");
        if (_dirtyRuns.IsEmpty
            && (DirtyCount != 0 || !ReferenceEquals(_current.RootIdentity, _checkpoint.RootIdentity)))
        {
            throw new InvalidOperationException("A clean vector is not canonicalized to its checkpoint root.");
        }

        var previousEnd = -1;
        var countedDirty = 0;
        foreach (var (start, end) in _dirtyRuns)
        {
            if (start < 0 || start >= end || end > Count || start <= previousEnd)
                throw new InvalidOperationException("Dirty runs are invalid, overlapping, adjacent, or unordered.");
            countedDirty = checked(countedDirty + end - start);
            previousEnd = end;
        }
        if (countedDirty != DirtyCount)
            throw new InvalidOperationException("Dirty run lengths do not sum to DirtyCount.");

        for (var index = 0; index < Count; index++)
        {
            var dirty = TryFindDirtyRun(index, out _);
            var current = _current[index];
            var checkpoint = _checkpoint[index];
            if (!dirty && !ReferenceEquals(current, checkpoint))
                throw new InvalidOperationException("A clean position does not reuse its exact checkpoint cell.");
            if (dirty && ValueComparer.Equals(current.Value, checkpoint.Value))
                throw new InvalidOperationException("A dirty position is comparer-equal to its checkpoint value.");
        }

        return new(Count, DirtyCount, _dirtyRuns.Count);
    }

    private static Cell GetCell(RrbVector<Cell> vector, int index)
    {
        if ((uint)index >= (uint)vector.Count)
            throw new ArgumentOutOfRangeException(nameof(index));
        return vector[index];
    }

    private void ValidateIndex(int index)
    {
        if ((uint)index >= (uint)Count)
            throw new ArgumentOutOfRangeException(nameof(index));
    }

    private bool TryFindDirtyRun(int index, out PersistentDirtyRun run)
    {
        if (_dirtyRuns.TryFloorEntry(index, out var entry) && entry.Value > index)
        {
            run = ToRun(entry);
            return true;
        }
        run = default;
        return false;
    }

    private PersistentRunDeltaVector<T> AcceptRun(PersistentDirtyRun run)
    {
        if (_dirtyRuns.Count == 1)
            return Checkpoint();

        var nextCheckpoint = ReplaceRange(_checkpoint, _current, run);
        return new(
            _current,
            nextCheckpoint,
            _dirtyRuns.Remove(run.Start),
            DirtyCount - run.Length,
            ValueComparer);
    }

    private PersistentRunDeltaVector<T> RevertRun(PersistentDirtyRun run)
    {
        if (_dirtyRuns.Count == 1)
            return Rollback();

        var nextCurrent = ReplaceRange(_current, _checkpoint, run);
        return new(
            nextCurrent,
            _checkpoint,
            _dirtyRuns.Remove(run.Start),
            DirtyCount - run.Length,
            ValueComparer);
    }

    private static PersistentDirtyRun ToRun(KeyValuePair<int, int> entry) =>
        new(entry.Key, entry.Value - entry.Key);

    private static SortedDictionary<int, int> AddDirtyPosition(
        SortedDictionary<int, int> runs,
        int index)
    {
        var hasLeft = runs.TryFloorEntry(index, out var left) && left.Value == index;
        var hasRight = runs.TryCeilingEntry(index, out var right) && right.Key == checked(index + 1);
        if (hasLeft && hasRight)
        {
            return runs
                .Remove(right.Key)
                .SetItem(left.Key, right.Value);
        }
        if (hasLeft)
            return runs.SetItem(left.Key, checked(index + 1));
        if (hasRight)
            return runs.Remove(right.Key).SetItem(index, right.Value);
        return runs.SetItem(index, checked(index + 1));
    }

    private static SortedDictionary<int, int> RemoveDirtyPosition(
        SortedDictionary<int, int> runs,
        int index)
    {
        if (!runs.TryFloorEntry(index, out var containing) || containing.Value <= index)
            throw new InvalidOperationException("The removed dirty position is absent from the run index.");

        var start = containing.Key;
        var end = containing.Value;
        if (start == index && end == index + 1)
            return runs.Remove(start);
        if (start == index)
            return runs.Remove(start).SetItem(index + 1, end);
        if (end == index + 1)
            return runs.SetItem(start, index);
        return runs.SetItem(start, index).SetItem(index + 1, end);
    }

    private static RrbVector<Cell> ReplaceRange(
        RrbVector<Cell> destination,
        RrbVector<Cell> source,
        PersistentDirtyRun run)
    {
        if (run.Start == 0 && run.Length == destination.Count)
            return source;

        var (left, destinationTail) = destination.SplitAt(run.Start);
        var (_, right) = destinationTail.SplitAt(run.Length);
        var (_, sourceTail) = source.SplitAt(run.Start);
        var (middle, _) = sourceTail.SplitAt(run.Length);
        return left.Concat(middle).Concat(right);
    }

    private sealed class Cell(T value)
    {
        internal T Value { get; } = value;
    }
}

internal readonly record struct PersistentRunDeltaVectorStatistics(
    int Count,
    int DirtyCount,
    int DirtyRunCount);
