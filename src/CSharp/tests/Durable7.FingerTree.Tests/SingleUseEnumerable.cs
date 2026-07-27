// Shared support for the single use enumerable tests.

using System.Collections;

namespace Durable7.FingerTree.Tests;

/// <summary>
/// A sequence that can be enumerated only once, so a test can prove an operation does not enumerate
/// its input twice.
/// </summary>
internal sealed class SingleUseEnumerable<T>(IReadOnlyList<T> items) : IEnumerable<T>
{
    /// <summary>Gets the enumeration count.</summary>
    public int EnumerationCount { get; private set; }

    /// <summary>Returns an enumerator over the elements, in the collection's own order.</summary>
    public IEnumerator<T> GetEnumerator()
    {
        EnumerationCount++;
        if (EnumerationCount > 1)
            throw new InvalidOperationException("The source was enumerated more than once.");

        foreach (var item in items)
            yield return item;
    }

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();
}
