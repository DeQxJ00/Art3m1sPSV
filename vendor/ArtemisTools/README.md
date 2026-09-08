## ArtemisTools

此工具用于压缩Artemis引擎的图片

可以设置压缩比例，给png写入pos坐标，进行png缩放，无损pal8的压缩，有损压缩


### 使用环境

- 需要安装.NET6

  - https://dotnet.microsoft.com/en-us/download/dotnet/6.0

- 将optipng和pngquant 放在和执行文件同一个目录

  - https://sourceforge.net/projects/optipng/files/OptiPNG/

  - https://pngquant.org/



### 使用例子

```
ResizeBatch.exe ResizeImage D:\Test\root 0.5
ResizeBatch.exe ResizeImage D:\Test\root 0.5 0
ResizeBatch.exe ResizeImage D:\Test\root 0.5 0 2 10
```

```
ResizeBatch.exe ResizeImage DirInputPath ScaleFactorStr
ResizeBatch.exe ResizeImage DirInputPath ScaleFactorStr Mode
ResizeBatch.exe ResizeImage DirInputPath ScaleFactorStr Mode MinThread MaxThread
```


### 参数说明

- **ResizeImage**

  - 调用的方法名 固定这个

- **DirInputPath**

  - 需要处理的图片目录(子文件夹里的图片也会压缩,输入整个文件夹即可)

- **ScaleFactorStr**

  - 缩小比例 例如0.5 0.75 

- **Mode**

  - 0 //以下三种全部都生成 【默认】

  - 1 //仅缩放图片png ResizeTemp

  - 2 //缩放图片后 生成无损的pal8图片png ResizeLossLess

  - 3 //缩放图片后 生成有损的pal8图片png ResizeLossy

- **MinThread,MaxThread**

  - 默认使用电脑的最高线程数。如果不想电脑满载使用可以写低点


---
### Open source used

https://blog.darkthread.net/blog/tpl-threadpool-usage/

https://github.com/amitsbaghel/PNGCompressor

https://github.com/hokejyo/VisualNovelUpscaler


