/* Best Park 1.2.1 controller bridge. Layouts and entry points were checked
 * against the Android 1.2.1 armeabi library.
 * Dispatch only from a live InGame update. Never retain a native entity pointer
 * for later dereferencing across room changes or character swaps.
 */
#include "gamepad.h"
#include "park.h"
#include "utils/logger.h"
#include "utils/dialog.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct { float x, y; } Point;
enum { POSITION=19, MOVEMENT=17, INPUT=53, WALK=64 };
enum { ATTACK=1, UPPER=2, LOWER=4, RETREAT=8, SUPER=16, SWAP=32 };
enum { PAD_UP=1, PAD_DOWN=2, PAD_LEFT=4, PAD_RIGHT=8 };
static unsigned pending, dpad;
static float axis_x, axis_y;
static void *last_entity;
static bool own_walk;
#ifdef PARK_DIAGNOSTICS
static unsigned action_logs;
static unsigned update_calls, raw_logs, state_logs, previous_state=~0u;

/* Report gate transitions, rather than printing every frame. This also proves
 * that the scheduler actually reaches the patched InGame vtable entry. */
static void log_state(unsigned state, void *entity, void *tap, void *walk) {
    if(state!=previous_state && state_logs<64) {
        ++state_logs;
        l_info("Controller state: gates=0x%x entity=%p tap=%p walk=%p",state,entity,tap,walk);
    }
    previous_state=state;
}
#else
#define log_state(...) ((void)0)
#endif

static void (*original_update)(void *,float);
static void *(*get_room)(void *);
static void *(*get_player)(void *);
static void *(*get_component)(void *,int);
static int (*get_group)(void *), (*get_next_group)(void *);
static bool (*is_enabled)(void *), (*is_running)(void *);
static bool (*can_walk)(void *);
static int (*get_facing)(void *);
static void (*set_facing)(void *,int);
static void (*walk_tap)(void *,void *), (*walk_stop)(void *);
static float (*get_speed)(void *);
static void (*set_speed)(void *,float);
static void (*tap_begin)(void *,void *), (*tap_end)(void *,void *);
static void (*do_super)(void *), (*swap_players)(void *,void *);
static void (*touch_ctor)(void *);
static void *(*shared_director)(void);
/* C++ CCPoint returns use an explicit hidden result pointer on this ABI. */
static void (*to_world)(Point *,void *,const Point *);
static void (*to_ui)(Point *,void *,const Point *);
static uintptr_t tap_vtable;
static void *synthetic_touch;

static unsigned native_u32(const void *p, unsigned offset) {
    unsigned v; memcpy(&v,(const char *)p+offset,4); return v;
}
static void *native_ptr(const void *p, unsigned offset) {
    return (void *)(uintptr_t)native_u32(p,offset);
}
static unsigned native_byte(const void *p, unsigned offset) {
    return ((const unsigned char *)p)[offset];
}

/* Native CCTouch is 52 bytes: id@20, start-valid@24, start@28,
 * current@36, previous@44. It stays alive while Tap holds its last pointer.
 * This object never enters the global touchscreen dispatcher.
 */
static void set_touch(Point p, bool first) {
    if(!synthetic_touch) {
        synthetic_touch=calloc(1,52);
        if(!synthetic_touch)fatal_error("Controller touch allocation failed");
        touch_ctor(synthetic_touch);
        int id=1000;
        memcpy((char *)synthetic_touch+20,&id,4);
    }
    if(first) {
        ((unsigned char *)synthetic_touch)[24]=1;
        memcpy((char *)synthetic_touch+28,&p,8);
        memcpy((char *)synthetic_touch+44,&p,8);
    } else {
        memcpy((char *)synthetic_touch+44,(char *)synthetic_touch+36,8);
    }
    memcpy((char *)synthetic_touch+36,&p,8);
}

static Point view_point(void *position, float x, float y) {
    Point local={x,y},world,view;
    void *node=native_ptr(position,36);
    to_world(&world,node,&local);
    to_ui(&view,shared_director(),&world);
    return view;
}

static void stop_owned_walk(void *walk, void *tap, void *entity) {
    /* Do not cancel a native dash, knockback or combo after the player moves
     * into a different state. Only stop movement which our stick owns. */
    if(own_walk && walk && tap && is_enabled(walk) && can_walk(tap) &&
       get_group(entity)==get_next_group(entity))walk_stop(walk);
    own_walk=false;
}

#ifdef PARK_DIAGNOSTICS
static void log_action(const char *name, void *entity, int facing) {
    if(action_logs++<48)l_info("Controller: %s entity=%p facing=%d",name,entity,facing);
}
#else
#define log_action(...) ((void)0)
#endif

