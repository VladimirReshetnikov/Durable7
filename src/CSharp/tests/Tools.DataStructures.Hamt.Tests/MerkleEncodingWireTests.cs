using System.Security.Cryptography;
using System.Text;
using Xunit;

namespace Tools.DataStructures.Hamt.Tests;

/// <summary>Golden-vector and strict wire-contract coverage for Merkle codecs and digests.</summary>
public sealed class MerkleEncodingWireTests
{
    /// <summary>Locks down every built-in codec's version identifier and canonical bytes.</summary>
    [Fact]
    public void BuiltInCodecs_HaveStableIdentifiersAndGoldenVectors()
    {
        Assert.Equal("i32-be-v1", MerkleCodecs.Int32.EncodingId);
        Assert.Equal("01020304", Convert.ToHexString(MerkleCodecs.Int32.Encode(0x01020304)));
        Assert.Equal("80000000", Convert.ToHexString(MerkleCodecs.Int32.Encode(int.MinValue)));

        Assert.Equal("i64-be-v1", MerkleCodecs.Int64.EncodingId);
        Assert.Equal("0102030405060708", Convert.ToHexString(MerkleCodecs.Int64.Encode(0x0102030405060708)));
        Assert.Equal("8000000000000000", Convert.ToHexString(MerkleCodecs.Int64.Encode(long.MinValue)));

        Assert.Equal("nullable-utf8-v1", MerkleCodecs.Utf8String.EncodingId);
        Assert.Equal("00", Convert.ToHexString(MerkleCodecs.Utf8String.Encode(null)));
        Assert.Equal("01", Convert.ToHexString(MerkleCodecs.Utf8String.Encode(string.Empty)));
        Assert.Equal("0141C3A9F09F9880", Convert.ToHexString(MerkleCodecs.Utf8String.Encode("A\u00e9\U0001f600")));

        Assert.Equal("nullable-bytes-v1", MerkleCodecs.Bytes.EncodingId);
        Assert.Equal("00", Convert.ToHexString(MerkleCodecs.Bytes.Encode(null)));
        Assert.Equal("01", Convert.ToHexString(MerkleCodecs.Bytes.Encode([])));
        Assert.Equal("0100FF", Convert.ToHexString(MerkleCodecs.Bytes.Encode([0, 255])));

        Assert.Equal("guid-rfc4122-v1", MerkleCodecs.Guid.EncodingId);
        var guid = Guid.Parse("00112233-4455-6677-8899-aabbccddeeff");
        Assert.Equal("00112233445566778899AABBCCDDEEFF", Convert.ToHexString(MerkleCodecs.Guid.Encode(guid)));
    }

    /// <summary>Round-trips boundary and Unicode values through all built-in codecs.</summary>
    [Fact]
    public void BuiltInCodecs_RoundTripCanonicalValues()
    {
        foreach (var value in new[] { int.MinValue, -1, 0, 1, int.MaxValue })
            AssertRoundTrip(MerkleCodecs.Int32, value);
        foreach (var value in new[] { long.MinValue, -1L, 0L, 1L, long.MaxValue })
            AssertRoundTrip(MerkleCodecs.Int64, value);
        foreach (var value in new string?[] { null, string.Empty, "plain", "nul\0inside", "e\u0301", "\u00e9\U0001f600" })
            AssertRoundTrip(MerkleCodecs.Utf8String, value);
        foreach (var value in new[]
                 {
                     Guid.Empty,
                     Guid.Parse("00112233-4455-6677-8899-aabbccddeeff"),
                     Guid.Parse("ffffffff-eeee-dddd-cccc-bbbbbbbbbbbb"),
                 })
        {
            AssertRoundTrip(MerkleCodecs.Guid, value);
        }

        byte[]?[] byteValues = [null, [], [0], [0, 1, 2, 254, 255]];
        foreach (var value in byteValues)
        {
            var encoding = MerkleCodecs.Bytes.Encode(value);
            var decoded = MerkleCodecs.Bytes.Decode(encoding);
            Assert.Equal(value, decoded);
            Assert.Equal(encoding, MerkleCodecs.Bytes.Encode(decoded));
            if (decoded is not null && encoding.Length > 1)
            {
                var first = decoded[0];
                encoding[1] ^= 0xff;
                Assert.Equal(first, decoded[0]);
            }
        }
    }

