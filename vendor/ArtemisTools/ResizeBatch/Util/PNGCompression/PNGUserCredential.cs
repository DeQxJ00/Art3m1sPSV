using System;
using System.Runtime.CompilerServices;
using System.Security;

namespace ResizeBatch.Util.PNGCompression
{
    /// <summary>
    /// PNGUserCredential
    /// </summary>
    public class PNGUserCredential
    {
        /// <summary>
        /// Gets a value that identifies the domain to use when starting optiPNG process.
        /// </summary>
        public string Domain
        {
            get;
            private set;
        }

        /// <summary>
        /// Gets a secure string that contains the user password to use when starting OptiPNG process.
        /// </summary>
        public SecureString Password
        {
            get;
            private set;
        }

        /// <summary>
        /// Gets the user name to be used when starting OptiPNG process.
        /// </summary>
        public string UserName
        {
            get;
            private set;
        }

        public PNGUserCredential()
        {
        }
    }
}