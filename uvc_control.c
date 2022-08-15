#include "uvc_control.h"
#include "libusb-1.0/libusb.h"
#include "libuvc/libuvc.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

static int usb_option_set = 0;
#define ERROR_SIZE 255
#define MAX_FORMATS 128
#define COMPRESSED_SIZE 2

#define USE_YUV


typedef struct uvcctl_frame_format {
    int width,height,fps;
} uvcctl_frame_format;


struct uvcctl {
    libusb_device_handle *usb_devh;
    uvcctl_frame_format formats[COMPRESSED_SIZE][MAX_FORMATS];
    uint16_t gain_min,gain_max;
    int gain_queried;
    int compressed;
    int width;
    int height;
    int buf_count,buf_size;
    int formats_N[COMPRESSED_SIZE];
    uvc_device_handle_t *devh;
    uvc_context_t *ctx;
    int stream_format_no;
    void *user_data;
    uvcctl_callback_type callback;
    uvc_stream_handle_t *strh;
    uvc_stream_ctrl_t ctrl;
    char error[ERROR_SIZE+1];
};

uvcctl *uvcctl_create()
{
    uvcctl *p=(uvcctl *)calloc(1,sizeof(uvcctl));
    if(p)
        uvcctl_set_size(p,640,480,0);
    return p;
}

void uvcctl_set_size(uvcctl *obj,int w,int h,int c)
{
    obj->width = w;
    obj->height = h;
    obj->compressed = c ? 1: 0;
}

void uvcctl_set_buffers(uvcctl *obj,int N,int size)
{
    obj->buf_count = N;
    obj->buf_size = size;
}

int uvcctl_open(uvcctl *obj,int fd,int *sizes,int n)
{
    if(!usb_option_set) {
        int r = libusb_set_option(NULL,LIBUSB_OPTION_NO_DEVICE_DISCOVERY, NULL);
        if(r < 0) {
            strncpy(obj->error,"Failed to set no discovery option",ERROR_SIZE);
            return -1;
        }
        usb_option_set=1;
    }
    int res = 0;
    res = uvc_init(&obj->ctx,NULL);
    if(res < 0) {
        snprintf(obj->error,ERROR_SIZE,"Failed to init UVC Context %s",uvc_strerror(res));
        return -1;
    }
    for(int tr=0;;tr++) {
        res = uvc_wrap(fd,obj->ctx,&obj->devh);
        if(res < 0 || obj->devh == NULL) {
            if(tr >= 0) {
                snprintf(obj->error,ERROR_SIZE,"Failed to Wrap Existing FD %s",uvc_strerror(res));
                return -1;
            }
            else {
                continue;
            }
        } 
        else {
            break;
        }
    }
    const uvc_format_desc_t *format_desc = uvc_get_format_descs(obj->devh);
    for(;format_desc;format_desc=format_desc->next) {
        if(format_desc->bDescriptorSubtype != UVC_VS_FORMAT_MJPEG && format_desc->bDescriptorSubtype != UVC_VS_FORMAT_UNCOMPRESSED)
            continue;
        int is_compressed = format_desc->bDescriptorSubtype == UVC_VS_FORMAT_MJPEG;
        const uvc_frame_desc_t *p = format_desc->frame_descs;
        while(p && obj->formats_N[is_compressed] < MAX_FORMATS) {
            uvcctl_frame_format *fmt = &obj->formats[is_compressed][obj->formats_N[is_compressed]];
            fmt->width = p->wWidth;
            fmt->height = p->wHeight;
            fmt->fps = 10000000 / p->dwDefaultFrameInterval;
            printf("Compressed=%d %dx%d %d\n",is_compressed,fmt->width,fmt->height,fmt->fps);
            if(obj->formats_N[is_compressed] <= n && is_compressed) {
                sizes[0] = fmt->width;
                sizes[1] = fmt->height;
                sizes += 2;
            }
            obj->formats_N[is_compressed] ++;
            p=p->next;
        }
    }
    if(obj->formats_N[0] == 0 && obj->formats_N[1] == 0) {
        strncpy(obj->error,"No YUV2 or MJPEG frame sizes found",ERROR_SIZE);
        return -1;
    }
    return obj->formats_N[1] < n ? obj->formats_N[1] : n;
}

static void my_callback(uvc_frame_t *frame, void *ptr)
{
    uvcctl *obj = (uvcctl *)(ptr);
    uvc_frame_t *rgb = NULL;
    int res;
    char const *error_message = NULL;
    rgb = uvc_allocate_frame(frame->width*frame->height*3);
    if(!rgb) {
        error_message = "Allocation failed";
        goto exit_point;
    }
    if(frame->frame_format != UVC_COLOR_FORMAT_YUYV) {
        error_message = "Got unexpected frame format";
        goto exit_point;
    }
    if(frame->data_bytes != frame->width * frame->height * 2) {
        error_message = "Frame does not contain all the data";
        goto exit_point;
    }

    res = uvc_any2rgb(frame,rgb);
    if(res < 0) {
        error_message = "YUV2 to RGB conversion failed";
        goto exit_point;
    }

exit_point:
    if(obj->callback) {
        if(error_message == NULL) {
            obj->callback(obj->user_data,frame->sequence,rgb->data,rgb->width,rgb->height,3,NULL);
        }
        else {
            obj->callback(obj->user_data,frame->sequence,NULL,-1,-1,-1,error_message);
        }
    }
    if(rgb) {
        uvc_free_frame(rgb);
    }
}

