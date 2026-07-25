using System.Text;
using System.Text.RegularExpressions;
using Xunit;

namespace Durable7.Numerics.Tests;

/// <summary>
/// Verifies declaration-level parity across 256-bit, 512-bit, and 1024-bit integer implementations.
/// </summary>
/// <remarks>
/// These tests intentionally parse source files instead of reflection metadata so they can enforce nitpicky
/// invariants such as declaration order, attribute placement, parameter naming, and signature shape.
/// </remarks>
public sealed partial class DeclarationParityTests
{
    /// <summary>
    /// Ensures <see cref="UInt256"/> and <see cref="UInt512"/> remain declaration-equivalent modulo width-specific tokens.
    /// </summary>
    [Fact]
    public void UInt256_And_UInt512_DeclarationShapes_AreCarbonCopiesModuloBitness()
    {
        var uint256 = ExtractDeclarations(ReadNumericsSource("UInt256.cs"));
        var uint512 = ExtractDeclarations(ReadNumericsSource("UInt512.cs"));

        var normalized256 = NormalizeUnsigned256Declarations(uint256);
        var normalized512 = NormalizeUnsigned512Declarations(uint512);

        AssertDeclarationsEqual(normalized256, normalized512, nameof(UInt256_And_UInt512_DeclarationShapes_AreCarbonCopiesModuloBitness));
    }

    /// <summary>
    /// Ensures <see cref="Int256"/> and <see cref="Int512"/> remain declaration-equivalent modulo width-specific tokens.
    /// </summary>
    [Fact]
    public void Int256_And_Int512_DeclarationShapes_AreCarbonCopiesModuloBitness()
    {
        var int256 = ExtractDeclarations(ReadNumericsSource("Int256.cs"));
        var int512 = ExtractDeclarations(ReadNumericsSource("Int512.cs"));

        var normalized256 = NormalizeSigned256Declarations(int256);
        var normalized512 = NormalizeSigned512Declarations(int512);

        AssertDeclarationsEqual(normalized256, normalized512, nameof(Int256_And_Int512_DeclarationShapes_AreCarbonCopiesModuloBitness));
    }

    /// <summary>
    /// Ensures <see cref="UInt512"/> and <see cref="UInt1024"/> remain declaration-equivalent modulo width-specific tokens.
    /// </summary>
    [Fact]
    public void UInt512_And_UInt1024_DeclarationShapes_AreCarbonCopiesModuloBitness()
    {
        var uint512 = ExtractDeclarations(ReadNumericsSource("UInt512.cs"));
        var uint1024 = ExtractDeclarations(ReadNumericsSource("UInt1024.cs"));

        var normalized512 = NormalizeUnsigned512Declarations(uint512);
        var normalized1024 = NormalizeUnsigned1024Declarations(uint1024);

        AssertDeclarationsEqual(normalized512, normalized1024, nameof(UInt512_And_UInt1024_DeclarationShapes_AreCarbonCopiesModuloBitness));
    }

    /// <summary>
    /// Ensures <see cref="Int512"/> and <see cref="Int1024"/> remain declaration-equivalent modulo width-specific tokens.
    /// </summary>
    [Fact]
    public void Int512_And_Int1024_DeclarationShapes_AreCarbonCopiesModuloBitness()
    {
        var int512 = ExtractDeclarations(ReadNumericsSource("Int512.cs"));
        var int1024 = ExtractDeclarations(ReadNumericsSource("Int1024.cs"));

        var normalized512 = NormalizeSigned512Declarations(int512);
        var normalized1024 = NormalizeSigned1024Declarations(int1024);

        AssertDeclarationsEqual(normalized512, normalized1024, nameof(Int512_And_Int1024_DeclarationShapes_AreCarbonCopiesModuloBitness));
    }

    /// <summary>
    /// Ensures signed and unsigned 256-bit declarations stay aligned for equivalent members.
    /// </summary>
    [Fact]
    public void UInt256_And_Int256_CommonDeclarationShapes_AreAligned()
    {
        var uint256 = ExtractDeclarations(ReadNumericsSource("UInt256.cs"));
        var int256 = ExtractDeclarations(ReadNumericsSource("Int256.cs"));

        var normalizedUnsigned = NormalizeUnsigned256CommonDeclarations(uint256);
        var normalizedSigned = NormalizeSigned256CommonDeclarations(int256);

        AssertDeclarationsEqual(normalizedUnsigned, normalizedSigned, nameof(UInt256_And_Int256_CommonDeclarationShapes_AreAligned));
    }

