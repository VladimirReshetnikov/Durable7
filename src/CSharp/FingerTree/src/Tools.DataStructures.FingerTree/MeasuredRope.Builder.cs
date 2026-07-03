using System.Collections;

namespace Tools.DataStructures.FingerTree;

public sealed partial class MeasuredRope<T, TMeasure, TMeasureOps>
    where TMeasureOps : IMeasure<T, TMeasure>
{
    /// <summary>
    /// Mutable append-only builder for <see cref="MeasuredRope{T, TMeasure, TMeasureOps}"/>. The builder keeps an
    /// immutable frozen prefix plus a mutable staged tail, while maintaining the combined measure of all staged
    /// elements.
    /// </summary>
    /// <remarks>
    /// The builder is single-threaded and mutable. Mutating it after <see cref="ToImmutable"/> never changes any
    /// previously produced rope; each snapshot is an immutable value safe for concurrent reads.
    /// </remarks>
    public sealed class Builder : IReadOnlyCollection<T>
    {
        private readonly MeasuredChunkBuilder<T, TMeasure, TMeasureOps> _tail = new();
        private MeasuredRope<T, TMeasure, TMeasureOps> _prefix;
        private int _version;

        internal Builder(MeasuredRope<T, TMeasure, TMeasureOps> prefix) => _prefix = prefix;

        /// <summary>Gets the number of staged elements. O(1).</summary>
        public int Count => _prefix.Count + _tail.Count;

        /// <summary>Gets the combined measure of the staged elements. O(1).</summary>
        public TMeasure Measure => TMeasureOps.Combine(_prefix.Measure, _tail.Measure);

        /// <summary>Appends one element to the staged tail. Amortized O(1).</summary>
        /// <param name="item">Element to append.</param>
        /// <exception cref="OverflowException">The builder would exceed <see cref="int.MaxValue"/> elements.</exception>
        public void Add(T item)
        {
            EnsureCanAdd(1);
            _tail.Add(item);
            _version++;
        }

        /// <summary>Appends a span of elements to the staged tail. O(m).</summary>
        /// <param name="items">Elements to append.</param>
        /// <exception cref="OverflowException">The builder would exceed <see cref="int.MaxValue"/> elements.</exception>
        public void AddRange(ReadOnlySpan<T> items)
        {
            if (items.IsEmpty)
                return;

            EnsureCanAdd(items.Length);
            _tail.Add(items);
            _version++;
        }

        /// <summary>Appends an enumerable source to the staged tail, enumerating it once. O(m).</summary>
        /// <param name="items">Elements to append.</param>
        /// <exception cref="ArgumentNullException"><paramref name="items"/> is <see langword="null"/>.</exception>
        /// <exception cref="OverflowException">The builder would exceed <see cref="int.MaxValue"/> elements.</exception>
        public void AddRange(IEnumerable<T> items)
        {
            ArgumentNullException.ThrowIfNull(items);
            if (items is T[] array)
            {
                AddRange(array);
                return;
            }

            foreach (var item in items)
                Add(item);
        }

        /// <summary>Removes all staged elements and resets the frozen prefix to <see cref="Empty"/>.</summary>
        public void Clear()
        {
            if (Count == 0)
                return;

            _prefix = EmptyInstance;
            _tail.Clear();
            _version++;
        }

        /// <summary>
        /// Freezes staged elements into an immutable measured rope. Repeated calls with no intervening mutation
        /// return the same immutable instance.
        /// </summary>
        /// <returns>An immutable measured rope containing the staged elements.</returns>
        public MeasuredRope<T, TMeasure, TMeasureOps> ToImmutable()
        {
            if (_tail.Count == 0)
                return _prefix;

            var tail = FromFrozenChunks(_tail.FreezeChunks());
            _prefix = _prefix.Concat(tail);
            return _prefix;
        }

        /// <summary>Returns an enumerator over the staged elements in order.</summary>
        /// <returns>A fail-fast enumerator over the staged elements.</returns>
        public IEnumerator<T> GetEnumerator()
        {
            var version = _version;
            var snapshot = ToImmutable();
            return Enumerate(snapshot, version);
        }

        IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

        private IEnumerator<T> Enumerate(MeasuredRope<T, TMeasure, TMeasureOps> snapshot, int version)
        {
            foreach (var item in snapshot)
            {
                ThrowIfVersionChanged(version);
                yield return item;
            }

            ThrowIfVersionChanged(version);
        }

        private void EnsureCanAdd(int count)
        {
            if (count > int.MaxValue - Count)
                throw new OverflowException("The measured rope builder cannot contain more than Int32.MaxValue elements.");
        }

        private void ThrowIfVersionChanged(int capturedVersion)
        {
            if (_version != capturedVersion)
                throw new InvalidOperationException("The measured rope builder was modified during enumeration.");
        }
    }
}
