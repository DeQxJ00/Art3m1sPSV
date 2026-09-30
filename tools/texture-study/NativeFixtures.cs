using PVRTexLib;
using SixLabors.ImageSharp;
using SixLabors.ImageSharp.PixelFormats;
internal static class NativeFixtures {
public static unsafe void Generate(string output) {
 string folder=Path.GetFullPath(output);Directory.CreateDirectory(folder);
 ulong rgbaFormat=PVRDefine.PVRTGENPIXELID4('r','g','b','a',8,8,8,8);
 int[] codes={7,9,11,12,12,13,13,0,1,2,3,4,5,6};
 for(int i=0;i<codes.Length;i++){
  int w=128,h=64;byte[] input=new byte[w*h*4];
  for(int y=0;y<h;y++)for(int x=0;x<w;x++){
   int at=(y*w+x)*4;input[at]=(byte)(x*2);input[at+1]=(byte)(y*4);input[at+2]=(byte)(((x/16+y/8)%2)*180+30);
   input[at+3]=(byte)(x<32?0:x<64?80:x<96?180:255);
  }
  fixed(byte* src=input){
   using var header=new PVRTextureHeader(rgbaFormat,(uint)w,(uint)h,1,1,1,1,PVRTexLibColourSpace.Linear,PVRTexLibVariableType.UnsignedByteNorm,false);
   using var tex=new PVRTexture(header,src);
   var type=i==4||i==6?PVRTexLibVariableType.SignedByteNorm:PVRTexLibVariableType.UnsignedByteNorm;
   if(!tex.Transcode((ulong)codes[i],type,PVRTexLibColourSpace.Linear,PVRTexLibCompressorQuality.PVRTCHigh))throw new Exception("encode "+i);
   string name=Path.Combine(folder,$"tex{i+1:00}");tex.SaveToFile(name+".pvr");
   if(i<7)tex.SaveToFile(name+".dds");
   if(!tex.Transcode(rgbaFormat,PVRTexLibVariableType.UnsignedByteNorm,PVRTexLibColourSpace.Linear))throw new Exception("decode "+i);
   byte[] decoded=new ReadOnlySpan<byte>(tex.GetTextureDataPointer(0),w*h*4).ToArray();
   if(i==3||i==4)for(int p=0;p<decoded.Length;p+=4){decoded[p+1]=decoded[p+2]=decoded[p];decoded[p+3]=255;}
   if(i==5||i==6)for(int p=0;p<decoded.Length;p+=4){decoded[p+2]=0;decoded[p+3]=255;}
   using var img=Image.LoadPixelData<Rgba32>(decoded,w,h);img.SaveAsPng(name+".png");
   Console.WriteLine($"{i+1} code={codes[i]} bytes={new FileInfo(name+".pvr").Length}");
  }
 }
 File.WriteAllText(Path.Combine(folder,"system.ini"),"[VITA]\nWIDTH=960\nHEIGHT=544\nBOOT=first.iet\nCHARSET=UTF-8\n[WINDOWS]\nWIDTH=960\nHEIGHT=544\nBOOT=first.iet\nCHARSET=UTF-8\n");
 File.WriteAllText(Path.Combine(folder,"title.txt"),"Native compressed texture comparison\n");
 var script=new List<string>{"*top","[debug mode=1 level=3]","[lyc id=0 width=960 height=544 color=204060]"};
 for(int i=1;i<=14;i++)for(int side=0;side<2;side++){
  int layer=i*2+side,x=16+480*((i-1)/7)+160*side,y=12+74*((i-1)%7);
  string ext=side==0?"pvr":"png";
  script.Add($"[lyc id={layer} file=\"tex{i:00}.{ext}\"]");script.Add($"[lyprop id={layer} left={x} top={y}]");
 }
 script.AddRange(new[]{"[trans time=0]","[debugprint data=\"NATIVE-FORMATS ready\"]","[wait time=600000 input=0]"});
 File.WriteAllLines(Path.Combine(folder,"first.iet"),script);

}

}
