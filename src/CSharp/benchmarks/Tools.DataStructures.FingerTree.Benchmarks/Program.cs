using System.Reflection;
using BenchmarkDotNet.Running;
using Tools.DataStructures.FingerTree.Benchmarks;

if (Axis2C0EvidenceCollector.TryRun(args))
    return;

// Discovers every [Benchmark]-bearing class in this assembly and dispatches on the command line, so a run can
// be scoped with --filter (e.g. --filter *Reverse*) and a job chosen with --job short for a quick docs pass or
// the default job for a full measurement. See README.md in this directory for the recommended commands.
BenchmarkSwitcher.FromAssembly(Assembly.GetExecutingAssembly()).Run(args);
