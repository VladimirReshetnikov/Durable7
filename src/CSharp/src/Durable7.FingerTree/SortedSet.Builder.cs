// The bulk builder for the sorted set.

using System.Collections;

namespace Durable7.FingerTree;

public sealed partial class SortedSet<T>
{
    /// <summary>
    /// Mutable staging builder for batched sorted-set edits. Dirty snapshots rebuild the immutable set from the
    /// staged sorted contents; clean snapshots return the cached immutable instance.
    /// </summary>
    /// <remarks>
    /// The builder is single-threaded and mutable. Use it for many edits followed by few snapshots; for small
    /// batches against a large existing set, the immutable <see cref="Add"/> and <see cref="AddRange"/> members
    /// avoid the builder's rebuild cost.
    /// </remarks>
    public sealed class Builder : IReadOnlyCollection<T>
    {
        private readonly System.Collections.Generic.SortedSet<T> _items;
        private int _version;
        private int _cachedVersion;
        private SortedSet<T> _cachedResult;

        internal Builder(IComparer<T> comparer)
        {
            _items = new System.Collections.Generic.SortedSet<T>(comparer);
            _cachedResult = Create(comparer);
        }

        internal Builder(SortedSet<T> source)
        {
            _items = new System.Collections.Generic.SortedSet<T>(source, source.Comparer);
            _cachedResult = source;
        }

        /// <summary>Gets the number of staged elements. O(1).</summary>
        public int Count => _items.Count;

        /// <summary>Gets the comparer that orders staged elements.</summary>
        public IComparer<T> Comparer => _items.Comparer;

        /// <summary>Gets the least staged element under <see cref="Comparer"/>. O(log n).</summary>
        /// <exception cref="InvalidOperationException">The builder is empty.</exception>
        public T Min => Count == 0 ? throw EmptyError() : _items.Min!;

        /// <summary>Gets the greatest staged element under <see cref="Comparer"/>. O(log n).</summary>
        /// <exception cref="InvalidOperationException">The builder is empty.</exception>
        public T Max => Count == 0 ? throw EmptyError() : _items.Max!;

        /// <summary>Determines whether an element comparing equal to <paramref name="item"/> is present. O(log n).</summary>
        /// <param name="item">Element to search for.</param>
        /// <returns><see langword="true"/> when an equal element is present; otherwise <see langword="false"/>.</returns>
        public bool Contains(T item) => _items.Contains(item);

        /// <summary>Adds <paramref name="item"/> when no comparer-equal element is already staged. O(log n).</summary>
        /// <param name="item">Element to add.</param>
        /// <returns><see langword="true"/> when the builder changed; otherwise <see langword="false"/>.</returns>
        public bool Add(T item)
        {
            if (!_items.Add(item))
                return false;
            _version++;
            return true;
        }

        /// <summary>Removes the element comparing equal to <paramref name="item"/>, if present. O(log n).</summary>
        /// <param name="item">Element to remove.</param>
        /// <returns><see langword="true"/> when an element was removed; otherwise <see langword="false"/>.</returns>
        public bool Remove(T item)
        {
            if (!_items.Remove(item))
                return false;
            _version++;
            return true;
        }

        /// <summary>Adds all elements from <paramref name="items"/>; duplicates are ignored. O(m log(n + m)).</summary>
        /// <param name="items">Elements to add.</param>
        /// <exception cref="ArgumentNullException"><paramref name="items"/> is <see langword="null"/>.</exception>
        public void UnionWith(IEnumerable<T> items)
        {
            ArgumentNullException.ThrowIfNull(items);
            var count = Count;
            _items.UnionWith(items);
            if (_items.Count != count)
                _version++;
        }

        /// <summary>Removes all elements comparing equal to elements from <paramref name="items"/>. O(m log n).</summary>
        /// <param name="items">Elements to remove.</param>
        /// <exception cref="ArgumentNullException"><paramref name="items"/> is <see langword="null"/>.</exception>
        public void ExceptWith(IEnumerable<T> items)
        {
            ArgumentNullException.ThrowIfNull(items);
            var count = Count;
            _items.ExceptWith(items);
            if (_items.Count != count)
                _version++;
        }

        /// <summary>Removes all staged elements.</summary>
        public void Clear()
        {
            if (Count == 0)
                return;
            _items.Clear();
            _version++;
        }

        /// <summary>
        /// Freezes the staged contents into an immutable sorted set. Repeated calls with no intervening mutation
        /// return the same immutable instance.
        /// </summary>
        /// <returns>An immutable sorted set containing the staged elements.</returns>
        public SortedSet<T> ToImmutable()
        {
            if (_cachedVersion == _version)
                return _cachedResult;

            var result = FromSortedDistinct(_items, Comparer);
            _cachedResult = result;
            _cachedVersion = _version;
            return result;
        }

        /// <summary>Returns an enumerator over the staged elements in comparer order.</summary>
        /// <returns>A fail-fast enumerator over the staged elements.</returns>
        public IEnumerator<T> GetEnumerator() => Enumerate(_version);

        IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

        private IEnumerator<T> Enumerate(int version)
        {
            foreach (var item in _items)
            {
                ThrowIfVersionChanged(version);
                yield return item;
            }

            ThrowIfVersionChanged(version);
        }

        private void ThrowIfVersionChanged(int capturedVersion)
        {
            if (_version != capturedVersion)
                throw new InvalidOperationException("The sorted-set builder was modified during enumeration.");
        }
    }
}