    /// <summary>Rejects malformed fixed-width, tagged, UTF-8, and trailing encodings.</summary>
    [Fact]
    public void BuiltInCodecs_RejectMalformedAndTrailingInput()
    {
        Assert.Throws<FormatException>(() => MerkleCodecs.Int32.Decode(new byte[3]));
        Assert.Throws<FormatException>(() => MerkleCodecs.Int32.Decode(new byte[5]));
        Assert.Throws<FormatException>(() => MerkleCodecs.Int64.Decode(new byte[7]));
        Assert.Throws<FormatException>(() => MerkleCodecs.Int64.Decode(new byte[9]));
        Assert.Throws<FormatException>(() => MerkleCodecs.Guid.Decode(new byte[15]));
        Assert.Throws<FormatException>(() => MerkleCodecs.Guid.Decode(new byte[17]));

        Assert.Throws<FormatException>(() => MerkleCodecs.Utf8String.Decode([]));
        Assert.Throws<FormatException>(() => MerkleCodecs.Utf8String.Decode([2]));
        Assert.Throws<FormatException>(() => MerkleCodecs.Utf8String.Decode([0, 0]));
        Assert.Throws<FormatException>(() => MerkleCodecs.Utf8String.Decode([1, 0xc0, 0x80]));
        Assert.Throws<FormatException>(() => MerkleCodecs.Utf8String.Decode([1, 0xe2, 0x82]));
        Assert.Throws<EncoderFallbackException>(() => MerkleCodecs.Utf8String.Encode("\ud800"));

        Assert.Throws<FormatException>(() => MerkleCodecs.Bytes.Decode([]));
        Assert.Throws<FormatException>(() => MerkleCodecs.Bytes.Decode([2]));
        Assert.Throws<FormatException>(() => MerkleCodecs.Bytes.Decode([0, 0]));
    }

    /// <summary>Locks down nullable framing and policy-field length framing against prefix ambiguity.</summary>
    [Fact]
    public void NullableAndPolicyFraming_RemainInjective()
    {
        AssertDistinct(
            MerkleCodecs.Utf8String.Encode(null),
            MerkleCodecs.Utf8String.Encode(string.Empty),
            MerkleCodecs.Utf8String.Encode("\0"));
        AssertDistinct(
            MerkleCodecs.Bytes.Encode(null),
            MerkleCodecs.Bytes.Encode([]),
            MerkleCodecs.Bytes.Encode([0]));

        var firstKeyCodec = new NamedIntCodec("key-v1");
        var firstValueCodec = new NamedIntCodec("part-v1rest-v1");
        var secondKeyCodec = new NamedIntCodec("key-v1part-v1");
        var secondValueCodec = new NamedIntCodec("rest-v1");
        Assert.Equal(
            firstKeyCodec.EncodingId + firstValueCodec.EncodingId,
            secondKeyCodec.EncodingId + secondValueCodec.EncodingId);

        var first = MerkleSearchTreePolicy<int, int>.Create(
            "wire-policy-v1", Comparer<int>.Default, firstKeyCodec, firstValueCodec);
        var second = MerkleSearchTreePolicy<int, int>.Create(
            "wire-policy-v1", Comparer<int>.Default, secondKeyCodec, secondValueCodec);
        Assert.NotEqual(first.DomainDigest, second.DomainDigest);
    }

    /// <summary>Rejects codec identifiers that are empty, padded, or lack an explicit numeric version.</summary>
    /// <param name="encodingId">The invalid identifier.</param>
    [Theory]
    [InlineData(null)]
    [InlineData("")]
    [InlineData(" ")]
    [InlineData("codec")]
    [InlineData("codec-v")]
    [InlineData("codec-vx")]
    [InlineData("codec-v1-extra")]
    [InlineData(" codec-v1")]
    [InlineData("codec-v1 ")]
    public void Policy_RejectsUnversionedCodecIdentifiers(string? encodingId)
    {
        var exception = Assert.Throws<ArgumentException>(() => MerkleSearchTreePolicy<int, int>.Create(
            "wire-policy-v1",
            Comparer<int>.Default,
            new NamedIntCodec(encodingId),
            MerkleCodecs.Int32));
        Assert.Equal("keyCodec", exception.ParamName);
    }

