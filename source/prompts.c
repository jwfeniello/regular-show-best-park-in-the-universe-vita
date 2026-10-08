#include "prompts.h"
#include "button_glyphs.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

typedef struct { char *text; size_t used, capacity; bool failed; } Buffer;
static void append(Buffer *b, const char *s, size_t n) {
    if (b->failed) return;
    if (n > 4*1024*1024 || b->used > 4*1024*1024-n) { b->failed=true; return; }
    if (b->used+n+1 > b->capacity) {
        size_t capacity=(b->used+n+1)*2;
        char *next=realloc(b->text,capacity);
        if (!next) { b->failed=true; return; }
        b->text=next; b->capacity=capacity;
    }
    memcpy(b->text+b->used,s,n); b->used+=n; b->text[b->used]=0;
}
static bool ends_with(const char *s, const char *suffix) {
    size_t n=strlen(s), m=strlen(suffix);
    return n>=m && !strcmp(s+n-m,suffix);
}
static bool resource(const char *path, const char *name) {
    size_t n=strlen(path), m=strlen(name);
    return n>=m && !strcmp(path+n-m,name) && (n==m || path[n-m-1]=='/');
}

static const char *fixed_text(const char *key) {
    static const struct { const char *key, *text; } rows[]={
        {"instruction1", "Use the left stick or D-pad to walk."},
        {"instruction2", "Press " PARK_ICON_SQUARE " to attack; repeat for combos. " PARK_ICON_CIRCLE " retreats."},
        {"instruction3", "Press " PARK_ICON_L " to switch characters."},
        {"instruction4", "Press " PARK_ICON_R " when your Super Meter is full."},
        {"tutorial0", "WALK:|Use the left stick or D-pad|to walk and pick up a coin"},
        {"tutorial1", "ATTACK:|Press " PARK_ICON_SQUARE " to attack|the enemy"},
        {"tutorial2", "COMBO:|Press " PARK_ICON_SQUARE " repeatedly|to do a combo"},
        {"tutorial3", "TAG:|Press " PARK_ICON_L " to switch|between characters"},
        {"tutorial4", "SUPER ATTACK:|When your Super Meter is full,|press " PARK_ICON_R " to do a Super Attack"},
    };
    for (size_t i=0;i<sizeof(rows)/sizeof(*rows);i++)
        if (!strcmp(key,rows[i].key)) return rows[i].text;
    return NULL;
}
static bool skill_description(const char *key) {
    static const char *characters[]={"mordecai_","rigby_","muscleman_","pops_"};
    for (size_t i=0;i<4;i++)
        if (!strncmp(key,characters[i],strlen(characters[i])) && ends_with(key,"_description")) return true;
    return false;
}
static void skill_text(Buffer *out, const char *s, bool *changed) {
    static const struct {const char *from,*to;} rows[]={
        {"Swipe Forward", "Press " PARK_ICON_SQUARE},
        {"Swipe Down", "Press " PARK_ICON_CROSS},
        {"Swipe Up", "Press " PARK_ICON_TRIANGLE},
        {"Keep swiping Forward", "Keep pressing " PARK_ICON_SQUARE},
        {"Tap with 2 fingers to use", "Press " PARK_ICON_R " to use"},
    };
    while (*s) {
        size_t i;
        for (i=0;i<sizeof(rows)/sizeof(*rows);i++) {
            size_t n=strlen(rows[i].from);
            if (!strncmp(s,rows[i].from,n)) {
                append(out,rows[i].to,strlen(rows[i].to)); s+=n; *changed=true; break;
            }
        }
        if (i==sizeof(rows)/sizeof(*rows)) append(out,s++,1);
    }
}
static void rewrite_locale(Buffer *out, char *text, bool *changed) {
    for (char *line=text; *line;) {
        char *end=strchr(line,'\n'); if (!end) end=line+strlen(line);
        char saved=*end; *end=0;
        char *colon=strchr(line,':');
        if (colon) {
            *colon=0;
            const char *replacement=fixed_text(line);
            bool skill=skill_description(line);
            *colon=':';
            if (replacement) {
                append(out,line,(size_t)(colon-line)+1);
                append(out,replacement,strlen(replacement));
                if (end>line && end[-1]=='\r') append(out,"\r",1);
                *changed=true;
            } else if (skill) {
                append(out,line,(size_t)(colon-line)+1); skill_text(out,colon+1,changed);
            } else append(out,line,(size_t)(end-line));
        } else append(out,line,(size_t)(end-line));
        if (saved) append(out,"\n",1);
        *end=saved; line=end+(saved!=0);
    }
}
static const char *gesture_icon(const char *name) {
    if (!strcmp(name,"double_tap")) return "r";
    if (!strcmp(name,"tap") || !strcmp(name,"tap_and_hold")) return "stick";
    if (!strcmp(name,"swipe_up_1")) return "triangle";
    if (!strncmp(name,"swipe_down_",11) && strlen(name)==12 && name[11]>='1' && name[11]<='3') return "cross";
    if (!strncmp(name,"swipe_right_",12) && strlen(name)==13 && name[12]>='1' && name[12]<='4') return "square";
    return NULL;
}
static void rewrite_animations(Buffer *out, char *text, bool *changed) {
    char *at=text, *start;
    while ((start=strstr(at,"<Animation "))) {
        char *header_end=strchr(start,'>'), *end=strstr(start,"</Animation>");
        if (!header_end || !end || header_end>end) break;
        char saved=*header_end; *header_end=0;
        char name[64]={0};
        char *name_at=strstr(start,"name=\"");
        if (name_at) sscanf(name_at,"name=\"%63[^\"]",name);
        const char *icon=gesture_icon(name);
        int frames=0;
        char *count_at=strstr(start,"frameCount=\"");
        if (count_at) sscanf(count_at,"frameCount=\"%d",&frames);
        if (frames<1 || frames>512) icon=NULL;
        *header_end=saved;
        if (icon) {
            /* Keep frame counts and animation names, including native event timing.
             * Notifications and character demonstration animations stay untouched. */
            append(out,at,(size_t)(header_end+1-at));
            char part[240];
            int n=snprintf(part,sizeof(part),
                "\n<Part height=\"64\" name=\"vita_%s\" registrationPointX=\"32\" registrationPointY=\"32\" width=\"64\" zIndex=\"2\">",icon);
            append(out,part,(size_t)n);
            /* This engine has discrete frame visibility, not interpolated
             * keyframes. Populate every frame so the icon never drops out. */
            for (int frame=0;frame<frames;frame++) {
                n=snprintf(part,sizeof(part),"<Frame index=\"%d\" x=\"0\" y=\"0\" alpha=\"1\" rotation=\"0\" scaleX=\"1\" scaleY=\"1\"/>",frame);
                append(out,part,(size_t)n);
            }
            append(out,"</Part>\n</Animation>",20);
            *changed=true;
        } else append(out,at,(size_t)(end+12-at));
        at=end+12;
    }
    append(out,at,strlen(at));
}
unsigned char *park_prompts_rewrite(const char *path, const unsigned char *data,
                                   size_t size, size_t *output_size) {
    if (!path || !data || !output_size || size>2*1024*1024) return NULL;
    bool locale=resource(path,"locale/english.txt");
    bool animations=resource(path,"gui/animations.xml");
    if (!locale && !animations) return NULL;
    /* These are text files. Do not truncate a malformed or unrelated binary blob. */
    if (memchr(data,0,size)) return NULL;
    char *text=malloc(size+1); if (!text) return NULL;
    memcpy(text,data,size); text[size]=0;
    Buffer out={0}; bool changed=false;
    if (locale) rewrite_locale(&out,text,&changed);
    else rewrite_animations(&out,text,&changed);
    free(text);
    if (out.failed || !changed) { free(out.text); return NULL; }
    *output_size=out.used; return (unsigned char *)out.text;
}
const char *park_prompt_sprite(const char *name) {
    static const struct {const char *from,*to;} rows[]={
        {"cn_instructions_icon_1.png", "app0:prompts/help_move.png"},
        {"cn_instructions_icon_2.png", "app0:prompts/help_attack.png"},
        {"cn_instructions_icon_3.png", "app0:prompts/help_tag.png"},
        {"cn_instructions_icon_4.png", "app0:prompts/help_super.png"},
        {"vita_square.png", "app0:prompts/square.png"},
        {"vita_cross.png", "app0:prompts/cross.png"},
        {"vita_triangle.png", "app0:prompts/triangle.png"},
        {"vita_stick.png", "app0:prompts/stick.png"},
        {"vita_r.png", "app0:prompts/r.png"},
    };
    if (name) for (size_t i=0;i<sizeof(rows)/sizeof(*rows);i++)
        if (!strcmp(name,rows[i].from)) return rows[i].to;
    return NULL;
}
