// A short end-to-end tour of the persistent finger-tree library, built around a text buffer:
//   Act 1  undo/redo as a cursor over O(1) snapshots (structural sharing)
//   Act 2  O(log n) line and column navigation (the newline measure)
//   Act 3  a background reader snapshotting a growing buffer lock-free, while a writer publishes versions
// It runs a bounded, deterministic scenario and exits 0 -- nothing here loops forever.

using Tools.DataStructures.FingerTree;
using TextRope = Tools.DataStructures.FingerTree.MeasuredRope<char, int, Tools.DataStructures.FingerTree.NewlineMeasure>;

Console.WriteLine("FingerTree tour: a persistent text buffer");
Console.WriteLine("=========================================\n");

// ---- Act 1: undo / redo over O(1) snapshots ---------------------------------------------------------
Console.WriteLine("Act 1 - Undo/redo as a cursor over retained snapshots");

var history = new List<TextRope> { "".ToTextRope() };
var cursor = 0;

void Apply(Func<TextRope, TextRope> edit)
{
    var next = edit(history[cursor]);
    history.RemoveRange(cursor + 1, history.Count - cursor - 1);   // a new edit drops the redo branch
    history.Add(next);
    cursor++;
}

string Current() => history[cursor].AsString();
string Undo() => history[cursor = Math.Max(0, cursor - 1)].AsString();
string Redo() => history[cursor = Math.Min(history.Count - 1, cursor + 1)].AsString();

Apply(buffer => buffer.InsertRange(buffer.Count, "hello".AsSpan()));
Apply(buffer => buffer.InsertRange(buffer.Count, " world".AsSpan()));
Apply(buffer => buffer.InsertRange(5, ",\nbrave".AsSpan()));
Console.WriteLine($"  after 3 edits    : {Quote(Current())}");
Console.WriteLine($"  undo             : {Quote(Undo())}");
Console.WriteLine($"  undo             : {Quote(Undo())}");
Console.WriteLine($"  redo             : {Quote(Redo())}");
Console.WriteLine($"  retained versions: {history.Count} (each snapshot is an O(1) reference, not a copy)\n");

// ---- Act 2: O(log n) line and column navigation -----------------------------------------------------
Console.WriteLine("Act 2 - O(log n) line and column navigation");

var doc = "first line\nsecond line\nthird line".ToTextRope();
var (line, column) = doc.LineColumnOf(14);
Console.WriteLine($"  document         : {Quote(doc.AsString())}");
Console.WriteLine($"  line count       : {doc.LineCount()}");
Console.WriteLine($"  offset 14        : line {line}, column {column}  (char '{doc[14]}')");
Console.WriteLine($"  GetLine(1)       : {Quote(doc.GetLine(1))}");
Console.WriteLine($"  OffsetOf(2, 0)   : {doc.OffsetOf(2, 0)}  (start of line 2)\n");

// ---- Act 3: lock-free concurrent reading while editing ----------------------------------------------
Console.WriteLine("Act 3 - A background reader snapshots a growing buffer lock-free");

var cell = new TextRope[] { "".ToTextRope() };   // one shared, atomically published version reference
var done = new bool[] { false };
var consoleLock = new object();
var samples = 0;        // how many lock-free snapshots the reader took (read after Join, which is a barrier)
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
                Console.WriteLine($"  [reader] saw {lines,4} lines / {snapshot.Count,6} chars  (consistent lock-free snapshot)");
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
    Console.WriteLine($"  [writer] published {final.LineCount() - 1} lines; final buffer {final.Count} chars");
    Console.WriteLine($"  [reader] took {samples:N0} lock-free snapshots (peak {peakLines} lines) -- no locks, no torn reads\n");
}

Console.WriteLine("Done. Every version above is immutable and shares structure with its predecessors.");

static string Quote(string text) => "\"" + text.Replace("\n", "\\n") + "\"";
