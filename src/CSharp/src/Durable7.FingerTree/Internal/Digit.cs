using System.Diagnostics;

namespace Durable7.FingerTree;

/// <summary>
/// A bounded buffer of one through three elements forming the prefix or suffix of a
/// <see cref="DeepTree{T, TChild}"/> level, following the simplified finger-tree paper.
/// </summary>
/// <typeparam name="T">Leaf element type stored in the deque.</typeparam>
/// <typeparam name="TChild">Element type at the current level.</typeparam>
/// <remarks>
/// <para>
/// A digit inside a deep node always holds one through three children. The zero-length value exists only as
/// a transient "possibly empty digit" in split construction paths and is never stored inside a tree; the
/// <see cref="TreeOperations"/> helpers convert it away before building a deep node.
/// </para>
/// <para>
/// Implemented as a readonly struct embedded directly in its owning deep node, so digit updates allocate
/// nothing beyond the rebuilt deep node itself.
/// </para>
/// </remarks>
internal readonly struct Digit<T, TChild> where TChild : ITreeElement<T, TChild>
{
    /// <summary>Gets the number of children, zero through three.</summary>
    public readonly int Length;

    /// <summary>First child; meaningful when <see cref="Length"/> is at least one.</summary>
    public readonly TChild A;

    /// <summary>Second child; meaningful when <see cref="Length"/> is at least two.</summary>
    public readonly TChild B;

    /// <summary>Third child; meaningful when <see cref="Length"/> is three.</summary>
    public readonly TChild C;

    /// <summary>Initializes a one-child digit.</summary>
    /// <param name="a">The only child.</param>
    public Digit(TChild a)
    {
        Length = 1;
        A = a;
        B = default!;
        C = default!;
    }

    /// <summary>Initializes a two-child digit.</summary>
    /// <param name="a">First child.</param>
    /// <param name="b">Second child.</param>
    public Digit(TChild a, TChild b)
    {
        Length = 2;
        A = a;
        B = b;
        C = default!;
    }

    /// <summary>Initializes a three-child digit.</summary>
    /// <param name="a">First child.</param>
    /// <param name="b">Second child.</param>
    /// <param name="c">Third child.</param>
    public Digit(TChild a, TChild b, TChild c)
    {
        Length = 3;
        A = a;
        B = b;
        C = c;
    }

    /// <summary>Gets the empty transient digit.</summary>
    public static Digit<T, TChild> Empty => default;

    /// <summary>Gets the total leaf count of all children. O(1).</summary>
    public int Size => Length switch
    {
        0 => 0,
        1 => A.Size,
        2 => A.Size + B.Size,
        _ => A.Size + B.Size + C.Size,
    };

    /// <summary>Gets the last child. Requires a non-empty digit.</summary>
    public TChild Last => Length switch
    {
        1 => A,
        2 => B,
        _ => C,
    };

    /// <summary>Gets the child at <paramref name="index"/> in <c>0..Length-1</c>.</summary>
    /// <param name="index">Zero-based child position.</param>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="index"/> does not address a stored child.</exception>
    public TChild ChildAt(int index)
    {
        if ((uint)index >= (uint)Length)
            throw new ArgumentOutOfRangeException(nameof(index), index, "The index must address a stored digit child.");

        return index switch
        {
            0 => A,
            1 => B,
            _ => C,
        };
    }

    /// <summary>Returns a digit with <paramref name="child"/> prepended. Requires <see cref="Length"/> at most two.</summary>
    /// <param name="child">Child to prepend.</param>
    public Digit<T, TChild> Cons(TChild child)
    {
        Debug.Assert(Length is 1 or 2, "Cons requires a digit with room for one more child.");
        return Length == 1 ? new(child, A) : new(child, A, B);
    }

    /// <summary>Returns a digit with <paramref name="child"/> appended. Requires <see cref="Length"/> at most two.</summary>
    /// <param name="child">Child to append.</param>
    public Digit<T, TChild> Snoc(TChild child)
    {
        Debug.Assert(Length is 1 or 2, "Snoc requires a digit with room for one more child.");
        return Length == 1 ? new(A, child) : new(A, B, child);
    }

    /// <summary>Returns a digit without the first child. Requires a non-empty digit.</summary>
    public Digit<T, TChild> RemoveFirst() => Length switch
    {
        1 => default,
        2 => new(B),
        _ => new(B, C),
    };

    /// <summary>Returns a digit without the last child. Requires a non-empty digit.</summary>
    public Digit<T, TChild> RemoveLast() => Length switch
    {
        1 => default,
        2 => new(A),
        _ => new(A, B),
    };

    /// <summary>Returns a digit with the first child replaced. Requires a non-empty digit.</summary>
    /// <param name="child">Replacement child; its leaf count may differ from the replaced child.</param>
    public Digit<T, TChild> ReplaceFirst(TChild child) => Length switch
    {
        1 => new(child),
        2 => new(child, B),
        _ => new(child, B, C),
    };

    /// <summary>Returns a digit with the last child replaced. Requires a non-empty digit.</summary>
    /// <param name="child">Replacement child; its leaf count may differ from the replaced child.</param>
    public Digit<T, TChild> ReplaceLast(TChild child) => Length switch
    {
        1 => new(child),
        2 => new(A, child),
        _ => new(A, B, child),
    };

    /// <summary>Returns a digit with the child at <paramref name="index"/> replaced.</summary>
    /// <param name="index">Zero-based child position.</param>
    /// <param name="child">Replacement child.</param>
    public Digit<T, TChild> WithChildAt(int index, TChild child) => index switch
    {
        0 => ReplaceFirst(child),
        1 => Length == 2 ? new(A, child) : new(A, child, C),
        _ => new(A, B, child),
    };

    /// <summary>Returns the possibly empty digit of children strictly before position <paramref name="index"/>.</summary>
    /// <param name="index">Zero-based child position.</param>
    public Digit<T, TChild> TakeBefore(int index) => index switch
    {
        0 => default,
        1 => new(A),
        _ => new(A, B),
    };

    /// <summary>Returns the possibly empty digit of children strictly after position <paramref name="index"/>.</summary>
    /// <param name="index">Zero-based child position.</param>
    public Digit<T, TChild> TakeAfter(int index) => (Length - index - 1) switch
    {
        0 => default,
        1 => new(Last),
        _ => new(B, C),
    };

    /// <summary>
    /// Locates the child containing the digit-relative leaf <paramref name="leafIndex"/> and the
    /// surrounding partial digits.
    /// </summary>
    /// <param name="leafIndex">Digit-relative leaf index, less than <see cref="Size"/>.</param>
    /// <param name="before">Children strictly before the hit child; zero through two children.</param>
    /// <param name="hit">The child containing the indexed leaf.</param>
    /// <param name="indexInHit">Leaf index relative to <paramref name="hit"/>.</param>
    /// <param name="after">Children strictly after the hit child; zero through two children.</param>
    public void Split(
        int leafIndex,
        out Digit<T, TChild> before,
        out TChild hit,
        out int indexInHit,
        out Digit<T, TChild> after)
    {
        Debug.Assert(leafIndex >= 0 && leafIndex < Size, "Digit split index must address a stored leaf.");
        var index = leafIndex;
        for (var position = 0; ; position++)
        {
            var child = ChildAt(position);
            if (index < child.Size)
            {
                before = TakeBefore(position);
                hit = child;
                indexInHit = index;
                after = TakeAfter(position);
                return;
            }

            index -= child.Size;
        }
    }
}
