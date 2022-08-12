#ifndef UVC_CONTROL_H
#define UVC_CONTROL_H

typedef void (*uvcctl_callback_type)(void *user_data,int frame_no,char const *data,int width,int height,int bytes_per_pixel,char const *error_message);
typedef struct uvcctl uvcctl;

typedef struct uvcctl_control_limits {
    float exp_msec_min;
    float exp_msec_max;
    int wb_temp_min;
    int wb_temp_max;
} uvcctl_control_limits;

uvcctl *uvcctl_create();
char const *uvcctl_error(uvcctl *obj);
int uvcctl_open_fd(char const *path);
void uvcctl_close_fd(int fd);
int uvcctl_auto_mode(uvcctl *obj,int is_auto);
int uvcctl_get_control_limits(uvcctl *obj,uvcctl_control_limits *limits);
int uvcctl_set_gain(uvcctl *obj,double range);
int uvcctl_set_exposure(uvcctl *obj,double exp_ms);
int uvcctl_set_wb(uvcctl *obj,int temperature);
int uvcctl_open(uvcctl *obj,int fd,int *sizes,int n);
void uvcctl_set_size(uvcctl *obj,int w,int h,int compressed);
void uvcctl_set_buffers(uvcctl *obj,int N,int size);
int uvcctl_start_stream(uvcctl *obj,uvcctl_callback_type callback,void *user_data);
int uvcctl_read_frame(uvcctl *obj,int timeout,int w,int h,char *buffer);
int uvcctl_stop_stream(uvcctl *obj);
void uvcctl_delete(uvcctl *obj);

#endif
