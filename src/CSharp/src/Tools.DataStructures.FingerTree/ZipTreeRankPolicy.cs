using System.Security.Cryptography;
using System.Numerics;

namespace Tools.DataStructures.FingerTree;

/// <summary>Defines ordering and keyed deterministic ranks for a canonical zip-tree set.</summary>
/// <typeparam name="T">The item type.</typeparam>
/// <remarks>
/// The rank hash must be constant for values that compare equal. A policy object is part of a set's
/// semantic identity and is retained by every derived version.
/// </remarks>
public sealed class ZipTreeRankPolicy<T>
{
    private readonly Func<T, ulong> _rankHash;

    private ZipTreeRankPolicy(IComparer<T> comparer, Func<T, ulong> rankHash, ulong seed)
    {
        Comparer = comparer;
        _rankHash = rankHash;
        Seed = seed;
    }

    /// <summary>Gets the process-local default policy.</summary>
    /// <remarks>
    /// Its seed is generated once from a cryptographic random source. Its fallback hash uses
    /// <see cref="EqualityComparer{T}.Default"/> and is therefore not a cross-process encoding.
    /// </remarks>
    public static ZipTreeRankPolicy<T> Default { get; } =
        new(Comparer<T>.Default, DefaultHash, RandomSeed());

    /// <summary>Gets the comparer that defines item order and set equivalence.</summary>
    public IComparer<T> Comparer { get; }

    /// <summary>Gets the secret or explicitly pinned seed mixed into ranks.</summary>
    public ulong Seed { get; }

    /// <summary>Creates a rank policy.</summary>
    /// <param name="comparer">The ordering/equivalence policy, or <see langword="null"/> for the default.</param>
    /// <param name="rankHash">
    /// A hash constant on comparer-equivalence classes, or <see langword="null"/> for the default equality hash.
    /// </param>
    /// <param name="seed">A keyed seed, or <see langword="null"/> to generate one cryptographically.</param>
    /// <returns>A policy suitable for constructing one interoperable family of set versions.</returns>
    public static ZipTreeRankPolicy<T> Create(
        IComparer<T>? comparer = null,
        Func<T, ulong>? rankHash = null,
        ulong? seed = null) =>
        new(comparer ?? Comparer<T>.Default, rankHash ?? DefaultHash, seed ?? RandomSeed());

    internal Rank GetRank(T item)
    {
        var source = _rankHash(item);
        var primaryHash = Mix(source ^ Seed);
        var secondary = Mix(source + Seed + 0x9e3779b97f4a7c15UL);
        return new Rank(BitOperations.LeadingZeroCount(primaryHash), secondary, primaryHash);
    }

    private static ulong DefaultHash(T item) => unchecked((uint)EqualityComparer<T>.Default.GetHashCode(item!));

    private static ulong RandomSeed()
    {
        Span<byte> bytes = stackalloc byte[sizeof(ulong)];
        RandomNumberGenerator.Fill(bytes);
        return BitConverter.ToUInt64(bytes);
    }

    internal static ulong Mix(ulong value)
    {
        value ^= value >> 30;
        value *= 0xbf58476d1ce4e5b9UL;
        value ^= value >> 27;
        value *= 0x94d049bb133111ebUL;
        return value ^ (value >> 31);
    }

    internal readonly record struct Rank(int Geometric, ulong Secondary, ulong Hash);
}