static void update_controls(void *game) {
    unsigned actions=pending;
    pending=0; /* Inputs in menus/loading screens must not fire on level entry. */
    void *room=get_room(game);
    void *entity=room?get_player(room):NULL;
    if(entity!=last_entity) {last_entity=entity;own_walk=false;}
    if(!entity) {log_state(1,NULL,NULL,NULL);return;}

    void *tap=get_component(entity,INPUT),*walk=get_component(entity,WALK);
    if(!tap || (uintptr_t)native_ptr(tap,0)!=tap_vtable || !walk) {
#ifdef PARK_DIAGNOSTICS
        if(previous_state!=2 && state_logs<64)
            l_info("Controller component: actual vtable=%p expected=%p",tap?native_ptr(tap,0):NULL,(void *)tap_vtable);
#endif
        log_state(2,entity,tap,walk);return;
    }
    /* InGame paused@252; Entity update strategy@12: 2=player/component
     * control, 1=scripted control. Do not gate on CCLayer::isTouchEnabled:
     * the native Room receives touches while that accessor returns false
     * (confirmed in the 0.4 hardware trace). It is not a gameplay-enable flag. */
    unsigned gates=(native_byte(game,252)?4:0) | (!is_running(game)?8:0) |
        (native_u32(entity,12)!=2?32:0) |
        (!is_enabled(tap)?64:0) | (controls_touch_count()?128:0) |
        (native_u32(tap,72)?256:0);
#ifdef PARK_DIAGNOSTICS
    if(gates!=previous_state && state_logs<64)
        l_info("Controller details: strategy=%u enabled=%u fingers=%u nativeTouches=%u",
            native_u32(entity,12),is_enabled(tap),controls_touch_count(),native_u32(tap,72));
#endif
    log_state(gates,entity,tap,walk);
    if(gates & (4|8|32|64)) {
        stop_owned_walk(walk,tap,entity);
        return;
    }
    /* Real touches get exclusive ownership for this frame. Walking does not
     * count as a finger, and button swipes never overlap a real held touch. */
    if(controls_touch_count() || native_u32(tap,72)) {
        stop_owned_walk(walk,tap,entity);
        return;
    }
    void *position=get_component(entity,POSITION);
    if(!position || !native_ptr(position,36))return;

    float x=axis_x,y=axis_y;
    if(dpad) {
        x=((dpad&PAD_RIGHT)!=0)-((dpad&PAD_LEFT)!=0);
        y=((dpad&PAD_DOWN)!=0)-((dpad&PAD_UP)!=0);
    }
    float magnitude=sqrtf(x*x+y*y);
    if(magnitude>1.0f) {x/=magnitude;y/=magnitude;magnitude=1.0f;}
    bool walk_allowed=is_enabled(walk) && can_walk(tap) &&
                      get_group(entity)==get_next_group(entity);

    if(actions) {
        stop_owned_walk(walk,tap,entity);
        if(actions&SWAP) {
            /* The original UI callback checks character availability, the
             * disabled/in-progress flag, and the current animation group. */
            if(native_ptr(game,308) && native_ptr(game,312)) {
                log_action("switch character",entity,get_facing(position));
                swap_players(game,NULL);
            }
            return;
        }
        if(actions&SUPER) {
            log_action("super",entity,get_facing(position));
            do_super(tap); /* Original meter, state and mission handling. */
            return;
        }
        /* Allow a fresh movement direction to aim a new attack, but never
         * turn an existing combo around halfway through its animation. */
        if(walk_allowed && fabsf(x)>0.2f)set_facing(position,x>0?1:-1);
        int facing=get_facing(position)==-1?-1:1;
        float dx=0,dy=0;
        if(actions&RETREAT)dx=-120*facing;
        else if(actions&UPPER)dy=-120;
        else if(actions&LOWER)dy=120;
        else dx=120*facing;
        Point start=view_point(position,0,0),end={start.x+dx,start.y+dy};
        set_touch(start,true);
        tap_begin(tap,synthetic_touch);
        /* Some animation states reject onBegan. Do not send an unmatched
         * ending event or modify a different touch's SwipeInfo. */
        if(native_u32(tap,72)==1 && native_ptr(tap,68)==synthetic_touch) {
            set_touch(end,false);
            tap_end(tap,synthetic_touch);
            log_action(actions&RETREAT?"retreat":actions&UPPER?"upper combo":
                       actions&LOWER?"lower combo":"attack",entity,facing);
        }
        return;
    }
    if(magnitude<=0.001f) {stop_owned_walk(walk,tap,entity);return;}
    if(!walk_allowed) {own_walk=false;return;}

    /* PlayerWalkController converts this back into a local destination and
     * applies native collision/pathing and the character's configured speed.
     * Refresh the target ahead of the player; stop it when the stick centers. */
    Point destination=view_point(position,120*x/magnitude,-120*y/magnitude);
    set_touch(destination,true);
    walk_tap(walk,synthetic_touch);
    void *movement=get_component(entity,MOVEMENT);
    if(movement)set_speed(movement,get_speed(movement)*magnitude);
    if(!own_walk)log_action("move",entity,get_facing(position));
    own_walk=true;
}

