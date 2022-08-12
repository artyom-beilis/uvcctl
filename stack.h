#ifndef STACK_H
#define STACK_H

#if __cplusplus
extern "C" {
#endif

typedef struct Stacker Stacker; 

Stacker *stacker_new(int w,int h,int roi_x,int roi_y,int roi_size);
void stacker_delete(Stacker *obj);
void stacker_set_darks(Stacker *obj,unsigned char *rgb);
void stacker_get_stacked(Stacker *obj,unsigned char *rgb);
int stacker_stack_image(Stacker *obj,unsigned char *rgb,int restart); 

#if __cplusplus
}
#endif

#endif