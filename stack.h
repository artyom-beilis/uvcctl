#ifndef STACK_H
#define STACK_H

#if __cplusplus
extern "C" {
#endif

typedef struct Stacker Stacker; 

Stacker *stacker_new(int w,int h,int roi_x,int roi_y,int roi_size);
char const *stacker_error();
void stacker_delete(Stacker *obj);
int stacker_set_darks(Stacker *obj,unsigned char *rgb);
int stacker_get_stacked(Stacker *obj,unsigned char *rgb);
int stacker_stack_image(Stacker *obj,unsigned char *rgb,int restart); 
void stacker_set_src_gamma(Stacker *obj,float gamma);
// -1 as auto stretch
void stacker_set_tgt_gamma(Stacker *obj,float gamma);
int stacker_load_darks(Stacker *obj,char const *path);
int stacker_save_stacked_darks(Stacker *obj,char const *path);

#if __cplusplus
}
#endif

#endif
