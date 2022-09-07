#include "stack.h"
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#ifdef __ANDROID__
#include <android/log.h>
#define LOG(format, ...) __android_log_print(ANDROID_LOG_ERROR, "UVC", "[%s:%d/%s] " format "\n", basename(__FILE__), __LINE__, __FUNCTION__, ##__VA_ARGS__)
#else
#define LOG(format, ...) fprintf(stderr,"[%s:%d/%s] " format "\n", basename(__FILE__), __LINE__, __FUNCTION__, ##__VA_ARGS__)
#endif
#include <fstream>

#ifdef INCLUDE_MAIN
#ifdef DO_STACK
#include <opencv2/imgcodecs.hpp>
static cv::Mat imreadrgb(std::string const &path)
{
    cv::Mat tmp=cv::imread(path);
    cv::Mat img;
    cv::cvtColor(tmp,img,cv::COLOR_BGR2RGB);
    return img;
}

static void imwritergb(std::string const &path,cv::Mat m)
{
    cv::Mat tmp;
    cv::cvtColor(m,tmp,cv::COLOR_RGB2BGR);
    cv::imwrite(path,tmp);
}



#endif
#endif

struct Stacker {
public:

    Stacker(int width,int height,int roi_x=-1,int roi_y=-1,int roi_size = -1) : 
        frames_(0),
        has_darks_(false)
    {
        error_message_[0]=0;
        fully_stacked_area_ = cv::Rect(0,0,width,height);
        if(roi_size == -1) {
            window_size_ = std::min(height,width);
            #if 0
            for(int i=0;i<16;i++) {
                if((1<<i)<=window_size_ && (1<<(i+1)) > window_size_) {
                    window_size_ = 1<<i;
                    break;
                }
            }
            #endif
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
        make_fft_blur();
    }

    void set_source_gamma(float g)
    {
        src_gamma_ = g;
        darks_corrected_ = false;
    }
    void set_target_gamma(float g)
    {
        if(g == -1.0f) {
            enable_stretch_ = true;
            tgt_gamma_ = 1.0f;
        }
        else {
            tgt_gamma_ = g;
            enable_stretch_ = false;
        }
    }

    void make_fft_blur()
    {
        if(window_size_ == 0)
            return;
        fft_kern_ = cv::Mat(window_size_,window_size_,CV_32FC2);
        int rad = (window_size_/16);
        for(int r=0;r<window_size_;r++) {
            for(int c=0;c<window_size_;c++) {
                int dy = fft_pos(r);
                int dx = fft_pos(c);
                std::complex<float> val = 1;
                if(dx*dx+dy*dy > rad*rad) {
                    val = 0;
                }
                fft_kern_.at<std::complex<float> >(r,c) = val;
            }
        }
    }

    void set_darks(unsigned char *rgb_img)
    {
        has_darks_ = true;
        darks_corrected_ = false;
        cv::Mat src(sum_.rows,sum_.cols,CV_8UC3,rgb_img);
        src.convertTo(darks_,CV_32FC3,1/(255.0));
    }

    void save_stacked_darks(char const *path)
    {
        cv::Mat stacked  = sum_ / count_;
        std::ofstream f(path);
        if(!f)
            throw std::runtime_error("Failed to open darks path");
        if(!f.write((char *)stacked.data,stacked.rows*stacked.cols*sizeof(float)*3))
            throw std::runtime_error("Failed to save darks");
    }

    void load_darks(char const *path)
    {
        has_darks_ = true;
        darks_corrected_ = false;
        darks_ = cv::Mat(sum_.rows,sum_.cols,CV_32FC3); 
        std::ifstream f(path);
        if(!f)
            throw std::runtime_error("Failed to open darks file");
        f.read((char *)darks_.data,sum_.rows*sum_.cols*sizeof(float)*3);
        if(!f)
            throw std::runtime_error("Failed to read darks file");
    }
    void get_stacked(unsigned char *rgb_img)
    {
        if(frames_ == 0)
            memset(rgb_img,0,sum_.rows*sum_.cols*3);
        else {
            cv::Mat tgt(sum_.rows,sum_.cols,CV_8UC3,rgb_img);
            //cv::Mat tmp = sum_ / count_;
            cv::Mat tmp = sum_ * (1.0/ fully_stacked_count_);
            if(enable_stretch_) {
                double scale[3],offset[3],mean=0.5;
                calc_scale_offset(tmp(fully_stacked_area_),scale,offset,mean);
                tmp = tmp.mul(cv::Scalar(scale[0],scale[1],scale[2]));
                tmp += cv::Scalar(offset[0],offset[1],offset[2]);
                tmp = cv::max(0,cv::min(1,tmp));
                float g=cv::min(2.2,log(mean)/log(0.25));
                printf("Mean %f gamma=%f\n",mean,g);
                cv::pow(tmp,1/g,tmp);
            }
            else {
                double max_v,min_v;
                cv::minMaxLoc(tmp,&min_v,&max_v);
                if(min_v < 0)
                    min_v = 0;
                tmp = cv::max(0,(tmp - float(min_v))*float(1.0/(max_v-min_v)));
                if(tgt_gamma_!=1.0f) {
                    cv::pow(tmp,1/tgt_gamma_,tmp);
                }
            }
            //tmp.convertTo(tgt,CV_8UC3,scale,offset);
            tmp.convertTo(tgt,CV_8UC3,255,0);
        }
    }


    bool stack_image(unsigned char *rgb_img,bool restart_position = false)
    {
        cv::Mat frame8bit(sum_.rows,sum_.cols,CV_8UC3,rgb_img);
        cv::Mat frame;
        frame8bit.convertTo(frame,CV_32FC3,1.0/255);
        if(src_gamma_ != 1.0) {
            cv::pow(frame,src_gamma_,frame);
        }
        if(has_darks_) {
            if(src_gamma_ != 1.0) { 
                if(!darks_corrected_) {
                    darks_corrected_ = true;
                    cv::pow(darks_,src_gamma_,darks_gamma_corrected_);
                }
                frame = cv::max(frame - darks_gamma_corrected_,0);
            }
            else {
                frame = cv::max(frame - darks_,0);
            }
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
            reset_step(cv::Point(0,0));
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
                else {
                    LOG("failed registration dx=%d dy=%d\n",shift.x,shift.y);
                    added = false;
                }
            }
        }
        return added;
    }
private:

