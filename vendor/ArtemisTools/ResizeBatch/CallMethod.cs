using ResizeBatch.Util.ImgBatch;
using ResizeBatch.Util.MultiThread;


namespace ResizeBatch
{
    internal class CallMethod
    {

        /// <summary>
        ///  ogv 过场动画替换用
        /// </summary>
        /// <param name="dirInputPath"></param>
        public static void BatchOgvPlay(string dirInputPath)
        {

            string dirOutputPath = dirInputPath + "_Resize";
            if (!Directory.Exists(dirOutputPath))
            {
                Directory.CreateDirectory(dirOutputPath);
            }
            foreach (var file in Directory.GetFiles(dirInputPath,"*.ast"))
            {
                string[] lines = File.ReadAllLines(file);
                for (int i = 0; i < lines.Length; i++)
                {
                    if (lines[i].Contains("movie=9,") && lines[i].Contains("bg"))
                    {
                        //{"bg",bx=480,by=270,file="アイキャッチ_01_in",id=1,loop=0,lv=5,maskid=1,movie=9,path=":ani/",sync=1,time=0},
                        if (lines[i].Contains("アイキャッチ_01_in"))
                        {
                            //_01_in 500ms
                            lines[i] = lines[i].Replace("movie=9,", "").Replace("time=0", "time=500");
                        }
                        else if (lines[i].Contains("アイキャッチ_02_out"))
                        {
                            //_02_out 600ms
                            lines[i] = lines[i].Replace("movie=9,", "").Replace("time=0", "time=600");
                        }
                    }
                }
                string outputFilePath = file.Replace(dirInputPath, dirOutputPath);
                File.WriteAllLines(outputFilePath, lines);
            }
        }


        /// <summary>
        /// ResizeImage Artemis引擎移植到psv压缩图片用的
        /// </summary>
        public static void ResizeImage(string dirInputPath, string scaleFactorStr)
        {
            ResizeImage(dirInputPath, scaleFactorStr,Mode.All);
        }

        public static void ResizeImage(string dirInputPath, string scaleFactorStr,string mode)
        {
            ResizeImage(dirInputPath, scaleFactorStr,(Mode)int.Parse(mode));
        }

        public static void ResizeImage(string dirInputPath, string scaleFactorStr,string mode,string minThread,string maxThread)
        {
            ThreadPool.SetMinThreads(int.Parse(minThread), int.Parse(minThread));
            ThreadPool.SetMaxThreads(int.Parse(maxThread), int.Parse(maxThread));
            ResizeImage(dirInputPath, scaleFactorStr,(Mode)int.Parse(mode));
        }

        public static void ResizeImage(string dirInputPath, string scaleFactorStr, Mode mode)
        {
            // string dirInputPath = args[0];
            float scaleFactor = float.Parse(scaleFactorStr);
          
            string dirTempPath = dirInputPath + "_Resize" + Path.DirectorySeparatorChar + "Temp";
            string dirOutputPath = dirInputPath + "_Resize" + Path.DirectorySeparatorChar + "LossLess";
            string dirOutputPath2 = dirInputPath + "_Resize" + Path.DirectorySeparatorChar + "Lossy";

            List<ImgBatchWorkParams> listParams = new List<ImgBatchWorkParams>();
            foreach (string file in Directory.GetFiles(dirInputPath, "*.png", SearchOption.AllDirectories))
            {
                string inputFile = file;
                string tempFile = file.Replace(dirInputPath, dirTempPath);
                string outputFile = file.Replace(dirInputPath, dirOutputPath);
                string outputFile2 = file.Replace(dirInputPath, dirOutputPath2);
                Console.WriteLine(inputFile); // 输出找到的 PNG 文件路径
                listParams.Add(new ImgBatchWorkParams()
                {
                    inputFile = inputFile,
                    tempFile = tempFile,
                    outputFile = outputFile,
                    outputFile2 = outputFile2,
                    scaleFactor = scaleFactor,
                    mode = mode
                });
            }

            //多线程执行
            DoWorkMultiThread.Work(
                delegate (ImgBatchWorkParams imgparams) {
                    new ImgBatchWork().DoWork(imgparams);
                }, listParams);

            Console.WriteLine("Convert Image End!");
            Console.ReadLine();
        }
    }
}