    /// <summary>Rejects ill-formed UTF-16 instead of collapsing identifiers through replacement fallback.</summary>
    [Fact]
    public void Policy_RejectsIllFormedUnicodeCodecIdentifier()
    {
        var encodingId = new string('\ud800', 1) + "-v1";
        var exception = Assert.Throws<ArgumentException>(() => MerkleSearchTreePolicy<int, int>.Create(
            "wire-policy-v1",
            Comparer<int>.Default,
            new NamedIntCodec(encodingId),
            MerkleCodecs.Int32));
        Assert.Equal("keyCodec", exception.ParamName);
    }

    /// <summary>Applies the same version validation to value-codec identifiers.</summary>
    [Fact]
    public void Policy_RejectsUnversionedValueCodecIdentifier()
    {
        var exception = Assert.Throws<ArgumentException>(() => MerkleSearchTreePolicy<int, int>.Create(
            "wire-policy-v1",
            Comparer<int>.Default,
            MerkleCodecs.Int32,
            new NamedIntCodec("value-codec")));
        Assert.Equal("valueCodec", exception.ParamName);
    }

    /// <summary>Validates the B=16 algorithm domain and exact canonical empty-manifest hash.</summary>
    [Fact]
    public void Policy_UsesB16V2DomainAndCanonicalEmptyManifest()
    {
        Assert.Equal("mst-sha256-b16-v2", MerkleSearchTreePolicy<int, int>.AlgorithmId);
        var policy = MerkleSearchTreePolicy<int, int>.Create(
            "wire-policy-v1", Comparer<int>.Default, MerkleCodecs.Int32, MerkleCodecs.Int32);

        var manifest = new byte[5 + MerkleDigest.ByteLength];
        "MST2"u8.CopyTo(manifest);
        manifest[4] = 0;
        policy.DomainDigest.WriteBytes(manifest.AsSpan(5));
        var expected = MerkleDigest.FromBytes(SHA256.HashData(manifest));

        Assert.Equal(expected, policy.EmptyDigest);
        Assert.Equal(expected, policy.HashBytes(manifest));
        Assert.Equal(expected, MerkleSearchTree<int, int>.Create(policy).RootHash);
    }

    /// <summary>Round-trips raw and hexadecimal digests through allocating and span-writing APIs.</summary>
    [Fact]
    public void MerkleDigest_ParsesAndWritesBytesAndHex()
    {
        var bytes = Enumerable.Range(0, MerkleDigest.ByteLength).Select(value => (byte)value).ToArray();
        const string hex = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
        var digest = MerkleDigest.FromBytes(bytes);

        Assert.Equal(digest, MerkleDigest.Parse(bytes));
        Assert.True(MerkleDigest.TryFromBytes(bytes, out var fromBytes));
        Assert.Equal(digest, fromBytes);
        Assert.True(MerkleDigest.TryParse(bytes, out var parsedBytes));
        Assert.Equal(digest, parsedBytes);
        Assert.Equal(digest, MerkleDigest.Parse(hex));
        Assert.Equal(digest, MerkleDigest.Parse(hex.AsSpan()));
        Assert.True(MerkleDigest.TryParse(hex.ToUpperInvariant(), out var parsedHex));
        Assert.Equal(digest, parsedHex);
        Assert.Equal(hex, digest.ToHexString());
        Assert.Equal(hex, digest.ToString());
        Assert.Equal(bytes, digest.ToArray());

        var byteDestination = Enumerable.Repeat((byte)0xcc, MerkleDigest.ByteLength + 2).ToArray();
        Assert.True(digest.TryWriteBytes(byteDestination));
        Assert.Equal(bytes, byteDestination[..MerkleDigest.ByteLength]);
        Assert.Equal(new byte[] { 0xcc, 0xcc }, byteDestination[MerkleDigest.ByteLength..]);
        Array.Fill(byteDestination, (byte)0xcc);
        digest.WriteBytes(byteDestination);
        Assert.Equal(bytes, byteDestination[..MerkleDigest.ByteLength]);
        Array.Fill(byteDestination, (byte)0xcc);
        digest.WriteTo(byteDestination);
        Assert.Equal(bytes, byteDestination[..MerkleDigest.ByteLength]);

        var hexDestination = new string('?', MerkleDigest.HexLength + 2).ToCharArray();
        Assert.True(digest.TryWriteHex(hexDestination, out var charsWritten));
        Assert.Equal(MerkleDigest.HexLength, charsWritten);
        Assert.Equal(hex, new string(hexDestination, 0, MerkleDigest.HexLength));
        Assert.Equal("??", new string(hexDestination, MerkleDigest.HexLength, 2));
        Array.Fill(hexDestination, '?');
        digest.WriteHex(hexDestination);
        Assert.Equal(hex, new string(hexDestination, 0, MerkleDigest.HexLength));
    }

