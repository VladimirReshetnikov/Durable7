using System.Buffers.Binary;
using System.Security.Cryptography;
using System.Text;

namespace Tools.DataStructures.Hamt;

/// <summary>Encodes values into canonical bytes for content-addressed structures.</summary>
/// <typeparam name="T">The encoded value type.</typeparam>
public interface IMerkleCodec<in T>
{
    /// <summary>Gets a stable, versioned identifier for the byte encoding.</summary>
    string EncodingId { get; }

    /// <summary>Encodes one value into a newly owned canonical byte array.</summary>
    /// <param name="value">The value to encode.</param>
    /// <returns>The canonical bytes.</returns>
    byte[] Encode(T value);
}

/// <summary>Provides built-in canonical codecs for common key and value types.</summary>
public static class MerkleCodecs
{
    /// <summary>Gets the signed 32-bit big-endian codec.</summary>
    public static IMerkleCodec<int> Int32 { get; } = new Int32Codec();

    /// <summary>Gets the signed 64-bit big-endian codec.</summary>
    public static IMerkleCodec<long> Int64 { get; } = new Int64Codec();

    /// <summary>Gets the nullable UTF-8 string codec with an explicit null/present tag.</summary>
    public static IMerkleCodec<string?> Utf8String { get; } = new StringCodec();

    /// <summary>Gets the nullable byte-array codec with an explicit null/present tag.</summary>
    public static IMerkleCodec<byte[]?> Bytes { get; } = new BytesCodec();

    /// <summary>Gets the RFC-4122/network-order GUID codec.</summary>
    public static IMerkleCodec<Guid> Guid { get; } = new GuidCodec();

    private sealed class Int32Codec : IMerkleCodec<int>
    {
        public string EncodingId => "i32-be-v1";

        public byte[] Encode(int value)
        {
            var bytes = new byte[sizeof(int)];
            BinaryPrimitives.WriteInt32BigEndian(bytes, value);
            return bytes;
        }
    }

    private sealed class Int64Codec : IMerkleCodec<long>
    {
        public string EncodingId => "i64-be-v1";

        public byte[] Encode(long value)
        {
            var bytes = new byte[sizeof(long)];
            BinaryPrimitives.WriteInt64BigEndian(bytes, value);
            return bytes;
        }
    }

    private sealed class StringCodec : IMerkleCodec<string?>
    {
        private static readonly UTF8Encoding Utf8 = new(false, true);

        public string EncodingId => "nullable-utf8-v1";

        public byte[] Encode(string? value)
        {
            if (value is null)
                return [0];
            var payload = Utf8.GetBytes(value);
            var result = new byte[payload.Length + 1];
            result[0] = 1;
            payload.CopyTo(result, 1);
            return result;
        }
    }

    private sealed class BytesCodec : IMerkleCodec<byte[]?>
    {
        public string EncodingId => "nullable-bytes-v1";

        public byte[] Encode(byte[]? value)
        {
            if (value is null)
                return [0];
            var result = new byte[value.Length + 1];
            result[0] = 1;
            value.CopyTo(result, 1);
            return result;
        }
    }

    private sealed class GuidCodec : IMerkleCodec<Guid>
    {
        public string EncodingId => "guid-rfc4122-v1";

        public byte[] Encode(Guid value)
        {
            Span<byte> little = stackalloc byte[16];
            value.TryWriteBytes(little);
            var result = new byte[16];
            result[0] = little[3];
            result[1] = little[2];
            result[2] = little[1];
            result[3] = little[0];
            result[4] = little[5];
            result[5] = little[4];
            result[6] = little[7];
            result[7] = little[6];
            little[8..].CopyTo(result.AsSpan(8));
            return result;
        }
    }
}

/// <summary>Represents a 256-bit SHA-256 content digest.</summary>
public readonly struct MerkleDigest : IEquatable<MerkleDigest>, IComparable<MerkleDigest>
{
    private readonly ulong _a;
    private readonly ulong _b;
    private readonly ulong _c;
    private readonly ulong _d;

    internal MerkleDigest(ReadOnlySpan<byte> bytes)
    {
        if (bytes.Length != 32)
            throw new ArgumentException("A Merkle digest must contain exactly 32 bytes.", nameof(bytes));
        _a = BinaryPrimitives.ReadUInt64BigEndian(bytes);
        _b = BinaryPrimitives.ReadUInt64BigEndian(bytes[8..]);
        _c = BinaryPrimitives.ReadUInt64BigEndian(bytes[16..]);
        _d = BinaryPrimitives.ReadUInt64BigEndian(bytes[24..]);
    }

    /// <summary>Returns a newly owned 32-byte big-endian digest.</summary>
    /// <returns>The digest bytes.</returns>
    public byte[] ToArray()
    {
        var result = new byte[32];
        WriteTo(result);
        return result;
    }

    /// <summary>Writes the digest to a 32-byte-or-larger destination.</summary>
    /// <param name="destination">The destination span.</param>
    public void WriteTo(Span<byte> destination)
    {
        if (destination.Length < 32)
            throw new ArgumentException("The destination must contain at least 32 bytes.", nameof(destination));
        BinaryPrimitives.WriteUInt64BigEndian(destination, _a);
        BinaryPrimitives.WriteUInt64BigEndian(destination[8..], _b);
        BinaryPrimitives.WriteUInt64BigEndian(destination[16..], _c);
        BinaryPrimitives.WriteUInt64BigEndian(destination[24..], _d);
    }

    /// <inheritdoc/>
    public bool Equals(MerkleDigest other) =>
        _a == other._a && _b == other._b && _c == other._c && _d == other._d;

    /// <inheritdoc/>
    public override bool Equals(object? obj) => obj is MerkleDigest other && Equals(other);

    /// <inheritdoc/>
    public override int GetHashCode() => HashCode.Combine(_a, _b, _c, _d);

    /// <inheritdoc/>
    public int CompareTo(MerkleDigest other)
    {
        var comparison = _a.CompareTo(other._a);
        if (comparison != 0) return comparison;
        comparison = _b.CompareTo(other._b);
        if (comparison != 0) return comparison;
        comparison = _c.CompareTo(other._c);
        return comparison != 0 ? comparison : _d.CompareTo(other._d);
    }

    /// <inheritdoc/>
    public override string ToString() => Convert.ToHexString(ToArray()).ToLowerInvariant();

    /// <summary>Tests digest equality.</summary>
    public static bool operator ==(MerkleDigest left, MerkleDigest right) => left.Equals(right);

    /// <summary>Tests digest inequality.</summary>
    public static bool operator !=(MerkleDigest left, MerkleDigest right) => !left.Equals(right);
}

