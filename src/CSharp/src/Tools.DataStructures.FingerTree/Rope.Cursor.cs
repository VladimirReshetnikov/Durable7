using System.Diagnostics.CodeAnalysis;

namespace Tools.DataStructures.FingerTree;

public sealed partial class Rope<T>
{
    /// <summary>
    /// Creates an immutable cursor whose gap is at <paramref name="position"/>. O(log n).
    /// </summary>
    /// <param name="position">
    /// The initial gap position in <c>0 .. Count</c>. The default is the gap before the first element.
    /// </param>
    /// <returns>A cursor over this rope. Its first clean <see cref="RopeCursor{T}.Snapshot()"/> is this instance.</returns>
    /// <exception cref="ArgumentOutOfRangeException">
    /// <paramref name="position"/> is outside <c>0 .. Count</c>.
    /// </exception>
    /// <remarks>
    /// A cursor owns its source version; it cannot be applied to another rope. Cursor creation extracts a
    /// bounded active window while retaining the source's immutable chunk trees.
    /// </remarks>
    public RopeCursor<T> GetCursor(int position = 0) => RopeCursor<T>.Create(this, position);
}

/// <summary>
/// An immutable, persistent edit cursor over a <see cref="Rope{T}"/>. The cursor denotes a gap between
/// elements rather than an element.
/// </summary>
/// <typeparam name="T">The rope element type.</typeparam>
/// <remarks>
/// <para>
/// At position <c>p</c>, elements <c>[0, p)</c> are before the gap and elements <c>[p, Count)</c> are after
/// it. Movement and edits return new cursor values. The receiver and all previously returned cursors remain
/// valid, so any retained cursor may be used to create an independent branch.
/// </para>
/// <para>
/// The cursor is a small value over immutable version and cursor state. Navigation shares the logical version;
/// an edit creates a new version. A bounded 16-element active window and at most one partial carry smaller than
/// 256 elements on each side keep local copying bounded. Along a linear editing lineage, local movement and
/// single-element edits are O(1) amortized and O(log n) worst-case. The stronger amortized claim does not extend
/// across arbitrary fan-out: editing <c>b</c> descendants retained at a boundary has the conservative
/// O(b log n) bound.
/// </para>
/// <para>
/// <see cref="Snapshot"/> returns the canonical rope for this logical version. A dirty first snapshot performs
/// bounded carry packing plus an O(log n) tree join; later snapshots of this cursor or any navigation cursor over
/// the same version are O(1) and return the same <see cref="Rope{T}"/> reference. First-snapshot races publish one
/// winner with an atomic compare/exchange; racing callers return that winner. If construction throws, no candidate
/// is published and the cursor remains reusable.
/// </para>
/// <para>
/// The default value is not an empty cursor. Every member invoked on <c>default(RopeCursor&lt;T&gt;)</c> throws
/// <see cref="InvalidOperationException"/>. Obtain an empty cursor from <c>Rope&lt;T&gt;.Empty.GetCursor()</c>.
/// Initialized cursor values are immutable and safe for concurrent reads; user code may independently retain and
/// operate on copies without synchronization.
/// </para>
/// </remarks>
public readonly struct RopeCursor<T>
{
    private const int FocusCapacity = 16;
    private const int FlushChunkSize = 256;

    private readonly CursorVersionState<T>? _version;
    private readonly RopeZipperContext<T> _context;

    internal RopeCursor(CursorVersionState<T> version, RopeZipperContext<T> context)
    {
        _version = version;
        _context = context;
    }

    /// <summary>Gets the number of elements in this cursor's logical version. O(1).</summary>
    /// <exception cref="InvalidOperationException">This is the default, uninitialized cursor value.</exception>
    public int Count => Version.Count;

    /// <summary>Gets the gap position in <c>0 .. Count</c>. O(1).</summary>
    /// <exception cref="InvalidOperationException">This is the default, uninitialized cursor value.</exception>
    public int Position
    {
        get
        {
            _ = Version;
            return _context.Position;
        }
    }

    /// <summary>Gets whether the gap is before the first element. O(1).</summary>
    /// <exception cref="InvalidOperationException">This is the default, uninitialized cursor value.</exception>
    public bool IsAtStart => Position == 0;

    /// <summary>Gets whether the gap is after the last element. O(1).</summary>
    /// <exception cref="InvalidOperationException">This is the default, uninitialized cursor value.</exception>
    public bool IsAtEnd => Position == Count;

    internal object VersionIdentityForDiagnostics => Version;

    internal RopeZipperContext<T> ContextForDiagnostics
    {
        get
        {
            _ = Version;
            return _context;
        }
    }

    internal static RopeCursor<T> Create(Rope<T> source, int position)
    {
        var configuration = RopeCursorPrototypeConfiguration.Create(FocusCapacity, FlushChunkSize);
        var created = RopeCursorPrototypeEngine<T>.Create(source, position, configuration);
        return new RopeCursor<T>(created.Version, created.Context);
    }

    /// <summary>
    /// Attempts to read the element immediately before the gap. O(1) amortized, O(log n) worst-case when a
    /// suspended boundary spine must be repaired.
    /// </summary>
    /// <param name="value">
    /// The preceding element when one exists; otherwise the default value of <typeparamref name="T"/>.
    /// </param>
    /// <returns><see langword="true"/> when the gap has a preceding element; otherwise <see langword="false"/>.</returns>
    /// <exception cref="InvalidOperationException">This is the default, uninitialized cursor value.</exception>
    public bool TryPeekPrevious([MaybeNullWhen(false)] out T value)
    {
        _ = Version;
        return RopeCursorPrototypeEngine<T>.TryPeekPrevious(_context, out value);
    }

    /// <summary>
    /// Attempts to read the element immediately after the gap. O(1) amortized, O(log n) worst-case when a
    /// suspended boundary spine must be repaired.
    /// </summary>
    /// <param name="value">
    /// The following element when one exists; otherwise the default value of <typeparamref name="T"/>.
    /// </param>
    /// <returns><see langword="true"/> when the gap has a following element; otherwise <see langword="false"/>.</returns>
    /// <exception cref="InvalidOperationException">This is the default, uninitialized cursor value.</exception>
    public bool TryPeekNext([MaybeNullWhen(false)] out T value)
    {
        var version = Version;
        return RopeCursorPrototypeEngine<T>.TryPeekNext(_context, version.Count, out value);
    }

    /// <summary>
    /// Returns a cursor with its gap moved one element toward the start. O(1) amortized along a linear lineage;
    /// O(log n) worst-case.
    /// </summary>
    /// <returns>A cursor at <c>Position - 1</c> over the same logical version.</returns>
    /// <exception cref="InvalidOperationException">
    /// This is the default cursor value, or the cursor is already at the start.
    /// </exception>
    public RopeCursor<T> MovePrevious()
    {
        var version = Version;
        return new RopeCursor<T>(
            version,
            RopeCursorPrototypeEngine<T>.MovePrevious(_context, version.Count));
    }

    /// <summary>
    /// Returns a cursor with its gap moved one element toward the end. O(1) amortized along a linear lineage;
    /// O(log n) worst-case.
    /// </summary>
    /// <returns>A cursor at <c>Position + 1</c> over the same logical version.</returns>
    /// <exception cref="InvalidOperationException">
    /// This is the default cursor value, or the cursor is already at the end.
    /// </exception>
    public RopeCursor<T> MoveNext()
    {
        var version = Version;
        return new RopeCursor<T>(
            version,
            RopeCursorPrototypeEngine<T>.MoveNext(_context, version.Count));
    }

    /// <summary>Returns a cursor whose gap is at <paramref name="position"/>. O(log n).</summary>
    /// <param name="position">The destination gap position in <c>0 .. Count</c>.</param>
    /// <returns>
    /// This unchanged value when <paramref name="position"/> equals <see cref="Position"/>; otherwise a new
    /// navigation context over the same logical version and snapshot cache.
    /// </returns>
    /// <exception cref="ArgumentOutOfRangeException">
    /// <paramref name="position"/> is outside <c>0 .. Count</c>.
    /// </exception>
    /// <exception cref="InvalidOperationException">This is the default, uninitialized cursor value.</exception>
    /// <remarks>
    /// Seeking a dirty version may first materialize its canonical snapshot; that snapshot is then shared by all
    /// navigation contexts over the version.
    /// </remarks>
    public RopeCursor<T> Seek(int position)
    {
        var version = Version;
        return position == _context.Position
            ? this
            : new RopeCursor<T>(
                version,
                RopeCursorPrototypeEngine<T>.Seek(version, _context, position));
    }

    /// <summary>
    /// Inserts <paramref name="value"/> at the gap and returns a cursor immediately after it. O(1) amortized
    /// along a linear lineage; O(log n) worst-case.
    /// </summary>
    /// <param name="value">The element to insert.</param>
    /// <returns>A cursor over the edited version at <c>Position + 1</c>.</returns>
    /// <exception cref="OverflowException">The resulting count would exceed <see cref="int.MaxValue"/>.</exception>
    /// <exception cref="InvalidOperationException">This is the default, uninitialized cursor value.</exception>
    /// <remarks>Count overflow is checked before allocating edit storage.</remarks>
    public RopeCursor<T> Insert(T value) => FromEdit(
        RopeCursorPrototypeEngine<T>.Insert(Version, _context, value));

    /// <summary>
    /// Inserts <paramref name="values"/> at the gap in enumeration order and returns a cursor immediately after
    /// them. O(m + log n) amortized for m inserted elements.
    /// </summary>
    /// <param name="values">The elements to copy into the edited version.</param>
    /// <returns>
    /// This unchanged value when <paramref name="values"/> is empty; otherwise a cursor over the edited version
    /// at <c>Position + values.Length</c>.
    /// </returns>
    /// <exception cref="OverflowException">The resulting count would exceed <see cref="int.MaxValue"/>.</exception>
    /// <exception cref="InvalidOperationException">This is the default, uninitialized cursor value.</exception>
    /// <remarks>Count overflow is checked before allocating or copying any edit storage.</remarks>
    public RopeCursor<T> InsertRange(ReadOnlySpan<T> values)
    {
        var version = Version;
        return values.IsEmpty
            ? this
            : FromEdit(RopeCursorPrototypeEngine<T>.InsertRange(version, _context, values));
    }

    /// <summary>
    /// Removes the element immediately before the gap (backspace) and returns a cursor at
    /// <c>Position - 1</c>. O(1) amortized along a linear lineage; O(log n) worst-case.
    /// </summary>
    /// <returns>A cursor over the edited version immediately after the element preceding the removed one.</returns>
    /// <exception cref="InvalidOperationException">
    /// This is the default cursor value, or no element precedes the gap.
    /// </exception>
    public RopeCursor<T> DeletePrevious() => FromEdit(
        RopeCursorPrototypeEngine<T>.DeletePrevious(Version, _context));

    /// <summary>
    /// Removes the element immediately after the gap and keeps the same position. O(1) amortized along a linear
    /// lineage; O(log n) worst-case.
    /// </summary>
    /// <returns>A cursor over the edited version at the same position.</returns>
    /// <exception cref="InvalidOperationException">
    /// This is the default cursor value, or no element follows the gap.
    /// </exception>
    public RopeCursor<T> DeleteNext() => FromEdit(
        RopeCursorPrototypeEngine<T>.DeleteNext(Version, _context));

    /// <summary>
    /// Replaces the element immediately after the gap and keeps the same position. O(1) amortized along a linear
    /// lineage; O(log n) worst-case.
    /// </summary>
    /// <param name="value">The replacement element.</param>
    /// <returns>A cursor over a new edited version at the same position.</returns>
    /// <exception cref="InvalidOperationException">
    /// This is the default cursor value, or no element follows the gap.
    /// </exception>
    /// <remarks>
    /// Replacement always creates a new logical version, even when <paramref name="value"/> is equal or identical
    /// to the stored element. The cursor invokes no element-equality callback.
    /// </remarks>
    public RopeCursor<T> ReplaceNext(T value) => FromEdit(
        RopeCursorPrototypeEngine<T>.ReplaceNext(Version, _context, value));

    /// <summary>
    /// Returns the canonical rope for this cursor's logical version. A dirty first call performs bounded carry
    /// packing plus an O(log n) join; repeated clean calls are O(1) and return the same reference.
    /// </summary>
    /// <returns>The canonical immutable rope snapshot.</returns>
    /// <exception cref="InvalidOperationException">This is the default, uninitialized cursor value.</exception>
    /// <remarks>
    /// Publication is thread-safe, winner-returning, and failure-atomic. Concurrent first callers all return the
    /// reference selected by the version's compare/exchange memo cell. A failed construction publishes nothing.
    /// </remarks>
    public Rope<T> Snapshot() => Version.Snapshot();

    internal RopeCursorPrototypeStateDiagnostics GetDiagnostics() =>
        RopeCursorPrototypeEngine<T>.GetDiagnostics(Version, _context);

    internal bool HasSameContextStateForDiagnostics(RopeCursor<T> other)
    {
        _ = Version;
        _ = other.Version;
        return ReferenceEquals(_context.LeftTree, other._context.LeftTree) &&
            ReferenceEquals(_context.LeftCarry, other._context.LeftCarry) &&
            ReferenceEquals(_context.Active, other._context.Active) &&
            _context.Gap == other._context.Gap &&
            ReferenceEquals(_context.RightCarry, other._context.RightCarry) &&
            ReferenceEquals(_context.RightTree, other._context.RightTree) &&
            _context.Position == other._context.Position &&
            _context.Configuration == other._context.Configuration;
    }

    internal void Validate() => RopeCursorPrototypeEngine<T>.Validate(_context, Version.Count);

    private CursorVersionState<T> Version =>
        _version ?? throw new InvalidOperationException(
            "The default RopeCursor<T> value is not initialized. Obtain a cursor from Rope<T>.GetCursor().");

    private static RopeCursor<T> FromEdit(RopeCursorPrototypeEdit<T> edit) =>
        new(edit.Version, edit.Context);
}
