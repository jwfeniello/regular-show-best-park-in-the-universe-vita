#include "button_glyphs.h"
#include <math.h>
#include <stddef.h>

int park_button_glyph(int cp) {
    return cp==0x25a1 || cp==0x25b3 || cp==0x25cb || cp==0xd7 || cp==0xe000 || cp==0xe001;
}
static int shoulder(int cp) { return cp==0xe000 || cp==0xe001; }
float park_button_advance(int cp,int size) {
    return size*(shoulder(cp)?1.55f:1.0f);
}
static float segment(float x,float y,float ax,float ay,float bx,float by) {
    float dx=bx-ax,dy=by-ay;
    float t=((x-ax)*dx+(y-ay)*dy)/(dx*dx+dy*dy);
    t=fmaxf(0,fminf(1,t)); dx=x-ax-t*dx; dy=y-ay-t*dy;
    return sqrtf(dx*dx+dy*dy);
}
static float shape_distance(int cp,float x,float y) {
    float d=100;
#define EDGE(a,b,c,e) d=fminf(d,segment(x,y,a,b,c,e))
    if (cp==0x25a1) {
        EDGE(.12f,.12f,.88f,.12f); EDGE(.88f,.12f,.88f,.88f);
        EDGE(.88f,.88f,.12f,.88f); EDGE(.12f,.88f,.12f,.12f);
    } else if (cp==0x25b3) {
        EDGE(.5f,.06f,.96f,.88f); EDGE(.96f,.88f,.04f,.88f); EDGE(.04f,.88f,.5f,.06f);
    } else if (cp==0x25cb) {
        d=fabsf(sqrtf((x-.5f)*(x-.5f)+(y-.5f)*(y-.5f))-.41f);
    } else if (cp==0xd7) {
        EDGE(.12f,.12f,.88f,.88f); EDGE(.88f,.12f,.12f,.88f);
    } else {
        /* Wide rounded rectangle, matching Vita L/R shoulder-button badges.
         * Coordinates use the same vertical unit as the face symbols. */
        float qx=fabsf(x-.85f)-.69f, qy=fabsf(y-.5f)-.34f;
        d=fabsf(sqrtf(fmaxf(qx,0)*fmaxf(qx,0)+fmaxf(qy,0)*fmaxf(qy,0))+
                fminf(fmaxf(qx,qy),0)-.08f);
        if (cp==0xe000) { EDGE(.67f,.24f,.67f,.76f); EDGE(.67f,.76f,1.04f,.76f); }
        else {
            EDGE(.64f,.76f,.64f,.24f); EDGE(.64f,.24f,.95f,.24f);
            EDGE(.95f,.24f,1.07f,.34f); EDGE(1.07f,.34f,1.04f,.46f);
            EDGE(1.04f,.46f,.94f,.51f); EDGE(.94f,.51f,.64f,.51f);
            EDGE(.87f,.51f,1.08f,.76f);
        }
    }
#undef EDGE
    return d;
}
void park_button_draw(int cp,int size,unsigned char *rgba,int width,int height,float x,int baseline) {
    if (!park_button_glyph(cp) || size<1 || !rgba || width<1 || height<1) return;
    float em=size*.8f, left=x+size*.06f, top=baseline-em;
    float gw=em*(shoulder(cp)?1.7f:1.0f);
    float radius=fmaxf(.7f,em*.052f);
    int x0=(int)floorf(left-radius),x1=(int)ceilf(left+gw+radius);
    int y0=(int)floorf(top-radius),y1=(int)ceilf(top+em+radius);
    if (x0<0) x0=0;
    if (y0<0) y0=0;
    if (x1>width) x1=width;
    if (y1>height) y1=height;
    for (int yy=y0;yy<y1;yy++) for (int xx=x0;xx<x1;xx++) {
        unsigned coverage=0;
        for (int sy=0;sy<4;sy++) for (int sx=0;sx<4;sx++) {
            float px=(xx+(sx+.5f)/4-left)/em,py=(yy+(sy+.5f)/4-top)/em;
            coverage+=shape_distance(cp,px,py)*em<=radius;
        }
        unsigned a=(coverage*255+8)/16;
        unsigned char *d=rgba+4*((size_t)yy*width+xx);
        unsigned v=a+d[3]*(255-a)/255;
        /* White premultiplied mask inherits the label's normal text color. */
        d[0]=d[1]=d[2]=d[3]=(unsigned char)v;
    }
}
