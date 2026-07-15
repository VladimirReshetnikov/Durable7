using System.Reflection;
using System.Xml.Linq;
using Xunit;

namespace Tools.DataStructures.Ordered.Tests;

/// <summary>Executable guards for Ordered ownership, public-substrate use, and Tungsten isolation.</summary>
public sealed class OrderedDependencyBoundaryTests
{
    /// <summary>Verifies product and test manifests contain only the approved project-reference edges.</summary>
    [Fact]
    public void ProjectManifests_HaveOnlyApprovedRepositoryDependencies()
    {
        var root = RepositoryRoot();
        var productProject = Path.Combine(root, "src", "CSharp", "src", "Tools.DataStructures.Ordered", "Tools.DataStructures.Ordered.csproj");
        var testProject = Path.Combine(root, "src", "CSharp", "tests", "Tools.DataStructures.Ordered.Tests", "Tools.DataStructures.Ordered.Tests.csproj");

        Assert.Equal(
            new[] { "Tools.DataStructures.FingerTree.csproj", "Tools.DataStructures.Hamt.csproj" },
            ProjectReferences(productProject).Select(Path.GetFileName).OrderBy(name => name));
        Assert.Equal(
            new[] { "Tools.DataStructures.Ordered.csproj" },
            ProjectReferences(testProject).Select(Path.GetFileName));

        foreach (var project in new[] { productProject, testProject })
        {
            var document = XDocument.Load(project);
            Assert.DoesNotContain(
                document.Descendants().Attributes("Include"),
                attribute => attribute.Value.Contains("Tungsten", StringComparison.OrdinalIgnoreCase));
            Assert.DoesNotContain(
                document.Descendants("Compile"),
                element => element.Attribute("Link") is not null || element.Attribute("Include") is not null);
        }
    }

    /// <summary>Verifies Ordered alone grants its tests internal access and foundations grant it none.</summary>
    [Fact]
    public void InternalsVisibleTo_ExistsOnlyAtTheOrderedOwnedTestSeam()
    {
        var root = RepositoryRoot();
        var ordered = File.ReadAllText(Path.Combine(root, "src", "CSharp", "src", "Tools.DataStructures.Ordered", "Tools.DataStructures.Ordered.csproj"));
        Assert.Contains("InternalsVisibleTo Include=\"Tools.DataStructures.Ordered.Tests\"", ordered, StringComparison.Ordinal);
        Assert.DoesNotContain("Tungsten", ordered, StringComparison.OrdinalIgnoreCase);

        foreach (var provider in new[] { "Tools.DataStructures.Hamt", "Tools.DataStructures.FingerTree" })
        {
            var text = File.ReadAllText(Path.Combine(root, "src", "CSharp", "src", provider, $"{provider}.csproj"));
            Assert.DoesNotContain("InternalsVisibleTo Include=\"Tools.DataStructures.Ordered", text, StringComparison.Ordinal);
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
                typeof(PersistentOrderedSet<>).FullName,
                typeof(PersistentOrderedSet<>.Enumerator).FullName,
            }.OrderBy(name => name),
            assembly.GetExportedTypes().Select(type => type.FullName).OrderBy(name => name));

        var closed = typeof(PersistentOrderedSet<string>);
        foreach (var memberType in PublicSignatureTypes(closed))
        {
            var name = memberType.Assembly.GetName().Name ?? string.Empty;
            Assert.DoesNotContain("Tungsten", name, StringComparison.OrdinalIgnoreCase);
            Assert.NotEqual("Tools.DataStructures.Hamt", name);
            Assert.NotEqual("Tools.DataStructures.FingerTree", name);
        }
        Assert.DoesNotContain(
            closed.GetMembers(BindingFlags.Public | BindingFlags.Instance | BindingFlags.Static),
            member => member.Name.Contains("Stamp", StringComparison.OrdinalIgnoreCase)
                || member.Name.Contains("Label", StringComparison.OrdinalIgnoreCase));
    }

    /// <summary>Verifies production and executable test sources contain no Tungsten symbol or live-oracle hook.</summary>
    [Fact]
    public void SourceTree_HasNoTungstenSymbolSourceLinkOrLiveOracle()
    {
        var root = RepositoryRoot();
        var productDirectory = Path.Combine(root, "src", "CSharp", "src", "Tools.DataStructures.Ordered");
        var testDirectory = Path.Combine(root, "src", "CSharp", "tests", "Tools.DataStructures.Ordered.Tests");
        var files = Directory.EnumerateFiles(productDirectory, "*.cs", SearchOption.TopDirectoryOnly)
            .Concat(Directory.EnumerateFiles(testDirectory, "*.cs", SearchOption.TopDirectoryOnly)
                .Where(path => !path.EndsWith(nameof(OrderedDependencyBoundaryTests) + ".cs", StringComparison.Ordinal)));
        foreach (var file in files)
        {
            var source = File.ReadAllText(file);
            Assert.DoesNotContain("Tools.DataStructures.Tungsten", source, StringComparison.Ordinal);
            Assert.DoesNotContain("PersistentAssociation", source, StringComparison.Ordinal);
            Assert.DoesNotContain("Assembly.Load", source, StringComparison.Ordinal);
            Assert.DoesNotContain("Process.Start", source, StringComparison.Ordinal);
        }
    }

    private static IEnumerable<string> ProjectReferences(string project) =>
        XDocument.Load(project)
            .Descendants("ProjectReference")
            .Select(element => element.Attribute("Include")!.Value);

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

    private static string RepositoryRoot()
    {
        for (var directory = new DirectoryInfo(AppContext.BaseDirectory); directory is not null; directory = directory.Parent)
        {
            if (File.Exists(Path.Combine(directory.FullName, "AGENTS.md"))
                && File.Exists(Path.Combine(directory.FullName, "src", "CSharp", "DataStructures.sln")))
            {
                return directory.FullName;
            }
        }
        throw new InvalidOperationException("Could not locate the DataStructures repository root.");
    }
}
