namespace Durable7.Hamt;

/// <summary>Identifies how a key differs between two persistent maps.</summary>
public enum MapDifferenceKind
{
    /// <summary>The key exists only in the target map.</summary>
    Added,

    /// <summary>The key exists only in the source map.</summary>
    Removed,

    /// <summary>The key exists in both maps with different values.</summary>
    Changed,
}

/// <summary>Describes one key-level difference between two persistent maps.</summary>
/// <typeparam name="TKey">The key type.</typeparam>
/// <typeparam name="TValue">The value type.</typeparam>
/// <param name="Kind">The kind of difference.</param>
/// <param name="Key">
/// The affected stored key. Removed and changed differences retain the source map's key
/// representative; added differences retain the target map's representative.
/// </param>
/// <param name="OldValue">The source value for removed and changed entries.</param>
/// <param name="NewValue">The target value for added and changed entries.</param>
public readonly record struct MapDifference<TKey, TValue>(
    MapDifferenceKind Kind,
    TKey Key,
    TValue? OldValue,
    TValue? NewValue)
{
    internal static MapDifference<TKey, TValue> Added(TKey key, TValue value) =>
        new(MapDifferenceKind.Added, key, default, value);

    internal static MapDifference<TKey, TValue> Removed(TKey key, TValue value) =>
        new(MapDifferenceKind.Removed, key, value, default);

    internal static MapDifference<TKey, TValue> Changed(TKey key, TValue oldValue, TValue newValue) =>
        new(MapDifferenceKind.Changed, key, oldValue, newValue);
}
