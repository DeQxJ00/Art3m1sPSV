using System;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Text;
using System.Web;

namespace ResizeBatch.Util.PNGCompression
{
    /// <summary>
    /// Wrapper for OptiPNG and PNGQuant 
    /// </summary>
    public class PNGCompressor
    {
        /// <summary>
        /// process 
        /// </summary>
        private Process PNGProcess;

        /// <summary>
        /// Gets or sets maximum execution time for conversion process (null is by default - means no timeout)
        /// </summary>
        public TimeSpan? ExecutionTimeout
        {
            get;
            set;
        }

        /// <summary>
        /// Gets or sets PNG tool EXE file name
        /// </summary>
        public string PNGExeName
        {
            get;
            set;
        }

        /// <summary>
        /// Gets or sets user credential used for starting PNG process.
        /// </summary>
        /// <remarks>By default this property is null and PNG process uses credential of parent process (application pool in case of ASP.NET).</remarks>
        public PNGUserCredential PNGProcessUser
        {
            get;
            set;
        }

        /// <summary>Gets or sets path where PNG tool is located</summary>
        /// <remarks>
        /// By default this property points to the folder where application assemblies are located.
        /// </remarks>
        public string PNGToolPath
        {
            get;
            set;
        }

        /// <summary>
        /// Initializes a new instance of the ImageCompressor class.
        /// </summary>
        /// <remarks>
        /// ImageCompressor is NOT thread-safe. Separate instance should be used for each thread.
        /// </remarks>
        public PNGCompressor()
        {
            PNGToolPath = AppDomain.CurrentDomain.BaseDirectory;
            //if (HttpContext.Current != null)
            //{
            //	this.PNGToolPath = string.Concat(HttpRuntime.AppDomainAppPath, "bin");
            //}
            if (string.IsNullOrEmpty(PNGToolPath))
            {
                PNGToolPath = Path.GetDirectoryName(typeof(PNGCompressor).Assembly.Location);
            }
        }

        /// <summary>
        /// Append all the arguments in a single string.
        /// </summary>
        /// <param name="inputFile"></param>
        /// <param name="outputFile"></param>
        /// <param name="lossyInputSettings"></param>
        /// <param name="losslessInputSettings"></param>
        /// <param name="compressionType"></param>
        /// <returns></returns>
        private string AppendArguments(string inputFile, string outputFile, string compressionType, LossyInputSettings lossyInputSettings, LosslessInputSettings losslessInputSettings)
        {
            string str = string.Format("\"{0}\"", inputFile);
            string str1 = string.Format("\"{0}\"", outputFile);
            StringBuilder stringBuilder = new StringBuilder();
            if (compressionType != "Lossy")
            {
                stringBuilder.AppendFormat("{0} -out {1} ", str, str1);
                if (losslessInputSettings != null)
                {
                    stringBuilder.AppendFormat(" {0} ", losslessInputSettings.OptimizationLevel);
                    stringBuilder.AppendFormat(" {0} ", losslessInputSettings.CustomInputArgs);
                }
            }
            else
            {
                stringBuilder.AppendFormat("{0} -o {1} ", str, str1);
                if (lossyInputSettings != null)
                {
                    if (lossyInputSettings.MinQuality >= 0 && lossyInputSettings.MaxQuality > 0 && lossyInputSettings.MaxQuality <= 100)
                    {
                        stringBuilder.AppendFormat(" --quality {0}-{1} ", lossyInputSettings.MinQuality, lossyInputSettings.MaxQuality);
                    }
                    if (lossyInputSettings.Speed > 0 && lossyInputSettings.Speed <= 10)
                    {
                        stringBuilder.AppendFormat(" -s{0} ", lossyInputSettings.Speed);
                    }
                    stringBuilder.AppendFormat(" {0} ", lossyInputSettings.CustomInputArgs);
                }
            }
            return stringBuilder.ToString();
        }

