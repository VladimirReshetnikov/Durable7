using System.ComponentModel;
using System.Diagnostics;
using System.Reflection;
using System.Xml.Linq;
using Xunit;

namespace Durable7.Ordered.Tests;

/// <summary>Executable guards for Ordered ownership, public-substrate use, and Tungsten isolation.</summary>
public sealed class OrderedDependencyBoundaryTests
{
    /// <summary>Verifies product and test manifests contain only the approved project-reference edges.</summary>
    [Fact]
    public void ProjectManifests_HaveOnlyApprovedRepositoryDependencies()
    {
        var root = RepositoryRoot();
        var csharpDirectory = Path.Combine(root, "src", "CSharp");
        var productProject = Path.Combine(root, "src", "CSharp", "src", "Durable7.Ordered", "Durable7.Ordered.csproj");
        var testProject = Path.Combine(root, "src", "CSharp", "tests", "Durable7.Ordered.Tests", "Durable7.Ordered.Tests.csproj");
        var buildProps = Path.Combine(csharpDirectory, "Directory.Build.props");
        var buildTargets = Path.Combine(csharpDirectory, "Directory.Build.targets");

        Assert.Equal(
            new[] { "Durable7.FingerTree.csproj", "Durable7.Hamt.csproj" },
            ProjectReferences(productProject).Select(ProjectFileName).OrderBy(name => name));
        Assert.Equal(
            new[] { "Durable7.Ordered.csproj" },
            ProjectReferences(testProject).Select(ProjectFileName));

        var productDocument = XDocument.Load(productProject);
        var testDocument = XDocument.Load(testProject);
        AssertPackageReferences(productDocument);
        AssertPackageReferences(
            testDocument,
            ("CsCheck", "4.7.0"),
            ("Microsoft.NET.Test.Sdk", "17.13.0"),
            ("xunit", "2.9.3"),
            ("xunit.runner.visualstudio", "2.8.2"));

        foreach (var document in new[] { productDocument, testDocument })
        {
            Assert.Equal("Microsoft.NET.Sdk", document.Root!.Attribute("Sdk")?.Value);
            AssertNoSourceInjectionRoutes(document);
            Assert.Empty(document.Descendants("Compile"));
            foreach (var reference in document.Descendants("ProjectReference"))
            {
                Assert.Equal(new[] { "Include" }, reference.Attributes().Select(attribute => attribute.Name.LocalName));
                Assert.Empty(reference.Elements());
            }
            Assert.DoesNotContain(
                document.Descendants().Attributes("Include"),
                attribute => attribute.Value.Contains("Tungsten", StringComparison.OrdinalIgnoreCase));
        }

        var propsDocument = XDocument.Load(buildProps);
        AssertNoSourceInjectionRoutes(propsDocument);
        Assert.Empty(propsDocument.Descendants("Compile"));
        Assert.Empty(propsDocument.Descendants("ProjectReference"));
        Assert.Empty(propsDocument.Descendants("PackageReference"));

        var targetsDocument = XDocument.Load(buildTargets);
        AssertNoSourceInjectionRoutes(targetsDocument);
        Assert.Empty(targetsDocument.Descendants("ProjectReference"));
        Assert.Empty(targetsDocument.Descendants("PackageReference"));
        var sharedCompile = Assert.Single(targetsDocument.Descendants("Compile"));
        Assert.Equal(
            "$(MSBuildThisFileDirectory)tests\\TestInfrastructure\\HeadlessTestProcess.cs",
            sharedCompile.Attribute("Include")?.Value);
        Assert.Equal("TestInfrastructure\\HeadlessTestProcess.cs", sharedCompile.Attribute("Link")?.Value);
        Assert.Equal(new[] { "Include", "Link" }, sharedCompile.Attributes().Select(attribute => attribute.Name.LocalName));
    }

    /// <summary>Verifies Ordered alone grants its tests internal access and foundations grant it none.</summary>
    [Fact]
    public void InternalsVisibleTo_ExistsOnlyAtTheOrderedOwnedTestSeam()
    {
        var root = RepositoryRoot();
        var ordered = File.ReadAllText(Path.Combine(root, "src", "CSharp", "src", "Durable7.Ordered", "Durable7.Ordered.csproj"));
        Assert.Contains("InternalsVisibleTo Include=\"Durable7.Ordered.Tests\"", ordered, StringComparison.Ordinal);
        Assert.DoesNotContain("Tungsten", ordered, StringComparison.OrdinalIgnoreCase);

        foreach (var provider in new[] { "Durable7.Hamt", "Durable7.FingerTree" })
        {
            var text = File.ReadAllText(Path.Combine(root, "src", "CSharp", "src", provider, $"{provider}.csproj"));
            Assert.DoesNotContain("InternalsVisibleTo Include=\"Durable7.Ordered", text, StringComparison.Ordinal);
        }
    }

