// Tests for the rope model.

using Durable7.FingerTree;
using Xunit;

namespace Durable7.FingerTree.Tests;

/// <summary>
/// Validates <see cref="Rope{T}"/> against a <see cref="List{T}"/> oracle under long randomized histories that
/// interleave every operation (driving the rope across chunk boundaries so split, coalesce, and grow paths all
/// fire), checks internal invariants after every step, and confirms branching persistence over retained
/// versions.
/// </summary>
public sealed class RopeModelTests
{
    /// <summary>
    /// Runs a deterministic mixed history of inserts, removals, set, range edits, splits, and concatenations,
    /// comparing element-for-element against a list model and validating chunk invariants after each step.
    /// </summary>
    /// <param name="seed">Deterministic random seed.</param>
    [Theory]
    [InlineData(0)]
    [InlineData(1)]
    [InlineData(2)]
    [InlineData(3)]
    [InlineData(4)]
    public void RandomizedHistory_MatchesListModel(int seed)
    {
        var random = new Random(seed);
        var model = new List<int>();
        var rope = Rope<int>.Empty;
        var next = 0;

        for (var step = 0; step < 400; step++)
        {
            switch (random.Next(11))
            {
                case 0:
                    rope = rope.AddFirst(next);
                    model.Insert(0, next++);
                    break;
                case 1:
                    rope = rope.AddLast(next);
                    model.Add(next++);
                    break;
                case 2 when model.Count > 0:
                    rope = rope.RemoveFirst();
                    model.RemoveAt(0);
                    break;
                case 3 when model.Count > 0:
                    rope = rope.RemoveLast();
                    model.RemoveAt(model.Count - 1);
                    break;
                case 4:
                {
                    var index = random.Next(model.Count + 1);
                    rope = rope.Insert(index, next);
                    model.Insert(index, next++);
                    break;
                }
                case 5 when model.Count > 0:
                {
                    var index = random.Next(model.Count);
                    rope = rope.RemoveAt(index);
                    model.RemoveAt(index);
                    break;
                }
                case 6 when model.Count > 0:
                {
                    var index = random.Next(model.Count);
                    rope = rope.SetItem(index, next);
                    model[index] = next++;
                    break;
                }
                case 7:
                {
                    // Bulk insert a run of new values.
                    var index = random.Next(model.Count + 1);
                    var run = Enumerable.Range(next, random.Next(0, 600)).ToArray();
                    next += run.Length;
                    rope = rope.InsertRange(index, run.AsSpan());
                    model.InsertRange(index, run);
                    break;
                }
                case 8 when model.Count > 0:
                {
                    // Remove a random range.
                    var index = random.Next(model.Count);
                    var count = random.Next(0, model.Count - index + 1);
                    rope = rope.RemoveRange(index, count);
                    model.RemoveRange(index, count);
                    break;
                }
                case 9:
                {
                    // Split then reconcatenate in the same order (identity round-trip).
                    var index = random.Next(model.Count + 1);
                    var (left, right) = rope.Split(index);
                    left.ValidateInvariants();
                    right.ValidateInvariants();
                    rope = left.Concat(right);
                    break;
                }
                default:
                {
                    // Rotate: split, then concat the halves swapped, mirrored in the model.
                    var index = random.Next(model.Count + 1);
                    var (left, right) = rope.Split(index);
                    rope = right.Concat(left);
                    var rotated = model.Skip(index).Concat(model.Take(index)).ToList();
                    model.Clear();
                    model.AddRange(rotated);
                    break;
                }
            }

            rope.ValidateInvariants();
            Assert.Equal(model.Count, rope.Count);
            Assert.Equal(model, rope.ToArray());
        }
    }

    /// <summary>
    /// Verifies repeatedly branching off a single RETAINED rope leaves it intact and yields correct, independent
    /// results — the structural-sharing persistence the finger-tree backbone provides.
    /// </summary>
    [Fact]
    public void BranchingOffRetainedVersion_StaysCorrect()
    {
        var baseValues = Enumerable.Range(0, 10000).ToArray();
        var baseRope = Rope<int>.Create(baseValues);

        for (var i = 0; i < 300; i++)
        {
            var index = (i * 137) % (baseRope.Count + 1);

            var inserted = baseRope.Insert(index, -1);
            Assert.Equal(10001, inserted.Count);
            Assert.Equal(-1, inserted[index]);

            var (left, right) = baseRope.Split(index);
            Assert.Equal(baseValues.Length, left.Concat(right).Count);

            // The retained base is never disturbed by any branch off it.
            Assert.Equal(baseValues.Length, baseRope.Count);
            Assert.Equal(baseValues, baseRope.ToArray());
        }
    }

    /// <summary>Verifies a very large rope stays valid and correctly indexable across chunk boundaries.</summary>
    [Fact]
    public void LargeRope_StaysValidAndIndexable()
    {
        const int size = 1_000_000;
        var rope = Rope<int>.Create(Enumerable.Range(0, size).ToArray());
        rope.ValidateInvariants();
        Assert.Equal(size, rope.Count);
        for (var i = 0; i < size; i += 9973)   // a prime stride across many chunks
            Assert.Equal(i, rope[i]);

        // A mid-buffer splice on a million elements stays cheap and correct.
        var spliced = rope.RemoveRange(400_000, 200_000).InsertRange(400_000, Enumerable.Range(-5, 5).ToArray().AsSpan());
        spliced.ValidateInvariants();
        Assert.Equal(size - 200_000 + 5, spliced.Count);
        Assert.Equal(-5, spliced[400_000]);
        Assert.Equal(600_000, spliced[400_005]);   // the element that followed the removed range
    }
}
