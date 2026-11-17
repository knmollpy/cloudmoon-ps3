#include "cloudmoon_input.h"
#include <stdio.h>
#include <string.h>
#define CM_LEFT_ID 10
#define CM_RIGHT_ID 11
#define CM_BUTTON_ID_BASE 12
#define CM_AXIS_DEADZONE 3932
static int clampi(int v,int lo,int hi){return v<lo?lo:(v>hi?hi:v);}
static int axis_to_delta(int16_t axis,int radius){int v=(int)axis;if(v>-CM_AXIS_DEADZONE&&v<CM_AXIS_DEADZONE)return 0;if(v>0)v=(v-CM_AXIS_DEADZONE)*32767/(32767-CM_AXIS_DEADZONE);else v=(v+CM_AXIS_DEADZONE)*32767/(32767-CM_AXIS_DEADZONE);return (v*radius)/32767;}
static int emit(char*out,size_t cap,size_t*used,const char*phase,int id,int x,int y){int n;if(*used>=cap)return -1;n=snprintf(out+*used,cap-*used,"touch>Touch %s:%d,%d,%d",phase,id,x,y);if(n<0||(size_t)n+1>cap-*used)return -1;*used+=(size_t)n+1;return 0;}
void cm_layout_default(cm_layout_t*l,int w,int h){int i;if(!l)return;memset(l,0,sizeof(*l));l->screen_w=w>0?w:1280;l->screen_h=h>0?h:720;l->stick_radius=(l->screen_w<l->screen_h?l->screen_w:l->screen_h)/7;if(l->stick_radius<48)l->stick_radius=48;l->left_x=l->screen_w/5;l->left_y=l->screen_h*3/4;l->right_x=l->screen_w*4/5;l->right_y=l->screen_h*3/4;
l->button_x[0]=l->screen_w*4/5;l->button_y[0]=l->screen_h*3/5;
l->button_x[1]=l->screen_w*9/10;l->button_y[1]=l->screen_h*3/4;
l->button_x[2]=l->screen_w*7/10;l->button_y[2]=l->screen_h*3/4;
l->button_x[3]=l->screen_w*4/5;l->button_y[3]=l->screen_h*9/10;
l->button_x[4]=l->screen_w/12;l->button_y[4]=l->screen_h/5;
l->button_x[5]=l->screen_w*11/12;l->button_y[5]=l->screen_h/5;
l->button_x[6]=l->screen_w/12;l->button_y[6]=l->screen_h*2/5;
l->button_x[7]=l->screen_w*11/12;l->button_y[7]=l->screen_h*2/5;
for(i=0;i<8;i++){l->button_x[i]=clampi(l->button_x[i],0,l->screen_w-1);l->button_y[i]=clampi(l->button_y[i],0,l->screen_h-1);}}
void cm_input_reset(cm_input_state_t*s){if(s)memset(s,0,sizeof(*s));}
int cm_input_translate(const cm_layout_t*l,cm_input_state_t*s,const cm_pad_t*p,char*out,size_t cap){size_t used=0;int lx,ly,rx,ry,b[8],i;int16_t alx=p?p->lx:0,aly=p?p->ly:0;if(!l||!s||!p||!out||!cap||l->screen_w<2||l->screen_h<2)return -1;
if(p->left)alx=-32767;else if(p->right)alx=32767;if(p->up)aly=-32767;else if(p->down)aly=32767;
lx=clampi(l->left_x+axis_to_delta(alx,l->stick_radius),0,l->screen_w-1);ly=clampi(l->left_y+axis_to_delta(aly,l->stick_radius),0,l->screen_h-1);rx=clampi(l->right_x+axis_to_delta(p->rx,l->stick_radius),0,l->screen_w-1);ry=clampi(l->right_y+axis_to_delta(p->ry,l->stick_radius),0,l->screen_h-1);
if(axis_to_delta(alx,l->stick_radius)||axis_to_delta(aly,l->stick_radius)){if(emit(out,cap,&used,s->left_active?"move":"start",CM_LEFT_ID,lx,ly))return -1;s->left_active=1;s->left_x=lx;s->left_y=ly;}else if(s->left_active){if(emit(out,cap,&used,"end",CM_LEFT_ID,s->left_x,s->left_y))return -1;s->left_active=0;}
if(axis_to_delta(p->rx,l->stick_radius)||axis_to_delta(p->ry,l->stick_radius)){if(emit(out,cap,&used,s->right_active?"move":"start",CM_RIGHT_ID,rx,ry))return -1;s->right_active=1;s->right_x=rx;s->right_y=ry;}else if(s->right_active){if(emit(out,cap,&used,"end",CM_RIGHT_ID,s->right_x,s->right_y))return -1;s->right_active=0;}
b[0]=!!p->cross;b[1]=!!p->circle;b[2]=!!p->square;b[3]=!!p->triangle;b[4]=!!p->l1;b[5]=!!p->r1;b[6]=!!p->l2;b[7]=!!p->r2;
for(i=0;i<8;i++){int id=CM_BUTTON_ID_BASE+i,x=clampi(l->button_x[i],0,l->screen_w-1),y=clampi(l->button_y[i],0,l->screen_h-1);if(b[i]&&!s->button_active[i]){if(emit(out,cap,&used,"start",id,x,y))return -1;s->button_active[i]=1;}else if(!b[i]&&s->button_active[i]){if(emit(out,cap,&used,"end",id,x,y))return -1;s->button_active[i]=0;}}
s->frame++;return (int)used;}
