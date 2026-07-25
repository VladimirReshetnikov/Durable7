using Xunit;

namespace Durable7.Ordered.Tests;

/// <summary>
/// Locks the repository-wide rule that every member of an invalid default cursor value throws the
/// same documented exception. <c>Position</c> and the members derived from it are covered explicitly
/// because an auto-property backing field would otherwise report gap zero for an uninitialized
/// struct while every other member throws.
/// </summary>
public sealed class CursorDefaultValueContractTests
{
    /// <summary>Verifies the neutral ordered checkpoint cursors reject their default value.</summary>
    [Fact]
    public void DefaultOrderedCursors_RejectPositionAndDerivedMembers()
    {
        Assert.Throws<InvalidOperationException>(() => _ = default(PersistentOrderedSetCursor<int>).Position);
        Assert.Throws<InvalidOperationException>(() => _ = default(PersistentOrderedSetCursor<int>).IsAtStart);
        Assert.Throws<InvalidOperationException>(() => _ = default(PersistentOrderedSetCursor<int>).Seek(0));

        Assert.Throws<InvalidOperationException>(() => _ = default(PersistentOrderedMapCursor<int, string>).Position);
        Assert.Throws<InvalidOperationException>(() => _ = default(PersistentOrderedMapCursor<int, string>).IsAtStart);
        Assert.Throws<InvalidOperationException>(() => _ = default(PersistentOrderedMapCursor<int, string>).Seek(0));

        Assert.Throws<InvalidOperationException>(() => _ = default(PersistentOrderedMultimapCursor<int, string>).Position);
        Assert.Throws<InvalidOperationException>(() => _ = default(PersistentOrderedMultimapCursor<int, string>).IsAtStart);
        Assert.Throws<InvalidOperationException>(() => _ = default(PersistentOrderedMultimapCursor<int, string>).Seek(0));
    }
}
