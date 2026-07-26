// The transient session over the persistent hash map.

using System.Collections;

namespace Durable7.Hamt;

public sealed partial class PersistentHashMap<TKey, TValue>
{
    /// <summary>Creates an empty single-owner mutable editing session.</summary>
    /// <param name="comparer">
    /// The comparer that defines key hashing and equality, or <see langword="null"/> to use
    /// <see cref="EqualityComparer{T}.Default"/>.
    /// </param>
    /// <returns>An active transient whose eventual publication preserves the selected comparer.</returns>
    /// <remarks>
    /// Creation is O(1) and allocates the session object without walking a trie. The returned session
    /// is unsynchronized, supports one logical owner, and is consumed by its first successful
    /// <see cref="Transient.Persist"/> call.
    /// </remarks>
    public static Transient CreateTransient(IEqualityComparer<TKey>? comparer = null) =>
        new(EmptyFor(comparer ?? EqualityComparer<TKey>.Default), enableDiagnostics: false, deferOwnershipUntilReuse: true);

    /// <summary>Adopts this map into a single-owner mutable editing session.</summary>
    /// <returns>An active transient initially representing this map.</returns>
    /// <remarks>
    /// <para>
    /// Adoption is O(1), allocates one session object, and does not walk or copy the trie. A clean
    /// session, including one subjected only to logical no-op edits, publishes this exact map object.
    /// </para>
    /// <para>
    /// The returned session is unsynchronized. Concurrent access to it is unsupported; sequential
    /// transfer between threads requires caller-provided synchronization. This immutable source map
    /// remains safe for concurrent readers throughout the session.
    /// </para>
    /// </remarks>
    public Transient ToTransient() =>
        new(this, enableDiagnostics: false, deferOwnershipUntilReuse: true);

    public sealed partial class Transient
    {
        /// <summary>Gets the value associated with a key in the active session.</summary>
        /// <param name="key">The key to locate.</param>
        /// <returns>The stored value.</returns>
        /// <exception cref="KeyNotFoundException">The key is absent.</exception>
        /// <exception cref="ObjectDisposedException">The session has already been published.</exception>
        public TValue this[TKey key]
        {
            get
            {
                if (TryGetValue(key, out var value))
                    return value;

                throw new KeyNotFoundException($"The key '{key}' was not present in the transient.");
            }
        }

        /// <summary>Gets a version-bound enumerable view of the active session's keys.</summary>
        /// <exception cref="ObjectDisposedException">The session has already been published.</exception>
        /// <remarks>
        /// The view follows trie bitmap and collision-bucket order. It throws
        /// <see cref="InvalidOperationException"/> if a successful content change occurs after the
        /// view is acquired, and <see cref="ObjectDisposedException"/> if the session is published.
        /// </remarks>
        public IEnumerable<TKey> Keys
        {
            get
            {
                EnsureActive();
                return new KeyView(this, _version);
            }
        }

        /// <summary>Gets a version-bound enumerable view of the active session's values.</summary>
        /// <exception cref="ObjectDisposedException">The session has already been published.</exception>
        /// <remarks>
        /// The view follows trie bitmap and collision-bucket order. It throws
        /// <see cref="InvalidOperationException"/> if a successful content change occurs after the
        /// view is acquired, and <see cref="ObjectDisposedException"/> if the session is published.
        /// </remarks>
        public IEnumerable<TValue> Values
        {
            get
            {
                EnsureActive();
                return new ValueView(this, _version);
            }
        }

        /// <summary>Returns an allocation-free, version-bound enumerator for the active session.</summary>
        /// <returns>An enumerator over the key/value pairs in trie enumeration order.</returns>
        /// <exception cref="ObjectDisposedException">The session has already been published.</exception>
        /// <remarks>
        /// A successful content change invalidates the enumerator; a logical no-op does not. A copied
        /// enumerator advances independently. Enumeration order is stable for an unchanged session
        /// but is neither insertion nor sorted order and is not otherwise semantically specified.
        /// </remarks>
        public Enumerator GetEnumerator()
        {
            EnsureActive();
            return new Enumerator(this, _version, new PersistentHashMap<TKey, TValue>.Enumerator(_root));
        }