    /// <summary>Verifies compiled product and test assemblies have no Tungsten reference.</summary>
    [Fact]
    public void CompiledAssemblies_DoNotReferenceTungsten()
    {
        foreach (var assembly in new[]
                 {
                     typeof(PersistentOrderedSet<>).Assembly,
                     typeof(OrderedDependencyBoundaryTests).Assembly,
                 })
        {
            Assert.DoesNotContain(
                assembly.GetReferencedAssemblies(),
                reference => reference.Name?.Contains("Tungsten", StringComparison.OrdinalIgnoreCase) == true);
        }
    }

    /// <summary>Verifies no exported Ordered signature leaks a substrate or application-owned type.</summary>
    [Fact]
    public void PublicSurface_LeaksNoFoundationOrTungstenTypes()
    {
        var assembly = typeof(PersistentOrderedSet<>).Assembly;
        Assert.Equal(
            new[]
            {
                typeof(PersistentOrderedMap<,>).FullName,
                typeof(PersistentOrderedMapCursor<,>).FullName,
                typeof(PersistentOrderedMultimap<,>).FullName,
                typeof(PersistentOrderedMultimapCursor<,>).FullName,
                typeof(PersistentOrderedSet<>).FullName,
                typeof(PersistentOrderedSetCursor<>).FullName,
                typeof(PersistentOrderedSet<>.Enumerator).FullName,
            }.OrderBy(name => name),
            assembly.GetExportedTypes().Select(type => type.FullName).OrderBy(name => name));

        foreach (var closed in new[]
                 {
                     typeof(PersistentOrderedSet<string>),
                     typeof(PersistentOrderedMap<string, string>),
                     typeof(PersistentOrderedMultimap<string, string>),
                 })
        {
            foreach (var memberType in PublicSignatureTypes(closed))
            {
                var name = memberType.Assembly.GetName().Name ?? string.Empty;
                Assert.DoesNotContain("Tungsten", name, StringComparison.OrdinalIgnoreCase);
                Assert.NotEqual("Durable7.Hamt", name);
                Assert.NotEqual("Durable7.FingerTree", name);
            }
            Assert.DoesNotContain(
                closed.GetMembers(BindingFlags.Public | BindingFlags.Instance | BindingFlags.Static),
                member => member.Name.Contains("Stamp", StringComparison.OrdinalIgnoreCase)
                    || member.Name.Contains("Label", StringComparison.OrdinalIgnoreCase));
        }
    }

    /// <summary>Verifies production and executable test sources contain no Tungsten symbol or live-oracle hook.</summary>
    [Fact]
    public void SourceTree_HasNoTungstenSymbolSourceLinkOrLiveOracle()
    {
        var root = RepositoryRoot();
        var productDirectory = Path.Combine(root, "src", "CSharp", "src", "Durable7.Ordered");
        var testDirectory = Path.Combine(root, "src", "CSharp", "tests", "Durable7.Ordered.Tests");
        var sharedTestDirectory = Path.Combine(root, "src", "CSharp", "tests", "TestInfrastructure");
        var files = EnumerateSdkSources(productDirectory)
            .Concat(EnumerateSdkSources(testDirectory)
                .Where(path => !path.EndsWith(nameof(OrderedDependencyBoundaryTests) + ".cs", StringComparison.Ordinal)));
        files = files.Concat(EnumerateSdkSources(sharedTestDirectory));
        foreach (var file in files)
        {
            var source = File.ReadAllText(file);
            Assert.DoesNotContain("Durable7.Tungsten", source, StringComparison.Ordinal);
            Assert.DoesNotContain("PersistentAssociation", source, StringComparison.Ordinal);
            Assert.DoesNotContain("Assembly.Load", source, StringComparison.Ordinal);
            Assert.DoesNotContain("Process.Start", source, StringComparison.Ordinal);
        }
    }

    private static IEnumerable<string> ProjectReferences(string project) =>
        XDocument.Load(project)
            .Descendants("ProjectReference")
            .Select(element => element.Attribute("Include")!.Value);

    // MSBuild project paths use Windows separators in the repository even when tests run on Unix.
    // Path.GetFileName treats the backslash as an ordinary character there, so normalize both
    // separator forms explicitly before comparing the dependency allowlist.
    private static string ProjectFileName(string path) =>
        path[(path.LastIndexOfAny(['\\', '/']) + 1)..];

    private static void AssertPackageReferences(
        XDocument document,
        params (string Id, string Version)[] expected)
    {
        var references = document.Descendants("PackageReference").ToArray();
        Assert.Equal(
            expected.OrderBy(package => package.Id, StringComparer.Ordinal),
            references
                .Select(reference => (
                    Id: reference.Attribute("Include")!.Value,
                    Version: reference.Attribute("Version")!.Value))
                .OrderBy(package => package.Id, StringComparer.Ordinal));

        foreach (var reference in references)
        {
            Assert.Equal(
                new[] { "Include", "Version" },
                reference.Attributes().Select(attribute => attribute.Name.LocalName).OrderBy(name => name, StringComparer.Ordinal));
            var id = reference.Attribute("Include")!.Value;
            if (id == "xunit.runner.visualstudio")
            {
                Assert.Equal(
                    new[]
                    {
                        (Name: "IncludeAssets", Value: "runtime; build; native; contentfiles; analyzers; buildtransitive"),
                        (Name: "PrivateAssets", Value: "all"),
                    },
                    reference.Elements().Select(element => (element.Name.LocalName, element.Value.Trim())));
            }
            else
            {
                Assert.Empty(reference.Elements());
            }
        }
    }

