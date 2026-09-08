using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace ResizeBatch.Util.MultiThread
{
    public class DoWorkMultiThread
    {
        public delegate void DoWorkDelegate<T>(T t);

        public static void Work<T>(DoWorkDelegate<T> doWork, List<T> list)
        {
            //src : https://blog.darkthread.net/blog/tpl-threadpool-usage/

            ThreadPool.GetMinThreads(out int minWorkerThreads, out int minCompletionPortThreads);
            ThreadPool.GetMaxThreads(out int maxWorkerThreads, out int maxCompletionPortThreads);
            Console.WriteLine($"ThreadPool Min: {minWorkerThreads} {minCompletionPortThreads}");
            Console.WriteLine($"ThreadPool Max: {maxWorkerThreads} {maxCompletionPortThreads}");

            int totalCount = list.Count;
            int remaining = totalCount;
            int running = 0;

            var startTime = DateTime.Now;
            var cts = new CancellationTokenSource();
            // 監看 Thread 數、待處理工作數變化
            Task.Run(() =>
            {
                Console.WriteLine("Time | Threads | Running | Pending | Percent ");
                Console.WriteLine("-----+---------+---------+---------+---------");
    
                // 檢查 CancellationTokenSource 是否已經被取消
                while (!cts.Token.IsCancellationRequested)
                {
                    double percentage = (((double)totalCount - (double)ThreadPool.PendingWorkItemCount) / (double)totalCount) * 100d;
                    string formattedPercentage = percentage.ToString("0.00")+"%";
                    Console.WriteLine($"{(DateTime.Now - startTime).TotalSeconds,3:n0}s | {ThreadPool.ThreadCount,7} | {running,7} | {ThreadPool.PendingWorkItemCount,7} | {formattedPercentage,7}");
                    Thread.Sleep(1000);
                }
            },
                cts.Token //允許還沒執行前取消(例如：還在 Queue 排隊時就取消)
            );

            var tasks = Enumerable.Range(0, totalCount).Select(i =>
                Task.Run(() =>
                {
                    Interlocked.Increment(ref running);

                    doWork(list[i]);

                    Interlocked.Decrement(ref remaining);
                    Interlocked.Decrement(ref running);
                })).ToArray();
            // 跟 QueueUserWorkItem 不同的是，Task.Run() 傳回 Task 物件，可掌握其執行狀態
            // 不必監看處理數字，改用 Task.WaitAll() 等待所有 Task 完成
            Task.WaitAll(tasks);

            // 如要觀察 Thread 減少，可多等 30 秒
            //Thread.Sleep(30000);

            // 等待所有 Task 完成後，停止 Thread 數、待處理工作數的監看迴圈
            cts.Cancel();
        }


    }
}