        /// <summary>
        /// Compress PNG image with lossless algorithm.
        /// </summary>
        /// <param name="inputFile"></param>
        /// <param name="outputFile"></param>
        /// <param name="compressionType">Use CompressionType enum </param>
        /// <param name="lossyInputSettings"></param>
        /// <param name="losslessInputSettings"></param>
        /// <param name="inputFormat"></param>
        private void CompressImage(string inputFile, string outputFile, string compressionType, LossyInputSettings lossyInputSettings, LosslessInputSettings losslessInputSettings, InputFormat inputFormat = null)
        {
            try
            {
                if (inputFile == null)
                {
                    throw new ArgumentNullException("inputFile");
                }
                if (outputFile == null)
                {
                    throw new ArgumentNullException("outputFile");
                }
                if (File.Exists(inputFile) && string.IsNullOrEmpty(Path.GetExtension(inputFile)) && string.IsNullOrEmpty(Convert.ToString(inputFormat)))
                {
                    throw new Exception("Input format is required for file without extension");
                }
                PlatformID platform = Environment.OSVersion.Platform;
                if (compressionType != "Lossy")
                {
                    switch (platform)
                    {
                        case PlatformID.Unix:
                            PNGExeName = "optipng";
                            break;
                        case PlatformID.MacOSX:
                            PNGExeName = "optipng";
                            break;
                        default:
                            PNGExeName = "optipng.exe";
                            break;
                    }
                }
                else
                {
                    switch (platform)
                    {
                        case PlatformID.Unix:
                            PNGExeName = "pngquant";
                            break;
                        case PlatformID.MacOSX:
                            PNGExeName = "pngquant";
                            break;
                        default:
                            PNGExeName = "pngquant.exe";
                            break;
                    }
                }
                EnsureExeExists();
                string pNGExePath = GetPNGExePath();
                string str = AppendArguments(inputFile, outputFile, compressionType, lossyInputSettings, losslessInputSettings);
                ProcessStartInfo processStartInfo = new ProcessStartInfo(pNGExePath, str)
                {
                    WindowStyle = ProcessWindowStyle.Hidden,
                    CreateNoWindow = true,
                    UseShellExecute = false,
                    WorkingDirectory = Path.GetDirectoryName(PNGToolPath),
                    RedirectStandardInput = true,
                    RedirectStandardOutput = true,
                    RedirectStandardError = true
                };
                InitStartInfo(processStartInfo);
                if (PNGProcess != null)
                {
                    throw new InvalidOperationException("PNG process is already started");
                }
                PNGProcess = Process.Start(processStartInfo);
                if (File.Exists(outputFile))
                {
                    File.Delete(outputFile);
                }
                WaitPNGProcessForExit();
                if (PNGProcess.ExitCode != 0)
                {
                    throw new Exception("Some error occured");
                }
                PNGProcess.Close();
                PNGProcess = null;
            }
            catch (Exception exception)
            {
                EnsurePNGProcessStopped();
                throw;
            }
        }

        /// <summary>
        /// Compress PNG image with lossless algorithm.
        /// </summary>
        /// <param name="inputFile">png file with .png extension</param>
        /// <param name="outputFile">output file name with .png extension.</param>
        public void CompressImageLossLess(string inputFile, string outputFile)
        {
            CompressImage(inputFile, outputFile, "LossLess", null, null, null);
        }

        /// <summary>
        /// </summary>
        /// <param name="inputFile"></param>
        /// <param name="outputFile"></param>
        /// <param name="inputSettings"></param>
        public void CompressImageLossLess(string inputFile, string outputFile, LosslessInputSettings inputSettings)
        {
            CompressImage(inputFile, outputFile, "LossLess", null, inputSettings, null);
        }

        /// <summary>
        /// </summary>
        /// <param name="inputFile"></param>
        /// <param name="outputFile"></param>
        /// <param name="inputSettings"></param>
        /// <param name="inputFormat">Not implemented yet</param>
        public void CompressImageLossLess(string inputFile, string outputFile, LosslessInputSettings inputSettings, InputFormat inputFormat)
        {
            CompressImage(inputFile, outputFile, "LossLess", null, inputSettings, inputFormat);
        }