    /// <summary>
    /// Ensures signed and unsigned 512-bit declarations stay aligned for equivalent members.
    /// </summary>
    [Fact]
    public void UInt512_And_Int512_CommonDeclarationShapes_AreAligned()
    {
        var uint512 = ExtractDeclarations(ReadNumericsSource("UInt512.cs"));
        var int512 = ExtractDeclarations(ReadNumericsSource("Int512.cs"));

        var normalizedUnsigned = NormalizeUnsigned512CommonDeclarations(uint512);
        var normalizedSigned = NormalizeSigned512CommonDeclarations(int512);

        AssertDeclarationsEqual(normalizedUnsigned, normalizedSigned, nameof(UInt512_And_Int512_CommonDeclarationShapes_AreAligned));
    }

    /// <summary>
    /// Ensures signed and unsigned 1024-bit declarations stay aligned for equivalent members.
    /// </summary>
    [Fact]
    public void UInt1024_And_Int1024_CommonDeclarationShapes_AreAligned()
    {
        var uint1024 = ExtractDeclarations(ReadNumericsSource("UInt1024.cs"));
        var int1024 = ExtractDeclarations(ReadNumericsSource("Int1024.cs"));

        var normalizedUnsigned = NormalizeUnsigned1024CommonDeclarations(uint1024);
        var normalizedSigned = NormalizeSigned1024CommonDeclarations(int1024);

        AssertDeclarationsEqual(normalizedUnsigned, normalizedSigned, nameof(UInt1024_And_Int1024_CommonDeclarationShapes_AreAligned));
    }

    private static string ReadNumericsSource(string fileName)
    {
        string directory = AppContext.BaseDirectory;

        while (!string.IsNullOrWhiteSpace(directory))
        {
            string candidate = Path.Combine(directory, "src", "Durable7.Numerics", fileName);
            if (File.Exists(candidate))
                return File.ReadAllText(candidate);

            var parent = Directory.GetParent(directory);
            if (parent is null)
                break;

            directory = parent.FullName;
        }

        throw new InvalidOperationException($"Could not locate {fileName} under src/Durable7.Numerics.");
    }

    private static IReadOnlyList<DeclarationShape> ExtractDeclarations(string source)
    {
        string[] lines = source.Split('\n');
        var declarations = new List<DeclarationShape>();
        var pendingAttributes = new List<string>();

        for (int lineIndex = 0; lineIndex < lines.Length; lineIndex++)
        {
            string rawLine = lines[lineIndex];
            string line = rawLine.Trim();

            if (line.Length == 0 || line.StartsWith("///", StringComparison.Ordinal) || line.StartsWith("//", StringComparison.Ordinal))
                continue;

            if (line.StartsWith('['))
            {
                pendingAttributes.Add(NormalizeWhitespace(line));
                continue;
            }

            if (!line.StartsWith("public ", StringComparison.Ordinal)
                && !line.StartsWith("private ", StringComparison.Ordinal)
                && !line.StartsWith("internal ", StringComparison.Ordinal))
            {
                pendingAttributes.Clear();
                continue;
            }

            if (line.StartsWith("public readonly partial struct", StringComparison.Ordinal)
                || line.StartsWith("public readonly struct", StringComparison.Ordinal))
            {
                pendingAttributes.Clear();
                continue;
            }

            string signature = RemoveTrailingComment(line);
            int parenthesesDepth = ParenthesesDelta(signature);

            while (!EndsDeclaration(signature) || parenthesesDepth > 0)
            {
                lineIndex++;
                if (lineIndex >= lines.Length)
                    break;

                string continuation = RemoveTrailingComment(lines[lineIndex].Trim());
                if (continuation.Length == 0)
                    continue;

                signature = $"{signature} {continuation}";
                parenthesesDepth += ParenthesesDelta(continuation);
            }

            signature = NormalizeWhitespace(signature);
            declarations.Add(new(signature, [.. pendingAttributes]));
            pendingAttributes.Clear();
        }

        return declarations;
    }

    private static bool EndsDeclaration(string value) =>
        value.Contains("=>", StringComparison.Ordinal)
        || value.EndsWith(';')
        || value.EndsWith('{');

    private static int ParenthesesDelta(string value)
    {
        int delta = 0;

        foreach (char c in value)
        {
            if (c == '(')
                delta++;
            else if (c == ')')
                delta--;
        }

        return delta;
    }