int uvcctl_start_stream(uvcctl *obj,uvcctl_callback_type callback,void *user_data)
{
    int i;
    obj->stream_format_no = -1;
    obj->user_data = user_data;
    for(i=0;i<obj->formats_N[obj->compressed];i++) {
        if(obj->formats[obj->compressed][i].height == obj->height && obj->formats[obj->compressed][i].width == obj->width) {
            obj->stream_format_no = i;
            break;
        }
    }
    if(obj->stream_format_no == -1) {
        snprintf(obj->error,ERROR_SIZE,"Unsupported format %dx%d",obj->width,obj->height);
        return -1;
    }
    int tries = 0;
    int res;
    while(tries < 5) {
        res = uvc_get_stream_ctrl_format_size(obj->devh,&obj->ctrl,
                (!obj->compressed ? UVC_FRAME_FORMAT_YUYV : UVC_FRAME_FORMAT_MJPEG),
                obj->width,obj->height,
                obj->formats[obj->compressed][obj->stream_format_no].fps);
        if(res == 0)
            break;
        tries ++;
    }
    if(res < 0) {
        snprintf(obj->error,ERROR_SIZE,"Failed to set stream size compressed=%d %dx%d fps=%d %s",
                                obj->compressed,obj->width,obj->height,obj->formats[obj->compressed][obj->stream_format_no].fps,
                                uvc_strerror(res));
        return -1;
    }
    uvc_stream_ctrl_t ctrl_save = obj->ctrl;
    res = uvc_stream_open_ctrl(obj->devh,&obj->strh,&ctrl_save);
    if(res < 0) {
        snprintf(obj->error,ERROR_SIZE,"Failed to open stream %s",uvc_strerror(res));
        return -1;
    }
    res = uvc_stream_ctrl(obj->strh,&obj->ctrl);
    if(res < 0) {
        snprintf(obj->error,ERROR_SIZE,"Failed to set stream config %s",uvc_strerror(res));
        return -1;
    }
    
    uvc_stream_set_transfer_buffer_sizes(obj->strh,obj->buf_count,obj->buf_size);

    obj->callback = callback;
    res = uvc_stream_start(obj->strh,callback ? my_callback : NULL,obj,0);
    if(res < 0) {
        snprintf(obj->error,ERROR_SIZE,"Failed to start stream %s",uvc_strerror(res));
        obj->callback = NULL;
        return -1;
    }
    return 0;
}


int uvcctl_read_frame(uvcctl *obj,int timeout,int w,int h,char *buffer)
{
    uvc_frame_t *frame = NULL;
    int res = uvc_stream_get_frame(obj->strh,&frame,timeout);
    if(res == UVC_ERROR_TIMEOUT)
        return 0;
    if(res < 0) {
        snprintf(obj->error,ERROR_SIZE,"Failed to get frame %s",uvc_strerror(res));
        return -1;
    }
    if(w != (int)frame->width || h != (int)frame->height) {
        snprintf(obj->error,ERROR_SIZE,"Frame #%d does not match requires w=%d h=%d frame %dx%d",
                frame->sequence,
                frame->width,frame->height,
                w,h);
        return -1;
    }
    if(frame->frame_format == UVC_COLOR_FORMAT_YUYV && frame->data_bytes != (size_t)(frame->width*frame->height*2)) {
        snprintf(obj->error,ERROR_SIZE,"Frame #%d does not give all uncompressed data - gived %ld, but needed %ld",
                frame->sequence,
                frame->data_bytes,(size_t)(frame->width*frame->height*2));
        return -1;
    }
    uvc_frame_t frame_out;
    memset(&frame_out,0,sizeof(frame_out));
    frame_out.data = buffer;
    frame_out.data_bytes = h*w*3;
    res = uvc_any2rgb(frame,&frame_out);
    if(res < 0) {
        snprintf(obj->error,ERROR_SIZE,"Failed to convert frame %d to RGB %s, source %d,%d of %ld",
            frame->sequence,
            uvc_strerror(res),frame->width,frame->height,frame->data_bytes);
        return -1;
    }

    return frame->sequence;
}

int uvcctl_stop_stream(uvcctl *obj)
{
    if(obj->strh) {
        int res = uvc_stream_stop(obj->strh);
        obj->strh = NULL;
        if(res < 0) {
            snprintf(obj->error,ERROR_SIZE,"Failed to stop stream %s",uvc_strerror(res));
            return -1;
        }
        return 0;
    }
    strncpy(obj->error,"Stream is not open",ERROR_SIZE);
    return -1;
}

