#pragma once
#include "cpu_cache_menu.hpp"
#include "large_image_settings.hpp"
#include "large_image_scope.hpp"
namespace direct {
struct LargeImageMenu {
    bool enabled=true,failed=false,scanPending=true,scanFailed=false;unsigned threshold=30;int row=0;
    LargeImageScope scope;std::vector<std::string> folders;
    int count()const{return 6+(scope.automatic?0:int(folders.size()));}
    bool selected(const std::string& f)const{return std::find(scope.folders.begin(),scope.folders.end(),f)!=scope.folders.end();}
    void scanned(std::vector<std::string> found,bool error){
        for(const auto& f:scope.folders)if(std::find(found.begin(),found.end(),f)==found.end())found.push_back(f);
        std::sort(found.begin(),found.end());folders=std::move(found);scanPending=false;scanFailed=error;
    }
    int input(uint32_t pressed,bool tap,const SceTouchData& touch){
        if(pressed&(SCE_CTRL_CROSS|SCE_CTRL_SQUARE))return -1;if(scanPending)return 0;
        if(pressed&SCE_CTRL_UP)row=(row+count()-1)%count();if(pressed&SCE_CTRL_DOWN)row=(row+1)%count();
        bool choose=pressed&SCE_CTRL_CIRCLE;int step=pressed&SCE_CTRL_LEFT?-1:pressed&SCE_CTRL_RIGHT?1:0;
        if(tap){int x=touch.report[0].x/2,y=touch.report[0].y/2;if(x>=70&&x<890&&y>=110&&y<406){int next=row/8*8+(y-110)/37;if(next<count()){row=next;choose=true;if(row==1){step=x<600?-1:1;choose=false;}}}}
        if(!choose&&!step)return 0;failed=false;
        if(row==0)enabled=!enabled;
        else if(row==1)threshold=unsigned(std::clamp(int(threshold)+(step?step:1),1,64));
        else if(row==2){int mode=scope.automatic?0:scope.pregenerate?2:1;mode=(mode+(step<0?2:1))%3;scope.automatic=mode==0;scope.pregenerate=mode==2;}
        else if(row==3)scope.shrinkOversized=!scope.shrinkOversized;
        else if(row<count()-2){const auto& f=folders[row-4];auto it=std::find(scope.folders.begin(),scope.folders.end(),f);if(it!=scope.folders.end())scope.folders.erase(it);else if(scope.folders.size()<128)scope.folders.push_back(f);}
        else if(choose)return row==count()-2?1:-1;return 0;
    }
    std::string label(int i)const{
        if(i==0)return "大图材质压缩";if(i==1)return "解码后 RGBA 至少";if(i==2)return "检测范围";if(i==3)return "超 4096 图片加入预生成";
        if(i<count()-2){auto f=folders[i-4];size_t end=0;unsigned cells=0;while(end<f.size()){unsigned char c=f[end];size_t n=c<128?1:c<224?2:c<240?3:4;unsigned width=c<128?1:2;if(cells+width>42)break;cells+=width;end=std::min(end+n,f.size());}return end<f.size()?f.substr(0,end)+"...":f;}
        return i==count()-2?"保存并返回":"取消并返回";
    }
    std::string value(int i)const{
        if(i==0)return enabled?"开启":"关闭";if(i==1)return std::to_string(threshold)+" MiB";
        if(i==2)return scope.automatic?"自动运行时生成（默认）":scope.pregenerate?"手动选择文件夹预生成":"手动选择文件夹运行时生成";
        if(i==3)return scope.shrinkOversized?"开启":"关闭";
        if(i<count()-2)return selected(folders[i-4])?"[x]":"[ ]";return {};
    }
    const char* note()const{
        if(failed)return "保存失败，请重试。";if(scanPending)return "正在扫描图片目录和 PFS 索引。";
        if(scanFailed)return "部分目录或资源包无法读取；已保存的勾选仍保留。";
        return scope.automatic?"自动模式在使用图片时生成缓存，不进行启动预生成。":scope.pregenerate?"启动预生成：满足内存门槛，或开启的超限缩小条件。":"使用时生成勾选目录的缓存，首次压缩可能卡顿。";
    }
    void prepare()const{
        ui_prepare("大图材质压缩",kMenuTitleSize);ui_prepare(note(),kMenuNoteSize);
        ui_prepare("有损压缩，保留尺寸；仅当前游戏，下次启动生效。",kMenuNoteSize);
        ui_prepare("↑↓ 选择  ←→ 调整  ○ 勾选  × 取消",kMenuNoteSize);
        for(int i=row/8*8;i<std::min(count(),row/8*8+8);++i){ui_prepare(label(i).c_str(),kMenuBodySize);ui_prepare(value(i).c_str(),kMenuBodySize);}
        ui_prepare((std::to_string(row+1)+" / "+std::to_string(count())).c_str(),kMenuNoteSize);
    }
    void draw()const{
        rect(0,0,960,544,0x101b2bff);ui_text(70,58,kMenuTitleSize,"大图材质压缩");
        ui_text(70,88,kMenuNoteSize,"有损压缩，保留尺寸；仅当前游戏，下次启动生效。",0xb5c4d4ff);
        for(int i=row/8*8;i<std::min(count(),row/8*8+8);++i){float y=110+(i%8)*37;rect(70,y,820,33,i==row?0x286482ff:0x1c2838ff);ui_setting_text(86,y+26,kMenuBodySize,label(i).c_str());ui_setting_text(i==2?420:650,y+26,kMenuBodySize,value(i).c_str());}
        ui_text(70,449,kMenuNoteSize,note(),failed?0xff8080ff:0xb5c4d4ff);
        ui_text(70,483,kMenuNoteSize,(std::to_string(row+1)+" / "+std::to_string(count())).c_str());
        ui_text(70,519,kMenuNoteSize,"↑↓ 选择  ←→ 调整  ○ 勾选  × 取消");
    }
};
}
