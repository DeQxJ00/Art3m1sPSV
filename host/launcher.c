#include "launcher.h"
#include <vitaGL.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/clib.h>
#include <psp2/io/fcntl.h>
#include <dirent.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "utils/font_utils.h"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include "../vendor/vitaGL/samples/skybox_env_map/stb_image.h"

#define GAME_ROOT "ux0:data/art3m1s/games"
typedef struct {char id[64],name[256];int ready;GLuint cover,label;} Game;
extern int art3m1s_launcher_label(const unsigned char *,size_t,const unsigned char *,size_t,unsigned char *,size_t,size_t);
static unsigned char *menu_font;
static size_t menu_font_size;
static void load_font(void){
    FILE *f=fopen("ux0:data/art3m1s/menu.ttf","rb");
    if(!f)f=fopen("app0:assets/menu.ttf","rb");
    if(!f)return;
    if(fseek(f,0,SEEK_END)){fclose(f);return;}
    long n=ftell(f);
    if(n>0&&n<=32*1024*1024&&fseek(f,0,SEEK_SET)==0){
        menu_font=malloc(n);
        if(menu_font&&fread(menu_font,1,n,f)==(size_t)n)menu_font_size=n;
        else {free(menu_font);menu_font=NULL;}
    }fclose(f);
}
static Game games[64];
static GLuint letters;
typedef struct {char value[512];int scale,width,height;GLuint texture;} MenuText;
static MenuText text_cache[128];
static int text_count;
static char loading_title[256];
static void rect(float x,float y,float w,float h){
    glBegin(GL_QUADS);glTexCoord2f(0,0);glVertex2f(x,y);glTexCoord2f(1,0);glVertex2f(x+w,y);
    glTexCoord2f(1,1);glVertex2f(x+w,y+h);glTexCoord2f(0,1);glVertex2f(x,y+h);glEnd();
}
static GLuint upload(const unsigned char *pixels,int w,int h){
    GLuint tex;glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);return tex;
}
static GLuint png(const char *path){
    int w,h,c;if(!stbi_info(path,&w,&h,&c)||w<1||h<1||w>1024||h>1024)return 0;
    unsigned char *p=stbi_load(path,&w,&h,&c,4);if(!p)return 0;
    GLuint tex=upload(p,w,h);stbi_image_free(p);return tex;
}
static void text(float x,float y,const char *s,int scale){
    // Rasterize each label once; page changes reuse their cached textures.
    MenuText *label=NULL;
    for(int i=0;i<text_count;i++)if(text_cache[i].scale==scale&&!strcmp(text_cache[i].value,s)){label=&text_cache[i];break;}
    if(!label&&menu_font&&strlen(s)<sizeof(text_cache[0].value)&&text_count<128){
        int height=scale==1?20:scale*10+4,width=(int)strlen(s)*height;
        if(width>924)width=924;
        if(width<1)width=1;
        unsigned char *pixels=malloc((size_t)width*height*4);
        if(pixels&&art3m1s_launcher_label(menu_font,menu_font_size,(const unsigned char*)s,strlen(s),pixels,width,height)==0){
            label=&text_cache[text_count++];strcpy(label->value,s);
            label->scale=scale;label->width=width;label->height=height;label->texture=upload(pixels,width,height);
        }
        free(pixels);
    }
    if(label){
        glEnable(GL_TEXTURE_2D);glBindTexture(GL_TEXTURE_2D,label->texture);
        rect(x,y,label->width,label->height);glDisable(GL_TEXTURE_2D);return;
    }
    glEnable(GL_TEXTURE_2D);glBindTexture(GL_TEXTURE_2D,letters);glBegin(GL_QUADS);
    for(int n=0;s[n]&&n<70;n++){
        unsigned char c=s[n];if(c>127)c='?';
        float u=(c%16)*6.0f/96,v=(c/16)*10.0f/80,du=6.0f/96,dv=10.0f/80;
        float a=x+n*6*scale,b=y,w=6*scale,h=10*scale;
        glTexCoord2f(u,v);glVertex2f(a,b);glTexCoord2f(u+du,v);glVertex2f(a+w,b);
        glTexCoord2f(u+du,v+dv);glVertex2f(a+w,b+h);glTexCoord2f(u,v+dv);glVertex2f(a,b+h);
    }glEnd();glDisable(GL_TEXTURE_2D);
}
static int compare(const void *a,const void *b){return strcmp(((const Game*)a)->id,((const Game*)b)->id);}
int host_launcher_select(char *selected,size_t capacity){
    memset(games,0,sizeof(games));memset(text_cache,0,sizeof(text_cache));text_count=0;
    int count=0,current=0,page=0,result=0;
    load_font();
    DIR *dir=opendir(GAME_ROOT);struct dirent *entry;
    while(dir&&(entry=readdir(dir))&&count<64){
        const char *id=entry->d_name;int valid=*id&&strlen(id)<64;
        for(const char *p=id;*p;p++)if(!isalnum((unsigned char)*p)&&*p!='-'&&*p!='_')valid=0;
        if(!valid)continue;
        char path[256];snprintf(path,sizeof(path),GAME_ROOT"/%s",id);DIR *sub=opendir(path);if(!sub)continue;
        Game *g=&games[count++];snprintf(g->id,sizeof(g->id),"%s",id);snprintf(g->name,sizeof(g->name),"%s",id);
        struct dirent *file;while((file=readdir(sub))){size_t n=strlen(file->d_name);if(n>4&&!strcmp(file->d_name+n-4,".pfs"))g->ready=1;}closedir(sub);
        if(!strcmp(id,"shuffle-psv"))strcpy(g->name,"SHUFFLE EP2 / PSV");
        if(!strcmp(id,"shuffle-steam"))strcpy(g->name,"SHUFFLE EP2 / Steam");
        if(!strcmp(id,"otomeriron"))strcpy(g->name,"Otomeriron / Steam");
        snprintf(path,sizeof(path),GAME_ROOT"/%s/title.txt",id);FILE *f=fopen(path,"r");
        if(f){if(fgets(g->name,sizeof(g->name),f))g->name[strcspn(g->name,"\r\n")]=0;fclose(f);}
        snprintf(path,sizeof(path),GAME_ROOT"/%s/cover.png",id);g->cover=png(path);
        if(!g->cover){snprintf(path,sizeof(path),GAME_ROOT"/%s/saveicon.png",id);g->cover=png(path);}
        if(menu_font){
            unsigned char *label=malloc(680*32*4);
            if(label&&art3m1s_launcher_label(menu_font,menu_font_size,(const unsigned char*)g->name,strlen(g->name),label,680,32)==0)
                g->label=upload(label,680,32);
            free(label);
        }
    }if(dir)closedir(dir);qsort(games,count,sizeof(Game),compare);
    char last[64]={0};FILE *f=fopen("ux0:data/art3m1s/game.txt","r");if(f){fgets(last,sizeof(last),f);fclose(f);last[strcspn(last,"\r\n")]=0;}
    for(int i=0;i<count;i++)if(!strcmp(last,games[i].id))current=i;
    unsigned char pixels[96*80*4]={0};
    for(int c=0;c<128;c++)for(int y=0;y<10;y++)for(int x=0;x<6;x++){
        int p=(((c/16)*10+y)*96+(c%16)*6+x)*4;
        pixels[p]=pixels[p+1]=pixels[p+2]=255;pixels[p+3]=(font[c*10+y]&(1<<(7-x)))?255:0;
    }letters=upload(pixels,96,80);
    glUseProgram(0);glBindFramebuffer(GL_FRAMEBUFFER,0);glActiveTexture(GL_TEXTURE0);
    glViewport(0,0,960,544);glDisable(GL_DEPTH_TEST);glDisable(GL_SCISSOR_TEST);glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(0,960,544,0,-1,1);glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT,SCE_TOUCH_SAMPLING_STATE_START);
    SceCtrlData pad={0};sceCtrlPeekBufferPositive(0,&pad,1);uint32_t previous=pad.buttons;int was_touch=1;
    for(;;){
        sceCtrlPeekBufferPositive(0,&pad,1);uint32_t press=pad.buttons&~previous;previous=pad.buttons;
        if(count&&(press&SCE_CTRL_DOWN))current=(current+1)%count;
        if(count&&(press&SCE_CTRL_UP))current=(current+count-1)%count;
        if(count&&(press&SCE_CTRL_RIGHT))current=(current+5<count)?current+5:count-1;
        if(count&&(press&SCE_CTRL_LEFT))current=current>=5?current-5:0;
        int launch=!!(press&(SCE_CTRL_CIRCLE|SCE_CTRL_CROSS|SCE_CTRL_START));
        SceTouchData touch={0};sceTouchPeek(SCE_TOUCH_PORT_FRONT,&touch,1);
        if(touch.reportNum&&!was_touch){
            int x=touch.report[0].x/2,y=touch.report[0].y/2;
            if(x>=34&&x<926&&y>=105&&y<465){int row=(y-105)/72,index=page*5+row;if(index<count){current=index;launch=1;}}
        }was_touch=touch.reportNum>0;
        if(press&SCE_CTRL_SELECT)break;
        if(launch&&count&&games[current].ready){snprintf(selected,capacity,"%s",games[current].id);result=1;break;}
        page=current/5;
        glDisable(GL_TEXTURE_2D);glColor4f(.035f,.055f,.10f,1);rect(0,0,960,544);
        glColor4f(.93f,.96f,1,1);text(36,26,"ART3M1S",3);glColor4f(.55f,.69f,.83f,1);text(36,67,menu_font?"选择游戏":"CHOOSE A GAME",2);
        for(int row=0;row<5;row++){
            int i=page*5+row;if(i>=count)break;Game *g=&games[i];int y=105+row*72;
            glColor4f(i==current?.12f:.065f,i==current?.27f:.095f,i==current?.38f:.15f,1);rect(34,y,892,66);
            if(i==current){glColor4f(.30f,.87f,.76f,1);rect(34,y,4,66);}
            glColor4f(1,1,1,1);if(g->cover){glEnable(GL_TEXTURE_2D);glBindTexture(GL_TEXTURE_2D,g->cover);rect(48,y+5,56,56);glDisable(GL_TEXTURE_2D);}
            if(g->label){glEnable(GL_TEXTURE_2D);glBindTexture(GL_TEXTURE_2D,g->label);rect(124,y+4,680,32);glDisable(GL_TEXTURE_2D);}else text(124,y+9,g->name,2);
            glColor4f(.60f,.72f,.80f,1);text(124,y+39,g->ready?(menu_font?"独立存档":"Separate save data"):(menu_font?"缺少 PFS 文件，请复制游戏资源":"Missing PFS archive - copy game resources"),1);
            if(i==current){glColor4f(.30f,.87f,.76f,1);text(826,y+23,g->ready?(menu_font?"启动":"PLAY"):(menu_font?"未就绪":"--"),2);}
        }
        if(!count){glColor4f(1,1,1,1);text(36,140,menu_font?"未找到游戏":"No games found",2);text(36,180,menu_font?"将游戏资源放入 ux0:data/art3m1s/games/<game>":"Copy resources to ux0:data/art3m1s/games/<game>",1);}
        glColor4f(.65f,.76f,.85f,1);text(36,484,menu_font?"上下键选择    O / X / START 启动    触摸游戏即可启动":"UP/DOWN select   O/X/START play   Touch to play",2);
        char footer[160];snprintf(footer,sizeof(footer),menu_font?"第 %d / %d 页    左右键翻页    SELECT 退出":"Page %d/%d    LEFT/RIGHT page    SELECT exit",page+1,count?(count+4)/5:1);text(36,519,footer,1);
        vglSwapBuffers(GL_FALSE);sceKernelDelayThread(16667);
    }
    if(result){int fd=sceIoOpen("ux0:data/art3m1s/game.txt",SCE_O_WRONLY|SCE_O_CREAT|SCE_O_TRUNC,0666);if(fd>=0){sceIoWrite(fd,selected,strlen(selected));sceIoWrite(fd,"\n",1);sceIoClose(fd);}}
    if(result)snprintf(loading_title,sizeof(loading_title),"%s",games[current].name);
    glFinish(); // Menu draws must finish before releasing their textures.
    for(int i=0;i<count;i++){if(games[i].cover)glDeleteTextures(1,&games[i].cover);if(games[i].label)glDeleteTextures(1,&games[i].label);}
    if(!result)host_loading_finish();
    glDisable(GL_BLEND);glDisable(GL_TEXTURE_2D);return result;
}

