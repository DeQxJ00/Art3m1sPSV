using System;
using System.Runtime.CompilerServices;

namespace ResizeBatch.Util.PNGCompression
{
    /// <summary>
    /// lossy
    /// </summary>
    public class LossyInputSettings
    {
        /// <summary>
        /// </summary>
        public string CustomInputArgs
        {
            get;
            set;
        }

        /// <summary>
        /// MaxQuality upto 100
        /// </summary>
        public int MaxQuality
        {
            get;
            set;
        }

        /// <summary>
        /// MinQuality from 0
        /// </summary>
        public int MinQuality
        {
            get;
            set;
        }

        /// <summary>
        /// speed to choose between 1 to 10 i.e. default is 3 and Speed/quality trade-off from 1 (brute-force) to 10 (fastest). The default is 3. Speed 10 has 5% lower quality, but is 8 times faster than the default.
        /// </summary>
        public int Speed
        {
            get;
            set;
        }

        /// <summary>
        /// </summary>
        /// <param name="CustomInputArgs"></param>
        /// <param name="qualityMin"></param>
        /// <param name="qualityMax"></param>
        /// <param name="speed"></param>
        public LossyInputSettings(string CustomInputArgs, int qualityMin, int qualityMax, int speed)
        {
            this.CustomInputArgs = CustomInputArgs;
            MinQuality = qualityMin;
            MaxQuality = qualityMax;
            Speed = speed;
        }

        /// <summary>
        /// </summary>
        public LossyInputSettings()
        {
        }
    }
}