#include "stack.h"
#include <opencv2/core.hpp>
#ifdef __ANDROID__
#include <android/log.h>
#define LOG(format, ...) __android_log_print(ANDROID_LOG_ERROR, "UVC", "[%s:%d/%s] " format "\n", basename(__FILE__), __LINE__, __FUNCTION__, ##__VA_ARGS__)
#else
#define LOG(format, ...) fprintf(stderr,"[%s:%d/%s] " format "\n", basename(__FILE__), __LINE__, __FUNCTION__, ##__VA_ARGS__)
#endif

#ifdef INCLUDE_MAIN
#ifdef DO_STACK
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#endif
#include <fstream>
#endif

struct Stacker {
public:

    Stacker(int width,int height,int roi_x=-1,int roi_y=-1,int roi_size = -1) : 
        frames_(0),
        has_darks_(false)
    {
        error_message_[0]=0;
        if(roi_size == -1) {
            window_size_ = std::min(height,width);
            for(int i=0;i<16;i++) {
                if((1<<i)<=window_size_ && (1<<(i+1)) > window_size_) {
                    window_size_ = 1<<i;
                    break;
                }
            }
        }
        else {
            window_size_ = roi_size;
        }
        if(roi_x == -1 && roi_y == -1) {
            dx_ = (width  - window_size_)/2;
            dy_ = (height - window_size_)/2;
        }
        else {
            dx_ = std::max(0,roi_x - window_size_/2);
            dy_ = std::max(0,roi_y - window_size_/2);
            dx_ = std::min(width-window_size_,dx_);
            dy_ = std::min(height-window_size_,dy_);
        }
        
        sum_ = cv::Mat(height,width,CV_32FC3);
        count_ = cv::Mat(height,width,CV_32FC3);
    }

    void set_darks(unsigned char *rgb_img)
    {
        has_darks_ = true;
        darks_ = cv::Mat(sum_.rows,sum_.cols,CV_8UC3,rgb_img).clone();
    }

    void get_stacked(unsigned char *rgb_img)
    {
        if(frames_ == 0)
            memset(rgb_img,0,sum_.rows*sum_.cols*3);
        else {
            cv::Mat tgt(sum_.rows,sum_.cols,CV_8UC3,rgb_img);
            cv::Mat tmp = sum_ / count_;
            tmp.convertTo(tgt,CV_8UC3);
        }
    }

    bool stack_image(unsigned char *rgb_img,bool restart_position = false)
    {
        cv::Mat frame(sum_.rows,sum_.cols,CV_8UC3,rgb_img);
        if(has_darks_) {
            cv::Mat updated = frame - darks_;
            frame = updated;
        }
        if(window_size_ == 0) {
            add_image(frame,cv::Point(0,0));
            frames_ ++;
            return true;
        }
        bool added = true;
        if(frames_ == 0) {
            add_image(frame,cv::Point(0,0));
            fft_roi_ = calc_fft(frame);
            frames_ = 1;
        }
        else {
            cv::Mat fft_frame = calc_fft(frame);
            cv::Point shift = get_dx_dy(fft_frame);
            if(restart_position) {
                add_image(frame,shift);
                reset_step(shift);
                frames_ ++;
            }
            else {
                if(check_step(shift)) {
                    add_image(frame,shift);
                    frames_ ++;
                }
                else
                    added = false;
            }
        }
        return added;
    }
private:

    void reset_step(cv::Point p)
    {
    }
    bool check_step(cv::Point p)
    {
        return true;
    }

    int fft_pos(int x)
    {
        if(x > window_size_ / 2)
            return x - window_size_;
        else
            return x;
    }
    cv::Point get_dx_dy(cv::Mat dft)
    {
        cv::Mat res,shift;
        cv::mulSpectrums(fft_roi_,dft,res,0,true);
        cv::idft(res,shift);
        cv::Point pos;
        cv::minMaxLoc(shift,nullptr,nullptr,nullptr,&pos);
        return cv::Point(fft_pos(pos.x),fft_pos(pos.y));
    }

    cv::Mat calc_fft(cv::Mat frame)
    {
        cv::Mat rgb[3],gray,dft;
        cv::Mat roi = cv::Mat(frame,cv::Rect(dx_,dy_,window_size_,window_size_));
        cv::split(roi,rgb);
        rgb[1].convertTo(gray,CV_32FC1);
        cv::dft(gray,dft);
        dft = dft / cv::abs(dft);
        return dft;
    }

    void add_image(cv::Mat img,cv::Point shift)
    {
        int dx = shift.x;
        int dy = shift.y;
        int width  = (sum_.cols - std::abs(dx));
        int height = (sum_.rows - std::abs(dy));
        cv::Rect src_rect = cv::Rect(std::max(dx,0),std::max(dy,0),width,height);
        cv::Rect img_rect = cv::Rect(std::max(-dx,0),std::max(-dy,0),width,height);
        cv::Mat(sum_,src_rect) += cv::Mat(img,img_rect);
        cv::Mat(count_,src_rect) += cv::Scalar(1,1,1);
    }
    int frames_;
    bool has_darks_;
    cv::Mat sum_;
    cv::Mat darks_;
    cv::Mat count_;
    cv::Mat fft_roi_;
    int dx_,dy_,window_size_;
public:
    static char error_message_[256];
};

