// Mutable staging for transferring exact-length chunks into immutable ropes.

namespace Durable7.FingerTree;

using System.Runtime.CompilerServices;

/// <summary>Mutable staging for transferring exact-length chunks into immutable ropes.</summary>
internal sealed class ChunkBuilder<T>
{
    private readonly List<Chunk<T>> _fullChunks = [];
    private T[] _tail = new T[RopeChunking.MaxChunkSize];
    private int _tailLength;

    /// <summary>Gets the number of elements in the chunk.</summary>
    public int Count { get; private set; }

    /// <summary>Returns a chunk containing the given element.</summary>
    public void Add(T item)
    {
        if (Count == int.MaxValue)
            throw new OverflowException("The rope builder cannot contain more than Int32.MaxValue elements.");
        if (_tailLength == RopeChunking.MaxChunkSize)
            RetireFullTail();

        _tail[_tailLength++] = item;
        Count++;
    }

    /// <summary>Returns a chunk containing the given element.</summary>
    public void Add(ReadOnlySpan<T> items)
    {
        if (items.Length == 0)
            return;
        if (items.Length > int.MaxValue - Count)
            throw new OverflowException("The rope builder cannot contain more than Int32.MaxValue elements.");

        while (!items.IsEmpty)
        {
            if (_tailLength == RopeChunking.MaxChunkSize)
                RetireFullTail();

            var take = Math.Min(items.Length, RopeChunking.MaxChunkSize - _tailLength);
            items[..take].CopyTo(_tail.AsSpan(_tailLength));
            _tailLength += take;
            Count += take;
            items = items[take..];
        }
    }

    /// <summary>Returns an empty chunk retaining the same policies.</summary>
    public void Clear()
    {
        _fullChunks.Clear();
        if (RuntimeHelpers.IsReferenceOrContainsReferences<T>() && _tailLength > 0)
            Array.Clear(_tail, 0, _tailLength);
        _tailLength = 0;
        Count = 0;
    }

    /// <summary>
    /// Freezes the accumulated chunks into their immutable form, ending the builder's exclusive ownership of them.
    /// </summary>
    public Chunk<T>[] FreezeChunks()
    {
        if (Count == 0)
            return [];

        var resultLength = _fullChunks.Count + (_tailLength == 0 ? 0 : 1);
        var result = new Chunk<T>[resultLength];
        for (var i = 0; i < _fullChunks.Count; i++)
            result[i] = _fullChunks[i];

        if (_tailLength > 0)
        {
            var final = new T[_tailLength];
            _tail.AsSpan(0, _tailLength).CopyTo(final);
            result[^1] = new Chunk<T>(final);
        }

        _fullChunks.Clear();
        _tail = new T[RopeChunking.MaxChunkSize];
        _tailLength = 0;
        Count = 0;
        return result;
    }

    private void RetireFullTail()
    {
        _fullChunks.Add(new Chunk<T>(_tail));
        _tail = new T[RopeChunking.MaxChunkSize];
        _tailLength = 0;
    }
}

/// <summary>Mutable staging for measured rope chunks with a live monoidal aggregate.</summary>
internal sealed class MeasuredChunkBuilder<T, TMeasure, TMeasureOps>
    where TMeasureOps : IMeasure<T, TMeasure>
{
    private readonly List<MeasuredChunk<T, TMeasure, TMeasureOps>> _fullChunks = [];
    private T[] _tail = new T[RopeChunking.MaxChunkSize];
    private int _tailLength;
    private TMeasure _tailMeasure = TMeasureOps.Empty;

    public int Count { get; private set; }

    public TMeasure Measure { get; private set; } = TMeasureOps.Empty;

    public void Add(T item)
    {
        if (Count == int.MaxValue)
            throw new OverflowException("The measured rope builder cannot contain more than Int32.MaxValue elements.");
        if (_tailLength == RopeChunking.MaxChunkSize)
            RetireFullTail();

        var itemMeasure = TMeasureOps.Measure(item);
        var tailMeasure = TMeasureOps.Combine(_tailMeasure, itemMeasure);
        var measure = TMeasureOps.Combine(Measure, itemMeasure);

        // Publish only after every user-supplied measure operation has succeeded. Advancing the
        // tail length first would leave a ghost element behind when Measure or Combine throws.
        _tail[_tailLength] = item;
        _tailLength++;
        _tailMeasure = tailMeasure;
        Measure = measure;
        Count++;
    }

    public void Add(ReadOnlySpan<T> items)
    {
        if (items.Length == 0)
            return;
        if (items.Length > int.MaxValue - Count)
            throw new OverflowException("The measured rope builder cannot contain more than Int32.MaxValue elements.");

        while (!items.IsEmpty)
        {
            if (_tailLength == RopeChunking.MaxChunkSize)
                RetireFullTail();

            var take = Math.Min(items.Length, RopeChunking.MaxChunkSize - _tailLength);
            var added = items[..take];
            var tailMeasure = _tailMeasure;
            var measure = Measure;
            foreach (var item in added)
            {
                var itemMeasure = TMeasureOps.Measure(item);
                tailMeasure = TMeasureOps.Combine(tailMeasure, itemMeasure);
                measure = TMeasureOps.Combine(measure, itemMeasure);
            }

            added.CopyTo(_tail.AsSpan(_tailLength));
            _tailLength += take;
            _tailMeasure = tailMeasure;
            Count += take;
            Measure = measure;
            items = items[take..];
        }
    }

    public void Clear()
    {
        _fullChunks.Clear();
        if (RuntimeHelpers.IsReferenceOrContainsReferences<T>() && _tailLength > 0)
            Array.Clear(_tail, 0, _tailLength);
        _tailLength = 0;
        _tailMeasure = TMeasureOps.Empty;
        Count = 0;
        Measure = TMeasureOps.Empty;
    }

    public MeasuredChunk<T, TMeasure, TMeasureOps>[] FreezeChunks()
    {
        if (Count == 0)
            return [];

        var resultLength = _fullChunks.Count + (_tailLength == 0 ? 0 : 1);
        var result = new MeasuredChunk<T, TMeasure, TMeasureOps>[resultLength];
        for (var i = 0; i < _fullChunks.Count; i++)
            result[i] = _fullChunks[i];

        if (_tailLength > 0)
        {
            var final = new T[_tailLength];
            _tail.AsSpan(0, _tailLength).CopyTo(final);
            result[^1] = new MeasuredChunk<T, TMeasure, TMeasureOps>(new Chunk<T>(final), _tailMeasure);
        }

        _fullChunks.Clear();
        _tail = new T[RopeChunking.MaxChunkSize];
        _tailLength = 0;
        _tailMeasure = TMeasureOps.Empty;
        Count = 0;
        Measure = TMeasureOps.Empty;
        return result;
    }

    private void RetireFullTail()
    {
        _fullChunks.Add(new MeasuredChunk<T, TMeasure, TMeasureOps>(new Chunk<T>(_tail), _tailMeasure));
        _tail = new T[RopeChunking.MaxChunkSize];
        _tailLength = 0;
        _tailMeasure = TMeasureOps.Empty;
    }
}
