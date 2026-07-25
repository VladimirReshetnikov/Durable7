using Durable7.FingerTree;
using Xunit;

namespace Durable7.FingerTree.Tests;

/// <summary>
/// Verifies representative generated operation histories against a simple <see cref="List{T}"/> model.
/// </summary>
public sealed class FingerTreeDequeRandomizedModelTests
{
    /// <summary>
    /// Runs deterministic mixed endpoint, indexed, split, and concatenation histories against a list model.
    /// </summary>
    /// <param name="seed">Deterministic random seed.</param>
    [Theory]
    [InlineData(0)]
    [InlineData(1)]
    [InlineData(2)]
    [InlineData(3)]
    [InlineData(4)]
    [InlineData(5)]
    [InlineData(6)]
    [InlineData(7)]
    public void MixedOperationHistories_MatchListModel(int seed)
    {
        var random = new Random(seed);
        var model = new List<int>();
        var deque = FingerTreeDeque<int>.Empty;

        for (var step = 0; step < 80; step++)
        {
            var operation = random.Next(10);
            switch (operation)
            {
                case 0:
                    AddFirst(random.Next(1000));
                    break;
                case 1:
                    AddLast(random.Next(1000));
                    break;
                case 2 when model.Count > 0:
                    RemoveFirst();
                    break;
                case 3 when model.Count > 0:
                    RemoveLast();
                    break;
                case 4:
                    InsertAt(random.Next(model.Count + 1), random.Next(1000));
                    break;
                case 5 when model.Count > 0:
                    RemoveAt(random.Next(model.Count));
                    break;
                case 6 when model.Count > 0:
                    SetItem(random.Next(model.Count), random.Next(1000));
                    break;
                case 7 when model.Count > 0:
                    SplitAndConcat(random.Next(model.Count + 1));
                    break;
                case 8:
                    AddRange(random.Next(4));
                    break;
                default:
                    Verify();
                    break;
            }

            Verify();
        }

        void AddFirst(int value)
        {
            model.Insert(0, value);
            deque = deque.AddFirst(value);
        }

        void AddLast(int value)
        {
            model.Add(value);
            deque = deque.AddLast(value);
        }

        void RemoveFirst()
        {
            model.RemoveAt(0);
            deque = deque.RemoveFirst();
        }

        void RemoveLast()
        {
            model.RemoveAt(model.Count - 1);
            deque = deque.RemoveLast();
        }

        void InsertAt(int index, int value)
        {
            model.Insert(index, value);
            deque = deque.InsertAt(index, value);
        }

        void RemoveAt(int index)
        {
            model.RemoveAt(index);
            deque = deque.RemoveAt(index);
        }

        void SetItem(int index, int value)
        {
            model[index] = value;
            deque = deque.SetItem(index, value);
        }

        void SplitAndConcat(int index)
        {
            var split = deque.SplitAt(index);
            deque = split.Left.Concat(split.Right);
        }

        void AddRange(int length)
        {
            var values = Enumerable.Range(0, length).Select(_ => random.Next(1000)).ToArray();
            model.AddRange(values);
            deque = deque.AddRange(values);
        }

        void Verify() => FingerTreeDequeAssert.SequenceEqual(model, deque);
    }
}
