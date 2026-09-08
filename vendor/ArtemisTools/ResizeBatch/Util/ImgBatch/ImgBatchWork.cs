using ResizeBatch.Util.PNGCompression;
using SixLabors.ImageSharp;
using SixLabors.ImageSharp.Formats.Png.Chunks;
using System.Drawing;


namespace ResizeBatch.Util.ImgBatch
{
    public class ImgBatchWork
    {
        public void DoWork<T>(T imgParams)
        {
            if (imgParams is ImgBatchWorkParams)
            {
                string inputFile = (imgParams as ImgBatchWorkParams).inputFile;
                string tempFile = (imgParams as ImgBatchWorkParams).tempFile;
                string outputFile = (imgParams as ImgBatchWorkParams).outputFile;
                string outputFile2 = (imgParams as ImgBatchWorkParams).outputFile2;
                float scaleFactor = (imgParams as ImgBatchWorkParams).scaleFactor;
                Mode mode = (imgParams as ImgBatchWorkParams).mode;
                FileInfo inputFileInfo = new FileInfo(inputFile);
                FileInfo tempFileInfo = new FileInfo(tempFile);
                FileInfo outputFileInfo = new FileInfo(outputFile);
                FileInfo outputFileInfo2 = new FileInfo(outputFile2);

                PNGCompressor compressor = new PNGCompressor();
                if (!Directory.Exists(tempFileInfo.DirectoryName))
                {
                    Directory.CreateDirectory(tempFileInfo.DirectoryName);
                }
                resizeImg(inputFile, tempFile, scaleFactor);


                if (mode == Mode.All)
                {
                    if (!Directory.Exists(outputFileInfo.DirectoryName))
                    {
                        Directory.CreateDirectory(outputFileInfo.DirectoryName);
                    }
                    if (!Directory.Exists(outputFileInfo2.DirectoryName))
                    {
                        Directory.CreateDirectory(outputFileInfo2.DirectoryName);
                    }
                    compressor.CompressImageLossLess(tempFile, outputFile);
                    compressor.CompressImageLossy(tempFile, outputFile2);
                }
                //缩放
                else if(mode == Mode.ResizeTemp)
                {

                }
                //缩放+创建无损压缩pal8 png
                else if (mode == Mode.ResizeLossLess)
                {
                    if (!Directory.Exists(outputFileInfo.DirectoryName))
                    {
                        Directory.CreateDirectory(outputFileInfo.DirectoryName);
                    }
                    compressor.CompressImageLossLess(tempFile, outputFile);
                }
                //缩放+创建有损压缩
                else if (mode == Mode.ResizeLossy)
                {
                    if (!Directory.Exists(outputFileInfo2.DirectoryName))
                    {
                        Directory.CreateDirectory(outputFileInfo2.DirectoryName);
                    }
                    compressor.CompressImageLossy(tempFile, outputFile2);
                }
                else
                {
                    compressor.CompressImageLossLess(tempFile, outputFile);
                    compressor.CompressImageLossy(tempFile, outputFile2);
                }

                List<PngTextData> list = new List<PngTextData>();
                //读取文本信息 
                
                using (SixLabors.ImageSharp.Image image = SixLabors.ImageSharp.Image.Load(inputFile))
                {
                    var metadata = image.Metadata.GetPngMetadata();
                    foreach (var entry in metadata.TextData)
                    {
                        //计算坐标信息 
                        //PngTextData [ Name=comment, Value=pos,261,78,408,756 ]
                        if (entry.Value.Contains(","))
                        {
                            string[] strtmps = entry.Value.Split(',');
                            for (int i = 0; i < strtmps.Length; i++)
                            {
                                int num = 0;
                                if (int.TryParse(strtmps[i],out num))
                                {
                                    float num2 = (float)num * scaleFactor;
                                    strtmps[i] = ((int)num2).ToString();
                                }
                            }
                            list.Add(new PngTextData(entry.Keyword, string.Join(",", strtmps),"",""));
                        }
                    }
                }

                //追加文本信息
                if (File.Exists(tempFile))
                {
                    AddPngTextData(tempFile, list);
                }
                if(File.Exists(outputFile))
                {
                    AddPngTextData(outputFile, list);
                }
                if (File.Exists(outputFile2))
                {
                    AddPngTextData(outputFile2, list);
                }
                list.Clear();

            }
            else
            {
                Console.WriteLine("输入类型不对");
            }

        }

        public void AddPngTextData(string tempFile, List<PngTextData> list)
        {
            using (SixLabors.ImageSharp.Image image = SixLabors.ImageSharp.Image.Load(tempFile))
            {
                var metadata = image.Metadata.GetPngMetadata();
                metadata.TextData.Clear();
                foreach (var entry in list)
                {
                    metadata.TextData.Add(entry);
                }
                image.Save(tempFile);
            }
        }

        public void resizeImg(string inputFile, string outputFile, float scaleFactor = 0.5f)
        {
            using (System.Drawing.Image image = System.Drawing.Image.FromFile(inputFile))
            {
                int newWidth = Math.Max(1, (int)(image.Width * scaleFactor));
                int newHeight = Math.Max(1, (int)(image.Height * scaleFactor));
                using (Bitmap resizedImage = new Bitmap(newWidth, newHeight))
                {
                    using (Graphics g = Graphics.FromImage(resizedImage))
                    {
                        g.InterpolationMode = System.Drawing.Drawing2D.InterpolationMode.HighQualityBicubic;
                        g.DrawImage(image, 0, 0, newWidth, newHeight);
                    }
                    resizedImage.Save(outputFile); // 保存缩小后的图片
                }
            }
        }
    }
}
