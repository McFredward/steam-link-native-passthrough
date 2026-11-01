// Public OpenVR stereo source. Admission requires the Arcturus EEPROM geometry
// to match the actual public camera extrinsics; attachment alone is insufficient.
#pragma once
#include <openvr_capi.h>
#include <nlohmann/json.hpp>
#include "native_math.h"
#include <fstream>
#include <vector>
#include <cstring>

namespace room_native {
using Json=nlohmann::json;
inline bool tracked(const TrackedDevicePose_t& value){
    return value.bPoseIsValid&&value.bDeviceIsConnected&&value.eTrackingResult==ETrackingResult_TrackingResult_Running_OK
        &&rigid(fromNative(value.mDeviceToAbsoluteTracking));
}
inline Json boundedJson(const std::string& path,std::size_t limit=131072){
    std::ifstream file(path,std::ios::binary);if(!file)throw std::runtime_error("Calibration unavailable");
    std::string raw(limit+1,'\0');file.read(raw.data(),std::streamsize(raw.size()));raw.resize(std::size_t(file.gcount()));
    if(raw.size()>limit)throw std::runtime_error("Calibration exceeds budget");
    return Json::parse(raw);
}
inline V3 vectorJson(const Json& value){
    if(!value.is_array()||value.size()!=3)throw std::runtime_error("Invalid calibration vector");
    V3 out{value.at(0).get<float>(),value.at(1).get<float>(),value.at(2).get<float>()};
    if(!std::isfinite(out.x)||!std::isfinite(out.y)||!std::isfinite(out.z))throw std::runtime_error("Nonfinite calibration");
    return out;
}
inline Mat factoryPose(const Json& value,float scale){
    const V3 x=vectorJson(value.at("plus_x")),z=vectorJson(value.at("plus_z")),y=cross(z,x),p=vectorJson(value.at("position"))*scale;
    Mat out;const std::array<V3,4> columns{x,y,z,p};for(unsigned c=0;c<4;++c){out.m[0][c]=columns[c].x;out.m[1][c]=columns[c].y;out.m[2][c]=columns[c].z;}
    if(!rigid(out))throw std::runtime_error("Invalid calibration transform");
    return out;
}
inline std::array<Mat,2> arcturusFactoryGeometry(){
    const std::string path="/sys/devices/platform/soc@0/ac15000.cci/i2c-0/0-0050/eeprom";
    std::ifstream file(path,std::ios::binary);if(!file)throw std::runtime_error("Arcturus calibration unavailable");
    std::string raw(131073,'\0');file.read(raw.data(),std::streamsize(raw.size()));raw.resize(std::size_t(file.gcount()));
    if(raw.size()>131072)throw std::runtime_error("Arcturus EEPROM exceeds budget");
    const auto key=raw.find("\"alignment_method\"");const auto begin=key==std::string::npos?key:raw.rfind('{',key);
    if(begin==std::string::npos)throw std::runtime_error("Arcturus EEPROM format unavailable");
    bool quoted=false,escaped=false;int depth=0;std::size_t end=begin;
    for(;end<raw.size();++end){const char c=raw[end];if(quoted){if(escaped)escaped=false;else if(c=='\\')escaped=true;else if(c=='\"')quoted=false;}
        else if(c=='\"')quoted=true;else if(c=='{')++depth;else if(c=='}'&&!--depth){++end;break;}}
    if(depth||end>raw.size())throw std::runtime_error("Incomplete Arcturus calibration");
    const auto rig=Json::parse(raw.substr(begin,end-begin));const auto device=boundedJson("/persist/device_config.json");
    const Mat headFromCad=inverse(factoryPose(device.at("head"),1));
    std::array<Mat,2> out;std::array<bool,2> found{};
    const auto& cameras=rig.at("cameras");if(!cameras.is_array()||cameras.size()>8)throw std::runtime_error("Unsupported Arcturus cameras");
    for(const auto& c:cameras){const auto name=c.at("sourceCamera").get<std::string>();
        const int index=name=="passthrough_left"?0:name=="passthrough_right"?1:-1;if(index<0)continue;
        if(found[unsigned(index)])throw std::runtime_error("Duplicate Arcturus camera");
        Mat transform=multiply(headFromCad,factoryPose(c.at("extrinsics"),.001f));
        // Factory camera coordinates are optical (+Y down,+Z forward).
        for(unsigned r=0;r<3;++r){transform.m[r][1]*=-1;transform.m[r][2]*=-1;}
        if(!rigid(transform))throw std::runtime_error("Unsupported Arcturus axes");
        out[unsigned(index)]=transform;found[unsigned(index)]=true;
    }
    if(!found[0]||!found[1])throw std::runtime_error("Arcturus stereo calibration missing");
    return out;
}
inline std::string stableId(const std::string& value){
    // Stable content identifier, not an authentication digest.
    std::uint64_t a=1469598103934665603ULL,b=1099511628211ULL;
    for(unsigned char c:value){a=(a^c)*1099511628211ULL;b=(b^c)*14029467366897019727ULL;}
    char text[37]{};std::snprintf(text,sizeof(text),"%08x-%04x-5%03x-a%03x-%012llx",unsigned(a>>32),unsigned((a>>16)&65535),
        unsigned(a&4095),unsigned((b>>48)&4095),static_cast<unsigned long long>(b&0xffffffffffffULL));return text;
}
struct StereoResult {std::array<float,160*120> depth{},confidence{};};
inline StereoResult matchStereo(const std::vector<std::uint8_t>& left,const std::vector<std::uint8_t>& right,float focal,float baseline,
                               const std::array<std::vector<std::uint8_t>,2>* support=nullptr){
    constexpr int W=320,H=240,D=48;StereoResult out;
    if(left.size()!=W*H||right.size()!=W*H||focal<=0||baseline<.02f||baseline>.15f)throw std::runtime_error("Invalid rectified stereo pair");
    if(support&&((*support)[0].size()!=W*H||(*support)[1].size()!=W*H))throw std::runtime_error("Invalid rectification support");
    std::array<std::vector<std::uint32_t>,2> census{std::vector<std::uint32_t>(W*H),std::vector<std::uint32_t>(W*H)};
    for(unsigned eye=0;eye<2;++eye){const auto& image=eye?right:left;
        for(int y=2;y<H-2;++y)for(int x=2;x<W-2;++x){std::uint32_t bits=0;
            for(int dy=-2;dy<=2;++dy)for(int dx=-2;dx<=2;++dx){
                if(dx||dy)bits=(bits<<1)|(image[std::size_t(y+dy)*W+x+dx]<image[std::size_t(y)*W+x]);}
            census[eye][std::size_t(y)*W+x]=bits;}}
    const auto cost=[&](int x,int y,int d){int sum=0;
        if(support&&(!(*support)[0][std::size_t(y)*W+x]||!(*support)[1][std::size_t(y)*W+x-d]))return 10000;
        for(int yy=-1;yy<=1;++yy)for(int xx=-1;xx<=1;++xx){
            sum+=__builtin_popcount(census[0][std::size_t(y+yy)*W+x+xx]^census[1][std::size_t(y+yy)*W+x+xx-d]);}
        return sum;};
    for(int y=4;y<H-4;y+=2)for(int x=D+4;x<W-4;x+=2){
        const int texture=__builtin_popcount(census[0][std::size_t(y)*W+x]);if(texture<4||texture>20)continue;
        std::array<int,D+1> costs{};int best=10000,second=10000,disparity=0;
        for(int d=1;d<D;++d){costs[unsigned(d)]=cost(x,y,d);if(costs[unsigned(d)]<best){second=best;best=costs[unsigned(d)];disparity=d;}
            else if(costs[unsigned(d)]<second)second=costs[unsigned(d)];}
        if(disparity<2||disparity>=D-1||best>70||second-best<6)continue;
        const float curvature=float(costs[unsigned(disparity-1)]+costs[unsigned(disparity+1)]-2*best);
        const float offset=curvature>0?std::clamp(float(costs[unsigned(disparity-1)]-costs[unsigned(disparity+1)])/(2*curvature),-.5f,.5f):0;
        // Independent reverse search rejects ambiguous foreground/background joins.
        const int rx=x-disparity;int reverse=0,reverseCost=10000;
        for(int d=1;d<D&&rx+d<W-3;++d){const int c=cost(rx+d,y,d);if(c<reverseCost){reverseCost=c;reverse=d;}}
        if(std::abs(reverse-disparity)>1)continue;
        const float z=focal*baseline/(disparity+offset);if(z<.25f||z>6)continue;
        const std::size_t index=std::size_t(y/2)*160+x/2;out.depth[index]=z;
        out.confidence[index]=disparity>=4&&second-best>=12?.9f:.55f;
    }
    return out;
}
class StereoCamera {
public:
    VR_IVRSystem_FnTable* system=nullptr;VR_IVRTrackedCamera_FnTable* api=nullptr;
    TrackedCameraHandle_t handle=0;Json binding;std::string status="not-started";
    ~StereoCamera(){stop();}
    bool start(const std::string& session,const std::string& origin){
        stop();status="public-stereo-source-unavailable";
        try{
            if(!api||!api->HasCamera||!api->GetCameraFrameSize||!api->GetCameraIntrinsics||!api->AcquireVideoStreamingService
                ||!api->ReleaseVideoStreamingService||!api->GetVideoStreamFrameBuffer||!api->GetCameraTrackingSpace)return false;
            bool has=false;if(api->HasCamera(0,&has)||!has)return false;
            ETrackedPropertyError error=ETrackedPropertyError_TrackedProp_Success;
            const int count=system->GetInt32TrackedDeviceProperty(0,ETrackedDeviceProperty_Prop_NumCameras_Int32,&error);
            if(error||count!=2)return false;
            layout=system->GetInt32TrackedDeviceProperty(0,ETrackedDeviceProperty_Prop_CameraFrameLayout_Int32,&error);
            if(error||!(layout&2)||((layout&48)!=16&&(layout&48)!=32))return false;
            const auto kind=EVRTrackedCameraFrameType_VRTrackedCameraFrameType_Undistorted;
            if(api->GetCameraFrameSize(0,kind,&width,&height,&bytes)||!width||!height||width>8192||height>8192
                ||std::uint64_t(width)*height*4!=bytes||bytes>64u*1024*1024)return false;
            eyeWidth=width/((layout&32)?2:1);eyeHeight=height/((layout&16)?2:1);
            if(eyeWidth*((layout&32)?2:1)!=width||eyeHeight*((layout&16)?2:1)!=height)return false;
            std::array<HmdMatrix34_t,2> transforms{};
            const auto size=system->GetArrayTrackedDeviceProperty(0,ETrackedDeviceProperty_Prop_CameraToHeadTransforms_Matrix34_Array,
                k_unHmdMatrix34PropertyTag,transforms.data(),sizeof(transforms),&error);
            if(error||size!=sizeof(transforms))return false;
            const auto factory=arcturusFactoryGeometry();
            for(unsigned i=0;i<2;++i){headFromCamera[i]=fromNative(transforms[i]);
                if(!rigid(headFromCamera[i])||length(position(headFromCamera[i])-position(factory[i]))>.015f
                    ||dot(column(headFromCamera[i],0),column(factory[i],0))<.98f
                    ||dot(column(headFromCamera[i],1),column(factory[i],1))<.98f
                    ||dot(column(headFromCamera[i],2),column(factory[i],2))<.98f){status="camera-module-association-unverified";return false;}
                HmdVector2_t f{},c{};if(api->GetCameraIntrinsics(0,i,kind,&f,&c))return false;
                intrinsics[i]={f.v[0],f.v[1],c.v[0],c.v[1]};for(float v:intrinsics[i])if(!std::isfinite(v))return false;
                if(f.v[0]<=0||f.v[1]<=0||c.v[0]<0||c.v[0]>=eyeWidth||c.v[1]<0||c.v[1]>=eyeHeight)return false;
            }
            const V3 left=position(headFromCamera[0]),right=position(headFromCamera[1]);baseline=length(right-left);
            if(baseline<.03f||baseline>.10f)return false;
            const V3 x=normalized(right-left),forward=normalized((column(headFromCamera[0],2)+column(headFromCamera[1],2))*(-1));
            const V3 down=normalized(cross(forward,x)),z=normalized(cross(x,down));
            rectifiedHead=Mat{};const std::array<V3,4> columns{x,down*(-1),z*(-1),left};
            for(unsigned c=0;c<4;++c){rectifiedHead.m[0][c]=columns[c].x;rectifiedHead.m[1][c]=columns[c].y;rectifiedHead.m[2][c]=columns[c].z;}
            if(!rigid(rectifiedHead))return false;
            buildMaps();trackingSpace=api->GetCameraTrackingSpace();
            if(trackingSpace==ETrackingUniverseOrigin_TrackingUniverseStanding)standingFromTracking=Mat{};
            else if(trackingSpace==ETrackingUniverseOrigin_TrackingUniverseRawAndUncalibrated)standingFromTracking=fromNative(system->GetRawZeroPoseToStandingAbsoluteTrackingPose());
            else if(trackingSpace==ETrackingUniverseOrigin_TrackingUniverseSeated)standingFromTracking=fromNative(system->GetSeatedZeroPoseToStandingAbsoluteTrackingPose());
            else return false;
            if(!rigid(standingFromTracking))return false;
            pixels.resize(bytes);if(api->AcquireVideoStreamingService(0,&handle)||!handle){stop();return false;}
            Json descriptor={{"intrinsics",intrinsics},{"layout",layout},{"width",width},{"height",height}};
            for(const auto& m:headFromCamera)for(const auto& row:m.m)for(float v:row)descriptor["camera_to_head"].push_back(v);
            binding={{"session_id",session},{"origin_id",origin},{"calibration_id",stableId(descriptor.dump())}};
            lastSequence=0;lastExposure=0;status="calibrated-public-arcturus-stereo";return true;
        }catch(const std::exception&){stop();status="camera-calibration-unverified";return false;}
    }
    void stop(){if(handle&&api&&api->ReleaseVideoStreamingService)api->ReleaseVideoStreamingService(handle);handle=0;pixels.clear();}
    Json next(){
        if(!handle)return {};
        CameraVideoStreamFrameHeader_t header{};
        if(api->GetCameraTrackingSpace()!=trackingSpace)throw std::runtime_error("Camera tracking origin changed");
        const auto result=api->GetVideoStreamFrameBuffer(handle,EVRTrackedCameraFrameType_VRTrackedCameraFrameType_Undistorted,
            pixels.data(),bytes,&header,sizeof(header));
        if(result==EVRTrackedCameraError_VRTrackedCameraError_NoFrameAvailable)return {};
        if(result||header.nWidth!=width||header.nHeight!=height||header.nBytesPerPixel!=4
            ||header.eFrameType!=EVRTrackedCameraFrameType_VRTrackedCameraFrameType_Undistorted
            ||!tracked(header.trackedDevicePose)||!header.ulFrameExposureTime)
            throw std::runtime_error("Unverified camera observation");
        if(header.nFrameSequence==lastSequence)return {};
        if(header.nFrameSequence<lastSequence||header.ulFrameExposureTime<=lastExposure)throw std::runtime_error("Camera sequence changed");
        const Mat trackedHead=fromNative(header.trackedDevicePose.mDeviceToAbsoluteTracking);
        if(!rigid(trackedHead))throw std::runtime_error("Invalid exposure pose");
        std::array<std::vector<std::uint8_t>,2> images{std::vector<std::uint8_t>(320*240),std::vector<std::uint8_t>(320*240)};
        for(unsigned eye=0;eye<2;++eye)for(unsigned i=0;i<320*240;++i){const auto offset=maps[eye][i];if(offset==UINT32_MAX)continue;
            const auto* p=pixels.data()+offset;images[eye][i]=std::uint8_t((299u*p[0]+587u*p[1]+114u*p[2])/1000);}
        const auto stereo=matchStereo(images[0],images[1],220,baseline,&support);
        const Mat pose=multiply(multiply(standingFromTracking,trackedHead),rectifiedHead);
        lastSequence=header.nFrameSequence;lastExposure=header.ulFrameExposureTime;
        return {{"event","depth"},{"binding",binding},{"sequence",lastSequence},{"exposure_ticks",lastExposure},
            {"pose",{{"position",{pose.m[0][3],pose.m[1][3],pose.m[2][3]}},{"orientation_xyzw",quaternion(pose)}}},
            {"width",160},{"height",120},{"intrinsics",{110.,110.,79.75,59.75}},
            {"depth_m",stereo.depth},{"confidence",stereo.confidence}};
    }
private:
    std::uint32_t width=0,height=0,eyeWidth=0,eyeHeight=0,bytes=0,lastSequence=0;std::uint64_t lastExposure=0;
    int layout=0;float baseline=0;ETrackingUniverseOrigin trackingSpace=ETrackingUniverseOrigin_TrackingUniverseStanding;
    std::array<std::array<float,4>,2> intrinsics{};std::array<Mat,2> headFromCamera{};
    Mat rectifiedHead,standingFromTracking;std::vector<std::uint8_t> pixels;
    std::array<std::vector<std::uint32_t>,2> maps;
    std::array<std::vector<std::uint8_t>,2> support;
    void buildMaps(){for(unsigned eye=0;eye<2;++eye){maps[eye].assign(320*240,UINT32_MAX);const Mat inverseCamera=inverse(headFromCamera[eye]);
        for(unsigned y=0;y<240;++y)for(unsigned x=0;x<320;++x){const V3 ray=column(rectifiedHead,0)*((float(x)-159.5f)/220)
                +column(rectifiedHead,1)*(-(float(y)-119.5f)/220)+column(rectifiedHead,2)*(-1);
            const V3 c=transform(inverseCamera,ray+position(headFromCamera[eye]));if(c.z>=-.01f)continue;
            const auto& in=intrinsics[eye];const float u=in[0]*c.x/-c.z+in[2],v=-in[1]*c.y/-c.z+in[3];
            if(u<0||v<0||u>=eyeWidth||v>=eyeHeight)continue;
            const unsigned px=unsigned(u)+((layout&32)?eye*eyeWidth:0),py=unsigned(v)+((layout&16)?eye*eyeHeight:0);
            maps[eye][y*320+x]=(py*width+px)*4;
        }
        // Every census + aggregation sample must come from a real source pixel.
        // Black padding at rectification borders cannot contribute a match.
        support[eye].assign(320*240,0);
        for(int y=3;y<237;++y)for(int x=3;x<317;++x){bool valid=true;
            for(int dy=-3;dy<=3&&valid;++dy)for(int dx=-3;dx<=3;++dx)
                if(maps[eye][std::size_t(y+dy)*320+x+dx]==UINT32_MAX){valid=false;break;}
            support[eye][std::size_t(y)*320+x]=valid;
        }}
    }
};
} // namespace room_native
