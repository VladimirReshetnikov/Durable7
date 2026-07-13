using System.Collections;

namespace Tools.DataStructures.Hamt;

public sealed partial class PersistentHashSet<T>
{
    internal enum TransientFailurePoint
    {
        BeforeSetWrapperAllocation,
        SetPublicationPrepared,
    }

    /// <summary>Creates an empty single-owner mutable set session.</summary>
    /// <param name="comparer">
    /// The comparer that defines item hashing and equality, or <see langword="null"/> to use
    /// <see cref="EqualityComparer{T}.Default"/>.
    /// </param>
    /// <returns>An active transient whose publication preserves the selected comparer.</returns>
    /// <remarks>
    /// Creation is O(1), allocates the session and its thin map-transient engine, and walks no trie.
    /// The session is unsynchronized and is consumed by its first successful
    /// <see cref="Transient.Persist"/> call.
    /// </remarks>
    public static Transient CreateTransient(IEqualityComparer<T>? comparer = null)
    {
        var source = Create(comparer);
        return source.ToTransient();
    }

    /// <summary>Adopts this set into a single-owner mutable editing session.</summary>
    /// <returns>An active transient initially representing this set.</returns>
    /// <remarks>
    /// Adoption is O(1) and does not walk or copy the trie. A clean session, including one subjected
    /// only to logical no-op edits, publishes this exact set object. Concurrent access to the
    /// transient is unsupported; this immutable source remains safe for concurrent readers.
    /// </remarks>
    public Transient ToTransient() => new(this, _map.ToTransient());

    internal Transient CreateTransientForDiagnostics() =>
        new(this, _map.CreateSeparateNodeTransientKernel());

    /// <summary>Provides a single-owner mutable editing session over a persistent hash set.</summary>
    /// <remarks>
    /// <para>
    /// The session is a thin facade over a map transient and implements <see cref="IReadOnlySet{T}"/>
    /// while active. <see cref="Persist"/> publishes once and consumes the session. Every later read,
    /// mutation, enumeration request, relation query, or publication attempt throws
    /// <see cref="ObjectDisposedException"/>.
    /// </para>
    /// <para>
    /// Enumerators fail fast after a successful content change. Duplicate additions, absent removals,
    /// and clearing an empty session are logical no-ops that leave enumerators valid and retain the
    /// first stored item representative.
    /// </para>
    /// <para>
    /// The session is unsynchronized and has one logical owner. Sequential transfer between threads
    /// requires caller-provided synchronization; concurrent access is unsupported. Adoption and
    /// publication are O(1) and do not walk the trie.
    /// </para>
    /// </remarks>
    public sealed class Transient : IReadOnlySet<T>
    {
        private readonly PersistentHashSet<T> _source;
        private readonly PersistentHashMap<T, Unit>.Transient _map;

        internal Transient(
            PersistentHashSet<T> source,
            PersistentHashMap<T, Unit>.Transient map)
        {
            _source = source;
            _map = map;
        }

        internal Action<TransientFailurePoint>? FailureInjector { get; set; }

        internal PersistentHashMap<T, Unit>.Transient MapForDiagnostics => _map;

        /// <summary>Gets the number of items in the active session.</summary>
        /// <exception cref="ObjectDisposedException">The session has already been published.</exception>
        public int Count => _map.Count;

        /// <summary>Gets the comparer that defines item hashing and equality.</summary>
        /// <exception cref="ObjectDisposedException">The session has already been published.</exception>
        public IEqualityComparer<T> Comparer => _map.Comparer;

        /// <summary>Determines whether an equivalent item is present in the active session.</summary>
        /// <param name="item">The item to locate.</param>
        /// <returns><see langword="true"/> when an equivalent item is present.</returns>
        /// <exception cref="ObjectDisposedException">The session has already been published.</exception>
        public bool Contains(T item) => _map.ContainsKey(item);

        /// <summary>Finds the originally stored item representative equivalent to a supplied value.</summary>
        /// <param name="equalValue">The value whose equivalent representative is requested.</param>
        /// <param name="actualValue">
        /// The stored representative when found; otherwise, <paramref name="equalValue"/> itself.
        /// </param>
        /// <returns><see langword="true"/> when an equivalent item is present.</returns>
        /// <exception cref="ObjectDisposedException">The session has already been published.</exception>
        public bool TryGetValue(T equalValue, out T actualValue) =>
            _map.TryGetKey(equalValue, out actualValue);