void uvcctl_delete(uvcctl *obj)
{
    if(obj->devh)
        uvc_close(obj->devh);
    if(obj->ctx)
        uvc_exit(obj->ctx);
}

char const *uvcctl_error(uvcctl *obj)
{
    return obj->error;
}

int uvcctl_open_fd(char const *path)
{
    return open(path,O_RDWR);
}
void uvcctl_close_fd(int fd)
{
    close(fd);
}


int uvcctl_auto_mode(uvcctl *obj,int is_auto)
{
    int res,res2;
    if(is_auto) {
        res = uvc_set_white_balance_temperature_auto(obj->devh,1);
        if(res < 0) {
            snprintf(obj->error,ERROR_SIZE,"WB auto on failed:%s",uvc_strerror(res));
            return -1;
        }
        res = uvc_set_ae_mode(obj->devh,2);
        if(res < 0) {
            res2 = uvc_set_ae_mode(obj->devh,8);
            if(res2 < 0) {
                snprintf(obj->error,ERROR_SIZE,"Failed to set AE mode 2 %s and mode 8 %s",uvc_strerror(res),uvc_strerror(res2));
                return -1;
            }
        }
    }
    else {
        res = uvc_set_white_balance_temperature_auto(obj->devh,0);
        if(res < 0) {
            snprintf(obj->error,ERROR_SIZE,"WB auto off failed:%s",uvc_strerror(res));
            return -1;
        }
        res = uvc_set_ae_mode(obj->devh,0);
        if(res < 0) {
            res = uvc_set_ae_mode(obj->devh,4);
            if(res < 0) {
                res = uvc_set_ae_mode(obj->devh,1);
                if(res < 0) {
                    snprintf(obj->error,ERROR_SIZE,"AE off failed:%s",uvc_strerror(res));
                    return -1;
                }
            }
        }
    }
    return 0;
}

static int query_gain(uvcctl *obj)
{
    uint16_t min_g=0,max_g=0;
    int res;
    if(obj->gain_queried)
        return 0;
    if((res=uvc_get_gain(obj->devh,&max_g,UVC_GET_MAX)) < 0) {
        snprintf(obj->error,ERROR_SIZE,"Failed to get gain max %s",uvc_strerror(res));
        return -1;
    }
    if((res=uvc_get_gain(obj->devh,&min_g,UVC_GET_MIN)) < 0) {
        snprintf(obj->error,ERROR_SIZE,"Failed to get gain min %s",uvc_strerror(res));
        return -1;
    }
    obj->gain_queried=1;
    obj->gain_min = min_g;
    obj->gain_max = max_g;
    return 0;
}

int uvcctl_set_wb(uvcctl *obj,int temperature)
{
    int res = uvc_set_white_balance_temperature(obj->devh,temperature);
    if(res < 0) {
        snprintf(obj->error,ERROR_SIZE,"Failed to set WB Temperature %s",uvc_strerror(res));
        return -1;
    }
    return 0;
}

int uvcctl_set_gain(uvcctl *obj,double gain)
{
    if(query_gain(obj)<0)
        return -1;
    int gain_value = obj->gain_min * (1 - gain) + obj->gain_max * gain;
    int res = uvc_set_gain(obj->devh,gain_value);
    if(res < 0) {
        snprintf(obj->error,ERROR_SIZE,"Failed to set gain %s",uvc_strerror(res));
        return -1;
    }
    return 0;
}

int uvcctl_set_exposure(uvcctl *obj,double exp_ms)
{
    int res = uvc_set_exposure_abs(obj->devh,exp_ms * 10);
    if(res < 0) {
        snprintf(obj->error,ERROR_SIZE,"Failed to set exposure %s",uvc_strerror(res));
        return -1;
    }
    return 0;
}

int uvcctl_get_control_limits(uvcctl *obj,uvcctl_control_limits *limits)
{
    if(query_gain(obj) < 0)
        return -1;
    int res;
    uint32_t min_time=0,max_time=0;
    if((res = uvc_get_exposure_abs(obj->devh,&min_time,UVC_GET_MIN)) < 0 ||
        (res = uvc_get_exposure_abs(obj->devh,&max_time,UVC_GET_MAX)) < 0)
    {
        snprintf(obj->error,ERROR_SIZE,"Failed to get exposure range %s",uvc_strerror(res));
        return -1;
    }
    uint16_t min_wb=0,max_wb=0;
    if((res=uvc_get_white_balance_temperature(obj->devh,&min_wb,UVC_GET_MIN)) < 0
        || (res=uvc_get_white_balance_temperature(obj->devh,&max_wb,UVC_GET_MAX)) < 0)
    {
        snprintf(obj->error,ERROR_SIZE,"Failed to get WB range %s",uvc_strerror(res));
        return -1;
    }
    limits->exp_msec_min = min_time * 0.1f;
    limits->exp_msec_max = max_time * 0.1f;
    limits->wb_temp_min = min_wb;
    limits->wb_temp_max = max_wb;
    return 0;
}


