using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace ResizeBatch.Util.ImgBatch
{
    public enum Mode
    {
        All = 0, //全部都生成
        ResizeTemp = 1,//仅缩放图片
        ResizeLossLess = 2,//缩放图片后 生成无损的pal8图片
        ResizeLossy = 3 //缩放图片 生成有损png
    }

    public class ImgBatchWorkParams
    {
        public string inputFile;
        public string tempFile;
        public string outputFile;
        public string outputFile2;
        public float scaleFactor;
        public Mode mode;
    }
}