        IEnumerator<KeyValuePair<TKey, TValue>> IEnumerable<KeyValuePair<TKey, TValue>>.GetEnumerator() =>
            GetEnumerator();

        IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

        private Enumerator GetEnumerator(long expectedVersion)
        {
            ValidateEnumerationVersion(expectedVersion);
            return new Enumerator(this, expectedVersion, new PersistentHashMap<TKey, TValue>.Enumerator(_root));
        }

        /// <summary>Enumerates the key/value pairs in an active transient session.</summary>
        /// <remarks>
        /// Traversal state is inline, so concrete enumeration allocates nothing and copied enumerators
        /// advance independently. A successful content change invalidates the enumerator; publication
        /// invalidates it with <see cref="ObjectDisposedException"/>.
        /// </remarks>
        public struct Enumerator : IEnumerator<KeyValuePair<TKey, TValue>>
        {
            private readonly Transient? _owner;
            private readonly long _version;
            private PersistentHashMap<TKey, TValue>.Enumerator _inner;

            internal Enumerator(
                Transient owner,
                long version,
                PersistentHashMap<TKey, TValue>.Enumerator inner)
            {
                _owner = owner;
                _version = version;
                _inner = inner;
            }

            /// <summary>Gets the key/value pair at the current enumerator position.</summary>
            /// <exception cref="InvalidOperationException">The active session was modified.</exception>
            /// <exception cref="ObjectDisposedException">The session was published.</exception>
            public readonly KeyValuePair<TKey, TValue> Current
            {
                get
                {
                    Validate();
                    return _inner.Current;
                }
            }

            readonly object IEnumerator.Current => Current;

            /// <summary>Advances the enumerator to the next key/value pair.</summary>
            /// <returns><see langword="true"/> when another pair is available.</returns>
            /// <exception cref="InvalidOperationException">The active session was modified.</exception>
            /// <exception cref="ObjectDisposedException">The session was published.</exception>
            public bool MoveNext()
            {
                Validate();
                return _inner.MoveNext();
            }

            /// <summary>Releases resources held by the enumerator.</summary>
            public readonly void Dispose()
            {
            }

            readonly void IEnumerator.Reset()
            {
                Validate();
                throw new NotSupportedException(
                    "Resetting this enumerator is not supported; create a new enumerator instead.");
            }

            internal readonly void Validate() =>
                _owner?.ValidateEnumerationVersion(_version);
        }

        private sealed class KeyView(Transient owner, long version) : IEnumerable<TKey>
        {
            public IEnumerator<TKey> GetEnumerator() =>
                new KeyEnumerator(owner.GetEnumerator(version));

            IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
        }

        private sealed class ValueView(Transient owner, long version) : IEnumerable<TValue>
        {
            public IEnumerator<TValue> GetEnumerator() =>
                new ValueEnumerator(owner.GetEnumerator(version));

            IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
        }

        private sealed class KeyEnumerator(Enumerator inner) : IEnumerator<TKey>
        {
            private Enumerator _inner = inner;

            public TKey Current => _inner.Current.Key;

            object? IEnumerator.Current => Current;

            public bool MoveNext() => _inner.MoveNext();

            public void Dispose()
            {
            }

            void IEnumerator.Reset()
            {
                _inner.Validate();
                throw new NotSupportedException(
                    "Resetting this enumerator is not supported; create a new enumerator instead.");
            }
        }

        private sealed class ValueEnumerator(Enumerator inner) : IEnumerator<TValue>
        {
            private Enumerator _inner = inner;

            public TValue Current => _inner.Current.Value;

            object? IEnumerator.Current => Current;

            public bool MoveNext() => _inner.MoveNext();

            public void Dispose()
            {
            }

            void IEnumerator.Reset()
            {
                _inner.Validate();
                throw new NotSupportedException(
                    "Resetting this enumerator is not supported; create a new enumerator instead.");
            }
        }
    }
}
