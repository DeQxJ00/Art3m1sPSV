/* Construction phase follows the data bindings found in the supplied PSV ELF.
 * All string lookup, blank-rectangle parsing, frame decoding and reference
 * resolution happens here. No third-party E-mote implementation is used.
 * Full original frame masks / inheritance / simultaneous tracks remain outside
 * this adapter's supported subset. Predecoding all keys is our storage choice;
 * original frame decoding is not claimed to be eager for every key.
 */
#include "scene_runtime.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
typedef EmPsbValue V;
static V get(V v,const char *s){V r={0};if(v.data&&*v.data==33)em_psb_find(&v,(const uint8_t*)s,&r);return r;}
static unsigned count(V v){return v.data&&(*v.data==32||*v.data==33)?em_psb_array(v.data+1).count:0;}
static V at(V v,unsigned i){V r={0};if(i<count(v))em_psb_at(&r,&v,i);return r;}
static float num(V v,float d){return v.data&&*v.data!=1?em_psb_float(&v):d;}
static float prop(V v,const char *s,float d){return num(get(v,s),d);}
static const char *str(V v){return v.data&&*v.data>=21&&*v.data<=24?em_psb_string(&v):"";}
static int present(V v){return v.data&&*v.data!=1;}
static int compile_path(EmScene *s,V v,EmPath **out);
static void free_path(EmPath *p);
unsigned em_scene_frame_index(const EmSceneLayer *l,float time){
    if(l->frame_count<2)return 0;
    unsigned last=l->frame_count-1;
    /* Original forward walkers stop at count-2. A single key or a list
     * without the authored empty sentinel is a host fixture extension. */
    if(l->frames[last].type==0)--last;
    unsigned lo=0,hi=last+1;
    while(lo+1<hi){unsigned mid=lo+(hi-lo)/2;
        if(l->frames[mid].time<=time)lo=mid;else hi=mid;}
    return lo;
}
static void *allocate(EmScene *s,size_t n,size_t size){
    if(!n)return NULL;if(n>SIZE_MAX/size)return NULL;
    if(s->allocation_limit && n*size>s->allocation_limit-s->bytes)return NULL;
    void *p=calloc(n,size);if(p)s->bytes+=n*size;return p;
}
/* 811933AC tag and 8119AC18 mask/act; only allocate for authored events. */
static int event_frames(EmScene *s,V list,int tag,EmMotionEventFrame **out){
    unsigned n=count(list);int active=0;
    for(unsigned i=0;i<n;++i){V f=at(list,i),c=get(f,"content");unsigned type=(unsigned)prop(f,"type",0);
        if(tag){if(type==1&&(prop(c,"align",0)||prop(c,"sync",0)||*str(get(c,"action"))))active=1;}
        else if(type&&((unsigned)prop(c,"mask",0)&0x40000))active=1;
    }
    if(!active)return 1;if(n<2)return 0;
    *out=allocate(s,n,sizeof(**out));if(!*out)return 0;s->total_event_frames+=n;
    for(unsigned i=0;i<n;++i){V f=at(list,i),c=get(f,"content");EmMotionEventFrame *e=*out+i;
        e->time=prop(f,"time",0);e->type=(unsigned)prop(f,"type",0);
        if(!isfinite(e->time)||(i&&e->time<(*out)[i-1].time))return 0;
        if(tag){e->align=prop(c,"align",0)!=0;e->sync=prop(c,"sync",0)!=0;e->action=str(get(c,"action"));}
        else{e->mask=e->type?(unsigned)prop(c,"mask",0):0;e->action=str(get(c,"act"));}
    }return 1;
}
static int motion_index(EmScene *s,V v){
    if(!v.data)return -1;
    for(unsigned i=0;i<s->motion_count;++i)if(s->motions[i].source.data==v.data)return (int)i;
    return -1;
}
static EmSceneContent content(EmScene *s,V root,V v,const char *const *atlases,unsigned atlas_count){
    EmSceneContent c={0};c.mask=(unsigned)prop(v,"mask",0);
    if(c.mask&1){c.content_origin_x=prop(v,"ox",0);c.content_origin_y=prop(v,"oy",0);}
    V coord=(c.mask&2)?get(v,"coord"):(V){0},cp=(c.mask&0x2000000)?get(get(v,"mesh"),"bp"):(V){0};
    const char *curve_keys[]={"ccc","occ","acc","zcc","scc"};
    const unsigned curve_masks[]={0x800,0x8000,0x1000,0x2000,0x4000};
    for(unsigned i=0;i<5;++i)c.curves[i]=(c.mask&curve_masks[i])?(int)prop(v,curve_keys[i],-1):-1;
    for(unsigned i=0;i<3;++i)c.coord[i]=num(at(coord,i),0);
    c.zx=(c.mask&0x60)?prop(v,"zx",0):1;c.zy=(c.mask&0x60)?prop(v,"zy",0):1;
    c.angle=(c.mask&0x10)?prop(v,"angle",0):0;
    V opacity=get(v,"opa");c.opa=(c.mask&0x400)?(opacity.data?(uint8_t)em_psb_int(&opacity):0):255;
    c.shear_x=(c.mask&0x180)?prop(v,"sx",0):0;c.shear_y=(c.mask&0x180)?prop(v,"sy",0):0;
    c.blend_mode=(c.mask&0x20000)?(int)prop(v,"bm",0):16;
    c.time_interval=(c.mask&0x4000000)?(int)prop(v,"ti",0):0;
    V color=(c.mask&0x200)?get(v,"color"):(V){0};for(int j=0;j<4;++j){V q=color.data&&*color.data==32?at(color,j):color;c.colors[j]=q.data?(uint32_t)em_psb_int(&q):(c.blend_mode&0xf0)?0x808080ffu:0xffffffffu;}
    c.fx=(c.mask&0xc)&&prop(v,"fx",0)!=0;c.fy=(c.mask&0xc)&&prop(v,"fy",0)!=0;c.has_patch=count(cp)==32;
    for(unsigned i=0;i<16;++i)c.cp[i]=(EmPoint){num(at(cp,2*i),(i%4)/3.0f),num(at(cp,2*i+1),(i/4)/3.0f)};
    const char *src=str(get(v,"src")),*icon=str(get(v,"icon"));
    c.atlas=-1;c.child_motion=motion_index(s,get(get(get(get(root,"object"),src),"motion"),icon));
    c.child_time_offset=prop(get(v,"motion"),"timeOffset",0);
    c.skip_sprite=!*src||!strcmp(src,"blank");
    if(!strcmp(src,"blank"))sscanf(icon,"%f:%f:%f:%f",&c.width,&c.height,&c.origin_x,&c.origin_y);
    else for(unsigned i=0;i<atlas_count;++i)if(!strcmp(src,atlases[i])){
        V ic=get(get(get(get(root,"source"),src),"icon"),icon);
        c.atlas=(int)i;c.icon_valid=ic.data!=NULL;
        c.left=prop(ic,"left",0);c.top=prop(ic,"top",0);c.attr=(int)prop(ic,"attr",0);
        c.width=prop(ic,"width",0);c.height=prop(ic,"height",0);c.w=c.width;c.h=c.height;
        c.origin_x=prop(ic,"originX",0);c.origin_y=prop(ic,"originY",0);c.ox=c.origin_x;c.oy=c.origin_y;
        if((unsigned)prop(v,"mask",0)&1){c.ox=prop(v,"ox",0);c.oy=prop(v,"oy",0);}break;
    }
    return c;
}
static int layer_count(V list,unsigned depth,unsigned *n){
    if(depth>64)return 0;
    for(unsigned i=0;i<count(list);++i){if(++*n>8192||!layer_count(get(at(list,i),"children"),depth+1,n))return 0;}
    return 1;
}
static unsigned u32le(const uint8_t *p){return (unsigned)p[0]|(unsigned)p[1]<<8|(unsigned)p[2]<<16|(unsigned)p[3]<<24;}
static const uint8_t *extra_mesh(V v,unsigned expected){
    const EmPsb *p=v.psb;if(!p||p->version!=4||!v.data||*v.data<34||*v.data>37||p->length<56)return NULL;
    unsigned index=0;for(unsigned i=0;i<(unsigned)(*v.data-33);++i)index|=(unsigned)v.data[i+1]<<(8*i);
    unsigned ao=u32le(p->raw+44),az=u32le(p->raw+48),base=u32le(p->raw+52);
    if(ao>=p->length||az>=p->length||base>=p->length)return NULL;
    EmPsbArray offsets=em_psb_array(p->raw+ao),sizes=em_psb_array(p->raw+az);
    if(index>=offsets.count||index>=sizes.count)return NULL;
    unsigned offset=em_psb_array_get(offsets,index),size=em_psb_array_get(sizes,index);
    if(size!=expected||offset>p->length-base||size>p->length-base-offset)return NULL;
    return p->raw+base+offset;
}
static int layers(EmScene *s,EmSceneMotion *m,V root,V list,int parent,unsigned *used,const char *const *atlases,unsigned atlas_count){
    for(unsigned i=0;i<count(list);++i){
        V v=at(list,i),p=get(v,"parameterize"),fs=get(v,"frameList");unsigned index=(*used)++;
        EmSceneLayer *l=m->layers+index;l->label=str(get(v,"label"));l->parent=parent;l->type=(int)prop(v,"type",0);
        l->has_parameter=present(p);l->parameter=(int)num(p,0);l->inherit_mask=(unsigned)prop(v,"inheritMask",0);
        V order=get(v,"transformOrder");const int default_order[]={0,3,2,1};for(int j=0;j<4;++j)l->transform_order[j]=(int)num(at(order,j),default_order[j]);
        l->mesh_transform=(int)prop(v,"meshTransform",0);l->mesh_combine=prop(v,"meshCombine",0)!=0;
        l->mesh_sync_child_mask=(unsigned)prop(v,"meshSyncChildMask",0);
        l->stencil_type=(int)prop(v,"stencilType",0);
        l->motion_independent_layer_inherit=prop(v,"motionIndependentLayerInherit",0)!=0;
        l->coordinate=(int)prop(v,"coordinate",0);l->shape=(int)prop(v,"shape",0);
        l->frame_count=count(fs);l->frames=allocate(s,l->frame_count,sizeof(*l->frames));
        if(l->frame_count&&!l->frames)return 0;
        s->total_frames+=l->frame_count;
        if(!event_frames(s,fs,0,&l->event_frames))return 0;
        for(unsigned j=0;j<l->frame_count;++j){
            V f=at(fs,j);l->frames[j]=(EmSceneFrame){prop(f,"time",0),(int)prop(f,"type",0),content(s,root,get(f,"content"),atlases,atlas_count)};
            EmSceneContent *fc=&l->frames[j].content;
            if((fc->mask&0x10000)&&!compile_path(s,get(get(f,"content"),"cp"),&fc->path))return 0;
            if(l->frames[j].type!=3){fc->time_interval=0;for(unsigned k=0;k<5;++k)fc->curves[k]=-1;}
            for(unsigned k=0;k<5;++k){int curve=l->frames[j].content.curves[k];if(curve<-1||(curve>=0&&(unsigned)curve>=s->curve_count))return 0;}
        }
        V combinators=get(get(v,"meshCombinator"),"combinatorList");l->combinator_count=count(combinators);
        l->combinators=allocate(s,l->combinator_count,sizeof(*l->combinators));if(l->combinator_count&&!l->combinators)return 0;
        for(unsigned j=0;j<l->combinator_count;++j){
            V item=at(combinators,j),var=get(item,"variable");EmSceneCombinator *q=l->combinators+j;
            q->id=str(get(var,"key"));q->begin=prop(var,"rangeBegin",0);q->end=prop(var,"rangeEnd",0);q->count=(unsigned)prop(var,"meshCount",0);
            q->neutral=(int)prop(item,"neutralIndex",-1);
            if(prop(item,"meshType",0)!=1||q->count<2||q->count>256||q->end<=q->begin)return 0;
            const uint8_t *data=extra_mesh(get(item,"rawMeshList"),q->count*128);if(!data)return 0;
            q->patches=allocate(s,q->count,16*sizeof(EmPoint));if(!q->patches)return 0;
            memcpy(q->patches,data,q->count*128);
        }
        if(!layers(s,m,root,get(v,"children"),(int)index,used,atlases,atlas_count))return 0;
    }
    return 1;
}
/* 8118E18E resolves composite mask names within the owning motion. */
static int masks(EmScene *s,EmSceneMotion *m,V list,unsigned *used){
 for(unsigned i=0;i<count(list);++i){V v=at(list,i);EmSceneLayer *l=m->layers+(*used)++;V names=get(v,"stencilCompositeMaskLayerList");
  unsigned n=count(names);if(n>32)return 0;l->mask_layers=allocate(s,n,sizeof(int));if(n&&!l->mask_layers)return 0;
  for(unsigned j=0;j<n;++j){const char *name=str(at(names,j));for(unsigned k=0;k<m->layer_count;++k)if(!strcmp(m->layers[k].label,name)&&(m->layers[k].type==0||m->layers[k].type==3)){l->mask_layers[l->mask_count++]=k;break;}}
  if(!masks(s,m,get(v,"children"),used))return 0;
 }return 1;
}
/* 811982BA reads precomputed x/y/p arrays; p is the second derivative,
 * not a Bezier control point. Ownership/validation below belong to this host. */
