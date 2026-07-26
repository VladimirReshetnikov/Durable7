// Gap cursors over the measured rope.

using System.Diagnostics.CodeAnalysis;

namespace Durable7.FingerTree;

public sealed partial class MeasuredRope<T, TMeasure, TMeasureOps>
    where TMeasureOps : IMeasure<T, TMeasure>
{
    internal FingerTree<
        MeasuredChunk<T, TMeasure, TMeasureOps>,
        MeasurePair<int, TMeasure>,
        MeasuredChunkMeasure<T, TMeasure, TMeasureOps>> TreeForMeasuredCursor => _tree;

    /// <summary>
    /// Creates an immutable measured cursor whose gap is at <paramref name="position"/>. O(log n), plus at most
    /// one ordinary-chunk scan (currently 2,048 elements) to prepare cursor-local measure caches.
    /// </summary>
    /// <param name="position">
    /// The initial gap position in <c>0 .. Count</c>. The default is the gap before the first element.
    /// </param>
    /// <returns>
    /// A cursor over this rope. Its first clean <see cref="MeasuredRopeCursor{T, TMeasure, TMeasureOps}.Snapshot"/>
    /// is this instance.
    /// </returns>
    /// <exception cref="ArgumentOutOfRangeException">
    /// <paramref name="position"/> is outside <c>0 .. Count</c>.
    /// </exception>
    /// <remarks>
    /// Cursor preparation retains every measured element in its bounded active window, so subsequent local
    /// movement, edits, and snapshot normalization do not need to remeasure those elements.
    /// </remarks>
    public MeasuredRopeCursor<T, TMeasure, TMeasureOps> GetCursor(int position = 0) =>
        MeasuredRopeCursor<T, TMeasure, TMeasureOps>.Create(this, position);

    /// <summary>
    /// Attempts to create a cursor immediately before the first element whose inclusive accumulated measure
    /// satisfies <paramref name="predicate"/>. O(log n) plus a bounded within-chunk scan.
    /// </summary>
    /// <param name="predicate">
    /// A monotone predicate over absolute prefixes accumulated from <typeparamref name="TMeasureOps"/>'s
    /// identity.
    /// </param>
    /// <param name="cursor">
    /// On success, a cursor immediately before the boundary element. On failure, the end cursor.
    /// </param>
    /// <returns><see langword="true"/> when a boundary element exists; otherwise <see langword="false"/>.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="predicate"/> is <see langword="null"/>.</exception>
    /// <remarks>
    /// A predicate already true for the empty measure selects position zero when the rope is nonempty. On a miss,
    /// including an empty rope, <paramref name="cursor"/> has <c>Position == Count</c> and its
    /// <c>MeasureBefore</c> is the whole-rope measure. This one-shot source factory defers focus preparation and
    /// does not retain a full element-measure array for the located ordinary chunk; movement or editing prepares
    /// that bounded chunk only if it is subsequently needed.
    /// </remarks>
    public bool TryGetCursorByMeasure(
        Func<TMeasure, bool> predicate,
        out MeasuredRopeCursor<T, TMeasure, TMeasureOps> cursor)
    {
        ArgumentNullException.ThrowIfNull(predicate);
        return TryGetCursorByMeasure(new FuncMeasurePredicate<TMeasure>(predicate), out cursor);
    }

    /// <summary>
    /// Allocation-free-predicate counterpart to <see cref="TryGetCursorByMeasure(Func{TMeasure, bool}, out MeasuredRopeCursor{T, TMeasure, TMeasureOps})"/>.
    /// </summary>
    /// <typeparam name="TPredicate">A non-capturing value-type monotone predicate.</typeparam>
    /// <param name="predicate">The absolute-prefix boundary predicate.</param>
    /// <param name="cursor">The boundary cursor, or the end cursor on failure.</param>
    /// <returns><see langword="true"/> when a boundary element exists; otherwise <see langword="false"/>.</returns>
    public bool TryGetCursorByMeasure<TPredicate>(
        TPredicate predicate,
        out MeasuredRopeCursor<T, TMeasure, TMeasureOps> cursor)
        where TPredicate : struct, IMeasurePredicate<TMeasure> =>
        MeasuredRopeCursor<T, TMeasure, TMeasureOps>.TryCreateByMeasure(this, predicate, out cursor);

    internal static MeasuredRope<T, TMeasure, TMeasureOps> FromMeasuredCursorParts(
        FingerTree<
            MeasuredChunk<T, TMeasure, TMeasureOps>,
            MeasurePair<int, TMeasure>,
            MeasuredChunkMeasure<T, TMeasure, TMeasureOps>> left,
        ReadOnlySpan<MeasuredChunk<T, TMeasure, TMeasureOps>> middle,
        FingerTree<
            MeasuredChunk<T, TMeasure, TMeasureOps>,
            MeasurePair<int, TMeasure>,
            MeasuredChunkMeasure<T, TMeasure, TMeasureOps>> right)
    {
        var result = Wrap(left);
        if (!middle.IsEmpty)
            result = result.Concat(FromFrozenChunks(middle.ToArray()));
        return result.Concat(Wrap(right));
    }
}