        /// <summary>
        /// Compress PNG image with lossy algorithm.
        /// </summary>
        /// <param name="inputFile"></param>
        /// <param name="outputFile"></param>
        public void CompressImageLossy(string inputFile, string outputFile)
        {
            CompressImage(inputFile, outputFile, "Lossy", null, null, null);
        }

        /// <summary>
        /// </summary>
        /// <param name="inputFile"></param>
        /// <param name="outputFile"></param>
        /// <param name="inputSettings"></param>
        public void CompressImageLossy(string inputFile, string outputFile, LossyInputSettings inputSettings)
        {
            CompressImage(inputFile, outputFile, "Lossy", inputSettings, null, null);
        }

        /// <summary>
        /// </summary>
        /// <param name="inputFile"></param>
        /// <param name="outputFile"></param>
        /// <param name="inputSettings"></param>
        /// <param name="inputFormat">Not implemented yet</param>
        public void CompressImageLossy(string inputFile, string outputFile, LossyInputSettings inputSettings, InputFormat inputFormat)
        {
            CompressImage(inputFile, outputFile, "Lossy", inputSettings, null, inputFormat);
        }

        /// <summary>
        /// to copy exes embedded as resources in DLL.
        /// </summary>
        private void EnsureExeExists()
        {
            string str = "PNGCompression.Utilities.";
            string str1 = Path.Combine(PNGToolPath, PNGExeName);
            if (!File.Exists(str1))
            {
                //using (Stream manifestResourceStream = Assembly.GetExecutingAssembly().GetManifestResourceStream(string.Concat(str, PNGExeName)))
                //{
                //    using (FileStream fileStream = new FileStream(str1, FileMode.Create, FileAccess.Write, FileShare.None))
                //    {
                //        manifestResourceStream.CopyTo(fileStream);
                //    }
                //}
            }
        }

        /// <summary>
        /// Ensure whether process is running or stopped.
        /// </summary>
        protected void EnsurePNGProcessStopped()
        {
            if (PNGProcess == null)
            {
                return;
            }
            if (PNGProcess.HasExited)
            {
                return;
            }
            try
            {
                PNGProcess.Kill();
                PNGProcess = null;
            }
            catch (Exception exception)
            {
                throw;
            }
        }

        /// <summary>
        /// Get full path of Exe to be executed.
        /// </summary>
        /// <returns></returns>
        internal string GetPNGExePath()
        {
            if(File.Exists(Path.Combine(PNGToolPath, PNGExeName)))
            {
                return Path.Combine(PNGToolPath, PNGExeName);
            }
            else
            {
                return Path.Combine(@"C:\ProgramData\chocolatey\bin",PNGExeName);
            }
        }

        /// <summary>
        /// Initialize process
        /// </summary>
        /// <param name="startInfo"></param>
        internal void InitStartInfo(ProcessStartInfo startInfo)
        {
            if (PNGProcessUser == null)
            {
                return;
            }
            if (PNGProcessUser.Domain != null)
            {
                startInfo.Domain = PNGProcessUser.Domain;
            }
            if (PNGProcessUser.UserName != null)
            {
                startInfo.UserName = PNGProcessUser.UserName;
            }
            if (PNGProcessUser.Password == null)
            {
                return;
            }
            startInfo.Password = PNGProcessUser.Password;
        }

        /// <summary>
        /// waiting for the process to complete
        /// </summary>
        protected void WaitPNGProcessForExit()
        {
            if (PNGProcess == null)
            {
                throw new Exception("PNG process was aborted");
            }
            if (!PNGProcess.HasExited)
            {
                if (!PNGProcess.WaitForExit(ExecutionTimeout.HasValue ? (int)ExecutionTimeout.Value.TotalMilliseconds : 2147483647))
                {
                    EnsurePNGProcessStopped();
                    throw new Exception("PNG process exceeded execution timeout and was aborted");
                }
            }
        }
    }
}