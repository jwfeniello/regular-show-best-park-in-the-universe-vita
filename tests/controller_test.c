#define _GNU_SOURCE
#include <assert.h>
#include <sys/mman.h>
#include <stdio.h>
#include "gamepad.c"
static unsigned char *game,*room,*player,*tap,*walk,*position,*movement,*node;
static int touches,current_group=1,next_group=1,facing=1,walks,stops,supers,swaps,swipes;
static bool enabled=true,running=true,maywalk=true;
static Point last_destination,start_point,end_point;
static float speed;
uintptr_t park_symbol(const char *name){(void)name;abort();}
unsigned controls_touch_count(void){return touches;}
static void *new_object(void){void *p=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT,-1,0);assert(p!=MAP_FAILED && (uintptr_t)p<UINT32_MAX);return p;}
static void word(void *p,unsigned off,uintptr_t v){uint32_t u=v;memcpy((char *)p+off,&u,4);}
static void *room_fn(void *p){assert(p==game);return room;}
static void *player_fn(void *p){assert(p==room);return player;}
static void *component_fn(void *p,int id){assert(p==player);switch(id){case INPUT:return tap;case WALK:return walk;case POSITION:return position;case MOVEMENT:return movement;default:abort();}}
static int group_fn(void *p){assert(p==player);return current_group;}
static int next_fn(void *p){assert(p==player);return next_group;}
static bool enabled_fn(void *p){assert(p==tap || p==walk);return enabled;}
static bool running_fn(void *p){assert(p==game);return running;}
static bool can_fn(void *p){assert(p==tap);return maywalk;}
static int facing_fn(void *p){assert(p==position);return facing;}
static void face_fn(void *p,int f){assert(p==position);facing=f;}
static void move_fn(void *p,void *t){assert(p==walk);memcpy(&last_destination,(char *)t+36,8);walks++;speed=200;}
static void stop_fn(void *p){assert(p==walk);stops++;}
static float speed_fn(void *p){assert(p==movement);return speed;}
static void speed_set_fn(void *p,float f){assert(p==movement);speed=f;}
static void begin_fn(void *p,void *t){assert(p==tap);assert(!native_u32(tap,72));word(tap,72,1);word(tap,68,(uintptr_t)t);memcpy(&start_point,(char *)t+36,8);}
static void end_fn(void *p,void *t){assert(p==tap);assert(native_u32(tap,72)==1);word(tap,72,0);memcpy(&end_point,(char *)t+36,8);swipes++;}
static void super_fn(void *p){assert(p==tap);supers++;}
static void swap_fn(void *p,void *sender){assert(p==game && !sender);swaps++;}
static void *director_fn(void){return game;}
static void world_fn(Point *out,void *p,const Point *in){assert(p==node);*out=(Point){in->x+400,in->y+250};}
static void ui_fn(Point *out,void *p,const Point *in){assert(p==game);*out=(Point){in->x,544-in->y};}
static void frame(void){update_controls(game);park_gamepad_begin_frame();}
static void press(int key){park_gamepad_key(key,CONTROLS_ACTION_DOWN);frame();park_gamepad_key(key,CONTROLS_ACTION_UP);}
static void zero_axis(void){park_gamepad_axis(CONTROLS_STICK_LEFT,0,0);}
int main(void){
 game=new_object();room=new_object();player=new_object();tap=new_object();walk=new_object();position=new_object();movement=new_object();node=new_object();synthetic_touch=new_object();
 tap_vtable=0x11223344;word(tap,0,tap_vtable);word(player,12,2);word(position,36,(uintptr_t)node);word(game,308,1);word(game,312,1);
 get_room=room_fn;get_player=player_fn;get_component=component_fn;get_group=group_fn;get_next_group=next_fn;is_enabled=enabled_fn;is_running=running_fn;can_walk=can_fn;get_facing=facing_fn;set_facing=face_fn;walk_tap=move_fn;walk_stop=stop_fn;get_speed=speed_fn;set_speed=speed_set_fn;tap_begin=begin_fn;tap_end=end_fn;do_super=super_fn;swap_players=swap_fn;shared_director=director_fn;to_world=world_fn;to_ui=ui_fn;
 park_gamepad_axis(CONTROLS_STICK_LEFT,.5,0);frame();assert(walks==1 && speed==100 && last_destination.x==520);frame();assert(walks==2 && speed==100);zero_axis();frame();assert(stops==1);frame();assert(stops==1);
 park_gamepad_key(AKEYCODE_DPAD_UP,CONTROLS_ACTION_DOWN);park_gamepad_key(AKEYCODE_DPAD_RIGHT,CONTROLS_ACTION_DOWN);frame();assert(fabsf(speed-200)<.01f);assert(last_destination.y<294 && last_destination.x>400);
 park_gamepad_key(AKEYCODE_DPAD_UP,CONTROLS_ACTION_UP);park_gamepad_key(AKEYCODE_DPAD_RIGHT,CONTROLS_ACTION_UP);frame();assert(stops==2);
 facing=1;press(AKEYCODE_BUTTON_X);assert(end_point.x-start_point.x==120 && supers==0);facing=-1;press(AKEYCODE_BUTTON_X);assert(end_point.x-start_point.x==-120);
 press(AKEYCODE_BUTTON_Y);assert(end_point.y-start_point.y==-120);press(AKEYCODE_BUTTON_A);assert(end_point.y-start_point.y==120);
 facing=1;press(AKEYCODE_BUTTON_B);assert(end_point.x-start_point.x==-120);facing=-1;press(AKEYCODE_BUTTON_B);assert(end_point.x-start_point.x==120);
 int before=swipes;frame();frame();assert(swipes==before);press(AKEYCODE_BUTTON_R1);assert(supers==1);press(AKEYCODE_BUTTON_L1);assert(swaps==1);
 park_gamepad_axis(CONTROLS_STICK_LEFT,1,0);frame();before=walks;press(AKEYCODE_BUTTON_X);assert(walks==before && supers==1 && stops==3);zero_axis();frame();
 game[252]=1;before=swipes;press(AKEYCODE_BUTTON_X);game[252]=0;frame();assert(swipes==before);word(player,12,1);press(AKEYCODE_BUTTON_R1);word(player,12,2);assert(supers==1);
 running=false;press(AKEYCODE_BUTTON_X);running=true;assert(swipes==before);enabled=false;press(AKEYCODE_BUTTON_X);enabled=true;assert(swipes==before);
 touches=1;press(AKEYCODE_BUTTON_X);touches=0;assert(swipes==before);word(tap,72,1);press(AKEYCODE_BUTTON_X);word(tap,72,0);assert(swipes==before);
 park_gamepad_axis(CONTROLS_STICK_LEFT,1,0);frame();before=stops;maywalk=false;zero_axis();frame();assert(stops==before);maywalk=true;
 park_gamepad_axis(CONTROLS_STICK_LEFT,-1,0);press(AKEYCODE_BUTTON_X);assert(facing==-1 && end_point.x-start_point.x==-120);zero_axis();
 park_gamepad_key(AKEYCODE_BUTTON_X,CONTROLS_ACTION_DOWN);park_gamepad_begin_frame();before=swipes;frame();assert(swipes==before);
 puts("PASS: movement speed/release, diagonals, facing, all six actions, no repeat, no accidental super, pause/cutscene/touch guards, no dash cancellation, no stale menu input");
}