    private static IReadOnlyList<string> NormalizeUnsigned256Declarations(IReadOnlyList<DeclarationShape> declarations) =>
        NormalizeDeclarations(declarations,
            static _ => true,
            ("UInt256", "TYPE"),
            ("UInt512", "TYPE"),
            ("Int256", "SIGNED_TYPE"),
            ("Int512", "SIGNED_TYPE"),
            ("Int128", "SIGNED_TYPE"),
            ("UInt128", "HALF_TYPE"),
            ("0xFF", "SHIFT_MASK"),
            ("256", "BITS"),
            ("128", "HALF_BITS"),
            ("32", "BYTE_COUNT"));

    private static IReadOnlyList<string> NormalizeUnsigned512Declarations(IReadOnlyList<DeclarationShape> declarations) =>
        NormalizeDeclarations(declarations,
            static declaration => IsAdjacentWidthDeclaration(declaration, "UInt128", "Int128"),
            ("UInt512", "TYPE"),
            ("UInt256", "HALF_TYPE"),
            ("Int512", "SIGNED_TYPE"),
            ("Int256", "SIGNED_TYPE"),
            ("0x1FF", "SHIFT_MASK"),
            ("512", "BITS"),
            ("256", "HALF_BITS"),
            ("64", "BYTE_COUNT"));

    private static IReadOnlyList<string> NormalizeSigned256Declarations(IReadOnlyList<DeclarationShape> declarations) =>
        NormalizeDeclarations(declarations,
            static _ => true,
            ("Int256", "TYPE"),
            ("Int512", "TYPE"),
            ("UInt256", "UNSIGNED_TYPE"),
            ("UInt512", "UNSIGNED_TYPE"),
            ("UInt128", "HALF_TYPE"),
            ("Int128", "TYPE"),
            ("255", "MAGNITUDE_BITS"),
            ("127", "SIGN_BIT"),
            ("0xFF", "SHIFT_MASK"),
            ("256", "BITS"),
            ("128", "HALF_BITS"),
            ("32", "BYTE_COUNT"));

    private static IReadOnlyList<string> NormalizeSigned512Declarations(IReadOnlyList<DeclarationShape> declarations) =>
        NormalizeDeclarations(declarations,
            static declaration => IsAdjacentWidthDeclaration(declaration, "UInt128", "Int128"),
            ("Int512", "TYPE"),
            ("Int256", "TYPE"),
            ("UInt512", "UNSIGNED_TYPE"),
            ("UInt256", "HALF_TYPE"),
            ("511", "MAGNITUDE_BITS"),
            ("255", "SIGN_BIT"),
            ("0x1FF", "SHIFT_MASK"),
            ("512", "BITS"),
            ("256", "HALF_BITS"),
            ("64", "BYTE_COUNT"));



    private static IReadOnlyList<string> NormalizeUnsigned1024Declarations(IReadOnlyList<DeclarationShape> declarations) =>
        NormalizeDeclarations(declarations,
            static declaration => IsAdjacentWidthDeclaration(declaration, "UInt128", "Int128", "UInt256", "Int256"),
            ("UInt1024", "TYPE"),
            ("UInt512", "HALF_TYPE"),
            ("Int1024", "SIGNED_TYPE"),
            ("Int512", "SIGNED_TYPE"),
            ("0x3FF", "SHIFT_MASK"),
            ("1024", "BITS"),
            ("512", "HALF_BITS"),
            ("128", "BYTE_COUNT"));

    private static IReadOnlyList<string> NormalizeSigned1024Declarations(IReadOnlyList<DeclarationShape> declarations) =>
        NormalizeDeclarations(declarations,
            static declaration => IsAdjacentWidthDeclaration(declaration, "UInt128", "Int128", "UInt256", "Int256"),
            ("Int1024", "TYPE"),
            ("Int512", "TYPE"),
            ("UInt1024", "UNSIGNED_TYPE"),
            ("UInt512", "HALF_TYPE"),
            ("1023", "MAGNITUDE_BITS"),
            ("511", "SIGN_BIT"),
            ("0x3FF", "SHIFT_MASK"),
            ("1024", "BITS"),
            ("512", "HALF_BITS"),
            ("128", "BYTE_COUNT"));

