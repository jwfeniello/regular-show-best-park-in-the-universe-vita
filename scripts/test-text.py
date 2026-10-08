"""Render production text and controller glyphs with host JNI adapters and ASan.

Uses the user's APK fonts when present, otherwise a local DejaVu Sans font.
Saves previews under the ignored analysis/controller-prompts/text-preview folder.
"""
from pathlib import Path
import os
import shutil
import struct
import subprocess
import tempfile
import zipfile
import zlib

ROOT=Path(__file__).resolve().parents[1]
PREVIEW=ROOT/'analysis/controller-prompts/text-preview'
PREVIEW.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='park-text-') as folder:
    work=Path(folder)
    (work/'utils').mkdir()
    (work/'fonts').mkdir()
    for name in ('text.c','text.h','button_glyphs.c','button_glyphs.h','assets.h'):
        shutil.copyfile(ROOT/'source'/name,work/name)
    (work/'utils/logger.h').write_text('#define l_error(...) ((void)0)\n')
    (work/'park.h').write_text(r'''
#include <stdint.h>
typedef signed char jbyte;
typedef void *jobject;
typedef struct Array {int size; unsigned char data[];} *jbyteArray;
struct Interface;
typedef const struct Interface *JNIEnv;
struct Interface {
    jbyteArray (*NewByteArray)(JNIEnv *,int);
    void (*SetByteArrayRegion)(JNIEnv *,jbyteArray,int,int,const jbyte *);
    void (*DeleteLocalRef)(JNIEnv *,jbyteArray);
};
extern JNIEnv jni;
uintptr_t park_symbol(const char *name);
''')
    (work/'driver.c').write_text(r'''
#include "park.h"
#include "text.h"
#include "button_glyphs.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const char *output;
static jbyteArray alloc_array(JNIEnv *e,int n) {
    (void)e; jbyteArray a=malloc(sizeof(*a)+n);assert(a);a->size=n;return a;
}
static void put_array(JNIEnv *e,jbyteArray a,int at,int n,const jbyte *p) {
    (void)e;assert(at>=0 && n>=0 && at+n<=a->size);memcpy(a->data+at,p,n);
}
static void free_array(JNIEnv *e,jbyteArray a) {(void)e;free(a);}
static const struct Interface interface={alloc_array,put_array,free_array};
JNIEnv jni=&interface;
void *park_asset_read(const char *name,size_t *size) {
    FILE *f=fopen(name,"rb");if(!f)return NULL;
    fseek(f,0,SEEK_END);*size=ftell(f);rewind(f);
    void *data=malloc(*size);assert(data);assert(fread(data,1,*size,f)==*size);fclose(f);return data;
}
static void deliver(JNIEnv *e,jobject obj,int w,int h,jbyteArray a) {
    (void)e;(void)obj;assert(w>0 && h>0 && w*h*4==a->size);
    FILE *f=fopen(output,"wb");assert(f);
    uint32_t dims[2]={(unsigned)w,(unsigned)h};fwrite(dims,4,2,f);
    fwrite(a->data,1,a->size,f);fclose(f);
}
uintptr_t park_symbol(const char *name) {(void)name;return (uintptr_t)deliver;}
int main(int argc,char **argv) {
    assert(argc==7);output=argv[1];
    const int cps[]={0x25a1,0x25b3,0x25cb,0xd7,0xe000,0xe001};
    for(unsigned i=0;i<6;i++) for(int size=1;size<=256;size*=2) {
        unsigned char *guard=malloc(8*8*4+32);assert(guard);memset(guard,0xa5,8*8*4+32);
        park_button_draw(cps[i],size,guard+16,8,8,-2,5);
        for(int n=0;n<16;n++)assert(guard[n]==0xa5 && guard[8*8*4+16+n]==0xa5);
        free(guard);
    }
    park_text_bitmap(argv[2],argv[3],atoi(argv[4]),0x11,atoi(argv[5]),atoi(argv[6]));
    return 0;
}
''')
    apk=ROOT/'data/game.apk'
    fonts=('schuboisehandwrite.ttf','sharkformalfunnyness.ttf','cnbold.ttf')
    if apk.exists():
        with zipfile.ZipFile(apk) as z:
            for name in fonts:(work/'fonts'/name).write_bytes(z.read('assets/fonts/'+name))
    else:
        fallback=Path('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf')
        for name in fonts:shutil.copyfile(fallback,work/'fonts'/name)
    flags=['gcc','-O1','-g','-Wall','-Wextra','-Wno-misleading-indentation',
           '-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(ROOT/'lib'),
           str(work/'driver.c'),str(work/'button_glyphs.c'),'-lm']
    binary=work/'render'
    subprocess.run([*flags,str(work/'text.c'),'-o',str(binary)],check=True)
    env={**os.environ,'ASAN_OPTIONS':'detect_leaks=1'}
    def render(text,font,size,width=0,height=0,exe=binary):
        subprocess.run([str(exe),str(work/'out.rgba'),text,font,str(size),str(width),str(height)],cwd=work,env=env,check=True)
        data=(work/'out.rgba').read_bytes();w,h=struct.unpack_from('<II',data)
        assert len(data)==8+w*h*4 and any(data[11::4])
        # The native text pipeline expects a white premultiplied mask.
        assert all(len(set(data[i:i+4]))==1 for i in range(8,len(data),4))
        return data
    samples=[('Press \u25b3 during a Regular Combo',20,380,90),
             ('Press \u00d7 after a Leg Sweep',20,380,90),
             ('Press \u25a1 to attack. \u25cb retreats.',20,380,90),
             ('Press \ue000 to switch. Press \ue001 for a Super Attack.',20,380,90)]
    def png(data,path):
        w,h=struct.unpack_from('<II',data);rgba=data[8:]
        raw=b''.join(b'\0'+bytes(255-rgba[(y*w+x)*4+3] for x in range(w)) for y in range(h))
        def chunk(kind,b):return struct.pack('>I',len(b))+kind+b+struct.pack('>I',zlib.crc32(kind+b)&0xffffffff)
        path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,0,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b''))
    for font in fonts:
        for n,(text,size,w,h) in enumerate(samples):
            png(render(text,font,size,w,h),PREVIEW/(font+f'-{n}.png'))
        icons=[render(chr(cp),font,24) for cp in (0x25a1,0x25b3,0x25cb,0xd7,0xe000,0xe001)]
        assert len(set(icons))==6,'Button shapes must be distinct even if the font lacks those glyphs'
    # Ordinary game dialogue must render exactly as before this change.
    baseline=work/'baseline.c'
    reference=subprocess.run(['git','show','7ec7691b7b3c1c2a5fe3b9862997e4629793cbed:source/text.c'],
                             cwd=ROOT,capture_output=True) if shutil.which('git') else None
    if reference is not None and reference.returncode==0:
        baseline.write_bytes(reference.stdout)
        old=work/'baseline'
        subprocess.run([*flags,str(baseline),'-o',str(old)],check=True)
        for text in ('Regular Combo','Walk through the doorway.','First line\nSecond line'):
            assert render(text,fonts[0],20,240,90)==render(text,fonts[0],20,240,90,old)
        print('Ordinary dialogue matches the original renderer exactly.')
    else:
        print('Original-renderer comparison skipped: reference commit unavailable.')
    print('Inline glyphs, wrapping and clipped drawing passed under ASan/UBSan.')
    print('Text previews:',PREVIEW)