    void calc_scale_offset(cv::Mat img,double scale[3],double offset[3],double &mean)
    {
        double minV,maxV;
        cv::minMaxLoc(img,&minV,&maxV);
        cv::Mat tmp;
        if(minV < 0)
            minV = 0;
        double a=255.0/(maxV-minV);
        double b=-minV*a;
        img.convertTo(tmp,CV_8UC3,a,b);
        int N=tmp.rows*tmp.cols;
        unsigned char *p=tmp.data;
        int counters[256][3]={};
        for(int i=0;i<N;i++) {
            for(int j=0;j<3;j++) {
               counters[*p++][j]++;
            }
        }
        mean = 0;
        int total=0;
        for(int color=0;color<3;color++) {
            int lp=-1,hp=-1;
            int sum=0;
            for(int i=0;i<255;i++) {
                sum+=counters[i][color];
                if(sum*100.0f/N >= low_per_) {
                    lp = i;
                    break;
                }
            }
            sum=N;
            for(int i=255;i>=0;i--) {
                sum-=counters[i][color];
                if(sum*100.0f/N < high_per_) {
                    hp = i;
                    break;
                }
            }
            for(int i=lp;i<=hp;i++) {
                mean += double(i-lp)/(hp-lp) * counters[i][color];
                total+=counters[i][color];
            }
            double L = (maxV*lp + minV*(255-lp))/255;
            double H = (maxV*hp + minV*(255-hp))/255;
            scale[color] = 1.0/(H-L);
            offset[color] = -L*scale[color];
            printf("Stretch %d:[%f-> %f %f<- %f]\n",color,minV,L,H,maxV);
        }
        mean/=total;
    }