/// <summary>Defines the deterministic comparison, encoding, and hash domain of a Merkle search tree.</summary>
/// <typeparam name="TKey">The key type.</typeparam>
/// <typeparam name="TValue">The value type.</typeparam>
public sealed class MerkleSearchTreePolicy<TKey, TValue>
{
    private static readonly byte[] AlgorithmIdBytes = Encoding.UTF8.GetBytes("mst-sha256-v1");

    private MerkleSearchTreePolicy(
        string policyId,
        IComparer<TKey> comparer,
        IMerkleCodec<TKey> keyCodec,
        IMerkleCodec<TValue> valueCodec)
    {
        PolicyId = policyId;
        Comparer = comparer;
        KeyCodec = keyCodec;
        ValueCodec = valueCodec;
        DomainDigest = HashFramed(
            0x50,
            AlgorithmIdBytes,
            Encoding.UTF8.GetBytes(policyId),
            Encoding.UTF8.GetBytes(keyCodec.EncodingId),
            Encoding.UTF8.GetBytes(valueCodec.EncodingId));
        EmptyDigest = HashFramed(0x00, DomainDigest.ToArray());
    }

    /// <summary>Gets the hash/serialization algorithm identifier.</summary>
    public static string AlgorithmId => "mst-sha256-v1";

    /// <summary>Gets the application-defined semantic policy identifier.</summary>
    public string PolicyId { get; }

    /// <summary>Gets the key comparer.</summary>
    public IComparer<TKey> Comparer { get; }

    /// <summary>Gets the canonical key codec.</summary>
    public IMerkleCodec<TKey> KeyCodec { get; }

    /// <summary>Gets the canonical value codec.</summary>
    public IMerkleCodec<TValue> ValueCodec { get; }

    /// <summary>Gets the digest identifying algorithm, policy, and codec versions.</summary>
    public MerkleDigest DomainDigest { get; }

    internal MerkleDigest EmptyDigest { get; }

    /// <summary>Creates a deterministic Merkle policy.</summary>
    /// <param name="policyId">A stable application semantic/version identifier.</param>
    /// <param name="comparer">The key order/equivalence policy.</param>
    /// <param name="keyCodec">An injective canonical encoding of key equivalence classes.</param>
    /// <param name="valueCodec">A canonical value encoding.</param>
    /// <returns>The policy.</returns>
    public static MerkleSearchTreePolicy<TKey, TValue> Create(
        string policyId,
        IComparer<TKey> comparer,
        IMerkleCodec<TKey> keyCodec,
        IMerkleCodec<TValue> valueCodec)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(policyId);
        ArgumentNullException.ThrowIfNull(comparer);
        ArgumentNullException.ThrowIfNull(keyCodec);
        ArgumentNullException.ThrowIfNull(valueCodec);
        return new(policyId, comparer, keyCodec, valueCodec);
    }

    internal MerkleDigest HashKey(byte[] keyBytes) => HashFramed(0x4b, DomainDigest.ToArray(), keyBytes);

    internal MerkleDigest HashNode(
        byte[] keyBytes,
        byte[] valueBytes,
        MerkleDigest left,
        MerkleDigest right) =>
        HashFramed(0x4e, DomainDigest.ToArray(), keyBytes, valueBytes, left.ToArray(), right.ToArray());

    private static MerkleDigest HashFramed(byte tag, params byte[][] fields)
    {
        using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        hash.AppendData([tag]);
        Span<byte> length = stackalloc byte[sizeof(int)];
        foreach (var field in fields)
        {
            BinaryPrimitives.WriteInt32BigEndian(length, field.Length);
            hash.AppendData(length);
            hash.AppendData(field);
        }
        return new MerkleDigest(hash.GetHashAndReset());
    }
}
