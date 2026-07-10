using System.Reflection;
using Xunit;

namespace Tools.DataStructures.FingerTree.Tests;

/// <summary>
/// Covers rope seams whose chunk shape is observable only around a split boundary, and mixed
/// <see cref="TextReader"/> reads across several underlying rope chunks.
/// </summary>
public sealed class RopeBoundaryCoverageTests
{
    /// <summary>
    /// Verifies splitting may temporarily expose a legal sub-minimum boundary chunk, while concatenating
    /// the two halves coalesces that seam back into the original full-sized chunks.
    /// </summary>
    [Theory]
    [InlineData(1)]
    [InlineData(255)]
    [InlineData(2047)]
    [InlineData(2049)]
    [InlineData(4095)]
    public void Split_SubMinimumBoundaryChunk_IsToleratedAndConcatRecoalesces(int index)
    {
        var expected = Enumerable.Range(0, 4096).ToArray();
        var rope = Rope<int>.Create(expected);

        Assert.Equal([2048, 2048], GetChunkLengths(rope));

        var (left, right) = rope.Split(index);
        left.ValidateInvariants();
        right.ValidateInvariants();
        Assert.Equal(expected[..index], left.ToArray());
        Assert.Equal(expected[index..], right.ToArray());
        Assert.Contains(GetChunkLengths(left).Concat(GetChunkLengths(right)), length => length < 256);

        var rejoined = left.Concat(right);
        rejoined.ValidateInvariants();
        Assert.Equal(expected, rejoined.ToArray());
        Assert.Equal([2048, 2048], GetChunkLengths(rejoined));
    }

    /// <summary>
    /// Verifies repeated peeks, direct single-character reads, and buffered reads can be freely
    /// interleaved across chunk boundaries for both positional and newline-measured text ropes.
    /// </summary>
    [Fact]
    public void TextReader_PeekSingleAndBufferedReads_InterleaveAcrossChunks()
    {
        var characters = Enumerable.Range(0, 6147)
            .Select(index => index % 97 == 0 ? '\n' : (char)(' ' + (index % 90)))
            .ToArray();
        var text = new string(characters);

        using (var reader = text.ToCharRope().AsTextReader())
            AssertInterleavedReads(text, reader);

        using (var reader = text.ToTextRope().AsTextReader())
            AssertInterleavedReads(text, reader);
    }

    private static int[] GetChunkLengths<T>(Rope<T> rope)
    {
        var field = typeof(Rope<T>).GetField("_tree", BindingFlags.Instance | BindingFlags.NonPublic);
        var tree = Assert.IsType<FingerTree<Chunk<T>, int, ChunkLengthMeasure<T>>>(field?.GetValue(rope));
        return tree.Select(chunk => chunk.Length).ToArray();
    }

    private static void AssertInterleavedReads(string expected, TextReader reader)
    {
        var position = 0;
        var buffer = new char[11];
        while (position < expected.Length)
        {
            switch (position % 4)
            {
                case 0:
                {
                    Assert.Equal(expected[position], (char)reader.Peek());
                    Assert.Equal(expected[position], (char)reader.Peek());
                    var requested = Math.Min(7, expected.Length - position);
                    Array.Fill(buffer, '\uffff');
                    var read = reader.Read(buffer, 2, requested);
                    Assert.Equal(requested, read);
                    Assert.Equal(expected.AsSpan(position, requested).ToArray(), buffer.AsSpan(2, requested).ToArray());
                    position += read;
                    break;
                }

                case 1:
                    Assert.Equal(expected[position++], (char)reader.Read());
                    break;

                case 2:
                    Assert.Equal(expected[position], (char)reader.Peek());
                    Assert.Equal(expected[position++], (char)reader.Read());
                    break;

                default:
                {
                    var requested = Math.Min(3, expected.Length - position);
                    var read = reader.Read(buffer, 0, requested);
                    Assert.Equal(requested, read);
                    Assert.Equal(expected.AsSpan(position, requested).ToArray(), buffer.AsSpan(0, requested).ToArray());
                    position += read;
                    break;
                }
            }
        }

        Assert.Equal(-1, reader.Peek());
        Assert.Equal(-1, reader.Peek());
        Assert.Equal(-1, reader.Read());
        Assert.Equal(-1, reader.Read());
        Assert.Equal(0, reader.Read(buffer, 0, buffer.Length));
    }
}
