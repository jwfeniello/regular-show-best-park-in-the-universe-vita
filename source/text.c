#include "text.h"
#include "park.h"
#include "assets.h"
#include "utils/logger.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb/stb_truetype.h>
typedef struct {char name[160];unsigned char *bytes;stbtt_fontinfo font;} Font;
static Font fonts[12];
static Font *get_font(const char *name) {
    char path[160];snprintf(path,sizeof(path),"%s%s",strchr(name,'/')?"":"fonts/",name);
    if(!strchr(path,'.'))strncat(path,".ttf",sizeof(path)-strlen(path)-1);
    for(unsigned i=0;i<12;i++)if(fonts[i].bytes && !strcmp(fonts[i].name,path))return &fonts[i];
    for(unsigned i=0;i<12;i++)if(!fonts[i].bytes) {
        size_t size;fonts[i].bytes=park_asset_read(path,&size);
        if(!fonts[i].bytes)fonts[i].bytes=park_asset_read("fonts/cnbold.ttf",&size);
        if(!fonts[i].bytes)return NULL;
        if(!stbtt_InitFont(&fonts[i].font,fonts[i].bytes,stbtt_GetFontOffsetForIndex(fonts[i].bytes,0))) {free(fonts[i].bytes);fonts[i].bytes=NULL;return NULL;}
        snprintf(fonts[i].name,sizeof(fonts[i].name),"%s",path);return &fonts[i];
    }
    return &fonts[0];
}
static int next_cp(const unsigned char **at) {
    const unsigned char *p=*at;unsigned c=*p++;int n=0;
    if(c>=0xc2 && c<=0xdf){c&=31;n=1;}else if(c>=0xe0 && c<=0xef){c&=15;n=2;}else if(c>=0xf0 && c<=0xf4){c&=7;n=3;}else if(c>=128)c=0xfffd;
    while(n--){if((*p&0xc0)!=0x80){c=0xfffd;break;}c=(c<<6)|(*p++&63);}
    *at=p;return (int)c;
}
void park_text_bitmap(const char *text,const char *name,int size,int align,int width,int height) {
    Font *f=get_font(name);if(!f)return;
    if(size<1)size=1;if(size>256)size=256;
    int cp[8192],n=0;const unsigned char *at=(const unsigned char *)text;
    while(*at && n<8191)cp[n++]=next_cp(&at);
    float scale=stbtt_ScaleForMappingEmToPixels(&f->font,(float)size);
    int ascent,descent,gap;stbtt_GetFontVMetrics(&f->font,&ascent,&descent,&gap);
    int lineheight=(int)ceilf((ascent-descent+gap)*scale),baseline=(int)ceilf(ascent*scale);
    if(lineheight<1)lineheight=1;
    struct {int start,end;float width;} lines[256]={0};int count=0,start=0,maxwidth=1;
    while(start<n && count<256) {
        int i=start,lastspace=-1;float w=0,spacewidth=0;
        for(;i<n && cp[i]!='\n';i++) {
            int advance;stbtt_GetCodepointHMetrics(&f->font,cp[i],&advance,NULL);
            float dx=advance*scale;if(i>start)dx+=stbtt_GetCodepointKernAdvance(&f->font,cp[i-1],cp[i])*scale;
            if(width>0 && w+dx>width && i>start){if(lastspace>=start){i=lastspace;w=spacewidth;}break;}
            if(cp[i]==' '){lastspace=i;spacewidth=w;}w+=dx;
        }
        lines[count].start=start;lines[count].end=i;lines[count++].width=w;
        if((int)ceilf(w)>maxwidth)maxwidth=(int)ceilf(w);
        start=i;if(start<n && (cp[start]=='\n' || cp[start]==' '))start++;
        if(start==i && i==lines[count-1].start)start++;
    }
    if(count==0)count=1;
    int w=width>0?width:maxwidth,h=height>0?height:count*lineheight;
    if(w<1)w=1;if(h<1)h=1;if(w>4096 || h>4096){l_error("Text bitmap too large %dx%d",w,h);return;}
    size_t bytes=(size_t)w*h*4;unsigned char *rgba=calloc(1,bytes);if(!rgba)return;
    int y=baseline,total=count*lineheight,va=(align>>4)&15;
    if(h>total){if(va==2)y+=h-total;else if(va==3)y+=(h-total)/2;}
    for(int l=0;l<count;l++,y+=lineheight) {
        float x=(align&15)==2?w-lines[l].width:(align&15)==3?(w-lines[l].width)/2:0;
        for(int i=lines[l].start;i<lines[l].end;i++) {
            if(i>lines[l].start)x+=stbtt_GetCodepointKernAdvance(&f->font,cp[i-1],cp[i])*scale;
            int bw,bh,bx,by;unsigned char *b=stbtt_GetCodepointBitmap(&f->font,0,scale,cp[i],&bw,&bh,&bx,&by);
            if(b)for(int yy=0;yy<bh;yy++)for(int xx=0;xx<bw;xx++) {
                int dx=(int)floorf(x)+bx+xx,dy=y+by+yy;if(dx<0 || dy<0 || dx>=w || dy>=h)continue;
                unsigned a=b[yy*bw+xx];unsigned char *d=rgba+4*((size_t)dy*w+dx);
                unsigned v=a+d[3]*(255-a)/255;d[0]=d[1]=d[2]=d[3]=(unsigned char)v;
            }
            stbtt_FreeBitmap(b,NULL);int adv;stbtt_GetCodepointHMetrics(&f->font,cp[i],&adv,NULL);x+=adv*scale;
        }
    }
    void (*deliver)(JNIEnv *,jobject,int,int,jbyteArray)=(void *)park_symbol("Java_org_cocos2dx_lib_Cocos2dxBitmap_nativeInitBitmapDC");
    jbyteArray arr=(*jni).NewByteArray(&jni,bytes);
    if(arr){(*jni).SetByteArrayRegion(&jni,arr,0,bytes,(jbyte *)rgba);deliver(&jni,NULL,w,h,arr);(*jni).DeleteLocalRef(&jni,arr);}
    free(rgba);
}
