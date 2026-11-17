#ifndef CLOUDMOON_INPUT_H
#define CLOUDMOON_INPUT_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { int screen_w,screen_h,left_x,left_y,right_x,right_y,stick_radius; int button_x[8],button_y[8]; } cm_layout_t;
typedef struct { int16_t lx,ly,rx,ry; uint8_t cross,circle,square,triangle,l1,r1,l2,r2,start,select,up,down,left,right; } cm_pad_t;
typedef struct { int left_active,right_active,button_active[8],left_x,left_y,right_x,right_y; unsigned int frame; } cm_input_state_t;
void cm_layout_default(cm_layout_t *layout,int width,int height);
void cm_input_reset(cm_input_state_t *state);
int cm_input_translate(const cm_layout_t *layout,cm_input_state_t *state,const cm_pad_t *pad,char *out,size_t out_cap);
#ifdef __cplusplus
}
#endif
#endif
