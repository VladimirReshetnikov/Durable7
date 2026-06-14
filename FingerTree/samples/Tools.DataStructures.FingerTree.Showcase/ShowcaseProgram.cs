using Tools.DataStructures.FingerTree;

namespace Tools.DataStructures.FingerTree.Showcase;

/// <summary>
/// A runnable showcase of the measure-driven structures: the same general measured finger tree becomes a
/// priority queue, a weighted-sampling structure, an order-statistic sorted set, and an interval index, purely
/// by choice of measure. Exposed as <see cref="Run"/> so it can be driven from a test with a captured writer.
/// </summary>
public static class ShowcaseProgram
{
    /// <summary>Runs the four-act showcase, writing a narrated transcript to <paramref name="output"/>.</summary>
    /// <param name="output">Destination for the transcript.</param>
    /// <param name="seed">Seed for the (otherwise reproducible) random acts.</param>
    public static void Run(TextWriter output, int seed = 20260614)
    {
        output.WriteLine("FingerTree showcase: one measured tree, many data structures");
        output.WriteLine("============================================================\n");

        PriorityQueueAct(output);
        WeightedSamplingAct(output, seed);
        OrderStatisticAct(output, seed);
        IntervalAct(output);
        ReversibleDequeAct(output);
        SortedMapAct(output);

        output.WriteLine("Done. Every structure above is a persistent finger tree; the measured ones differ only by their measure.");
    }

    private static void PriorityQueueAct(TextWriter output)
    {
        output.WriteLine("Act 1 - Meldable priority queue (minimum-priority measure)");
        var queue = PriorityQueue<string, int>.Empty
            .Enqueue("deploy", 3).Enqueue("alert", 1).Enqueue("backup", 5).Enqueue("build", 2);
        var more = PriorityQueue<string, int>.Empty.Enqueue("hotfix", 0).Enqueue("report", 4);
        queue = queue.Meld(more);   // O(log min) structural meld

        output.Write("  drain order  : ");
        var first = true;
        while (queue.TryDequeue(out var task, out var priority, out queue))
        {
            output.Write($"{(first ? "" : ", ")}{task}({priority})");
            first = false;
        }

        output.WriteLine("\n");
    }

    private static void WeightedSamplingAct(TextWriter output, int seed)
    {
        output.WriteLine("Act 2 - Weighted random sampling (cumulative-sum measure)");
        int[] weights = [5, 1, 4];
        string[] names = ["apple", "fig", "date"];
        var tree = ProductMeasures.CreateSizeAndSum(weights);
        var total = weights.Sum();
        var random = new Random(seed);
        const int draws = 100_000;
        var counts = new int[weights.Length];
        for (var i = 0; i < draws; i++)
            if (tree.TrySelectByCumulativeWeight(random.Next(total), out _, out var index))
                counts[index]++;

        for (var i = 0; i < weights.Length; i++)
            output.WriteLine(FormattableString.Invariant(   // invariant so the percentages read the same in any culture
                $"  {names[i],-6} weight {weights[i]} ({100.0 * weights[i] / total,5:0.0}% expected) -> sampled {100.0 * counts[i] / draws,5:0.0}%"));
        output.WriteLine();
    }

    private static void OrderStatisticAct(TextWriter output, int seed)
    {
        output.WriteLine("Act 3 - Order-statistic sorted set");
        var random = new Random(seed + 1);
        var values = Enumerable.Range(0, 15).Select(_ => random.Next(0, 100)).ToArray();
        var set = SortedSet<int>.CreateRange(values);
        var median = set[set.Count / 2];
        output.WriteLine($"  {set.Count} distinct of 15 : min {set.Min}, median {median}, max {set.Max}");
        output.WriteLine($"  rank of median {median}  : index {set.IndexOf(median)} (0-based)");
        output.WriteLine($"  values within 20..60  : {set.GetRange(20, 60).Count}");
        var other = SortedSet<int>.CreateRange(Enumerable.Range(0, 15).Select(_ => random.Next(0, 100)));
        output.WriteLine($"  |A union B| = {set.Union(other).Count},  |A intersect B| = {set.Intersect(other).Count}\n");
    }

    private static void IntervalAct(TextWriter output)
    {
        output.WriteLine("Act 4 - Interval index (maximum-endpoint measure, overlap queries)");
        var intervals = IntervalTree<int>.Empty
            .Insert(1, 5).Insert(3, 8).Insert(10, 15).Insert(12, 20).Insert(22, 25);
        foreach (var (low, high) in new[] { (2, 4), (11, 13), (21, 23) })
        {
            var hits = intervals.FindOverlaps(new Interval<int>(low, high)).OrderBy(interval => interval.Low).ToList();
            var rendered = hits.Count == 0 ? "none" : string.Join(", ", hits.Select(interval => $"[{interval.Low},{interval.High}]"));
            output.WriteLine($"  overlapping [{low},{high}]  : {rendered}");
        }

        output.WriteLine();
    }

    private static void ReversibleDequeAct(TextWriter output)
    {
        output.WriteLine("Act 5 - Reversible deque (O(1) reverse via a per-node orientation bit)");
        var deque = ReversibleDeque<int>.CreateRange(Enumerable.Range(1, 8));
        var reversed = deque.Reverse();   // O(1): flips an orientation bit and shares all structure
        output.WriteLine($"  forward  : {Render(deque)}   (first {deque.First}, last {deque.Last})");
        output.WriteLine($"  reversed : {Render(reversed)}   (first {reversed.First}, last {reversed.Last})  -- O(1), shared structure");
        output.WriteLine($"  d ++ rev : {Render(deque.Concat(reversed))}   (mixed-orientation concat stays O(log min))\n");
    }

    private static void SortedMapAct(TextWriter output)
    {
        output.WriteLine("Act 6 - Navigable sorted map (order-statistic + floor/ceiling)");
        var map = SortedDictionary<int, string>.CreateRange(
        [
            new(10, "ten"), new(25, "twenty-five"), new(40, "forty"), new(55, "fifty-five"), new(70, "seventy"),
        ]);
        var median = map.EntryAt(map.Count / 2);
        output.WriteLine($"  count {map.Count}, min key {map.MinEntry.Key}, max key {map.MaxEntry.Key}");
        output.WriteLine($"  this[40]            : {map[40]}");
        output.WriteLine($"  median entry (k={map.Count / 2}) : {median.Key} -> {median.Value}");
        map.TryFloorEntry(50, out var floor);
        map.TryCeilingEntry(50, out var ceiling);
        output.WriteLine($"  floor(50)           : {floor.Key} -> {floor.Value}");
        output.WriteLine($"  ceiling(50)         : {ceiling.Key} -> {ceiling.Value}\n");
    }

    private static string Render(ReversibleDeque<int> deque) =>
        string.Join(" ", Enumerable.Range(0, deque.Count).Select(index => deque[index]));
}