    private static IReadOnlyList<string> NormalizeUnsigned256CommonDeclarations(IReadOnlyList<DeclarationShape> declarations) =>
        NormalizeDeclarations(declarations,
            IsCommonDeclaration,
            ("UInt256", "TYPE"),
            ("UInt512", "TYPE"),
            ("Int256", "TYPE"),
            ("Int512", "TYPE"),
            ("UInt128", "HALF_TYPE"),
            ("Int128", "HALF_TYPE"),
            ("255", "SIGN_BIT"),
            ("127", "SIGN_BIT"),
            ("0xFF", "SHIFT_MASK"),
            ("256", "BITS"),
            ("128", "HALF_BITS"),
            ("32", "BYTE_COUNT"),
            ("ulong", "SIGNEDNESS_FREE_LONG"),
            ("long", "SIGNEDNESS_FREE_LONG"));

    private static IReadOnlyList<string> NormalizeSigned256CommonDeclarations(IReadOnlyList<DeclarationShape> declarations) =>
        NormalizeDeclarations(declarations,
            IsCommonDeclaration,
            ("Int256", "TYPE"),
            ("Int512", "TYPE"),
            ("UInt256", "TYPE"),
            ("UInt512", "TYPE"),
            ("UInt128", "HALF_TYPE"),
            ("Int128", "HALF_TYPE"),
            ("255", "SIGN_BIT"),
            ("127", "SIGN_BIT"),
            ("0xFF", "SHIFT_MASK"),
            ("256", "BITS"),
            ("128", "HALF_BITS"),
            ("32", "BYTE_COUNT"),
            ("ulong", "SIGNEDNESS_FREE_LONG"),
            ("long", "SIGNEDNESS_FREE_LONG"));

    private static IReadOnlyList<string> NormalizeUnsigned512CommonDeclarations(IReadOnlyList<DeclarationShape> declarations) =>
        NormalizeDeclarations(declarations,
            IsCommonDeclaration,
            ("UInt512", "TYPE"),
            ("UInt256", "HALF_TYPE"),
            ("Int512", "TYPE"),
            ("Int256", "TYPE"),
            ("0x1FF", "SHIFT_MASK"),
            ("511", "SIGN_BIT"),
            ("255", "SIGN_BIT"),
            ("512", "BITS"),
            ("256", "HALF_BITS"),
            ("64", "BYTE_COUNT"),
            ("ulong", "SIGNEDNESS_FREE_LONG"),
            ("long", "SIGNEDNESS_FREE_LONG"));

    private static IReadOnlyList<string> NormalizeSigned512CommonDeclarations(IReadOnlyList<DeclarationShape> declarations) =>
        NormalizeDeclarations(declarations,
            IsCommonDeclaration,
            ("Int512", "TYPE"),
            ("Int256", "TYPE"),
            ("UInt512", "TYPE"),
            ("UInt256", "HALF_TYPE"),
            ("0x1FF", "SHIFT_MASK"),
            ("511", "SIGN_BIT"),
            ("255", "SIGN_BIT"),
            ("512", "BITS"),
            ("256", "HALF_BITS"),
            ("64", "BYTE_COUNT"),
            ("ulong", "SIGNEDNESS_FREE_LONG"),
            ("long", "SIGNEDNESS_FREE_LONG"));

    private static IReadOnlyList<string> NormalizeUnsigned1024CommonDeclarations(IReadOnlyList<DeclarationShape> declarations) =>
        NormalizeDeclarations(declarations,
            IsCommonDeclaration,
            ("UInt1024", "TYPE"),
            ("UInt512", "HALF_TYPE"),
            ("Int1024", "TYPE"),
            ("Int512", "TYPE"),
            ("0x3FF", "SHIFT_MASK"),
            ("1023", "SIGN_BIT"),
            ("511", "SIGN_BIT"),
            ("1024", "BITS"),
            ("512", "HALF_BITS"),
            ("128", "BYTE_COUNT"),
            ("ulong", "SIGNEDNESS_FREE_LONG"),
            ("long", "SIGNEDNESS_FREE_LONG"));

    private static IReadOnlyList<string> NormalizeSigned1024CommonDeclarations(IReadOnlyList<DeclarationShape> declarations) =>
        NormalizeDeclarations(declarations,
            IsCommonDeclaration,
            ("Int1024", "TYPE"),
            ("Int512", "TYPE"),
            ("UInt1024", "TYPE"),
            ("UInt512", "HALF_TYPE"),
            ("0x3FF", "SHIFT_MASK"),
            ("1023", "SIGN_BIT"),
            ("511", "SIGN_BIT"),
            ("1024", "BITS"),
            ("512", "HALF_BITS"),
            ("128", "BYTE_COUNT"),
            ("ulong", "SIGNEDNESS_FREE_LONG"),
            ("long", "SIGNEDNESS_FREE_LONG"));

