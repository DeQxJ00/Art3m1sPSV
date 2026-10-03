#pragma once
#include "cpu_cache_settings.hpp"
#include "gpu.hpp"
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <iterator>

namespace direct {
std::vector<std::string> cpu_cache_scan_folders(const std::string& root,bool& failed);
struct CpuCacheMenu {
    enum Row{Enabled,SizeCheck,Minimum,RatioCheck,Percent,RunCheck,Mean,FirstFolder};
    CpuCacheSettings value;std::vector<std::string> folders;
    int row=0;bool failed=false,scanPending=true,scanFailed=false;
    int count()const{return FirstFolder+2+int(folders.size());}
    bool selected(const std::string& f)const{return std::find(value.folders.begin(),value.folders.end(),f)!=value.folders.end();}
    void scanned(std::vector<std::string> found,bool error){
        for(const auto& f:value.folders)if(std::find(found.begin(),found.end(),f)==found.end())found.push_back(f);
        std::sort(found.begin(),found.end());folders=std::move(found);scanPending=false;scanFailed=error;
    }
    int input(uint32_t pressed,bool tap,const SceTouchData& touch){
        if(pressed&(SCE_CTRL_CROSS|SCE_CTRL_SQUARE))return -1;
        if(scanPending)return 0;
        if(pressed&SCE_CTRL_UP)row=(row+count()-1)%count();
        if(pressed&SCE_CTRL_DOWN)row=(row+1)%count();
        bool choose=pressed&SCE_CTRL_CIRCLE;int step=pressed&SCE_CTRL_LEFT?-1:pressed&SCE_CTRL_RIGHT?1:0;
        if(tap){int x=touch.report[0].x/2,y=touch.report[0].y/2;if(x>=70&&x<890&&y>=110&&y<406){int next=(row/8)*8+(y-110)/37;if(next<count()){row=next;choose=true;if(row==Minimum||row==Percent||row==Mean){step=x<600?-1:1;choose=false;}}}}
        if(!choose&&!step)return 0;failed=false;
        if(row==Enabled)value.enabled=!value.enabled;
        else if(row==SizeCheck&&value.enabled)value.sizeCheck=!value.sizeCheck;
        else if(row==Minimum&&value.enabled&&value.sizeCheck)value.minKiB=std::clamp((step<0?value.minKiB/2:value.minKiB*2),128u,16384u);
        else if(row==RatioCheck&&value.enabled)value.ratio=!value.ratio;
        else if(row==Percent&&value.enabled&&value.ratio)value.percent=unsigned(std::clamp(int(value.percent)+5*(step?step:1),0,100));
        else if(row==RunCheck&&value.enabled)value.runs=!value.runs;
        else if(row==Mean&&value.enabled&&value.runs){static const unsigned limits[]={64,128,256,512,1024,2048,4096,8192,16384,32768,65536};auto i=std::lower_bound(std::begin(limits),std::end(limits),value.mean)-std::begin(limits);i=std::clamp<int>(int(i)+(step?step:1),0,10);value.mean=limits[i];}
        else if(row>=FirstFolder&&row<FirstFolder+int(folders.size())&&value.enabled){const auto& f=folders[row-FirstFolder];auto it=std::find(value.folders.begin(),value.folders.end(),f);if(it==value.folders.end()){if(value.folders.size()<128)value.folders.push_back(f);}else value.folders.erase(it);}
        else if(row==count()-2&&choose)return 1;
        else if(row==count()-1&&choose)return -1;
        return 0;
    }
    std::string label(int i)const{
        if(i==Enabled)return "CPU 图片缓存压缩";if(i==SizeCheck)return "RGBA 最小体积判断";if(i==Minimum)return "解码后 RGBA 至少";
        if(i==RatioCheck)return "全零透明比例判断";if(i==Percent)return "全零透明比例大于";
        if(i==RunCheck)return "连续全零平均长度判断";if(i==Mean)return "平均连续长度至少";
        if(i<count()-2){const auto& f=folders[i-FirstFolder];size_t end=0;unsigned cells=0;
            while(end<f.size()){unsigned char c=f[end];size_t n=c<128?1:c<224?2:c<240?3:4;unsigned width=c<128?1:2;
                if(cells+width>46)break;cells+=width;end=std::min(end+n,f.size());}
            return end<f.size()?f.substr(0,end)+"...":f;}
        return i==count()-2?"保存并返回":"取消并返回";
    }
    std::string text(int i)const{
        if(i==Enabled)return value.enabled?"开启":"关闭（默认）";
        if(i==SizeCheck)return value.sizeCheck?"开启":"关闭";if(i==Minimum)return std::to_string(value.minKiB)+" KiB";
        if(i==RatioCheck)return value.ratio?"开启":"关闭";if(i==Percent)return std::to_string(value.percent)+"%";
        if(i==RunCheck)return value.runs?"开启":"关闭（默认）";if(i==Mean)return std::to_string(value.mean)+" B";
        if(i<count()-2)return selected(folders[i-FirstFolder])?"[x]":"[ ]";return {};
    }
    const char* note()const{
        if(failed)return "保存失败，请重试。";if(scanPending)return "正在扫描 PNG 目录和 PFS 索引。";
        if(scanFailed)return "部分目录或资源包无法读取；已保存的勾选仍保留。";
        if(row==SizeCheck||row==Minimum)return "按宽 × 高 × 4 计算；低于门槛跳过统计和压缩。";
        if(row==RatioCheck||row==Percent)return "RGBA 四通道全零；在解码输出时统计，压缩前判断。";
        if(row==RunCheck||row==Mean)return "每 64 字节判断全零；128 KiB 边界重新计段。";
        return "勾选包含子目录；仅当前游戏，下次启动生效。";
    }
    void prepare()const{
        menu_prepare("CPU 图片缓存压缩",kMenuTitleSize);menu_prepare(note(),kMenuNoteSize);
        menu_prepare("各条件独立启用；开启的条件必须全部满足。",kMenuNoteSize);
        menu_prepare("↑↓ 选择  ←→ 调整  ○ 勾选  × 取消",kMenuNoteSize);
        for(int i=row/8*8;i<std::min(count(),row/8*8+8);++i){menu_prepare(label(i).c_str(),kMenuBodySize);menu_prepare(text(i).c_str(),kMenuBodySize);}
        menu_prepare((std::to_string(row+1)+" / "+std::to_string(count())).c_str(),kMenuNoteSize);
    }
    void draw()const{
        rect(0,0,960,544,0x101b2bff);menu_text(70,58,kMenuTitleSize,"CPU 图片缓存压缩");
        menu_text(70,88,kMenuNoteSize,"各条件独立启用；开启的条件必须全部满足。",0xb5c4d4ff);
        for(int i=row/8*8;i<std::min(count(),row/8*8+8);++i){float y=110+(i%8)*37;rect(70,y,820,33,i==row?0x286482ff:0x1c2838ff);
            bool inactive=(i>0&&i<count()-2&&!value.enabled)||(i==Minimum&&!value.sizeCheck)||(i==Percent&&!value.ratio)||(i==Mean&&!value.runs);
            menu_text(86,y+26,kMenuBodySize,label(i).c_str(),inactive?0x8895a5ff:0xffffffff);menu_text(710,y+26,kMenuBodySize,text(i).c_str(),inactive?0x8895a5ff:0xffffffff);}
        menu_text(70,449,kMenuNoteSize,note(),failed?0xff8080ff:0xb5c4d4ff);
        menu_text(70,483,kMenuNoteSize,(std::to_string(row+1)+" / "+std::to_string(count())).c_str());
        menu_text(70,519,kMenuNoteSize,"↑↓ 选择  ←→ 调整  ○ 勾选  × 取消");
    }
};
}
