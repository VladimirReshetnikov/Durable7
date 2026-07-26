// Shared chunk-size policy for the rope family.

namespace Durable7.FingerTree;

/// <summary>Shared chunk-size policy for the rope family.</summary>
internal static class RopeChunking
{
    public const int MinChunkSize = 256;
    public const int MaxChunkSize = 2048;
}
