using System.Buffers.Binary;
using System.Numerics;
using System.Security.Cryptography;

namespace Tools.DataStructures.FingerTree;

/// <summary>Defines ordering and deterministic pseudorandom ranks for a canonical zip-zip set.</summary>
/// <typeparam name="T">The item type.</typeparam>
/// <remarks>
/// The rank hash must be constant for values that compare equal. A policy object is retained as
/// representation and canonical-algebra compatibility state by every derived version. Random keys make one policy
/// process-local; pinned seeds or caller-retained keys reproduce ranks across policy instances.
/// </remarks>
public sealed class ZipTreeRankPolicy<T>
{
    private const int MinimumKeyBytes = 32;
    private const int RankKeyBytes = 32;
    private readonly Func<T, ulong> _rankHash;
    private readonly byte[] _rankKey;

    private ZipTreeRankPolicy(
        IComparer<T> comparer,
        Func<T, ulong> rankHash,
        byte[] rankKey,
        ulong? seed)
    {
        Comparer = comparer;
        _rankHash = rankHash;
        _rankKey = rankKey;
        Seed = seed;
    }

    /// <summary>Gets the process-local default policy.</summary>
    /// <remarks>
    /// Its HMAC key is generated once from a cryptographic random source and is not exposed. Its
    /// fallback rank hash uses <see cref="EqualityComparer{T}.Default"/>; canonicality therefore
    /// requires the default equality hash to be constant on default-comparer equivalence classes.
    /// </remarks>
    public static ZipTreeRankPolicy<T> Default { get; } =
        new(Comparer<T>.Default, DefaultHash, RandomKey(), seed: null);

    /// <summary>Gets the comparer that defines item order and set equivalence.</summary>
    public IComparer<T> Comparer { get; }

    /// <summary>Gets the public reproducibility seed, or null when an unexposed rank key is used.</summary>
    /// <remarks>A public seed is a deterministic salt, not a secret adversarial-security key.</remarks>
    public ulong? Seed { get; }

    /// <summary>Creates a random-keyed or publicly seeded rank policy.</summary>
    /// <param name="comparer">The ordering/equivalence policy, or null for the default comparer.</param>
    /// <param name="rankHash">
    /// A hash constant on comparer-equivalence classes. It is required when an explicit comparer is
    /// supplied; otherwise the default equality hash is used.
    /// </param>
    /// <param name="seed">
    /// A public reproducibility seed, or null to generate an unexposed cryptographic rank key.
    /// </param>
    /// <returns>A policy suitable for constructing one interoperable family of set versions.</returns>
    /// <exception cref="ArgumentException">
    /// <paramref name="comparer"/> is explicit but <paramref name="rankHash"/> is missing.
    /// </exception>
    public static ZipTreeRankPolicy<T> Create(
        IComparer<T>? comparer = null,
        Func<T, ulong>? rankHash = null,
        ulong? seed = null)
    {
        if (comparer is not null && rankHash is null)
        {
            throw new ArgumentException(
                "An explicit comparer requires a rank hash that is constant on its equivalence classes.",
                nameof(rankHash));
        }
        return new(
            comparer ?? Comparer<T>.Default,
            rankHash ?? DefaultHash,
            seed is { } value ? DerivePublicSeedKey(value) : RandomKey(),
            seed);
    }

    /// <summary>Creates a policy using a caller-retained HMAC key.</summary>
    /// <param name="rankKey">
    /// At least 32 secret bytes. The policy takes an owned copy and never exposes it.
    /// </param>
    /// <param name="comparer">The ordering/equivalence policy, or null for the default comparer.</param>
    /// <param name="rankHash">
    /// A hash constant on comparer-equivalence classes. It is required when an explicit comparer is
    /// supplied; otherwise the default equality hash is used.
    /// </param>
    /// <returns>A policy whose ranks can be reproduced by supplying the same key and hash policy.</returns>
    public static ZipTreeRankPolicy<T> CreateKeyed(
        ReadOnlySpan<byte> rankKey,
        IComparer<T>? comparer = null,
        Func<T, ulong>? rankHash = null)
    {
        if (rankKey.Length < MinimumKeyBytes)
            throw new ArgumentException($"A zip-zip rank key must contain at least {MinimumKeyBytes} bytes.", nameof(rankKey));
        if (comparer is not null && rankHash is null)
        {
            throw new ArgumentException(
                "An explicit comparer requires a rank hash that is constant on its equivalence classes.",
                nameof(rankHash));
        }
        return new(comparer ?? Comparer<T>.Default, rankHash ?? DefaultHash, rankKey.ToArray(), seed: null);
    }

    internal Rank GetRank(T item)
    {
        Span<byte> source = stackalloc byte[sizeof(ulong)];
        BinaryPrimitives.WriteUInt64BigEndian(source, _rankHash(item));
        Span<byte> digest = stackalloc byte[SHA256.HashSizeInBytes];
        _ = HMACSHA256.HashData(_rankKey, source, digest);
        var primary = BinaryPrimitives.ReadUInt64BigEndian(digest);
        var secondary = BinaryPrimitives.ReadUInt64BigEndian(digest[sizeof(ulong)..]);
        var content = BinaryPrimitives.ReadUInt64BigEndian(digest[(sizeof(ulong) * 2)..]);
        return new Rank(BitOperations.LeadingZeroCount(primary), secondary, content);
    }

    private static ulong DefaultHash(T item) => unchecked((uint)EqualityComparer<T>.Default.GetHashCode(item!));

    private static byte[] RandomKey()
    {
        var key = new byte[RankKeyBytes];
        RandomNumberGenerator.Fill(key);
        return key;
    }

    private static byte[] DerivePublicSeedKey(ulong seed)
    {
        Span<byte> material = stackalloc byte[4 + sizeof(ulong)];
        "ZZT2"u8.CopyTo(material);
        BinaryPrimitives.WriteUInt64BigEndian(material[4..], seed);
        return SHA256.HashData(material);
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
