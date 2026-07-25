using System.Buffers.Binary;
using System.Security.Cryptography;
using System.Text;

namespace Durable7.Hamt;

/// <summary>Encodes and decodes canonical values for content-addressed structures.</summary>
/// <typeparam name="T">The encoded value type.</typeparam>
/// <remarks>
/// Implementations must define an injective, versioned wire encoding. <see cref="Decode"/> consumes
/// exactly one complete encoding and must reject malformed, non-canonical, or trailing input.
/// </remarks>
public interface IMerkleCodec<T>
{
    /// <summary>Gets a stable, versioned identifier for the byte encoding.</summary>
    string EncodingId { get; }

    /// <summary>Encodes one value into a newly owned canonical byte array.</summary>
    /// <param name="value">The value to encode.</param>
    /// <returns>The canonical bytes.</returns>
    byte[] Encode(T value);

    /// <summary>Decodes exactly one value from its complete canonical encoding.</summary>
    /// <param name="encoding">The complete canonical encoding.</param>
    /// <returns>The decoded value.</returns>
    /// <exception cref="FormatException">
    /// <paramref name="encoding"/> is malformed, non-canonical, or contains trailing bytes.
    /// </exception>
    T Decode(ReadOnlySpan<byte> encoding);
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

        public int Decode(ReadOnlySpan<byte> encoding)
        {
            RequireLength(encoding, sizeof(int), EncodingId);
            return BinaryPrimitives.ReadInt32BigEndian(encoding);
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

        public long Decode(ReadOnlySpan<byte> encoding)
        {
            RequireLength(encoding, sizeof(long), EncodingId);
            return BinaryPrimitives.ReadInt64BigEndian(encoding);
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

        public string? Decode(ReadOnlySpan<byte> encoding)
        {
            RequireTag(encoding, EncodingId);
            if (encoding[0] == 0)
            {
                RequireLength(encoding, 1, EncodingId);
                return null;
            }
            if (encoding[0] != 1)
                throw InvalidEncoding(EncodingId, "the nullable tag must be 0 or 1");
            try
            {
                return Utf8.GetString(encoding[1..]);
            }
            catch (DecoderFallbackException exception)
            {
                throw new FormatException($"The {EncodingId} payload is not canonical UTF-8.", exception);
            }
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

        public byte[]? Decode(ReadOnlySpan<byte> encoding)
        {
            RequireTag(encoding, EncodingId);
            if (encoding[0] == 0)
            {
                RequireLength(encoding, 1, EncodingId);
                return null;
            }
            if (encoding[0] != 1)
                throw InvalidEncoding(EncodingId, "the nullable tag must be 0 or 1");
            return encoding[1..].ToArray();
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

        public Guid Decode(ReadOnlySpan<byte> encoding)
        {
            RequireLength(encoding, 16, EncodingId);
            Span<byte> little = stackalloc byte[16];
            little[0] = encoding[3];
            little[1] = encoding[2];
            little[2] = encoding[1];
            little[3] = encoding[0];
            little[4] = encoding[5];
            little[5] = encoding[4];
            little[6] = encoding[7];
            little[7] = encoding[6];
            encoding[8..].CopyTo(little[8..]);
            return new Guid(little);
        }
    }

    private static void RequireTag(ReadOnlySpan<byte> encoding, string encodingId)
    {
        if (encoding.IsEmpty)
            throw InvalidEncoding(encodingId, "the nullable tag is missing");
    }

    private static void RequireLength(ReadOnlySpan<byte> encoding, int length, string encodingId)
    {
        if (encoding.Length != length)
            throw InvalidEncoding(encodingId, $"the encoding must contain exactly {length} bytes");
    }

    private static FormatException InvalidEncoding(string encodingId, string reason) =>
        new($"Invalid {encodingId} encoding: {reason}.");
}

/// <summary>Represents a 256-bit SHA-256 content digest.</summary>
public readonly struct MerkleDigest : IEquatable<MerkleDigest>, IComparable<MerkleDigest>
{
    /// <summary>Gets the size of a binary digest in bytes.</summary>
    public const int ByteLength = 32;

    /// <summary>Gets the size of a hexadecimal digest in characters.</summary>
    public const int HexLength = ByteLength * 2;

    private const string LowerHexDigits = "0123456789abcdef";
    private readonly ulong _a;
    private readonly ulong _b;
    private readonly ulong _c;
    private readonly ulong _d;

    internal MerkleDigest(ReadOnlySpan<byte> bytes)
    {
        if (bytes.Length != ByteLength)
            throw new ArgumentException($"A Merkle digest must contain exactly {ByteLength} bytes.", nameof(bytes));
        _a = BinaryPrimitives.ReadUInt64BigEndian(bytes);
        _b = BinaryPrimitives.ReadUInt64BigEndian(bytes[8..]);
        _c = BinaryPrimitives.ReadUInt64BigEndian(bytes[16..]);
        _d = BinaryPrimitives.ReadUInt64BigEndian(bytes[24..]);
    }

    /// <summary>Creates a digest from exact 32-byte binary content.</summary>
    /// <param name="bytes">The complete big-endian digest bytes.</param>
    /// <returns>The digest.</returns>
    /// <exception cref="ArgumentException"><paramref name="bytes"/> does not contain exactly 32 bytes.</exception>
    public static MerkleDigest FromBytes(ReadOnlySpan<byte> bytes) => new(bytes);

    /// <summary>Tries to create a digest from exact 32-byte binary content.</summary>
    /// <param name="bytes">The complete big-endian digest bytes.</param>
    /// <param name="digest">The digest on success; otherwise, the zero digest.</param>
    /// <returns><see langword="true"/> when <paramref name="bytes"/> contains exactly 32 bytes.</returns>
    public static bool TryFromBytes(ReadOnlySpan<byte> bytes, out MerkleDigest digest) =>
        TryParse(bytes, out digest);

    /// <summary>Parses an exact 32-byte binary digest.</summary>
    /// <param name="bytes">The complete big-endian digest bytes.</param>
    /// <returns>The parsed digest.</returns>
    /// <exception cref="ArgumentException"><paramref name="bytes"/> does not contain exactly 32 bytes.</exception>
    public static MerkleDigest Parse(ReadOnlySpan<byte> bytes) => FromBytes(bytes);

    /// <summary>Tries to parse an exact 32-byte binary digest.</summary>
    /// <param name="bytes">The complete big-endian digest bytes.</param>
    /// <param name="digest">The parsed digest on success; otherwise, the zero digest.</param>
    /// <returns><see langword="true"/> when <paramref name="bytes"/> contains exactly 32 bytes.</returns>
    public static bool TryParse(ReadOnlySpan<byte> bytes, out MerkleDigest digest)
    {
        if (bytes.Length != ByteLength)
        {
            digest = default;
            return false;
        }
        digest = new MerkleDigest(bytes);
        return true;
    }

    /// <summary>Parses an exact 64-character hexadecimal digest.</summary>
    /// <param name="hex">The hexadecimal digest.</param>
    /// <returns>The parsed digest.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="hex"/> is <see langword="null"/>.</exception>
    /// <exception cref="FormatException"><paramref name="hex"/> is not exactly 64 hexadecimal characters.</exception>
    public static MerkleDigest Parse(string hex)
    {
        ArgumentNullException.ThrowIfNull(hex);
        return Parse(hex.AsSpan());
    }

    /// <summary>Parses an exact 64-character hexadecimal digest.</summary>
    /// <param name="hex">The hexadecimal digest.</param>
    /// <returns>The parsed digest.</returns>
    /// <exception cref="FormatException"><paramref name="hex"/> is not exactly 64 hexadecimal characters.</exception>
    public static MerkleDigest Parse(ReadOnlySpan<char> hex)
    {
        if (!TryParse(hex, out var digest))
            throw new FormatException($"A Merkle digest must contain exactly {HexLength} hexadecimal characters.");
        return digest;
    }

    /// <summary>Tries to parse an exact 64-character hexadecimal digest.</summary>
    /// <param name="hex">The hexadecimal digest, or <see langword="null"/>.</param>
    /// <param name="digest">The parsed digest on success; otherwise, the zero digest.</param>
    /// <returns><see langword="true"/> when <paramref name="hex"/> is a valid hexadecimal digest.</returns>
    public static bool TryParse(string? hex, out MerkleDigest digest)
    {
        if (hex is null)
        {
            digest = default;
            return false;
        }
        return TryParse(hex.AsSpan(), out digest);
    }

    /// <summary>Tries to parse an exact 64-character hexadecimal digest.</summary>
    /// <param name="hex">The hexadecimal digest.</param>
    /// <param name="digest">The parsed digest on success; otherwise, the zero digest.</param>
    /// <returns><see langword="true"/> when <paramref name="hex"/> is a valid hexadecimal digest.</returns>
    public static bool TryParse(ReadOnlySpan<char> hex, out MerkleDigest digest)
    {
        if (hex.Length != HexLength)
        {
            digest = default;
            return false;
        }

        Span<byte> bytes = stackalloc byte[ByteLength];
        for (var index = 0; index < bytes.Length; index++)
        {
            var high = HexValue(hex[index * 2]);
            var low = HexValue(hex[index * 2 + 1]);
            if ((high | low) < 0)
            {
                digest = default;
                return false;
            }
            bytes[index] = (byte)((high << 4) | low);
        }

        digest = new MerkleDigest(bytes);
        return true;
    }

    /// <summary>Returns a newly owned 32-byte big-endian digest.</summary>
    /// <returns>The digest bytes.</returns>
    public byte[] ToArray()
    {
        var result = new byte[ByteLength];
        WriteBytes(result);
        return result;
    }

    /// <summary>Writes the digest to a 32-byte-or-larger destination.</summary>
    /// <param name="destination">The destination span.</param>
    public void WriteBytes(Span<byte> destination)
    {
        if (!TryWriteBytes(destination))
            throw new ArgumentException($"The destination must contain at least {ByteLength} bytes.", nameof(destination));
    }

    /// <summary>Tries to write the digest to a 32-byte-or-larger destination.</summary>
    /// <param name="destination">The destination span.</param>
    /// <returns><see langword="true"/> when the destination was large enough and was written.</returns>
    public bool TryWriteBytes(Span<byte> destination)
    {
        if (destination.Length < ByteLength)
            return false;
        BinaryPrimitives.WriteUInt64BigEndian(destination, _a);
        BinaryPrimitives.WriteUInt64BigEndian(destination[8..], _b);
        BinaryPrimitives.WriteUInt64BigEndian(destination[16..], _c);
        BinaryPrimitives.WriteUInt64BigEndian(destination[24..], _d);
        return true;
    }

    /// <summary>Writes the digest to a 32-byte-or-larger destination.</summary>
    /// <param name="destination">The destination span.</param>
    /// <remarks>This compatibility alias has the same contract as <see cref="WriteBytes"/>.</remarks>
    public void WriteTo(Span<byte> destination) => WriteBytes(destination);

    /// <summary>Returns the canonical lower-case hexadecimal digest.</summary>
    /// <returns>The 64-character hexadecimal digest.</returns>
    public string ToHexString() => string.Create(HexLength, this, static (destination, digest) =>
        digest.WriteHex(destination));

    /// <summary>Writes the canonical lower-case hexadecimal digest.</summary>
    /// <param name="destination">The 64-character-or-larger destination.</param>
    public void WriteHex(Span<char> destination)
    {
        if (!TryWriteHex(destination, out _))
            throw new ArgumentException($"The destination must contain at least {HexLength} characters.", nameof(destination));
    }

    /// <summary>Tries to write the canonical lower-case hexadecimal digest.</summary>
    /// <param name="destination">The destination span.</param>
    /// <param name="charsWritten">The number of characters written: 64 on success; otherwise, zero.</param>
    /// <returns><see langword="true"/> when the destination was large enough and was written.</returns>
    public bool TryWriteHex(Span<char> destination, out int charsWritten)
    {
        if (destination.Length < HexLength)
        {
            charsWritten = 0;
            return false;
        }

        Span<byte> bytes = stackalloc byte[ByteLength];
        _ = TryWriteBytes(bytes);
        for (var index = 0; index < bytes.Length; index++)
        {
            destination[index * 2] = LowerHexDigits[bytes[index] >> 4];
            destination[index * 2 + 1] = LowerHexDigits[bytes[index] & 0x0f];
        }
        charsWritten = HexLength;
        return true;
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
    public override string ToString() => ToHexString();

    /// <summary>Tests digest equality.</summary>
    public static bool operator ==(MerkleDigest left, MerkleDigest right) => left.Equals(right);

    /// <summary>Tests digest inequality.</summary>
    public static bool operator !=(MerkleDigest left, MerkleDigest right) => !left.Equals(right);

    private static int HexValue(char value)
    {
        if (value is >= '0' and <= '9')
            return value - '0';
        if (value is >= 'a' and <= 'f')
            return value - 'a' + 10;
        if (value is >= 'A' and <= 'F')
            return value - 'A' + 10;
        return -1;
    }
}

/// <summary>Defines the deterministic comparison, encoding, and hash domain of a Merkle search tree.</summary>
/// <typeparam name="TKey">The key type.</typeparam>
/// <typeparam name="TValue">The value type.</typeparam>
public sealed class MerkleSearchTreePolicy<TKey, TValue>
{
    private const string AlgorithmIdValue = "mst-sha256-b16-v2";
    private static readonly UTF8Encoding Utf8 = new(false, true);
    private static readonly byte[] AlgorithmIdBytes = Utf8.GetBytes(AlgorithmIdValue);

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
        var keyEncodingId = ValidateEncodingId(keyCodec.EncodingId, nameof(keyCodec));
        var valueEncodingId = ValidateEncodingId(valueCodec.EncodingId, nameof(valueCodec));
        var policyIdBytes = EncodeIdentifier(policyId, nameof(policyId));
        var keyEncodingIdBytes = EncodeIdentifier(keyEncodingId, nameof(keyCodec));
        var valueEncodingIdBytes = EncodeIdentifier(valueEncodingId, nameof(valueCodec));
        DomainDigest = HashFramed(
            0x50,
            AlgorithmIdBytes,
            policyIdBytes,
            keyEncodingIdBytes,
            valueEncodingIdBytes);

        Span<byte> emptyManifest = stackalloc byte[5 + MerkleDigest.ByteLength];
        "MST2"u8.CopyTo(emptyManifest);
        emptyManifest[4] = 0;
        DomainDigest.WriteBytes(emptyManifest[5..]);
        EmptyDigest = HashBytes(emptyManifest);
    }

    /// <summary>Gets the hash/serialization algorithm identifier.</summary>
    public static string AlgorithmId => AlgorithmIdValue;

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
    /// <exception cref="ArgumentException">
    /// <paramref name="policyId"/> is empty, or a codec identifier is empty or lacks a terminal
    /// <c>-v&lt;digits&gt;</c> version suffix.
    /// </exception>
    /// <exception cref="ArgumentNullException">
    /// <paramref name="comparer"/>, <paramref name="keyCodec"/>, or <paramref name="valueCodec"/> is
    /// <see langword="null"/>.
    /// </exception>
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

    internal MerkleDigest HashBytes(ReadOnlySpan<byte> bytes)
    {
        Span<byte> digest = stackalloc byte[MerkleDigest.ByteLength];
        _ = SHA256.HashData(bytes, digest);
        return new MerkleDigest(digest);
    }

    internal MerkleDigest HashNode(
        byte[] keyBytes,
        byte[] valueBytes,
        MerkleDigest left,
        MerkleDigest right) =>
        HashFramed(0x4e, DomainDigest.ToArray(), keyBytes, valueBytes, left.ToArray(), right.ToArray());

    private static string ValidateEncodingId(string? encodingId, string parameterName)
    {
        if (string.IsNullOrWhiteSpace(encodingId)
            || char.IsWhiteSpace(encodingId[0])
            || char.IsWhiteSpace(encodingId[^1]))
        {
            throw new ArgumentException("A Merkle codec must declare a nonempty stable encoding identifier.", parameterName);
        }

        var marker = encodingId.LastIndexOf("-v", StringComparison.Ordinal);
        var version = marker < 0 ? ReadOnlySpan<char>.Empty : encodingId.AsSpan(marker + 2);
        if (marker == 0 || version.IsEmpty || !IsAsciiDigits(version))
        {
            throw new ArgumentException(
                "A Merkle codec encoding identifier must end in '-v' followed by decimal version digits.",
                parameterName);
        }
        return encodingId;
    }

    private static bool IsAsciiDigits(ReadOnlySpan<char> value)
    {
        foreach (var character in value)
        {
            if (character is < '0' or > '9')
                return false;
        }
        return true;
    }

    private static byte[] EncodeIdentifier(string value, string parameterName)
    {
        try
        {
            return Utf8.GetBytes(value);
        }
        catch (EncoderFallbackException exception)
        {
            throw new ArgumentException("A Merkle identifier must contain valid Unicode scalar values.", parameterName, exception);
        }
    }

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
