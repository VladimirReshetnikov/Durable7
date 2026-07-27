// Shared chunk-size policy for the rope family.

namespace Durable7.FingerTree;

/// <summary>Shared chunk-size policy for the rope family.</summary>
internal static class RopeChunking
{
    /// <summary>Gets the min chunk size.</summary>
    public const int MinChunkSize = 256;
    /// <summary>Gets the max chunk size.</summary>
    public const int MaxChunkSize = 2048;
}