        /// <summary>Adds an item when no equivalent item is present.</summary>
        /// <param name="item">The item to add.</param>
        /// <returns><see langword="true"/> when the item was added; otherwise, <see langword="false"/>.</returns>
        /// <exception cref="ObjectDisposedException">The session has already been published.</exception>
        /// <remarks>A duplicate is a logical no-op and retains the first stored item object.</remarks>
        public bool Add(T item) => _map.TryAdd(item, default);

        /// <summary>Removes an equivalent item when present.</summary>
        /// <param name="item">The item to remove.</param>
        /// <returns><see langword="true"/> when an item was removed; otherwise, <see langword="false"/>.</returns>
        /// <exception cref="ObjectDisposedException">The session has already been published.</exception>
        /// <remarks>An absent item is a logical no-op.</remarks>
        public bool Remove(T item) => _map.Remove(item);

        /// <summary>Removes every item from the active session.</summary>
        /// <exception cref="ObjectDisposedException">The session has already been published.</exception>
        /// <remarks>Clearing an already empty session is a logical no-op.</remarks>
        public void Clear() => _map.Clear();

        /// <summary>Determines whether this active session is a subset of the supplied items.</summary>
        /// <param name="other">The items to compare against.</param>
        /// <returns><see langword="true"/> when every session item is present in <paramref name="other"/>.</returns>
        /// <exception cref="ArgumentNullException"><paramref name="other"/> is <see langword="null"/>.</exception>
        /// <exception cref="ObjectDisposedException">The session has already been published.</exception>
        public bool IsSubsetOf(IEnumerable<T> other)
        {
            _map.EnsureActiveForFacade();
            ArgumentNullException.ThrowIfNull(other);
            var probe = new HashSet<T>(other, _map.Comparer);
            foreach (var item in this)
            {
                if (!probe.Contains(item))
                    return false;
            }
            return true;
        }

        /// <summary>Determines whether this active session is a proper subset of supplied items.</summary>
        /// <param name="other">The items to compare against.</param>
        /// <returns><see langword="true"/> when the session is a strict subset.</returns>
        /// <exception cref="ArgumentNullException"><paramref name="other"/> is <see langword="null"/>.</exception>
        /// <exception cref="ObjectDisposedException">The session has already been published.</exception>
        public bool IsProperSubsetOf(IEnumerable<T> other)
        {
            _map.EnsureActiveForFacade();
            ArgumentNullException.ThrowIfNull(other);
            var probe = new HashSet<T>(other, _map.Comparer);
            if (_map.Count >= probe.Count)
                return false;
            foreach (var item in this)
            {
                if (!probe.Contains(item))
                    return false;
            }
            return true;
        }

        /// <summary>Determines whether this active session is a superset of supplied items.</summary>
        /// <param name="other">The items to compare against.</param>
        /// <returns><see langword="true"/> when every supplied item is present.</returns>
        /// <exception cref="ArgumentNullException"><paramref name="other"/> is <see langword="null"/>.</exception>
        /// <exception cref="ObjectDisposedException">The session has already been published.</exception>
        public bool IsSupersetOf(IEnumerable<T> other)
        {
            _map.EnsureActiveForFacade();
            ArgumentNullException.ThrowIfNull(other);
            foreach (var item in other)
            {
                if (!_map.ContainsKey(item))
                    return false;
            }
            return true;
        }

        /// <summary>Determines whether this active session is a proper superset of supplied items.</summary>
        /// <param name="other">The items to compare against.</param>
        /// <returns><see langword="true"/> when the session is a strict superset.</returns>
        /// <exception cref="ArgumentNullException"><paramref name="other"/> is <see langword="null"/>.</exception>
        /// <exception cref="ObjectDisposedException">The session has already been published.</exception>
        public bool IsProperSupersetOf(IEnumerable<T> other)
        {
            _map.EnsureActiveForFacade();
            ArgumentNullException.ThrowIfNull(other);
            var probe = new HashSet<T>(other, _map.Comparer);
            if (probe.Count >= _map.Count)
                return false;
            foreach (var item in probe)
            {
                if (!_map.ContainsKey(item))
                    return false;
            }
            return true;
        }