void host_loading_show(int stage,const char *detail){
    // Main thread only, between initialization calls; never interrupt a GL draw.
    static const char *steps[]={"准备启动","创建运行环境","读取游戏资源","加载字体与脚本","进入游戏"};
    static const char *fallback[]={"Preparing","Creating runtime","Reading archives","Loading fonts and scripts","Starting game"};
    if(stage<0)stage=0;if(stage>4)stage=4;
    glUseProgram(0);glBindFramebuffer(GL_FRAMEBUFFER,0);glActiveTexture(GL_TEXTURE0);
    glViewport(0,0,960,544);glDisable(GL_DEPTH_TEST);glDisable(GL_SCISSOR_TEST);glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(0,960,544,0,-1,1);glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    glDisable(GL_TEXTURE_2D);glColor4f(.035f,.055f,.10f,1);rect(0,0,960,544);
    glColor4f(.93f,.96f,1,1);text(64,150,loading_title,3);
    glColor4f(.60f,.74f,.84f,1);text(64,218,menu_font?steps[stage]:fallback[stage],2);
    glColor4f(.12f,.22f,.30f,1);rect(64,280,832,12);
    glColor4f(.30f,.87f,.76f,1);if(stage)rect(64,280,832.0f*stage/4,12);
    // This is completed initialization stages, not an estimated byte percentage.
    char status[128];snprintf(status,sizeof(status),menu_font?"启动阶段 %d / 4":"Startup stage %d / 4",stage);
    glColor4f(.65f,.76f,.85f,1);text(64,310,status,1);
    if(detail&&*detail)text(64,352,detail,1);
    text(64,430,menu_font?"正在加载，请稍候":"Loading, please wait",2);
    vglSwapBuffers(GL_FALSE);
    glDisable(GL_BLEND);glDisable(GL_TEXTURE_2D);
}

void host_loading_finish(void){
    glFinish();
    for(int i=0;i<text_count;i++)glDeleteTextures(1,&text_cache[i].texture);
    text_count=0;free(menu_font);menu_font=NULL;menu_font_size=0;
    if(letters)glDeleteTextures(1,&letters);letters=0;
}
