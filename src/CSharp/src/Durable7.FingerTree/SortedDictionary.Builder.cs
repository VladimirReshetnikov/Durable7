// The bulk builder for the sorted dictionary.

using System.Collections;
using System.Diagnostics.CodeAnalysis;

namespace Durable7.FingerTree;

public sealed partial class SortedDictionary<TKey, TValue>
{
    /// <summary>
    /// Mutable staging builder for batched sorted-dictionary edits. Dirty snapshots rebuild the immutable
    /// dictionary from the staged sorted entries; clean snapshots return the cached immutable instance.
    /// </summary>
    /// <remarks>
    /// The builder is single-threaded and mutable. The <see cref="SetItem"/> member is an unconditional write:
    /// it invalidates the cached snapshot even when the assigned value compares equal to the old value.
    /// </remarks>
    public sealed class Builder : IReadOnlyDictionary<TKey, TValue>
    {
        private readonly System.Collections.Generic.SortedSet<KeyValuePair<TKey, TValue>> _items;
        private readonly IComparer<TKey> _comparer;
        private readonly KeyEnumerable _keys;
        private readonly ValueEnumerable _values;
        private int _version;
        private int _cachedVersion;
        private SortedDictionary<TKey, TValue> _cachedResult;

        internal Builder(IComparer<TKey> comparer)
        {
            _comparer = comparer;
            _items = new System.Collections.Generic.SortedSet<KeyValuePair<TKey, TValue>>(new EntryKeyComparer(comparer));
            _keys = new KeyEnumerable(this);
            _values = new ValueEnumerable(this);
            _cachedResult = Create(comparer);
        }

        internal Builder(SortedDictionary<TKey, TValue> source)
        {
            _comparer = source.Comparer;
            _items = new System.Collections.Generic.SortedSet<KeyValuePair<TKey, TValue>>(new EntryKeyComparer(source.Comparer));
            _keys = new KeyEnumerable(this);
            _values = new ValueEnumerable(this);
            foreach (var entry in source)
                _items.Add(entry);
            _cachedResult = source;
        }

        /// <summary>Gets the number of staged entries. O(1).</summary>
        public int Count => _items.Count;

        /// <summary>Gets the comparer that orders staged keys.</summary>
        public IComparer<TKey> Comparer => _comparer;

        /// <summary>Gets the staged value associated with <paramref name="key"/>. O(log n).</summary>
        /// <param name="key">Key to look up.</param>
        /// <exception cref="KeyNotFoundException"><paramref name="key"/> is not present.</exception>
        public TValue this[TKey key] =>
            TryGetValue(key, out var value) ? value : throw new KeyNotFoundException("The key was not present in the dictionary.");

        /// <summary>Gets the staged keys in comparer order.</summary>
        public IEnumerable<TKey> Keys => _keys;

        /// <summary>Gets the staged values in comparer-key order.</summary>
        public IEnumerable<TValue> Values => _values;

        /// <summary>Determines whether <paramref name="key"/> is present. O(log n).</summary>
        /// <param name="key">Key to test.</param>
        /// <returns><see langword="true"/> when present; otherwise <see langword="false"/>.</returns>
        public bool ContainsKey(TKey key) => _items.TryGetValue(Probe(key), out _);

        /// <summary>Looks up the value for <paramref name="key"/>. O(log n).</summary>
        /// <param name="key">Key to look up.</param>
        /// <param name="value">The value when present; otherwise <see langword="default"/>.</param>
        /// <returns><see langword="true"/> when present; otherwise <see langword="false"/>.</returns>
        public bool TryGetValue(TKey key, [MaybeNullWhen(false)] out TValue value)
        {
            if (_items.TryGetValue(Probe(key), out var entry))
            {
                value = entry.Value;
                return true;
            }

            value = default;
            return false;
        }

        /// <summary>Adds a new staged entry. O(log n).</summary>
        /// <param name="key">Key to add.</param>
        /// <param name="value">Value to associate.</param>
        /// <exception cref="ArgumentException"><paramref name="key"/> is already present.</exception>
        public void Add(TKey key, TValue value)
        {
            if (!_items.Add(new KeyValuePair<TKey, TValue>(key, value)))
                throw new ArgumentException("An entry with the same key already exists.", nameof(key));
            _version++;
        }