/// <summary>
/// An immutable, persistent gap cursor over a <see cref="MeasuredRope{T, TMeasure, TMeasureOps}"/>. It mirrors
/// <see cref="RopeCursor{T}"/>'s editing surface while retaining ordered prefix and suffix measures.
/// </summary>
/// <typeparam name="T">The rope element type.</typeparam>
/// <typeparam name="TMeasure">The user measure type.</typeparam>
/// <typeparam name="TMeasureOps">The static measure algebra.</typeparam>
/// <remarks>
/// <para>
/// At position <c>p</c>, elements <c>[0, p)</c> are before the gap and elements <c>[p, Count)</c> are after it.
/// Movement and edits return new values; the receiver and every retained ancestor remain valid and branchable.
/// </para>
/// <para>
/// The cursor is a readonly value over immutable version and cursor state. A 16-element active window, a partial
/// carry smaller than 256 elements on either side, and lineage-shared per-element prefix/suffix measure caches keep
/// local copying and measure work bounded. Absolute measure seeks on an existing lineage prepare a selected
/// ordinary fragment once; later seeks and focus materialization reuse its element measures, and directional
/// aggregate tables are published lazily. On a linear editing lineage, local movement and single-element edits are O(1)
/// amortized and O(log n) worst-case. Arbitrary fan-out retains the conservative O(b log n) aggregate bound for
/// <c>b</c> branches at a deferred boundary.
/// </para>
/// <para>
/// <see cref="MeasureBefore"/> is the ordered measure of <c>[0, Position)</c>; <see cref="MeasureAfter"/> is the
/// ordered measure of <c>[Position, Count)</c>. Both are O(1) cached reads. No inverse or commutativity is assumed.
/// </para>
/// <para>
/// <see cref="Snapshot"/> returns the canonical rope for the logical edit version. A dirty first call performs
/// bounded packing plus an O(log n) join; all later calls through any navigation cursor over that version are O(1)
/// and return the same reference. Publication uses compare/exchange, returns the winning reference under races,
/// and publishes nothing when construction throws. Snapshot construction never invokes
/// <typeparamref name="TMeasureOps"/>'s element-measure callback for already prepared cursor state.
/// </para>
/// <para>
/// The default value is invalid, not an empty cursor. Every member on
/// <c>default(MeasuredRopeCursor&lt;...&gt;)</c> throws <see cref="InvalidOperationException"/>. Obtain an actual empty
/// cursor from <c>MeasuredRope&lt;...&gt;.Empty.GetCursor()</c>. Initialized values are immutable and safe for
/// concurrent reads.
/// </para>
/// </remarks>
public readonly struct MeasuredRopeCursor<T, TMeasure, TMeasureOps>
    where TMeasureOps : IMeasure<T, TMeasure>
{
    private const int FocusCapacity = 16;
    private const int FlushChunkSize = 256;

    private readonly MeasuredCursorVersionState<T, TMeasure, TMeasureOps>? _version;
    private readonly MeasuredRopeZipperContext<T, TMeasure, TMeasureOps> _context;

    internal MeasuredRopeCursor(
        MeasuredCursorVersionState<T, TMeasure, TMeasureOps> version,
        MeasuredRopeZipperContext<T, TMeasure, TMeasureOps> context)
    {
        _version = version;
        _context = context;
    }

    /// <summary>Gets the number of elements in this logical version. O(1).</summary>
    /// <exception cref="InvalidOperationException">This is the default, uninitialized cursor.</exception>
    public int Count => Version.Count;

    /// <summary>Gets the gap position in <c>0 .. Count</c>. O(1).</summary>
    /// <exception cref="InvalidOperationException">This is the default, uninitialized cursor.</exception>
    public int Position
    {
        get
        {
            _ = Version;
            return _context.Position;
        }
    }

    /// <summary>Gets whether the gap precedes the first element. O(1).</summary>
    /// <exception cref="InvalidOperationException">This is the default, uninitialized cursor.</exception>
    public bool IsAtStart => Position == 0;

    /// <summary>Gets whether the gap follows the last element. O(1).</summary>
    /// <exception cref="InvalidOperationException">This is the default, uninitialized cursor.</exception>
    public bool IsAtEnd => Position == Count;

    /// <summary>Gets the ordered measure of <c>[0, Position)</c>. O(1).</summary>
    /// <exception cref="InvalidOperationException">This is the default, uninitialized cursor.</exception>
    public TMeasure MeasureBefore
    {
        get
        {
            _ = Version;
            return _context.MeasureBefore;
        }
    }

    /// <summary>Gets the ordered measure of <c>[Position, Count)</c>. O(1).</summary>
    /// <exception cref="InvalidOperationException">This is the default, uninitialized cursor.</exception>
    public TMeasure MeasureAfter
    {
        get
        {
            _ = Version;
            return _context.MeasureAfter;
        }
    }

    internal object VersionIdentityForDiagnostics => Version;

    internal MeasuredRopeZipperContext<T, TMeasure, TMeasureOps> ContextForDiagnostics
    {
        get
        {
            _ = Version;
            return _context;
        }
    }

    internal static MeasuredRopeCursor<T, TMeasure, TMeasureOps> Create(
        MeasuredRope<T, TMeasure, TMeasureOps> source,
        int position)
    {
        var configuration = RopeCursorPrototypeConfiguration.Create(FocusCapacity, FlushChunkSize);
        var created = MeasuredRopeCursorEngine<T, TMeasure, TMeasureOps>.Create(source, position, configuration);
        return new MeasuredRopeCursor<T, TMeasure, TMeasureOps>(created.Version, created.Context);
    }

    internal static bool TryCreateByMeasure<TPredicate>(
        MeasuredRope<T, TMeasure, TMeasureOps> source,
        TPredicate predicate,
        out MeasuredRopeCursor<T, TMeasure, TMeasureOps> cursor)
        where TPredicate : struct, IMeasurePredicate<TMeasure>
    {
        var configuration = RopeCursorPrototypeConfiguration.Create(FocusCapacity, FlushChunkSize);
        var (found, created) = MeasuredRopeCursorEngine<T, TMeasure, TMeasureOps>.CreateByMeasure(
            source,
            predicate,
            configuration);
        cursor = new MeasuredRopeCursor<T, TMeasure, TMeasureOps>(created.Version, created.Context);
        return found;
    }

    /// <summary>Attempts to read the element immediately before the gap.</summary>
    /// <param name="value">The preceding element, or <see langword="default"/> when none exists.</param>
    /// <returns><see langword="true"/> when an element precedes the gap; otherwise <see langword="false"/>.</returns>
    /// <exception cref="InvalidOperationException">This is the default, uninitialized cursor.</exception>
    public bool TryPeekPrevious([MaybeNullWhen(false)] out T value)
    {
        _ = Version;
        return MeasuredRopeCursorEngine<T, TMeasure, TMeasureOps>.TryPeekPrevious(_context, out value);
    }

    /// <summary>Attempts to read the element immediately after the gap.</summary>
    /// <param name="value">The following element, or <see langword="default"/> when none exists.</param>
    /// <returns><see langword="true"/> when an element follows the gap; otherwise <see langword="false"/>.</returns>
    /// <exception cref="InvalidOperationException">This is the default, uninitialized cursor.</exception>
    public bool TryPeekNext([MaybeNullWhen(false)] out T value)
    {
        var version = Version;
        return MeasuredRopeCursorEngine<T, TMeasure, TMeasureOps>.TryPeekNext(
            _context,
            version.Count,
            out value);
    }

    /// <summary>Returns a cursor one element toward the start. O(1) amortized, O(log n) worst-case.</summary>
    /// <returns>A cursor at <c>Position - 1</c> over the same logical version.</returns>
    /// <exception cref="InvalidOperationException">The cursor is default or already at the start.</exception>
    public MeasuredRopeCursor<T, TMeasure, TMeasureOps> MovePrevious()
    {
        var version = Version;
        return new MeasuredRopeCursor<T, TMeasure, TMeasureOps>(
            version,
            MeasuredRopeCursorEngine<T, TMeasure, TMeasureOps>.MovePrevious(
                _context,
                version.Count));
    }

    /// <summary>Returns a cursor one element toward the end. O(1) amortized, O(log n) worst-case.</summary>
    /// <returns>A cursor at <c>Position + 1</c> over the same logical version.</returns>
    /// <exception cref="InvalidOperationException">The cursor is default or already at the end.</exception>
    public MeasuredRopeCursor<T, TMeasure, TMeasureOps> MoveNext()
    {
        var version = Version;
        return new MeasuredRopeCursor<T, TMeasure, TMeasureOps>(
            version,
            MeasuredRopeCursorEngine<T, TMeasure, TMeasureOps>.MoveNext(
                _context,
                version.Count));
    }

    /// <summary>Returns a cursor whose gap is at <paramref name="position"/>. O(log n).</summary>
    /// <param name="position">The destination in <c>0 .. Count</c>.</param>
    /// <returns>This unchanged value for the current position; otherwise a context over the same version.</returns>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="position"/> is outside the rope.</exception>
    /// <exception cref="InvalidOperationException">This is the default, uninitialized cursor.</exception>
    /// <remarks>A dirty nonlocal seek first materializes the version's canonical snapshot.</remarks>
    public MeasuredRopeCursor<T, TMeasure, TMeasureOps> Seek(int position)
    {
        var version = Version;
        return position == _context.Position
            ? this
            : new MeasuredRopeCursor<T, TMeasure, TMeasureOps>(
                version,
                MeasuredRopeCursorEngine<T, TMeasure, TMeasureOps>.Seek(version, _context, position));
    }

    /// <summary>
    /// Attempts an absolute measure seek to immediately before the first boundary element. O(log n) plus a
    /// bounded within-chunk scan.
    /// </summary>
    /// <param name="predicate">A monotone predicate over whole-version prefix measures.</param>
    /// <param name="cursor">The boundary cursor, or the end cursor on failure.</param>
    /// <returns><see langword="true"/> when a boundary element exists; otherwise <see langword="false"/>.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="predicate"/> is <see langword="null"/>.</exception>
    /// <exception cref="InvalidOperationException">This is the default, uninitialized cursor.</exception>
    public bool TrySeekByMeasure(
        Func<TMeasure, bool> predicate,
        out MeasuredRopeCursor<T, TMeasure, TMeasureOps> cursor)
    {
        _ = Version;
        ArgumentNullException.ThrowIfNull(predicate);
        return TrySeekByMeasure(new FuncMeasurePredicate<TMeasure>(predicate), out cursor);
    }

    /// <summary>
    /// Allocation-free-predicate counterpart to <see cref="TrySeekByMeasure(Func{TMeasure, bool}, out MeasuredRopeCursor{T, TMeasure, TMeasureOps})"/>.
    /// </summary>
    /// <typeparam name="TPredicate">A non-capturing value-type monotone predicate.</typeparam>
    /// <param name="predicate">The absolute-prefix boundary predicate.</param>
    /// <param name="cursor">The boundary cursor, or the end cursor on failure.</param>
    /// <returns><see langword="true"/> when a boundary element exists; otherwise <see langword="false"/>.</returns>
    /// <exception cref="InvalidOperationException">This is the default, uninitialized cursor.</exception>
    /// <remarks>
    /// A cache miss measures at most one ordinary chunk and installs that successful preparation in the immutable
    /// version lineage. Repeated seeks that select the same fragment reuse its element measures; the ordered suffix
    /// table is built and published only when needed. Failed preparation is never installed, and racing preparation
    /// may duplicate bounded work but cannot publish partial state.
    /// </remarks>
    public bool TrySeekByMeasure<TPredicate>(
        TPredicate predicate,
        out MeasuredRopeCursor<T, TMeasure, TMeasureOps> cursor)
        where TPredicate : struct, IMeasurePredicate<TMeasure>
    {
        var version = Version;
        var (found, context) = MeasuredRopeCursorEngine<T, TMeasure, TMeasureOps>.SeekByMeasure(
            version,
            _context,
            predicate);
        cursor = new MeasuredRopeCursor<T, TMeasure, TMeasureOps>(version, context);
        return found;
    }

    /// <summary>Inserts <paramref name="value"/> at the gap and returns a cursor after it.</summary>
    /// <param name="value">The element to insert.</param>
    /// <returns>A cursor over the edited version at <c>Position + 1</c>.</returns>
    /// <exception cref="OverflowException">The resulting count exceeds <see cref="int.MaxValue"/>.</exception>
    /// <exception cref="InvalidOperationException">This is the default, uninitialized cursor.</exception>
    /// <remarks>Overflow is checked before edit allocation or any user measure callback.</remarks>
    public MeasuredRopeCursor<T, TMeasure, TMeasureOps> Insert(T value) => FromEdit(
        MeasuredRopeCursorEngine<T, TMeasure, TMeasureOps>.Insert(Version, _context, value));

    /// <summary>Inserts <paramref name="values"/> at the gap in order and returns a cursor after them.</summary>
    /// <param name="values">The values to copy into the new version.</param>
    /// <returns>This unchanged value for an empty span; otherwise the edited cursor.</returns>
    /// <exception cref="OverflowException">The resulting count exceeds <see cref="int.MaxValue"/>.</exception>
    /// <exception cref="InvalidOperationException">This is the default, uninitialized cursor.</exception>
    /// <remarks>Overflow is checked before copying or measuring any value.</remarks>
    public MeasuredRopeCursor<T, TMeasure, TMeasureOps> InsertRange(ReadOnlySpan<T> values)
    {
        var version = Version;
        return values.IsEmpty
            ? this
            : FromEdit(MeasuredRopeCursorEngine<T, TMeasure, TMeasureOps>.InsertRange(
                version,
                _context,
                values));
    }

    /// <summary>Deletes the element immediately before the gap (backspace).</summary>
    /// <returns>A cursor over the edited version at <c>Position - 1</c>.</returns>
    /// <exception cref="InvalidOperationException">The cursor is default or has no preceding element.</exception>
    public MeasuredRopeCursor<T, TMeasure, TMeasureOps> DeletePrevious() => FromEdit(
        MeasuredRopeCursorEngine<T, TMeasure, TMeasureOps>.DeletePrevious(Version, _context));

    /// <summary>Deletes the element immediately after the gap, retaining the position.</summary>
    /// <returns>A cursor over the edited version at the same position.</returns>
    /// <exception cref="InvalidOperationException">The cursor is default or has no following element.</exception>
    public MeasuredRopeCursor<T, TMeasure, TMeasureOps> DeleteNext() => FromEdit(
        MeasuredRopeCursorEngine<T, TMeasure, TMeasureOps>.DeleteNext(Version, _context));

    /// <summary>Replaces the element immediately after the gap, retaining the position.</summary>
    /// <param name="value">The replacement element.</param>
    /// <returns>A cursor over a new edited version.</returns>
    /// <exception cref="InvalidOperationException">The cursor is default or has no following element.</exception>
    /// <remarks>
    /// Replacement always creates a new version, even for an equal or identical value. It invokes no element
    /// equality callback, but does invoke the configured measure callbacks for the replacement.
    /// </remarks>
    public MeasuredRopeCursor<T, TMeasure, TMeasureOps> ReplaceNext(T value) => FromEdit(
        MeasuredRopeCursorEngine<T, TMeasure, TMeasureOps>.ReplaceNext(Version, _context, value));

    /// <summary>
    /// Returns the canonical immutable rope snapshot. A dirty first call performs bounded packing plus an O(log n)
    /// join; repeated calls return the same reference in O(1).
    /// </summary>
    /// <returns>The canonical snapshot for this logical version.</returns>
    /// <exception cref="InvalidOperationException">This is the default, uninitialized cursor.</exception>
    /// <remarks>
    /// Publication is winner-returning and failure-atomic under races. Snapshot normalization combines prepared
    /// measures but never re-invokes the element-measure callback.
    /// </remarks>
    public MeasuredRope<T, TMeasure, TMeasureOps> Snapshot() => Version.Snapshot();

    internal MeasuredRopeCursorStateDiagnostics<TMeasure> GetDiagnostics() =>
        MeasuredRopeCursorEngine<T, TMeasure, TMeasureOps>.GetDiagnostics(Version, _context);

    internal bool HasSameContextStateForDiagnostics(
        MeasuredRopeCursor<T, TMeasure, TMeasureOps> other)
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
            _context.Configuration == other._context.Configuration &&
            ReferenceEquals(_context.DeferredSource, other._context.DeferredSource) &&
            Nullable.Equals(_context.DeferredChunk, other._context.DeferredChunk) &&
            _context.DeferredChunkStart == other._context.DeferredChunkStart &&
            _context.DeferredGap == other._context.DeferredGap;
    }

    internal void Validate() =>
        MeasuredRopeCursorEngine<T, TMeasure, TMeasureOps>.Validate(_context, Version.Count);

    private MeasuredCursorVersionState<T, TMeasure, TMeasureOps> Version =>
        _version ?? throw new InvalidOperationException(
            "The default MeasuredRopeCursor<T, TMeasure, TMeasureOps> value is not initialized. " +
            "Obtain a cursor from MeasuredRope<T, TMeasure, TMeasureOps>.GetCursor().");

    private static MeasuredRopeCursor<T, TMeasure, TMeasureOps> FromEdit(
        MeasuredRopeCursorEdit<T, TMeasure, TMeasureOps> edit) =>
        new(edit.Version, edit.Context);
}
