#include "assets.h"
#include "utils/logger.h"
#include "utils/dialog.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
typedef struct { char *name;uint32_t offset,stored,size,crc;uint16_t method,archive; } Entry;
static Entry *entries;static unsigned count;
static int compare(const void *a,const void *b) {return strcmp(((const Entry *)a)->name,((const Entry *)b)->name);}
void park_assets_init(void) {
    FILE *f=fopen(DATA_PATH "assets.idx","rb");uint32_t h[2];
    if(!f || fread(h,1,8,f)!=8 || h[0]!=0x31415042 || h[1]>8192)fatal_error("Missing or invalid Best Park asset index");
    count=h[1];entries=calloc(count,sizeof(*entries));if(!entries)fatal_error("Asset index allocation failed");
    for(unsigned i=0;i<count;i++) {
        uint32_t v[4];uint16_t s[3];
        if(fread(v,1,16,f)!=16 || fread(s,1,6,f)!=6 || s[2]>1023 || s[1]>1 || v[2]>32*1024*1024)fatal_error("Invalid asset index record");
        entries[i]=(Entry){malloc(s[2]+1),v[0],v[1],v[2],v[3],s[0],s[1]};
        if(!entries[i].name || fread(entries[i].name,1,s[2],f)!=s[2])fatal_error("Truncated asset index");
        entries[i].name[s[2]]=0;
    }
    fclose(f);qsort(entries,count,sizeof(*entries),compare);l_info("Indexed %u audio/font assets",count);
}
void *park_asset_read(const char *name,size_t *size) {
    *size=0;if(!name)return NULL;
    if(!strncmp(name,"assets/",7))name+=7;
    Entry key={.name=(char *)name},*e=bsearch(&key,entries,count,sizeof(*entries),compare);
    if(!e){l_warn("Asset missing: %s",name);return NULL;}
    FILE *f=fopen(e->archive?DATA_PATH "main.16.com.turner.bestparkintheuniverse.obb":DATA_PATH "game.apk","rb");
    if(!f)return NULL;
    unsigned char *data=malloc(e->size?e->size:1),*packed=NULL;
    if(!data || fseek(f,e->offset,SEEK_SET))goto fail;
    if(!e->method) {
        if(e->stored!=e->size || fread(data,1,e->size,f)!=e->size)goto fail;
    } else if(e->method==8) {
        packed=malloc(e->stored?e->stored:1);if(!packed || fread(packed,1,e->stored,f)!=e->stored)goto fail;
        z_stream z={.next_in=packed,.avail_in=e->stored,.next_out=data,.avail_out=e->size};
        if(inflateInit2(&z,-15)!=Z_OK)goto fail;
        int result=inflate(&z,Z_FINISH);inflateEnd(&z);
        if(result!=Z_STREAM_END || z.total_out!=e->size)goto fail;
    } else goto fail;
    fclose(f);free(packed);*size=e->size;return data;
fail:
    fclose(f);free(data);free(packed);l_error("Asset read failed: %s",name);return NULL;
}
