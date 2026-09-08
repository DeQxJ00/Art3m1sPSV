using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Threading.Tasks;

namespace ResizeBatch.Util
{
    public class CommandTool
    {
        public static void Execute(string[] args,string strClass)
        {
            try
            {
                if (args.Length == 0)//无参数
                {
                    Type type = Type.GetType(strClass);
                    Object obj = System.Activator.CreateInstance(type);
                    MethodInfo[] info = type.GetMethods();
                    for (int i = 0; i < info.Length; i++)
                    {
                        var md = info[i];
                        string mothodName = md.Name;
                        ParameterInfo[] paramInfos = md.GetParameters();
                        Console.WriteLine("参数样例");
                        Console.WriteLine($"方法名:{mothodName}");
                        foreach (var item in md.GetParameters())
                        {
                            Console.WriteLine($"参数名:{item.ParameterType.Name} {item.Name}");
                        }
                        Console.WriteLine();
                    }
                }
                else
                {
                    string[] param = new string[args.Length - 1];
                    if (args.Length > 0)
                    {
                        Array.Copy(args, 1, param, 0, args.Length - 1);
                    }
                    string strMethod = args[0];
                    Type type = Type.GetType(strClass);
                    Object obj = System.Activator.CreateInstance(type);
                    MethodInfo[] info = type.GetMethods();
                    bool isFound = false;

                    for (int i = 0; i < info.Length; i++)
                    {
                        var md = info[i];
                        string mothodName = md.Name;
                        ParameterInfo[] paramInfos = md.GetParameters();
                        if (mothodName == strMethod && paramInfos.Length == param.Length)
                        {
                            isFound = true;
                            md.Invoke(obj, param);
                        }
                    }
                    if (!isFound)
                    {
                        bool isFound2 = false;
                        for (int i = 0; i < info.Length; i++)
                        {
                            var md = info[i];
                            string mothodName = md.Name;
                            ParameterInfo[] paramInfos = md.GetParameters();
                            if (mothodName == strMethod)
                            {
                                isFound2 = true;
                                Console.WriteLine("参数错误");
                                Console.WriteLine($"方法名:{strMethod}");
                                foreach (var item in md.GetParameters())
                                {
                                    Console.WriteLine($"参数名:{item.ParameterType.Name} {item.Name}");
                                }
                                Console.WriteLine();
                            }
                        }
                        if (!isFound2)
                        {
                            Console.WriteLine($"方法名:{strMethod},无此方法名");
                        }
                    }
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine(ex.ToString());
            }
        }
    }
}