static void ingame_update(void *game, float dt) {
#ifdef PARK_DIAGNOSTICS
    if(!update_calls++)l_info("Controller update entered: game=%p",game);
#endif
    update_controls(game);
    original_update(game,dt);
}

void park_gamepad_begin_frame(void) {pending=0;}
void park_gamepad_axis(ControlsStickId stick,float x,float y) {
    if(stick==CONTROLS_STICK_LEFT) {
#ifdef PARK_DIAGNOSTICS
        if((x!=0 || y!=0) && axis_x==0 && axis_y==0 && raw_logs<32) {
            ++raw_logs;l_info("Controller raw stick: x=%d y=%d updates=%u",(int)(x*100),(int)(y*100),update_calls);
        }
#endif
        axis_x=x;axis_y=y;
    }
}
void park_gamepad_key(int key,ControlsAction action) {
    unsigned direction=0,command=0;
    switch(key) {
    case AKEYCODE_DPAD_UP:direction=PAD_UP;break;
    case AKEYCODE_DPAD_DOWN:direction=PAD_DOWN;break;
    case AKEYCODE_DPAD_LEFT:direction=PAD_LEFT;break;
    case AKEYCODE_DPAD_RIGHT:direction=PAD_RIGHT;break;
    case AKEYCODE_BUTTON_X:command=ATTACK;break;
    case AKEYCODE_BUTTON_Y:command=UPPER;break;
    case AKEYCODE_BUTTON_A:command=LOWER;break;
    case AKEYCODE_BUTTON_B:command=RETREAT;break;
    case AKEYCODE_BUTTON_R1:command=SUPER;break;
    case AKEYCODE_BUTTON_L1:command=SWAP;break;
    }
    if(action==CONTROLS_ACTION_DOWN) {
#ifdef PARK_DIAGNOSTICS
        if((direction || command) && raw_logs<32) {
            ++raw_logs;l_info("Controller raw key: key=%d command=%u updates=%u",key,command,update_calls);
        }
#endif
        dpad|=direction;pending|=command;
    }
    else if(action==CONTROLS_ACTION_UP)dpad&=~direction;
}

void park_gamepad_install(void) {
#define RESOLVE(var,name) var=(void *)park_symbol(name)
    RESOLVE(original_update,"_ZN6InGame6updateEf");
    RESOLVE(get_room,"_ZN6InGame14getCurrentRoomEv");
    RESOLVE(get_player,"_ZN4Room22getCurrentPlayerEntityEv");
    RESOLVE(get_component,"_ZN6Entity4getCE11ComponentId");
    RESOLVE(get_group,"_ZN6Entity15getCurrentGroupEv");
    RESOLVE(get_next_group,"_ZN6Entity12getNextGroupEv");
    RESOLVE(is_enabled,"_ZN9Component9isEnabledEv");
    RESOLVE(is_running,"_ZN7cocos2d6CCNode9isRunningEv");
    RESOLVE(can_walk,"_ZN3Tap9canAttackEv");
    RESOLVE(get_facing,"_ZN8Position16getLookDirectionEv");
    RESOLVE(set_facing,"_ZN8Position6lookAtENS_13LookDirectionE");
    RESOLVE(walk_tap,"_ZN20PlayerWalkController5onTapEPN7cocos2d7CCTouchE");
    RESOLVE(walk_stop,"_ZN20PlayerWalkController15onTapAndHoldEndEv");
    RESOLVE(get_speed,"_ZN8Movement8getSpeedEv");
    RESOLVE(set_speed,"_ZN8Movement8setSpeedEf");
    RESOLVE(tap_begin,"_ZN3Tap7onBeganEPN7cocos2d7CCTouchE");
    RESOLVE(tap_end,"_ZN3Tap7onEndedEPN7cocos2d7CCTouchE");
    RESOLVE(do_super,"_ZN3Tap7doSuperEv");
    RESOLVE(swap_players,"_ZN6InGame11swapPlayersEPN7cocos2d8CCObjectE");
    RESOLVE(touch_ctor,"_ZN7cocos2d7CCTouchC1Ev");
    RESOLVE(shared_director,"_ZN7cocos2d10CCDirector14sharedDirectorEv");
    RESOLVE(to_world,"_ZN7cocos2d6CCNode21convertToWorldSpaceARERKNS_7CCPointE");
    RESOLVE(to_ui,"_ZN7cocos2d10CCDirector11convertToUIERKNS_7CCPointE");
#undef RESOLVE
    tap_vtable=park_symbol("_ZTV3Tap")+8;
    uintptr_t *slot=(void *)(park_symbol("_ZTV6InGame")+24);
    if(*slot!=(uintptr_t)original_update)fatal_error("Unexpected InGame update vtable");
    *slot=(uintptr_t)ingame_update;
    l_info("Controller layout enabled: stick/dpad, face-button swipes, L swap, R super");
}
