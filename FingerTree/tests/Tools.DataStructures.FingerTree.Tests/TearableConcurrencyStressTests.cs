using System.Collections.Concurrent;
using System.Diagnostics;
using System.Globalization;
using Tools.DataStructures.FingerTree;
using Xunit;

namespace Tools.DataStructures.FingerTree.Tests;

/// <summary>
/// Long-running, non-deterministic concurrency stress tests that use a deliberately <em>tearable</em> element
/// type — a 32-byte struct whose four fields must always agree — so any torn read is directly observable. These
/// drive the library's claim that immutable, atomically-published versions are safe to read from many threads at
/// once, including the lazy-memoized spine and a large struct used as both element and measure.
/// </summary>
/// <remarks>
/// The duration-bounded tests honor the <c>FINGERTREE_STRESS_SECONDS</c> environment variable (default 2 seconds)
/// so a soak run can be requested without changing code: e.g. <c>FINGERTREE_STRESS_SECONDS=60 dotnet test</c>.
/// Every test fails loudly if any reader ever observes a non-intact value, a discontinuous sequence, a count
/// mismatch, or an exception.
/// </remarks>
public sealed class TearableConcurrencyStressTests
{
    private static TimeSpan StressDuration()
    {
        var raw = Environment.GetEnvironmentVariable("FINGERTREE_STRESS_SECONDS");
        return double.TryParse(raw, NumberStyles.Float, CultureInfo.InvariantCulture, out var seconds) && seconds > 0
            ? TimeSpan.FromSeconds(seconds)
            : TimeSpan.FromSeconds(2);
    }

    /// <summary>
    /// Many threads read every element of one shared <c>Rope&lt;Tearable&gt;</c> repeatedly. Because the rope is
    /// immutable, no element slot is ever rewritten, so every read must be intact and equal to its index.
    /// </summary>
    [Fact]
    public void ConcurrentReads_RopeOfTearable_NeverTear()
    {
        const int n = 20_000;
        var rope = Rope<Tearable>.CreateRange(Enumerable.Range(0, n).Select(i => new Tearable(i)));
        var failures = new ConcurrentQueue<string>();

        Parallel.For(0, 64, _ =>
        {
            for (var pass = 0; pass < 3; pass++)
                for (var i = 0; i < n; i++)
                {
                    var value = rope[i];
                    if (!value.IsIntact || value.Value != i)
                        failures.Enqueue($"index {i}: {value}");
                }
        });

        Assert.Empty(failures);
    }

    /// <summary>
    /// Many threads take the FIRST reads of a freshly built measured rope whose element <em>and</em> measure are
    /// the tearable struct. First reads force the lazy measure boxes concurrently; the boxed measure and the
    /// immutable element storage must both come back fully intact.
    /// </summary>
    [Fact]
    public void ConcurrentFirstReads_MeasuredRopeOfTearable_MeasureAndElementsIntact()
    {
        const int n = 1 << 15;
        var expectedSum = (long)n * (n - 1) / 2;
        var fresh = MeasuredRope<Tearable, Tearable, TearableSumMeasure>.CreateRange(
            Enumerable.Range(0, n).Select(i => new Tearable(i)));   // intentionally not read before the race
        var failures = new ConcurrentQueue<string>();

        Parallel.For(0, 48, _ =>
        {
            var measure = fresh.Measure;   // forces the lazy memoized measure boxes down the spine, concurrently
            if (!measure.IsIntact || measure.Value != expectedSum)
                failures.Enqueue($"measure: {measure}");

            var array = fresh.ToArray();
            for (var i = 0; i < array.Length; i++)
                if (!array[i].IsIntact || array[i].Value != i)
                {
                    failures.Enqueue($"element {i}: {array[i]}");
                    break;
                }
        });

        Assert.Empty(failures);
    }

    /// <summary>
    /// Lock-free publication under load: one writer maintains a sliding window of tearable elements and publishes
    /// each new version through a single volatile reference, while readers continuously snapshot and validate.
    /// Every observed snapshot must be fully intact and strictly contiguous — a torn element or a torn structure
    /// would break one of those checks.
    /// </summary>
    [Fact]
    public async Task LockFreePublication_Tearable_SnapshotsStayIntact()
    {
        var duration = StressDuration();
        const int window = 8_000;
        var cell = new[] { Rope<Tearable>.Empty };
        var stop = new bool[1];
        var failures = new ConcurrentQueue<string>();

        var writer = Task.Run(() =>
        {
            var current = Rope<Tearable>.Empty;
            long value = 0;
            var stopwatch = Stopwatch.StartNew();
            while (stopwatch.Elapsed < duration)
            {
                current = current.AddLast(new Tearable(value++));
                if (current.Count > window)
                    current = current.RemoveFirst();
                Volatile.Write(ref cell[0], current);
            }

            Volatile.Write(ref stop[0], true);
        });

        var readers = Enumerable.Range(0, 4).Select(_ => Task.Run(() =>
        {
            while (!Volatile.Read(ref stop[0]))
            {
                var snapshot = Volatile.Read(ref cell[0]);
                var array = snapshot.ToArray();
                for (var k = 0; k < array.Length; k++)
                {
                    if (!array[k].IsIntact)
                    {
                        failures.Enqueue($"torn element at {k}: {array[k]}");
                        break;
                    }

                    if (k > 0 && array[k].Value != array[k - 1].Value + 1)
                    {
                        failures.Enqueue($"discontinuity at {k}: {array[k - 1].Value} then {array[k].Value}");
                        break;
                    }
                }
            }
        })).ToArray();

        await Task.WhenAll([writer, .. readers]);
        Assert.Empty(failures);
    }

