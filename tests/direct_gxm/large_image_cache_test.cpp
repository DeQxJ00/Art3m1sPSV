#include "../../host-direct/src/large_image_cache.hpp"
#include "../../host-direct/src/large_image_settings.hpp"
#define STB_DXT_STATIC
#define STB_DXT_IMPLEMENTATION
#include "../../host-direct/third_party/stb_dxt.h"
#include "../../host-direct/src/large_image_menu.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>
int main(){
    using namespace direct;
    auto fitted=image_texture_size(3840,8861);
    assert(fitted.w==1775&&fitted.h==4096);
    assert(image_texture_size(1920,4430).h==4096);
    assert(image_texture_size(4096,4096).w==4096);
    assert(image_texture_size(8192,1).h==1);
    std::vector<uint8_t> wide(8192*4,0);
    for(unsigned x=0;x<8192;++x){wide[x*4]=(x&1)?0:255;wide[x*4+2]=(x&1)?255:0;wide[x*4+3]=(x&1)?255:0;}
    uint8_t sampled[4];image_texture_sample(wide.data(),8192,1,image_texture_size(8192,1),0,0,sampled);
    assert(sampled[0]==0&&sampled[2]==255&&sampled[3]==128);
    std::vector<uint8_t> fittedBlocks(large_image_blocks(3840,8861,true),0);
    auto fittedPackage=large_image_package(3840,8861,123,fittedBlocks,true);
    assert(large_image_valid(fittedPackage,3840,8861,123,true));
    assert(fittedPackage[4]==4&&fittedBlocks.size()==size_t((1775+3)/4)*1024*8);
    LargeImageScope scope;assert(scope.automatic&&scope.folders==std::vector<std::string>{"image/bg"});
    assert(large_image_scope_matches(scope,"image/ev/a.png"));scope.automatic=false;
    assert(large_image_scope_matches(scope,"IMAGE\\BG\\nested\\a.png"));
    assert(!large_image_scope_matches(scope,"image/bg2/a.png"));assert(!large_image_scope_matches(scope,"image/fg/a.png"));
    assert(!large_image_scope_matches(scope,"image/bg/../a.png"));assert(!large_image_scope_matches(scope,":bg/a"));
    LargeImageScope parsed;assert(parse_large_image_scope("1 0\nimage/bg\nimage/ev\n",parsed));
    assert(!parsed.automatic&&large_image_scope_matches(parsed,"image/ev/a.png"));
    assert(!parse_large_image_scope("1 0\n../image/bg\n",parsed));
    assert(parse_large_image_scope("2 1\nimage/bg\n",parsed)&&!parsed.automatic&&!parsed.pregenerate);
    assert(parse_large_image_scope("2 2\nimage/bg\n",parsed)&&!parsed.automatic&&parsed.pregenerate);
    assert(!parse_large_image_scope("2 3\n",parsed));
    assert(parse_large_image_scope("1 0\n",parsed)&&!large_image_scope_matches(parsed,"image/bg/a.png"));
    LargeImageMenu menu;menu.scanned({"image/fg","image/bg"},false);SceTouchData touch{};
    menu.row=2;assert(menu.input(SCE_CTRL_CIRCLE,false,touch)==0&&!menu.scope.automatic);
    assert(menu.selected("image/bg")&&!menu.selected("image/fg"));
    menu.row=3;menu.input(SCE_CTRL_CIRCLE,false,touch);assert(!menu.selected("image/bg"));
    menu.row=2;menu.input(SCE_CTRL_CIRCLE,false,touch);assert(menu.scope.pregenerate&&!menu.scope.automatic);menu.input(SCE_CTRL_CIRCLE,false,touch);assert(menu.scope.automatic);menu.input(SCE_CTRL_CIRCLE,false,touch);assert(!menu.selected("image/bg"));
    menu.row=1;menu.threshold=1;menu.input(SCE_CTRL_LEFT,false,touch);assert(menu.threshold==1);
    menu.threshold=64;menu.input(SCE_CTRL_RIGHT,false,touch);assert(menu.threshold==64);
    assert(large_image_blocks(17,9)==240);
    uint8_t pixels[64],encoded[16];
    for(unsigned i=0;i<16;++i){pixels[i*4]=255;pixels[i*4+1]=0;pixels[i*4+2]=0;pixels[i*4+3]=128;}
    stb_compress_dxt_block(encoded,pixels,1,STB_DXT_HIGHQUAL);
    assert(encoded[0]==128&&encoded[1]==128);
    const unsigned color0=encoded[8]|(unsigned(encoded[9])<<8),color1=encoded[10]|(unsigned(encoded[11])<<8);
    assert((color0>>11)>=30&&(color1>>11)>=30);
    assert((color0&0x7ff)==0&&(color1&0x7ff)==0);
    std::vector<uint8_t> blocks(240,0x5a);auto data=large_image_package(17,9,123,blocks);
    assert(large_image_valid(data,17,9,123));
    assert(!large_image_valid(data,18,9,123));assert(!large_image_valid(data,17,9,124));
    auto corrupt=data;corrupt.back()^=1;assert(!large_image_valid(corrupt,17,9,123));
    corrupt=data;corrupt[4]=2;assert(!large_image_valid(corrupt,17,9,123));
    std::vector<uint8_t> bc1(120,0x5a);auto one=large_image_package(17,9,123,bc1,true);
    assert(large_image_valid(one,17,9,123,true));assert(!large_image_valid(one,17,9,123));
    assert(!large_image_valid(data,17,9,123,true));
    data.pop_back();assert(!large_image_valid(data,17,9,123));
    auto dir=std::filesystem::temp_directory_path()/"art3-large-image-settings-test";
    std::filesystem::create_directories(dir);
    const auto root=dir.string();assert(load_large_image_settings(root,"sample01"));
    assert(save_large_image_settings(root,"sample01",false));assert(!load_large_image_settings(root,"sample01"));
    assert(load_large_image_settings(root,"sample02"));
    assert(save_large_image_settings(root,"sample01",true));assert(load_large_image_settings(root,"sample01"));
    assert(load_large_image_threshold_settings(root,"sample01")==30);
    assert(save_large_image_threshold_settings(root,"sample01",16));
    assert(load_large_image_threshold_settings(root,"sample01")==16);
    assert(load_large_image_threshold_settings(root,"sample02")==30);
    assert(!save_large_image_threshold_settings(root,"sample01",0));
    assert(!save_large_image_threshold_settings(root,"sample01",65));
    assert(load_large_image_threshold_settings(root,"sample01")==16);
    const auto scopePath=large_image_scope_path(root,"sample01");
    assert(save_large_image_scope(scopePath,scope));assert(!load_large_image_scope(scopePath).automatic);
    assert(load_large_image_scope(large_image_scope_path(root,"sample02")).automatic);
    std::filesystem::remove(scopePath);
    std::filesystem::remove(large_image_threshold_settings_path(root,"sample01"));
    std::filesystem::remove(large_image_settings_path(root,"sample01"));std::filesystem::remove(dir);
    std::cout<<"PASS cache identity, corruption, dimensions, versions and independent default-on settings\n";
}