    private static bool IsCommonDeclaration(string declaration)
    {
        string[] signedOnlyTokens =
        [
            " IsNegative ",
            "operator -(",
            "operator checked -(",
            " Abs(",
            " Sign(",
            "GetUnsignedMagnitude",
            "GetLowerSignedIfSignExtended",
            "BigMulUnsigned",
            "s_signMaskUpper",
            "s_bigMinValue",
            "s_bigMaxValue",
            "sbyte",
            "short",
            "int",
            "long",
            "Int128",
            "Int256",
            "Int512",
            "Int1024"
        ];

        string[] unsignedOnlyTokens =
        [
            "UInt128",
            "UInt256",
            "UInt512",
            "UInt1024"
        ];

        bool hasSignedOnly = signedOnlyTokens.Any(declaration.Contains);
        bool hasUnsignedOnly = unsignedOnlyTokens.Any(declaration.Contains);

        if (hasSignedOnly)
            return false;

        // Keep type names that are part of cross-signed conversion declarations.
        if (declaration.Contains("operator", StringComparison.Ordinal))
            return !hasSignedOnly;

        return !hasUnsignedOnly;
    }

    // The carbon-copy guard models each type in terms of its immediately smaller and larger widths.
    // Direct conversions that deliberately skip one or more widths are validated separately by
    // WideIntegerConversionParityTests and must not disturb this adjacent-width comparison.
    private static bool IsAdjacentWidthDeclaration(string declaration, params string[] nonAdjacentTypes) =>
        !declaration.Contains("operator", StringComparison.Ordinal)
        || !nonAdjacentTypes.Any(declaration.Contains);

    private static string NormalizeTokens(string value, params (string Source, string Target)[] replacements)
    {
        string normalized = value;

        foreach ((string source, string target) in replacements)
        {
            string escapedSource = Regex.Escape(source);
            normalized = Regex.Replace(
                normalized,
                $@"(?<![A-Za-z0-9_]){escapedSource}(?![A-Za-z0-9_])",
                target);
        }

        return normalized;
    }

    private static string RemoveTrailingComment(string line)
    {
        int commentIndex = line.IndexOf("//", StringComparison.Ordinal);
        return commentIndex >= 0 ? line[..commentIndex].TrimEnd() : line;
    }

    private static string NormalizeWhitespace(string value) =>
        MyRegex().Replace(value, " ").Trim();

    private static IReadOnlyList<string> NormalizeDeclarations(
        IReadOnlyList<DeclarationShape> declarations,
        Func<string, bool> includeSignature,
        params (string Source, string Target)[] replacements)
    {
        var normalized = new List<string>(declarations.Count * 2);

        foreach (DeclarationShape declaration in declarations)
        {
            if (!includeSignature(declaration.Signature))
                continue;

            normalized.AddRange(declaration.Attributes.Select(attribute => $"@{NormalizeTokens(attribute, replacements)}"));
            normalized.Add(NormalizeTokens(declaration.Signature, replacements));
        }

        return normalized;
    }

    private static void AssertDeclarationsEqual(IReadOnlyList<string> expected, IReadOnlyList<string> actual, string testName)
    {
        if (expected.SequenceEqual(actual))
            return;

        int mismatchIndex = -1;
        int commonLength = Math.Min(expected.Count, actual.Count);

        for (int index = 0; index < commonLength; index++)
        {
            if (expected[index] != actual[index])
            {
                mismatchIndex = index;
                break;
            }
        }

        if (mismatchIndex < 0)
            mismatchIndex = commonLength;

        int start = Math.Max(0, mismatchIndex - 3);
        int end = Math.Min(Math.Max(expected.Count, actual.Count), mismatchIndex + 4);

        var diagnostic = new StringBuilder()
            .AppendLine($"{testName} declaration mismatch at index {mismatchIndex}.")
            .AppendLine($"Expected count: {expected.Count}; actual count: {actual.Count}.")
            .AppendLine("Context window:");

        for (int index = start; index < end; index++)
        {
            string expectedValue = index < expected.Count ? expected[index] : "<missing>";
            string actualValue = index < actual.Count ? actual[index] : "<missing>";
            diagnostic.AppendLine($"  [{index}] expected: {expectedValue}");
            diagnostic.AppendLine($"        actual:   {actualValue}");
        }

        Assert.Fail(diagnostic.ToString());
    }

    private readonly record struct DeclarationShape(string Signature, IReadOnlyList<string> Attributes);

    [GeneratedRegex("\\s+")]
    private static partial Regex MyRegex();
}