    void reset_step(cv::Point p)
    {
        current_position_ = p;
        step_sum_sq_ = 0;
        count_frames_ = 0;
        missed_frames_ = 0;
    }
    bool check_step(cv::Point p)
    {
        float dx = current_position_.x - p.x;
        float dy = current_position_.y - p.y;
        float step_sq_ = dx*dx + dy*dy;
        if(count_frames_ == 0) {
            current_position_ = p;
            step_sum_sq_ = dx*dx + dy*dy;
            count_frames_ = 1;
            missed_frames_ = 0;
            return true;
        }
        else {
            float step_avg_ = sqrt(step_sum_sq_ / count_frames_);
            constexpr int missed_in_a_row_limit = 5;
            constexpr float pixel_0_threshold = 3;
            float step = sqrt(step_sq_);
            float step_limit = std::max((2 + (float)sqrt(missed_frames_)) * step_avg_,pixel_0_threshold);
#ifdef DEBUG            
            printf("Step size %5.2f from (%d,%d) to (%d,%d) limit =%5.1f avg_step=%5.1f\n",step,
                    current_position_.x,current_position_.y,
                    p.x,p.y,
                    step_limit,step_avg_);
#endif            
            if(missed_frames_ > missed_in_a_row_limit || step > step_limit) {
                missed_frames_ ++;
                return false;
            }
            else {
                current_position_ = p;
                count_frames_ ++;
                step_sum_sq_+=step_sq_;
                missed_frames_ = 0;
                return true;
            }
        }
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
        cv::Mat dspec;
        cv::divSpectrums(res,cv::abs(res),dspec,0);
        cv::idft(dspec,shift,cv::DFT_REAL_OUTPUT);
        cv::Point pos;
#ifdef DEBUG
        double minv,maxv;
        cv::minMaxLoc(shift,&minv,&maxv,nullptr,&pos);
        static int n=1;
        cv::Mat a,b;
        a=255*(shift-minv)/(maxv-minv);
        a.convertTo(b,CV_8UC1);
        imwritergb(std::to_string(n++) + "_shift.png",b);
#else
        cv::minMaxLoc(shift,nullptr,nullptr,nullptr,&pos);
#endif        
        return cv::Point(fft_pos(pos.x),fft_pos(pos.y));
    }

    cv::Mat calc_fft(cv::Mat frame)
    {
        cv::Mat rgb[3],gray,dft;
        cv::Mat roi = cv::Mat(frame,cv::Rect(dx_,dy_,window_size_,window_size_));
        cv::split(roi,rgb);
        rgb[1].convertTo(gray,CV_32FC1);
        cv::dft(gray,dft,cv::DFT_COMPLEX_OUTPUT);
        cv::mulSpectrums(dft,fft_kern_,dft,0);
#ifdef DEBUG
        {
            cv::idft(dft,gray,cv::DFT_REAL_OUTPUT);
            double minv,maxv;
            cv::minMaxLoc(gray,&minv,&maxv);
            gray = (gray - minv)/(maxv-minv)*255;
            cv::Mat tmp;
            gray.convertTo(tmp,CV_8UC1);
            static int n=1;
            imwritergb(std::to_string(n++) + "_b.png",tmp);
        }
#endif        
        return dft;
    }

    void add_image(cv::Mat img,cv::Point shift)
    {
        int dx = shift.x;
        int dy = shift.y;
        LOG("Adding at %d %d\n",dx,dy);
        int width  = (sum_.cols - std::abs(dx));
        int height = (sum_.rows - std::abs(dy));
        cv::Rect src_rect = cv::Rect(std::max(dx,0),std::max(dy,0),width,height);
        cv::Rect img_rect = cv::Rect(std::max(-dx,0),std::max(-dy,0),width,height);
        cv::Mat(sum_,src_rect) += cv::Mat(img,img_rect);
        cv::Mat(count_,src_rect) += cv::Scalar(1,1,1);
        fully_stacked_area_ = fully_stacked_area_ & src_rect;
        fully_stacked_count_++;
    }
    int frames_;
    bool has_darks_;
    cv::Rect fully_stacked_area_;
    int fully_stacked_count_ = 0;
    cv::Mat sum_;
    cv::Mat darks_;
    cv::Mat darks_gamma_corrected_;
    bool darks_corrected_ = false;
    cv::Mat count_;
    cv::Mat fft_kern_;
    cv::Mat fft_roi_;
    cv::Point current_position_;
    int count_frames_,missed_frames_;
    float step_sum_sq_;
    int dx_,dy_,window_size_;
    float src_gamma_ = 1.0f;
    float tgt_gamma_ = 1.0f;
    bool enable_stretch_ = true;
    float low_per_= 5.0f;
    float high_per_=99.9f;
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

