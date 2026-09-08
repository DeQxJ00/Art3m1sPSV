using System;

namespace ResizeBatch.Util.PNGCompression
{
    /// <summary>
    /// CompressionType to be lossless or lossy .
    /// </summary>
    public static class CompressionType
    {
        /// <summary>
        /// Image loses its quality .
        /// </summary>
        public const string Lossy = "Lossy";

        /// <summary>
        /// Image does not lose its quality .
        /// </summary>
        public const string LossLess = "LossLess";
    }
}