    /// <summary>
    /// The headline soak: many workers concurrently branch random edit histories off one retained shared base
    /// while also reading that base, for the configured duration. Each branch is validated against an independent
    /// model copy, the retained base must stay byte-for-byte unchanged and intact throughout, and no operation
    /// may throw. This exercises persistence, structural sharing, the lazy spine, and element integrity together
    /// under genuinely non-deterministic interleaving.
    /// </summary>
    [Fact]
    public async Task LongRunning_BranchingAndReading_Tearable_StaysConsistent()
    {
        var duration = StressDuration();
        const int baseSize = 512;
        var baseRope = Rope<Tearable>.CreateRange(Enumerable.Range(0, baseSize).Select(i => new Tearable(i)));
        var baseModel = Enumerable.Range(0, baseSize).Select(i => (long)i).ToArray();
        var failures = new ConcurrentQueue<string>();

        var workers = Enumerable.Range(0, Math.Max(4, Environment.ProcessorCount)).Select(workerId => Task.Run(() =>
        {
            var rng = new Random(workerId * 7919 + 17);
            var next = (long)(workerId + 1) * 1_000_000_000L;   // per-worker disjoint value space
            var stopwatch = Stopwatch.StartNew();

            try
            {
                while (stopwatch.Elapsed < duration)
                {
                    // The retained shared base is read by everyone and must never change or tear.
                    for (var probe = 0; probe < 16; probe++)
                    {
                        var i = rng.Next(baseSize);
                        var value = baseRope[i];
                        if (!value.IsIntact || value.Value != i)
                            failures.Enqueue($"base tear at {i}: {value}");
                    }

                    // Branch a fresh random edit history off the retained base, mirrored in a model copy.
                    var rope = baseRope;
                    var model = new List<long>(baseModel);
                    var ops = rng.Next(3, 12);
                    for (var step = 0; step < ops; step++)
                    {
                        var n = model.Count;
                        switch (rng.Next(8))
                        {
                            case 0: { var x = next++; rope = rope.AddFirst(new Tearable(x)); model.Insert(0, x); break; }
                            case 1: { var x = next++; rope = rope.AddLast(new Tearable(x)); model.Add(x); break; }
                            case 2 when n > 0: rope = rope.RemoveFirst(); model.RemoveAt(0); break;
                            case 3 when n > 0: rope = rope.RemoveLast(); model.RemoveAt(n - 1); break;
                            case 4: { var i = rng.Next(n + 1); var x = next++; rope = rope.Insert(i, new Tearable(x)); model.Insert(i, x); break; }
                            case 5 when n > 0: { var i = rng.Next(n); rope = rope.RemoveAt(i); model.RemoveAt(i); break; }
                            case 6 when n > 0: { var i = rng.Next(n); var x = next++; rope = rope.SetItem(i, new Tearable(x)); model[i] = x; break; }
                            default: { var i = rng.Next(n + 1); var (left, right) = rope.Split(i); rope = left.Concat(right); break; }
                        }
                    }

                    rope.ValidateInvariants();
                    if (rope.Count != model.Count)
                    {
                        failures.Enqueue($"branch count {rope.Count} != model {model.Count}");
                        continue;
                    }

                    var array = rope.ToArray();
                    for (var k = 0; k < array.Length; k++)
                        if (!array[k].IsIntact || array[k].Value != model[k])
                        {
                            failures.Enqueue($"branch mismatch at {k}: {array[k]} vs {model[k]}");
                            break;
                        }
                }
            }
            catch (Exception ex)
            {
                failures.Enqueue(ex.ToString());
            }
        })).ToArray();

        await Task.WhenAll(workers);

        Assert.Empty(failures);

        // The retained base is intact and unchanged after all the concurrent branching.
        Assert.Equal(baseSize, baseRope.Count);
        for (var i = 0; i < baseSize; i++)
        {
            var value = baseRope[i];
            Assert.True(value.IsIntact, $"base element {i} torn: {value}");
            Assert.Equal(i, value.Value);
        }
    }

    /// <summary>
    /// A 32-byte value whose four longs are written together and must stay equal. Reading it from a concurrently
    /// mutated location would copy the fields non-atomically and expose a mix of two writes (a "tear"); reading it
    /// from immutable, atomically-published storage never can. <see cref="IsIntact"/> is the tear detector.
    /// </summary>
    private readonly struct Tearable(long value)
    {
        public readonly long A = value, B = value, C = value, D = value;

        public long Value => A;

        public bool IsIntact => A == B && B == C && C == D;

        public override string ToString() => $"Tearable(A={A}, B={B}, C={C}, D={D})";
    }

    /// <summary>A monoidal sum measure over <see cref="Tearable"/>: combine adds field-wise, so a correct combine
    /// of intact values is itself intact, and the whole-rope measure is the sum of element values.</summary>
    private readonly struct TearableSumMeasure : IMeasure<Tearable, Tearable>
    {
        public static Tearable Empty => new(0);

        public static Tearable Measure(Tearable value) => value;

        public static Tearable Combine(Tearable left, Tearable right) => new(left.Value + right.Value);
    }
}
