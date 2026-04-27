using System.Collections;

namespace Tools.DataStructures.FingerTree.Tests;

internal sealed class SingleUseEnumerable<T>(IReadOnlyList<T> items) : IEnumerable<T>
{
    public int EnumerationCount { get; private set; }

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