    private static void AssertNoSourceInjectionRoutes(XDocument document)
    {
        Assert.DoesNotContain(
            "Tungsten",
            document.ToString(SaveOptions.DisableFormatting),
            StringComparison.OrdinalIgnoreCase);
        string[] forbiddenElements =
        [
            "AdditionalFiles",
            "Analyzer",
            "CompilerVisibleItemMetadata",
            "CompilerVisibleProperty",
            "Import",
            "OutputItemType",
            "ReferenceOutputAssembly",
            "Sdk",
            "Target",
            "UsingTask",
        ];
        foreach (var elementName in forbiddenElements)
            Assert.Empty(document.Descendants(elementName));

        foreach (var attribute in document.Root!.DescendantsAndSelf().Attributes())
        {
            Assert.DoesNotContain("Tungsten", attribute.Value, StringComparison.OrdinalIgnoreCase);
            if (attribute.Name.LocalName == "Sdk")
                Assert.Equal("Microsoft.NET.Sdk", attribute.Value);
            Assert.False(
                attribute.Name.LocalName is "OutputItemType" or "ReferenceOutputAssembly",
                $"Build metadata '{attribute.Name.LocalName}' can turn an approved dependency into a compiler extension.");
        }
    }

    private static IEnumerable<string> EnumerateSdkSources(string directory) =>
        Directory.EnumerateFiles(directory, "*.cs", SearchOption.AllDirectories)
            .Where(path => !Path.GetRelativePath(directory, path)
                .Split(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar)
                .Any(segment => segment.Equals("bin", StringComparison.OrdinalIgnoreCase)
                    || segment.Equals("obj", StringComparison.OrdinalIgnoreCase)));

    private static IEnumerable<Type> PublicSignatureTypes(Type type)
    {
        foreach (var implemented in type.GetInterfaces())
            foreach (var item in Flatten(implemented))
                yield return item;
        foreach (var property in type.GetProperties(BindingFlags.Public | BindingFlags.Instance | BindingFlags.Static))
            foreach (var item in Flatten(property.PropertyType))
                yield return item;
        foreach (var method in type.GetMethods(BindingFlags.Public | BindingFlags.Instance | BindingFlags.Static))
        {
            foreach (var item in Flatten(method.ReturnType))
                yield return item;
            foreach (var parameter in method.GetParameters())
                foreach (var item in Flatten(parameter.ParameterType))
                    yield return item;
        }
    }

    private static IEnumerable<Type> Flatten(Type type)
    {
        if (type.IsByRef || type.IsArray || type.IsPointer)
            type = type.GetElementType()!;
        yield return type;
        if (!type.IsGenericType)
            yield break;
        foreach (var argument in type.GetGenericArguments())
            foreach (var item in Flatten(argument))
                yield return item;
    }

    /// <summary>
    /// Resolves the repository root by asking git for the work-tree top level.
    /// </summary>
    /// <remarks>
    /// Probing for a marker file is brittle: a root document can be deleted, and in a linked
    /// worktree <c>.git</c> is a file rather than a directory, so a directory probe would miss it.
    /// <c>git rev-parse --show-toplevel</c> resolves both layouts. The query is anchored with
    /// <c>-C</c> to the test binary's location because the process working directory is not
    /// guaranteed to sit inside the work tree. If git is unavailable the solution file, which these
    /// assertions read anyway, still identifies the root.
    /// </remarks>
    private static string RepositoryRoot()
    {
        var gitRoot = GitTopLevel();
        if (gitRoot is not null)
            return gitRoot;

        for (var directory = new DirectoryInfo(AppContext.BaseDirectory); directory is not null; directory = directory.Parent)
        {
            if (File.Exists(Path.Combine(directory.FullName, "src", "CSharp", "Durable7.sln")))
                return directory.FullName;
        }
        throw new InvalidOperationException("Could not locate the Durable7 repository root.");
    }

    /// <summary>Returns the git work-tree root, or <see langword="null"/> when git cannot answer.</summary>
    private static string? GitTopLevel()
    {
        var startInfo = new ProcessStartInfo("git")
        {
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
            CreateNoWindow = true,
        };
        startInfo.ArgumentList.Add("-C");
        startInfo.ArgumentList.Add(AppContext.BaseDirectory);
        startInfo.ArgumentList.Add("rev-parse");
        startInfo.ArgumentList.Add("--show-toplevel");

        try
        {
            using var process = Process.Start(startInfo);
            if (process is null)
                return null;

            var output = process.StandardOutput.ReadToEnd();
            process.WaitForExit();
            if (process.ExitCode != 0)
                return null;

            output = output.Trim();
            // git reports POSIX separators even on Windows; GetFullPath normalizes them.
            return output.Length == 0 ? null : Path.GetFullPath(output);
        }
        catch (Win32Exception)
        {
            // git is not installed or not on PATH.
            return null;
        }
    }
}