static int compile_block(EmScene *s,V v,EmCurveBlock *b){
    V x=get(v,"x"),y=get(v,"y"),p=get(v,"p");unsigned n=count(x);
    if(n<2||count(y)!=n||count(p)!=n)return 0;
    b->begin=allocate(s,n,sizeof(EmCurveNode));if(!b->begin)return 0;b->end=b->capacity_end=b->begin+n;
    for(unsigned k=0;k<n;++k){EmCurveNode *q=b->begin+k;*q=(EmCurveNode){num(at(x,k),NAN),num(at(y,k),NAN),num(at(p,k),NAN)};
        if(!isfinite(q->x)||!isfinite(q->y)||!isfinite(q->second)||(k&&q->x<=q[-1].x))return 0;}
    return 1;
}
static void free_path(EmPath *p){
    if(!p)return;
    if(p->blocks)for(size_t j=0;j+1<(size_t)(p->times_end-p->times);++j)free(p->blocks[j].begin);
    free(p->blocks);free(p->x);free(p->y);free(p->times);free(p);
}
/* 81199014: x/y Bezier control coordinates, t segment boundaries, and
 * s per-segment spline remapping. Each segment shares its last control. */
static int compile_path(EmScene *s,V v,EmPath **out){
    *out=NULL;if(!present(v))return 1;
    V x=get(v,"x"),y=get(v,"y"),t=get(v,"t"),blocks=get(v,"s");unsigned n=count(t);
    if(n<2||n>4096||count(x)!=(n-1)*3+1||count(y)!=count(x)||count(blocks)!=n-1)return 0;
    EmPath *p=allocate(s,1,sizeof(*p));if(!p)return 0;*out=p;
    p->x=allocate(s,count(x),sizeof(float));p->y=allocate(s,count(y),sizeof(float));p->times=allocate(s,n,sizeof(float));
    if(!p->x||!p->y||!p->times)return 0;
    p->x_end=p->x_capacity=p->x+count(x);p->y_end=p->y_capacity=p->y+count(y);p->times_end=p->times_capacity=p->times+n;
    p->blocks=allocate(s,n-1,sizeof(EmCurveBlock));if(!p->blocks)return 0;
    for(unsigned j=0;j<count(x);++j){p->x[j]=num(at(x,j),NAN);p->y[j]=num(at(y,j),NAN);if(!isfinite(p->x[j])||!isfinite(p->y[j]))return 0;}
    for(unsigned j=0;j<n;++j){p->times[j]=num(at(t,j),NAN);if(!isfinite(p->times[j])||(j&&p->times[j]<=p->times[j-1]))return 0;}
    if(p->times[0]>0||p->times[n-1]<1)return 0;
    for(unsigned j=0;j<n-1;++j)if(!compile_block(s,at(blocks,j),p->blocks+j))return 0;
    return 1;
}
static int compile_curves(EmScene *s,V root){
    V list=get(root,"easing");s->curve_count=count(list);
    s->curves=allocate(s,s->curve_count,sizeof(*s->curves));if(s->curve_count&&!s->curves)return 0;
    for(unsigned i=0;i<s->curve_count;++i){EmSceneCurve *c=s->curves+i;V blocks=at(list,i);c->block_count=count(blocks);
        if(!c->block_count)return 0;c->curve.blocks=allocate(s,c->block_count,sizeof(EmCurveBlock));if(!c->curve.blocks)return 0;
        float previous=0;
        for(unsigned j=0;j<c->block_count;++j){EmCurveBlock *b=c->curve.blocks+j;
            if(!compile_block(s,at(blocks,j),b))return 0;
            if(j&&b->begin[0].x!=previous)return 0;previous=b->end[-1].x;
        }
        if(c->curve.blocks[0].begin[0].x>0||c->curve.blocks[c->block_count-1].end[-1].x<1)return 0;
    }return 1;
}
float em_scene_curve(const EmScene *s,int index,float ratio){
    if(index<0||(unsigned)index>=s->curve_count)return ratio;
    const EmSceneCurve *c=s->curves+index;int32_t block=0,segment=0;
    float low=c->curve.blocks[0].begin[0].x,high=c->curve.blocks[c->block_count-1].end[-1].x;
    return em_curve_sample(&c->curve,&block,&segment,fminf(high,fmaxf(low,ratio)));
}
int em_scene_clone(EmScene *s,const EmScene *prototype){
    if(!s||!prototype||!prototype->motions||prototype->borrowed_inputs)return 0;
    *s=*prototype;s->borrowed_inputs=1;s->bytes=0;s->allocation_limit=0;
    s->motions=allocate(s,s->motion_count,sizeof(*s->motions));
    if(!s->motions){memset(s,0,sizeof(*s));s->base_motion=-1;return 0;}
    for(unsigned i=0;i<s->motion_count;++i){
        s->motions[i]=prototype->motions[i];
        s->motions[i].parameters=allocate(s,s->motions[i].parameter_count,sizeof(EmSceneParameter));
        if(s->motions[i].parameter_count&&!s->motions[i].parameters){em_scene_free(s);return 0;}
        for(unsigned j=0;j<s->motions[i].parameter_count;++j){
            s->motions[i].parameters[j]=prototype->motions[i].parameters[j];
            s->motions[i].parameters[j].frame=0;s->motions[i].parameters[j].epoch=0;
        }
    }
    return 1;
}
void em_scene_free(EmScene *s){
    if(s->borrowed_inputs){
        if(s->motions)for(unsigned i=0;i<s->motion_count;++i)free(s->motions[i].parameters);
        free(s->motions);memset(s,0,sizeof(*s));s->base_motion=-1;return;
    }
    if(s->curves)for(unsigned i=0;i<s->curve_count;++i){EmSceneCurve *c=s->curves+i;
        if(c->curve.blocks)for(unsigned j=0;j<c->block_count;++j)free(c->curve.blocks[j].begin);free(c->curve.blocks);}
    free(s->curves);
    if(s->motions)for(unsigned i=0;i<s->motion_count;++i){EmSceneMotion *m=s->motions+i;
        if(m->layers)for(unsigned j=0;j<m->layer_count;++j){EmSceneLayer *l=m->layers+j;
            if(l->frames)for(unsigned k=0;k<l->frame_count;++k)free_path(l->frames[k].content.path);free(l->frames);free(l->event_frames);
            if(l->combinators)for(unsigned k=0;k<l->combinator_count;++k)free(l->combinators[k].patches);free(l->combinators);free(l->mask_layers);}
        if(m->priorities)for(unsigned j=0;j<m->priority_count;++j)free(m->priorities[j].indices);
        free(m->layers);free(m->parameters);free(m->priorities);free(m->tags);
    }
    free(s->motions);memset(s,0,sizeof(*s));s->base_motion=-1;
}
int em_scene_compile(EmScene *s,V root,const char *const *atlases,unsigned atlas_count){
    return em_scene_compile_limited(s,root,atlases,atlas_count,0);
}
int em_scene_compile_limited(EmScene *s,V root,const char *const *atlases,unsigned atlas_count,size_t limit){
    memset(s,0,sizeof(*s));s->allocation_limit=limit;s->base_motion=-1;V objects=get(root,"object");
    if(!compile_curves(s,root))goto fail;
    for(unsigned i=0;i<count(objects);++i){V o;em_psb_object_at(&o,&objects,i);s->motion_count+=count(get(o,"motion"));}
    if(!s->motion_count||s->motion_count>4096)goto fail;
    s->motions=allocate(s,s->motion_count,sizeof(*s->motions));if(!s->motions)goto fail;
    unsigned index=0;
    for(unsigned i=0;i<count(objects);++i){V o;em_psb_object_at(&o,&objects,i);V motions=get(o,"motion");
        for(unsigned j=0;j<count(motions);++j)em_psb_object_at(&s->motions[index++].source,&motions,j);
    }
    s->empty_frame.content=content(s,root,(V){0},atlases,atlas_count);
    for(unsigned i=0;i<s->motion_count;++i){EmSceneMotion *m=s->motions+i;V v=m->source,p=get(v,"parameterize"),params=get(v,"parameter"),pri=get(v,"priority"),ls=get(v,"layer");
        m->has_parameter=present(p);m->parameter=(int)num(p,0);
        if(!event_frames(s,get(v,"tag"),1,&m->tags))goto fail;m->tag_count=m->tags?count(get(v,"tag")):0;
        m->length=prop(v,"lastTime",0);m->loop_time=prop(v,"loopTime",-1);m->loop_begin=prop(v,"loopBegin",-1);m->loop_end=prop(v,"loopEnd",-1);
        m->parameter_count=count(params);m->parameters=allocate(s,m->parameter_count,sizeof(*m->parameters));if(m->parameter_count&&!m->parameters)goto fail;
        for(unsigned j=0;j<m->parameter_count;++j){V x=at(params,j);EmSceneParameter *q=m->parameters+j;q->id=str(get(x,"id"));q->range.begin=prop(x,"rangeBegin",0);q->range.end=prop(x,"rangeEnd",0);q->range.division=prop(x,"division",0);q->range.discretize=prop(x,"discretization",0)!=0;}
        if(!layer_count(ls,0,&m->layer_count))goto fail;
        m->layers=allocate(s,m->layer_count,sizeof(*m->layers));if(m->layer_count&&!m->layers)goto fail;
        m->pose_offset=s->total_layers;unsigned used=0;if(!layers(s,m,root,ls,-1,&used,atlases,atlas_count))goto fail;s->total_layers+=used;
        used=0;if(!masks(s,m,ls,&used))goto fail;
        m->priority_count=count(pri);m->priorities=allocate(s,m->priority_count,sizeof(*m->priorities));if(m->priority_count&&!m->priorities)goto fail;
        for(unsigned j=0;j<m->priority_count;++j){V f=at(pri,j),order=get(f,"content");EmScenePriority *q=m->priorities+j;q->time=prop(f,"time",0);q->count=count(order);q->indices=allocate(s,q->count,sizeof(*q->indices));if(q->count&&!q->indices)goto fail;
            for(unsigned k=0;k<q->count;++k)q->indices[k]=(int)num(at(order,k),-1);
        }
    }
    V base=get(get(root,"metadata"),"base");s->base_motion=motion_index(s,get(get(get(objects,str(get(base,"chara"))),"motion"),str(get(base,"motion"))));
    if(s->base_motion<0)goto fail;return 1;
fail:em_scene_free(s);return 0;
}

/* Reserve only reachable reference depths, including every authored key. */
int em_scene_node_capacities(const EmScene *s,unsigned capacities[9]){
    memset(capacities,0,9*sizeof(*capacities));
    if(s->base_motion<0||(unsigned)s->base_motion>=s->motion_count)return 0;
    unsigned char *reachable=calloc((size_t)s->motion_count*9,1);if(!reachable)return 0;
    reachable[s->base_motion]=1;
    for(unsigned depth=0;depth<9;++depth)for(unsigned i=0;i<s->motion_count;++i){
        if(!reachable[depth*s->motion_count+i])continue;
        const EmSceneMotion *m=s->motions+i;
        if(m->layer_count>capacities[depth])capacities[depth]=m->layer_count;
        if(depth==8)continue;
        for(unsigned l=0;l<m->layer_count;++l)for(unsigned f=0;f<m->layers[l].frame_count;++f){
            int child=m->layers[l].frames[f].content.child_motion;
            if(child>=0&&(unsigned)child<s->motion_count)reachable[(depth+1)*s->motion_count+child]=1;
        }
    }
    free(reachable);return 1;
}