        /// <summary>Determines whether this active session overlaps supplied items.</summary>
        /// <param name="other">The items to compare against.</param>
        /// <returns><see langword="true"/> when at least one supplied item is present.</returns>
        /// <exception cref="ArgumentNullException"><paramref name="other"/> is <see langword="null"/>.</exception>
        /// <exception cref="ObjectDisposedException">The session has already been published.</exception>
        public bool Overlaps(IEnumerable<T> other)
        {
            _map.EnsureActiveForFacade();
            ArgumentNullException.ThrowIfNull(other);
            foreach (var item in other)
            {
                if (_map.ContainsKey(item))
                    return true;
            }
            return false;
        }

        /// <summary>Determines whether this active session and supplied items form equal sets.</summary>
        /// <param name="other">The items to compare against.</param>
        /// <returns><see langword="true"/> when both sides contain the same distinct items.</returns>
        /// <exception cref="ArgumentNullException"><paramref name="other"/> is <see langword="null"/>.</exception>
        /// <exception cref="ObjectDisposedException">The session has already been published.</exception>
        public bool SetEquals(IEnumerable<T> other)
        {
            _map.EnsureActiveForFacade();
            ArgumentNullException.ThrowIfNull(other);
            var probe = new HashSet<T>(other, _map.Comparer);
            if (probe.Count != _map.Count)
                return false;
            foreach (var item in this)
            {
                if (!probe.Contains(item))
                    return false;
            }
            return true;
        }

        /// <summary>Publishes the active contents as an immutable set and consumes this session.</summary>
        /// <returns>
        /// The published set. A clean adopted session returns its exact source set; a clean factory
        /// session returns its comparer-preserving empty source.
        /// </returns>
        /// <exception cref="ObjectDisposedException">The session has already been published.</exception>
        /// <remarks>
        /// Publication is O(1) and walks no trie. Both the immutable map and set wrappers are prepared
        /// before the non-throwing commit, so any preparation failure leaves the session active,
        /// unchanged, and retryable.
        /// </remarks>
        public PersistentHashSet<T> Persist()
        {
            var preparedMap = _map.PreparePublication();
            PersistentHashSet<T> result;
            if (ReferenceEquals(preparedMap.Result, _source._map))
            {
                result = _source;
            }
            else if (ReferenceEquals(preparedMap.Result, PersistentHashMap<T, Unit>.Empty))
            {
                result = Empty;
            }
            else
            {
                FailureInjector?.Invoke(TransientFailurePoint.BeforeSetWrapperAllocation);
                result = new PersistentHashSet<T>(preparedMap.Result);
            }

            FailureInjector?.Invoke(TransientFailurePoint.SetPublicationPrepared);
            _map.CommitPublication(preparedMap);
            return result;
        }

        /// <summary>Returns an allocation-free, version-bound enumerator for the active session.</summary>
        /// <returns>An enumerator over items in trie order.</returns>
        /// <exception cref="ObjectDisposedException">The session has already been published.</exception>
        /// <remarks>
        /// Successful changes invalidate the enumerator with <see cref="InvalidOperationException"/>;
        /// publication invalidates it with <see cref="ObjectDisposedException"/>. Copied enumerators
        /// advance independently.
        /// </remarks>
        public Enumerator GetEnumerator() => new(_map.GetEnumerator());

        IEnumerator<T> IEnumerable<T>.GetEnumerator() => GetEnumerator();

        IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

        /// <summary>Enumerates items in an active set transient.</summary>
        public struct Enumerator : IEnumerator<T>
        {
            private PersistentHashMap<T, Unit>.Transient.Enumerator _inner;

            internal Enumerator(PersistentHashMap<T, Unit>.Transient.Enumerator inner)
            {
                _inner = inner;
            }

            /// <summary>Gets the item at the current enumerator position.</summary>
            /// <exception cref="InvalidOperationException">The active session was modified.</exception>
            /// <exception cref="ObjectDisposedException">The session was published.</exception>
            public readonly T Current => _inner.Current.Key;

            readonly object? IEnumerator.Current => Current;

            /// <summary>Advances the enumerator to the next item.</summary>
            /// <returns><see langword="true"/> when another item is available.</returns>
            /// <exception cref="InvalidOperationException">The active session was modified.</exception>
            /// <exception cref="ObjectDisposedException">The session was published.</exception>
            public bool MoveNext() => _inner.MoveNext();

            /// <summary>Releases resources held by the enumerator.</summary>
            public readonly void Dispose()
            {
            }

            readonly void IEnumerator.Reset() => ((IEnumerator)_inner).Reset();
        }
    }
}
