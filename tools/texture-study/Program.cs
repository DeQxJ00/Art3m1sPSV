using PVRTexLib;
using SixLabors.ImageSharp;
using SixLabors.ImageSharp.PixelFormats;
using System.Runtime.InteropServices;
if(args.Length==2&&args[0]=="--native-fixtures"){NativeFixtures.Generate(args[1]);return;}
unsafe {
 ulong rgbaFormat=PVRDefine.PVRTGENPIXELID4('r','g','b','a',8,8,8,8);
 foreach(var folder in args) foreach(var file in Directory.GetFiles(folder,"layer*.png").Where(p => System.Text.RegularExpressions.Regex.IsMatch(Path.GetFileName(p), @"^layer[0-9]+\.png$"))) {
  using var img=Image.Load<Rgba32>(file);int w=img.Width,h=img.Height;
  byte[] rgba=new byte[w*h*4];img.CopyPixelDataTo(rgba);File.WriteAllBytes(Path.ChangeExtension(file,"rgba"),rgba);
  foreach(var mode in new[]{"bc3","pvr1","pvr2"}) {
   int pw=mode=="bc3"?w:Pot(w),ph=mode=="bc3"?h:Pot(h);
   byte[] input=new byte[pw*ph*4];
   for(int y=0;y<ph;y++) for(int x=0;x<pw;x++) Array.Copy(rgba,(Math.Min(y,h-1)*w+Math.Min(x,w-1))*4,input,(y*pw+x)*4,4);
   fixed(byte* src=input) {
    using var header=new PVRTextureHeader(rgbaFormat,(uint)pw,(uint)ph,1,1,1,1,PVRTexLibColourSpace.Linear,PVRTexLibVariableType.UnsignedByteNorm,false);
    using var tex=new PVRTexture(header,src);
    var format=mode=="bc3"?PVRTexLibPixelFormat.DXT5:mode=="pvr1"?PVRTexLibPixelFormat.PVRTCI_4bpp_RGBA:PVRTexLibPixelFormat.PVRTCII_4bpp;
    if(!tex.Transcode((ulong)format,PVRTexLibVariableType.UnsignedByteNorm,PVRTexLibColourSpace.Linear,PVRTexLibCompressorQuality.PVRTCHigh))throw new Exception("Encode "+file+mode);
    var raw=new ReadOnlySpan<byte>(tex.GetTextureDataPointer(0),(int)tex.GetTextureDataSize(0)).ToArray();
    File.WriteAllBytes(Path.ChangeExtension(file,mode),raw);tex.SaveToFile(Path.ChangeExtension(file,mode+".pvr"));
    if(mode=="bc3") {
     var dst=new byte[Pot(w)*Pot(h)];int bw=Pot(w)/4,bh=Pot(h)/4,sw=(w+3)/4,sh=(h+3)/4;
     for(int y=0;y<sh;y++)for(int x=0;x<sw;x++)Array.Copy(raw,(y*sw+x)*16,dst,Morton(x,y,bw,bh)*16,16);
     File.WriteAllBytes(Path.ChangeExtension(file,"bc3psv"),dst);
    }
    if(mode=="pvr2") {
     var dst=new byte[raw.Length];int bw=pw/4,bh=ph/4;
     for(int y=0;y<bh;y++)for(int x=0;x<bw;x++)Array.Copy(raw,(y*bw+x)*8,dst,Morton(x,y,bw,bh)*8,8);
     File.WriteAllBytes(Path.ChangeExtension(file,"pvr2tw"),dst);
    }
    if(!tex.Transcode(rgbaFormat,PVRTexLibVariableType.UnsignedByteNorm,PVRTexLibColourSpace.Linear))throw new Exception("Decode");
    var decoded=new ReadOnlySpan<byte>(tex.GetTextureDataPointer(0),(int)tex.GetTextureDataSize(0));
    byte[] crop=new byte[w*h*4];for(int y=0;y<h;y++)decoded.Slice(y*pw*4,w*4).CopyTo(crop.AsSpan(y*w*4));
    File.WriteAllBytes(Path.ChangeExtension(file,mode+".ref"),crop);
    using var preview=Image.LoadPixelData<Rgba32>(crop,w,h);preview.SaveAsPng(Path.ChangeExtension(file,mode+".png"));
    Console.WriteLine($"{file} {mode} {w}x{h} storage={pw}x{ph} bytes={raw.Length}");
   }
  }
 }
}
static int Pot(int n){int p=4;while(p<n)p*=2;return p;}
static int Morton(int x,int y,int bw,int bh){int index=0,shift=0;for(int b=1;b<bw||b<bh;b*=2){if(b<bh){if((y&b)!=0)index|=1<<shift;shift++;}if(b<bw){if((x&b)!=0)index|=1<<shift;shift++;}}return index;}
