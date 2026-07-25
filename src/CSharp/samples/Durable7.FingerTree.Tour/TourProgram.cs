using Durable7.FingerTree;
using TextCursor = Durable7.FingerTree.MeasuredRopeCursor<char, int, Durable7.FingerTree.NewlineMeasure>;
using TextRope = Durable7.FingerTree.MeasuredRope<char, int, Durable7.FingerTree.NewlineMeasure>;

namespace Durable7.FingerTree.Tour;

/// <summary>
/// A short end-to-end tour of the persistent finger-tree library built around a text buffer, in three acts:
/// undo/redo over retained measured cursor versions with explicit snapshot boundaries, O(log n) line/column
/// navigation, and a background reader observing a growing buffer lock-free while a writer publishes versions.
/// Exposed as <see cref="Run"/> so it can be driven from a test with a captured writer; it runs a bounded,
/// deterministic scenario and returns.
/// </summary>
public static class TourProgram
{
    /// <summary>Runs the three-act tour, writing a narrated transcript to <paramref name="output"/>.</summary>
    /// <param name="output">Destination for the transcript.</param>
    public static void Run(TextWriter output)
    {
        output.WriteLine("FingerTree tour: a persistent text buffer");
        output.WriteLine("=========================================\n");

        UndoRedoAct(output);
        LineNavigationAct(output);
        ConcurrencyAct(output);

        output.WriteLine("Done. Every version above is immutable and shares structure with its predecessors.");
    }

    private static void UndoRedoAct(TextWriter output)
    {
        output.WriteLine("Act 1 - Undo/redo with retained measured cursors");

        const int snapshotCadence = 16;
        var history = new List<TextCursor> { "".ToTextRope().GetCursor() };
        var historyPosition = 0;
        var cursorEditCount = 0;

        void Apply(int operationCount, Func<TextCursor, TextCursor> edit)
        {
            var next = edit(history[historyPosition]);
            history.RemoveRange(
                historyPosition + 1,
                history.Count - historyPosition - 1);   // a new edit drops the redo branch
            history.Add(next);
            historyPosition++;
            cursorEditCount += operationCount;
        }

        string DisplayCurrent() => history[historyPosition].Snapshot().AsString();
        string Undo() => history[historyPosition = Math.Max(0, historyPosition - 1)].Snapshot().AsString();
        string Redo() => history[historyPosition = Math.Min(history.Count - 1, historyPosition + 1)].Snapshot().AsString();

        Apply(3, cursor => cursor.InsertRange("he").InsertRange("ll").Insert('o'));
        Apply(6, cursor => " world".Aggregate(cursor, static (current, character) => current.Insert(character)));
        Apply(7, cursor =>
        {
            for (var i = 0; i < 6; i++)
                cursor = cursor.MovePrevious();
            return ",\nbrave".Aggregate(cursor, static (current, character) => current.Insert(character));
        });

        output.WriteLine($"  after 3 edits    : {Quote(DisplayCurrent())}");
        output.WriteLine($"  undo             : {Quote(Undo())}");
        output.WriteLine($"  undo             : {Quote(Undo())}");
        output.WriteLine($"  redo             : {Quote(Redo())}");
        output.WriteLine($"  history position : {historyPosition} of {history.Count - 1}");
        output.WriteLine($"  snapshot cadence : {snapshotCadence} cursor edits ({cursorEditCount} before the first display)");
        output.WriteLine($"  retained versions: {history.Count} immutable cursor versions; snapshots occur only for display/commit\n");
    }

    private static void LineNavigationAct(TextWriter output)
    {
        output.WriteLine("Act 2 - O(log n) line and column navigation");

        var doc = "first line\nsecond line\nthird line".ToTextRope();
        var (line, column) = doc.LineColumnOf(14);
        output.WriteLine($"  document         : {Quote(doc.AsString())}");
        output.WriteLine($"  line count       : {doc.LineCount()}");
        output.WriteLine($"  offset 14        : line {line}, column {column}  (char '{doc[14]}')");
        output.WriteLine($"  GetLine(1)       : {Quote(doc.GetLine(1))}");
        output.WriteLine($"  OffsetOf(2, 0)   : {doc.OffsetOf(2, 0)}  (start of line 2)\n");
    }

    private static void ConcurrencyAct(TextWriter output)
    {
        output.WriteLine("Act 3 - A background reader snapshots a growing buffer lock-free");

        var cell = new TextRope[] { "".ToTextRope() };   // one shared, atomically published version reference
        var done = new bool[] { false };
        var consoleLock = new object();
        var samples = 0;        // read after Join, which is a barrier
        var peakLines = 0;

        var reader = new Thread(() =>
        {
            var nextThreshold = 100;
            while (!Volatile.Read(ref done[0]))
            {
                var snapshot = Volatile.Read(ref cell[0]);   // a complete, immutable version -- never torn
                var lines = snapshot.LineCount();
                samples++;
                if (lines > peakLines)
                    peakLines = lines;
                if (lines >= nextThreshold)
                {
                    lock (consoleLock)
                        output.WriteLine($"  [reader] saw {lines,4} lines / {snapshot.Count,6} chars  (consistent lock-free snapshot)");
                    nextThreshold += 100;
                }
            }
        });
        reader.Start();

        var current = "".ToTextRope();
        for (var i = 0; i < 500; i++)
        {
            current = current.InsertRange(current.Count, $"log line {i}\n".AsSpan());
            Volatile.Write(ref cell[0], current);   // publish atomically; the reader only ever sees a whole version
        }

        Volatile.Write(ref done[0], true);
        reader.Join();

        var final = Volatile.Read(ref cell[0]);
        lock (consoleLock)
        {
            output.WriteLine($"  [writer] published {final.LineCount() - 1} lines; final buffer {final.Count} chars");
            output.WriteLine($"  [reader] took {samples:N0} lock-free snapshots (peak {peakLines} lines) -- no locks, no torn reads\n");
        }
    }

    private static string Quote(string text) => "\"" + text.Replace("\n", "\\n") + "\"";
}
