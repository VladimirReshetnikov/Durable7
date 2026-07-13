using System.Reflection;
using BenchmarkDotNet.Configs;
using BenchmarkDotNet.Jobs;
using BenchmarkDotNet.Running;
using BenchmarkDotNet.Toolchains.InProcess.NoEmit;
using Tools.DataStructures.FingerTree.Benchmarks;

if (Axis2C0EvidenceCollector.TryRun(args))
    return;

// Discovers every [Benchmark]-bearing class in this assembly and dispatches on the command line, so a run can
// be scoped with --filter (e.g. --filter *Reverse*) and a job chosen with --job short for a quick docs pass or
// the default job for a full measurement. See README.md in this directory for the recommended commands.
var switcher = BenchmarkSwitcher.FromAssembly(Assembly.GetExecutingAssembly());
if (string.Equals(
        Environment.GetEnvironmentVariable("TDS_BENCHMARK_IN_PROCESS"),
        "1",
        StringComparison.Ordinal))
{
    var config = ManualConfig.Create(DefaultConfig.Instance)
        .AddJob(Job.Default
            .WithToolchain(InProcessNoEmitToolchain.Instance)
            .WithId("SerialInProcess"));
    switcher.Run(args, config);
}
else
{
    switcher.Run(args);
}