    Stacker s(W,H,-1,-1,64);
    //s.set_darks(darks);

    s.set_source_gamma(2.2);
    s.set_target_gamma(2.2);

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
        std::string darks_path;
        std::string save_darks;
        bool has_darks=false;
        float src_gamma=1.0;
        float tgt_gamma=1.0;
        int roi=-1;
        while(argc >= 3 && argv[1][0]=='-') {
            std::string param=argv[1];
            if(param == "-d") {
                darks_path=argv[2];
                has_darks = true;
            }
            else if(param == "-D") {
                save_darks = argv[2];
                roi = 0;
            }
            else if(param == "-r")
                roi = atoi(argv[2]);
            else if(param == "-g")
                src_gamma = atof(argv[2]);
            else if(param == "-G")
                tgt_gamma = atof(argv[2]);
            else {
                printf("Unknown flag %s\n",param.c_str());
                return 1;
            }
            argv+=2;
            argc-=2;
        }
        std::vector<cv::Mat> pictures;
        std::vector<bool> restart;
        bool flag=false;
        for(int i=1;i<argc;i++) {
            if(argv[i]==std::string("restart")) {
                flag=true;
                continue;
            }
            if(strstr(argv[i],"restart"))
                flag=true;
            cv::Mat img = imreadrgb(argv[i]);
            pictures.push_back(img);
            restart.push_back(flag);
            flag=false;
        }
        int H=pictures.at(0).rows;
        int W=pictures.at(0).cols;
        Stacker stacker(W,H,-1,-1,roi);
        if(has_darks) {
            if(darks_path.find(".flt")!=std::string::npos) {
                stacker.load_darks(darks_path.c_str());
            }
            else {
                cv::Mat darks = imreadrgb(darks_path);
                if(H != darks.rows || W != darks.cols) {
                    printf("Invalid darks size\n");
                    return 1;
                }
                stacker.set_darks((unsigned char *)darks.data);
            }
        }
        stacker.set_source_gamma(src_gamma);
        stacker.set_target_gamma(tgt_gamma);
        /*std::vector<unsigned char> darks(H*W*3);
        make_darks(pictures,darks,W,H);
        stacker.set_darks(darks.data());
        {
            std::ofstream tmp("darks.ppm");
            tmp<<"P6\n"<<W<<" " << H << " 255\n";
            tmp.write((char*)darks.data(),3*H*W);
        }*/
        for(unsigned i=0;i<pictures.size();i++) {
            if(pictures[i].rows != H || pictures[i].cols != W) {
                printf("Skipping %d\n",i);
            }
            stacker.stack_image(pictures[i].data,restart[i]);
        }
        if(save_darks.empty()) {
            std::vector<unsigned char> data(H*W*3);
            stacker.get_stacked(data.data());
            std::ofstream tmp("res.ppm");
            tmp << "P6\n"<<W<<" " << H << " 255\n";
            tmp.write((char*)(data.data()),H*W*3);
            tmp.close();
        }
        else {
            stacker.save_stacked_darks(save_darks.c_str());
        }
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
    
    int stacker_save_stacked_darks(Stacker *obj,char const *path)
    {
        try {
            obj->save_stacked_darks(path);
        }
        catch(std::exception const &e) {
            snprintf(obj->error_message_,sizeof(obj->error_message_),"Failed: %s",e.what());
            return -1;
        }
        catch(...) { strcpy(obj->error_message_,"Unknown exceptiopn"); return -1; }
        return 0;
    }
    int stacker_load_darks(Stacker *obj,char const *path)
    {
        try {
            obj->load_darks(path);
        }
        catch(std::exception const &e) {
            snprintf(obj->error_message_,sizeof(obj->error_message_),"Failed: %s",e.what());
            return -1;
        }
        catch(...) { strcpy(obj->error_message_,"Unknown exceptiopn"); return -1; }
        return 0;
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

    void stacker_set_src_gamma(Stacker *obj,float gamma)
    {
        obj->set_source_gamma(gamma);
    }

    void stacker_set_tgt_gamma(Stacker *obj,float gamma)
    {
        obj->set_target_gamma(gamma);
    }
    char const *stacker_error()
    {
        return Stacker::error_message_;
    }
}