        /// <summary>Adds a new staged entry unless the key is already present. O(log n).</summary>
        /// <param name="key">Key to add.</param>
        /// <param name="value">Value to associate.</param>
        /// <returns><see langword="true"/> when the entry was added; otherwise <see langword="false"/>.</returns>
        public bool TryAdd(TKey key, TValue value)
        {
            if (!_items.Add(new KeyValuePair<TKey, TValue>(key, value)))
                return false;

            _version++;
            return true;
        }

        /// <summary>Adds or replaces the staged entry for <paramref name="key"/>. O(log n).</summary>
        /// <param name="key">Key to set.</param>
        /// <param name="value">Value to associate.</param>
        public void SetItem(TKey key, TValue value)
        {
            _items.Remove(Probe(key));
            _items.Add(new KeyValuePair<TKey, TValue>(key, value));
            _version++;
        }

        /// <summary>Adds each entry from <paramref name="items"/>, throwing on the first duplicate key.</summary>
        /// <param name="items">Entries to add.</param>
        /// <exception cref="ArgumentNullException"><paramref name="items"/> is <see langword="null"/>.</exception>
        /// <exception cref="ArgumentException">An entry key is already present.</exception>
        public void AddRange(IEnumerable<KeyValuePair<TKey, TValue>> items)
        {
            ArgumentNullException.ThrowIfNull(items);
            foreach (var entry in items)
                Add(entry.Key, entry.Value);
        }

        /// <summary>Removes the staged entry for <paramref name="key"/>, if present. O(log n).</summary>
        /// <param name="key">Key to remove.</param>
        /// <returns><see langword="true"/> when an entry was removed; otherwise <see langword="false"/>.</returns>
        public bool Remove(TKey key)
        {
            if (!_items.Remove(Probe(key)))
                return false;
            _version++;
            return true;
        }

        /// <summary>Removes all staged entries.</summary>
        public void Clear()
        {
            if (Count == 0)
                return;
            _items.Clear();
            _version++;
        }

        /// <summary>
        /// Freezes the staged contents into an immutable sorted dictionary. Repeated calls with no intervening
        /// mutation return the same immutable instance.
        /// </summary>
        /// <returns>An immutable sorted dictionary containing the staged entries.</returns>
        public SortedDictionary<TKey, TValue> ToImmutable()
        {
            if (_cachedVersion == _version)
                return _cachedResult;

            var result = FromSortedDistinctKeys(_items, Comparer);
            _cachedResult = result;
            _cachedVersion = _version;
            return result;
        }

        /// <summary>Returns an enumerator over staged entries in comparer-key order.</summary>
        /// <returns>A fail-fast enumerator over the staged entries.</returns>
        public IEnumerator<KeyValuePair<TKey, TValue>> GetEnumerator() => Enumerate(_version);

        IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

        private IEnumerable<TKey> EnumerateKeys(int version)
        {
            foreach (var entry in _items)
            {
                ThrowIfVersionChanged(version);
                yield return entry.Key;
            }

            ThrowIfVersionChanged(version);
        }

        private IEnumerable<TValue> EnumerateValues(int version)
        {
            foreach (var entry in _items)
            {
                ThrowIfVersionChanged(version);
                yield return entry.Value;
            }

            ThrowIfVersionChanged(version);
        }

        private IEnumerator<KeyValuePair<TKey, TValue>> Enumerate(int version)
        {
            foreach (var entry in _items)
            {
                ThrowIfVersionChanged(version);
                yield return entry;
            }

            ThrowIfVersionChanged(version);
        }

        private void ThrowIfVersionChanged(int capturedVersion)
        {
            if (_version != capturedVersion)
                throw new InvalidOperationException("The sorted-dictionary builder was modified during enumeration.");
        }

        private static KeyValuePair<TKey, TValue> Probe(TKey key) => new(key, default!);

        private sealed class KeyEnumerable(Builder builder) : IEnumerable<TKey>
        {
            public IEnumerator<TKey> GetEnumerator() => builder.EnumerateKeys(builder._version).GetEnumerator();

            IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
        }

        private sealed class ValueEnumerable(Builder builder) : IEnumerable<TValue>
        {
            public IEnumerator<TValue> GetEnumerator() => builder.EnumerateValues(builder._version).GetEnumerator();

            IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
        }

        private sealed class EntryKeyComparer(IComparer<TKey> keyComparer) : IComparer<KeyValuePair<TKey, TValue>>
        {
            public int Compare(KeyValuePair<TKey, TValue> x, KeyValuePair<TKey, TValue> y) =>
                keyComparer.Compare(x.Key, y.Key);
        }
    }
}