char Stacker::error_message_[256];

#ifdef INCLUDE_MAIN

void test()
{
    constexpr int H=240;
    constexpr int W=320;
    unsigned char img[H*W*3],stacked[H*W*3],darks[H*W*3];

    memset(darks,25,sizeof(darks));

    Stacker s(W,H,-1,-1,240);
    s.set_darks(darks);

    for(int i=0;i<20;i++) {
        unsigned char *pos = img;
        for(int r=0;r<H;r++) {
            for(int c=0;c<W;c++) {
                int dx = r - (H/2 + i * 2);
                int dy = c - (W/2 - i * 1);
                int d2 =dx*dx + dy*dy;
                int R = d2 < 20*20 ? 100 : 0;
                int G = d2 < 30*20 ? 150 : 0;
                int B = d2 < 10*10 ? 120 : 0;
                *pos++ = R + 50 * (double) rand()/RAND_MAX;
                *pos++ = G + 50 * (double)rand()/RAND_MAX;
                *pos++ = B + 50 * (double)rand()/RAND_MAX;
            }
        }
        s.stack_image(img);
    }
    s.get_stacked(stacked);
    std::ofstream tmp("res.ppm");
    tmp << "P6\n"<<W<<" " << H << " 255\n";
    tmp.write((char*)(stacked),H*W*3);
    tmp.close();

}


void make_darks(std::vector<cv::Mat> &pictures,std::vector<unsigned char> &data,int H,int W)
{
    int N=pictures.size();
    std::vector<unsigned char> buffer(N);
    for(int p=0;p<H*W*3;p++) {
        for(int j=0;j<N;j++) {
            buffer[j] = ((unsigned char *)(pictures[j].data))[p];
            std::nth_element(buffer.begin(),buffer.begin()+N/2,buffer.end());
            data[p] = buffer[N/2];
        }
    }
}

#ifdef DO_STACK

int main(int argc,char **argv)
{
    if(argc == 1) {
        test();
    }
    else {
        std::vector<cv::Mat> pictures;
        for(int i=1;i<argc;i++) {
            pictures.push_back(cv::imread(argv[i]));
        }
        int H=pictures.at(0).rows;
        int W=pictures.at(0).cols;
        Stacker stacker(W,H);
        std::vector<unsigned char> darks(H*W*3);
        make_darks(pictures,darks,W,H);
        stacker.set_darks(darks.data());
        for(unsigned i=0;i<pictures.size();i++) {
            if(pictures[i].rows != H || pictures[i].cols != W) {
                printf("Skipping %d\n",i);
            }
            stacker.stack_image(pictures[i].data);
        }
        std::vector<unsigned char> data(H*W*3);
        stacker.get_stacked(data.data());
        std::ofstream tmp("res.ppm");
        tmp << "P6\n"<<W<<" " << H << " 255\n";
        tmp.write((char*)(data.data()),H*W*3);
        tmp.close();
    }
}

#else

int main()
{
    test();
    return 0;
}

#endif
#endif

extern "C" {
    Stacker *stacker_new(int w,int h,int roi_x,int roi_y,int roi_size)
    {
        try {
            LOG("Creating stacker of size %d,%d roi=%d",w,h,roi_size);
            return new Stacker(w,h,roi_x,roi_y,roi_size);
        }
        catch(std::exception const &e) {
            snprintf(Stacker::error_message_,sizeof(Stacker::error_message_),"Failed to create stacker %s",e.what());
            return 0;
        }
        catch(...) {
            snprintf(Stacker::error_message_,sizeof(Stacker::error_message_),"Unknown exceptiopn");
            return 0;
        }
    }
    void stacker_delete(Stacker *obj)
    {
        delete obj;
    }
    int stacker_set_darks(Stacker *obj,unsigned char *rgb)
    {
        try {
            obj->set_darks(rgb);
        }
        catch(std::exception const &e) {
            snprintf(obj->error_message_,sizeof(obj->error_message_),"Failed: %s",e.what());
            return -1;
        }
        catch(...) { strcpy(obj->error_message_,"Unknown exceptiopn"); return -1; }
        return 0;
    }
    int stacker_stack_image(Stacker *obj,unsigned char *rgb,int restart)
    {
        try {
            return obj->stack_image(rgb,restart);
        }
        catch(std::exception const &e) {
            snprintf(obj->error_message_,sizeof(obj->error_message_),"Failed: %s",e.what());
            return -1;
        }
        catch(...) { strcpy(obj->error_message_,"Unknown exceptiopn"); return -1; }
    }
    int stacker_get_stacked(Stacker *obj,unsigned char *rgb)
    {
        try {
            obj->get_stacked(rgb);
        }
        catch(std::exception const &e) {
            snprintf(obj->error_message_,sizeof(obj->error_message_),"Failed: %s",e.what());
            return -1;
        }
        catch(...) { strcpy(obj->error_message_,"Unknown exceptiopn"); return -1; }
        return 0;
    }
    char const *stacker_error()
    {
        return Stacker::error_message_;
    }
}
