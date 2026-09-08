using System;
using System.Runtime.CompilerServices;

namespace ResizeBatch.Util.PNGCompression
{
    /// <summary>
    /// LosslessInputSettings
    /// </summary>
    public class LosslessInputSettings
    {
        /// <summary>
        /// </summary>
        public string CustomInputArgs
        {
            get;
            set;
        }

        /// <summary>
        /// Use OptimizationLevel enum 
        /// </summary>
        public string OptimizationLevel
        {
            get;
            set;
        }

        /// <summary>
        /// </summary>
        /// <param name="CustomInputArgs"></param>
        /// <param name="OptimizationLevel"></param>
        public LosslessInputSettings(string CustomInputArgs, string OptimizationLevel)
        {
            this.CustomInputArgs = CustomInputArgs;
            this.OptimizationLevel = OptimizationLevel;
        }

        /// <summary>
        /// </summary>
        public LosslessInputSettings()
        {
        }
    }
}