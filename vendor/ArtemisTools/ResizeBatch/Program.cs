using ResizeBatch.Util;

namespace ResizeBatch
{
    internal class Program
    {
        static void Main(string[] args)
        {
            const string strClass = "ResizeBatch.CallMethod";
            System.AppContext.SetSwitch("System.Drawing.EnableUnixSupport", true);
            CommandTool.Execute(args,strClass);
        }
    }
}