    /// <summary>Rejects wrong digest lengths and malformed hexadecimal without partially writing destinations.</summary>
    [Fact]
    public void MerkleDigest_StrictApisRejectMalformedInputAndShortDestinations()
    {
        Assert.Throws<ArgumentException>(() => MerkleDigest.FromBytes(new byte[MerkleDigest.ByteLength - 1]));
        Assert.Throws<ArgumentException>(() => MerkleDigest.Parse(new byte[MerkleDigest.ByteLength + 1]));
        Assert.False(MerkleDigest.TryFromBytes(new byte[MerkleDigest.ByteLength - 1], out var shortBytes));
        Assert.Equal(default, shortBytes);
        Assert.False(MerkleDigest.TryParse(new byte[MerkleDigest.ByteLength + 1], out var longBytes));
        Assert.Equal(default, longBytes);

        Assert.Throws<ArgumentNullException>(() => MerkleDigest.Parse((string)null!));
        Assert.Throws<FormatException>(() => MerkleDigest.Parse(new string('0', MerkleDigest.HexLength - 1)));
        Assert.Throws<FormatException>(() => MerkleDigest.Parse(new string('0', MerkleDigest.HexLength + 1)));
        Assert.Throws<FormatException>(() => MerkleDigest.Parse(new string('g', MerkleDigest.HexLength)));
        Assert.Throws<FormatException>(() => MerkleDigest.Parse("0x" + new string('0', MerkleDigest.HexLength - 2)));
        Assert.False(MerkleDigest.TryParse((string?)null, out var nullHex));
        Assert.Equal(default, nullHex);

        var digest = MerkleDigest.FromBytes(new byte[MerkleDigest.ByteLength]);
        var shortByteDestination = Enumerable.Repeat((byte)0xcc, MerkleDigest.ByteLength - 1).ToArray();
        Assert.False(digest.TryWriteBytes(shortByteDestination));
        Assert.All(shortByteDestination, value => Assert.Equal(0xcc, value));
        Assert.Throws<ArgumentException>(() => digest.WriteBytes(shortByteDestination));
        Assert.Throws<ArgumentException>(() => digest.WriteTo(shortByteDestination));

        var shortHexDestination = new string('?', MerkleDigest.HexLength - 1).ToCharArray();
        Assert.False(digest.TryWriteHex(shortHexDestination, out var charsWritten));
        Assert.Equal(0, charsWritten);
        Assert.All(shortHexDestination, value => Assert.Equal('?', value));
        Assert.Throws<ArgumentException>(() => digest.WriteHex(shortHexDestination));
    }

    private static void AssertRoundTrip<T>(IMerkleCodec<T> codec, T value)
    {
        var encoding = codec.Encode(value);
        var decoded = codec.Decode(encoding);
        Assert.Equal(value, decoded);
        Assert.Equal(encoding, codec.Encode(decoded));
    }

    private static void AssertDistinct(params byte[][] encodings)
    {
        for (var left = 0; left < encodings.Length; left++)
        {
            for (var right = left + 1; right < encodings.Length; right++)
                Assert.False(encodings[left].AsSpan().SequenceEqual(encodings[right]));
        }
    }

    private sealed class NamedIntCodec(string? encodingId) : IMerkleCodec<int>
    {
        public string EncodingId => encodingId!;

        public byte[] Encode(int value) => MerkleCodecs.Int32.Encode(value);

        public int Decode(ReadOnlySpan<byte> encoding) => MerkleCodecs.Int32.Decode(encoding);
    }
}
