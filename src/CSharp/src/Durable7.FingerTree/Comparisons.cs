namespace Durable7.FingerTree;

/// <summary>
/// A total order over <typeparamref name="T"/> supplied as a type argument, so the comparison is resolved
/// statically with no allocation or virtual dispatch. This is the comparison analogue of
/// <see cref="IMonoid{TMeasure}"/> — it lets the ready-made comparison measures and ordered-split operations
/// use a custom order without the caller hand-rolling a full <see cref="IMeasure{TElement, TMeasure}"/>.
/// </summary>
/// <typeparam name="T">The compared type.</typeparam>
/// <remarks>
/// Distinct from the BCL <see cref="System.Comparison{T}"/> delegate and <see cref="IComparer{T}"/>: this is
/// a static-abstract strategy type, never instantiated. Implementations must define a consistent total order
/// (the same contract as <see cref="IComparer{T}.Compare"/>).
/// </remarks>
/// <example>
/// Ordering by a projection, used to build a priority queue keyed on string length:
/// <code>
/// public readonly struct ByLength : IComparison&lt;string&gt;
/// {
///     public static int Compare(string left, string right) =&gt; left.Length.CompareTo(right.Length);
/// }
/// // FingerTree&lt;string, Optional&lt;string&gt;, MaxMeasure&lt;string, ByLength&gt;&gt; is a longest-first priority queue.
/// </code>
/// </example>
public interface IComparison<T>
{
    /// <summary>
    /// Compares two values, returning a negative number, zero, or a positive number when
    /// <paramref name="left"/> is respectively less than, equal to, or greater than <paramref name="right"/>.
    /// </summary>
    /// <param name="left">Left value.</param>
    /// <param name="right">Right value.</param>
    /// <returns>The sign of <c>left - right</c> under this order.</returns>
    static abstract int Compare(T left, T right);
}

/// <summary>
/// The natural order, delegating to <see cref="Comparer{T}.Default"/>. Using a measure with this comparison
/// is equivalent to using its non-parameterized form (for example, <c>MaxMeasure&lt;T, DefaultComparison&lt;T&gt;&gt;</c>
/// behaves as <c>MaxMeasure&lt;T&gt;</c>).
/// </summary>
/// <typeparam name="T">The compared type.</typeparam>
public readonly struct DefaultComparison<T> : IComparison<T>
{
    /// <inheritdoc/>
    public static int Compare(T left, T right) => Comparer<T>.Default.Compare(left, right);
}

/// <summary>
/// The reverse of another comparison, useful for descending orders (for example a min-priority queue from
/// max logic, or lower-bound search over a descending-sorted sequence).
/// </summary>
/// <typeparam name="T">The compared type.</typeparam>
/// <typeparam name="TComparison">The comparison to reverse.</typeparam>
public readonly struct ReverseComparison<T, TComparison> : IComparison<T>
    where TComparison : IComparison<T>
{
    /// <inheritdoc/>
    public static int Compare(T left, T right) => TComparison.Compare(right, left);
}
