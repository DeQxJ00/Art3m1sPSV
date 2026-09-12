#include "../../host/media_io.h"
#include <stdio.h>
#include <string.h>

int host_loop_cache_probe(const char *root,const char *report){
    FILE *log=fopen(report,"w");if(!log)return 2;
    int status=1;host_files_open(root,"ux0:data/art3m1s-loop-probe");
    const char *names[]={"sakura.ogv","sakura_m.ogv"};size_t budget=4u*1024u*1024u;
    for(int k=0;k<2;k++){
        HostMediaInput a={0},b={0};AVPacket *pa=av_packet_alloc(),*pb=av_packet_alloc();int ok=0;
        if(!pa||!pb||host_media_input_open(&a,names[k])<0||host_media_input_open(&b,names[k])<0)goto close;
        size_t cached=host_media_input_preload(&b,budget);
        if(!cached||cached>budget||b.reader)goto close;
        budget-=cached;uint64_t reads=b.read_calls;unsigned packets=0;
        for(int loop=0;loop<3;loop++){
            if(av_seek_frame(a.format,0,0,AVSEEK_FLAG_BACKWARD)<0||av_seek_frame(b.format,0,0,AVSEEK_FLAG_BACKWARD)<0)goto close;
            unsigned count=0;
            for(;;){
                int r=av_read_frame(a.format,pa),rr=av_read_frame(b.format,pb);
                if(r!=rr)goto close;
                if(r<0){if(r!=AVERROR_EOF||!count)goto close;break;}
                if(pa->size!=pb->size||pa->pts!=pb->pts||pa->dts!=pb->dts||pa->flags!=pb->flags||
                   (pa->size&&memcmp(pa->data,pb->data,pa->size)))goto close;
                count++;packets++;av_packet_unref(pa);av_packet_unref(pb);
            }
        }
        if(b.read_calls!=reads||!b.cache_reads)goto close;
        fprintf(log,"PASS %s packets=%u cache_bytes=%u cached_reads=%llu no_disk_after_preload=1\n",names[k],packets,(unsigned)cached,(unsigned long long)b.cache_reads);fflush(log);ok=1;
close:
        av_packet_free(&pa);av_packet_free(&pb);host_media_input_close(&a);host_media_input_close(&b);
        if(!ok){fprintf(log,"FAIL %s\n",names[k]);goto done;}
    }
    status=0;
done:
    host_files_close();fprintf(log,"Loop cache probe %s\n",status?"FAIL":"PASS");fclose(log);return status;
}
