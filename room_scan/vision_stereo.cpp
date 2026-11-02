// OpenCV performs matching and edge-aware filtering. An inferred filter value
// never acquires an observation without the current raw bilateral match.
#include "vision_common.h"
#include <opencv2/calib3d.hpp>
#include <opencv2/ximgproc/disparity_filter.hpp>
#include <array>
#include <cmath>
#include <memory>
#include <algorithm>
#include <vector>

namespace room_vision {
thread_local std::string lastError;
namespace {
constexpr int Width=320,Height=240,Output=160*120,Disparities=64;
struct Stereo {
    cv::Ptr<cv::StereoSGBM> matcher;
    cv::Ptr<cv::StereoMatcher> reverse;
    cv::Ptr<cv::ximgproc::DisparityWLSFilter> filter;
    cv::Mat disparity,rightDisparity,filtered;
    Stereo(){
        // Bound worker use independently of the system-wide CPU count.
        cv::setNumThreads(1);
        matcher=cv::StereoSGBM::create(0,Disparities,5,200,800,1,31,10,75,2,cv::StereoSGBM::MODE_SGBM_3WAY);
        reverse=cv::ximgproc::createRightMatcher(matcher);
        filter=cv::ximgproc::createDisparityWLSFilter(matcher);
        filter->setLambda(8000);filter->setSigmaColor(1.5);
        // The WLS factory disables matcher uniqueness and speckle rejection.
        // Restore bilateral raw evidence checks after constructing the filter.
        matcher->setUniquenessRatio(10);matcher->setSpeckleWindowSize(75);matcher->setSpeckleRange(2);matcher->setDisp12MaxDiff(1);
        auto rightSgbm=reverse.dynamicCast<cv::StereoSGBM>();
        require(!rightSgbm.empty(),"Stereo reverse matcher unavailable");
        // createRightMatcher does not copy speckleRange. A zero range would
        // erase natural subpixel disparity slopes when speckles are enabled.
        rightSgbm->setUniquenessRatio(10);rightSgbm->setSpeckleWindowSize(75);rightSgbm->setSpeckleRange(2);rightSgbm->setDisp12MaxDiff(1);
    }
    int compute(const uint8_t* left,const uint8_t* right,const uint8_t* ls,const uint8_t* rs,
                float focal,float baseline,float* depth,float* confidence){
        require(left&&right&&ls&&rs&&depth&&confidence,"Incomplete rectified stereo pair");
        require(std::isfinite(focal)&&focal>0&&focal<=100000&&std::isfinite(baseline)&&baseline>=.02f&&baseline<=.15f,"Invalid stereo geometry");
        std::fill(depth,depth+Output,0);std::fill(confidence,confidence+Output,0);
        const cv::Mat l(Height,Width,CV_8UC1,const_cast<uint8_t*>(left));
        const cv::Mat r(Height,Width,CV_8UC1,const_cast<uint8_t*>(right));
        matcher->compute(l,r,disparity);reverse->compute(r,l,rightDisparity);filter->filter(disparity,l,filtered,rightDisparity);
        const auto certainty=filter->getConfidenceMap();
        // Independent optical evidence distinguishes strong samples from
        // plausible regularized matches. It does not promote filter-filled gaps.
        std::array<std::vector<uint32_t>,2> census{std::vector<uint32_t>(Width*Height),std::vector<uint32_t>(Width*Height)};
        std::array<std::vector<uint8_t>,2> texture{std::vector<uint8_t>(Width*Height),std::vector<uint8_t>(Width*Height)};
        for(unsigned e=0;e<2;++e){const auto* image=e?right:left;
            for(int y=2;y<Height-2;++y)for(int x=2;x<Width-2;++x){unsigned lo=255,hi=0;uint32_t bits=0;
                for(int v=-2;v<=2;++v)for(int u=-2;u<=2;++u){const unsigned p=image[(y+v)*Width+x+u];lo=std::min(lo,p);hi=std::max(hi,p);
                    if(u||v)bits=(bits<<1)|(p<image[y*Width+x]);}
                census[e][y*Width+x]=bits;const auto n=__builtin_popcount(bits);texture[e][y*Width+x]=hi-lo>=8&&n>=4&&n<=20;
            }
        }
        const auto cost=[&](int x,int y,int rx){unsigned sum=0;
            for(int v=-1;v<=1;++v)for(int u=-1;u<=1;++u)sum+=unsigned(__builtin_popcount(census[0][(y+v)*Width+x+u]^census[1][(y+v)*Width+rx+u]));
            return sum;};
        int accepted=0;
        for(int y=4;y<Height-4;y+=2)for(int x=4;x<Width-4;x+=2){
            const float raw=disparity.at<int16_t>(y,x)/16.f;if(raw<=0)continue;
            const int rx=int(std::lround(x-raw));if(rx<4||rx>=Width-4||!ls[y*Width+x]||!rs[y*Width+rx])continue;
            const float rev=rightDisparity.at<int16_t>(y,rx)/16.f;
            if(rev>=0||rev<=reverse->getMinDisparity()-1||std::abs(raw+rev)>1)continue;
            const float smooth=filtered.at<int16_t>(y,x)/16.f;
            // WLS can infer textureless/occluded pixels. Keep only samples
            // with raw left/right agreement, actual texture and filter support.
            if(certainty.at<float>(y,x)<128||smooth<=0||std::abs(smooth-raw)>.5f
                ||!texture[0][y*Width+x]||!texture[1][y*Width+rx])continue;
            const float z=focal*baseline/smooth;if(z<.25f||z>6)continue;
            const int d=int(std::lround(raw)),matchedX=x-d;if(matchedX<4||matchedX>=Width-4)continue;
            const unsigned best=cost(x,y,matchedX);unsigned alternate=10000,back=10000;
            for(int a=1;a<Disparities;++a){if(std::abs(a-d)<=1)continue;const int rX=x-a,lX=matchedX+a;
                if(rX>=4&&rX<Width-4&&rs[y*Width+rX]&&texture[1][y*Width+rX])alternate=std::min(alternate,cost(x,y,rX));
                if(lX>=4&&lX<Width-4&&ls[y*Width+lX]&&texture[0][y*Width+lX])back=std::min(back,cost(lX,y,matchedX));}
            const bool strong=certainty.at<float>(y,x)>=200&&std::abs(smooth-raw)<=.25f&&best<=70&&alternate>=best+2&&back>=best+2;
            const auto index=(y/2)*160+x/2;depth[index]=z;confidence[index]=strong?.9f:.6f;++accepted;
        }
        return accepted;
    }
};
}
}
extern "C" {
uint32_t sfnp_room_vision_abi(){return 1;}
const char* sfnp_room_vision_error(){return room_vision::lastError.c_str();}
void* sfnp_stereo_create(){void* result=nullptr;room_vision::checked([&]{result=new room_vision::Stereo;return 0;});return result;}
void sfnp_stereo_destroy(void* context){delete static_cast<room_vision::Stereo*>(context);}
int sfnp_stereo_compute(void* context,const uint8_t* left,const uint8_t* right,const uint8_t* ls,const uint8_t* rs,
                        float focal,float baseline,float* depth,float* confidence){
    return room_vision::checked([&]{room_vision::require(context,"Stereo context unavailable");
        return static_cast<room_vision::Stereo*>(context)->compute(left,right,ls,rs,focal,baseline,depth,confidence);});
}
}
