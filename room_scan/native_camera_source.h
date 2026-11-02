#pragma once
#include "native_arcturus_source.h"
namespace room_native {
class RoomStereoCamera {
    StereoCamera public_;NativeArcturusSource native_;NativeStereo publicMatcher_;bool manual_=false;
public:
    VR_IVRSystem_FnTable* system=nullptr;VR_IVRTrackedCamera_FnTable* api=nullptr;
    Json binding,diagnostics;std::string status,visionLibrary;bool pcMode=false;
    bool start(const std::string& session,const std::string& origin){
        stop();native_.system=system;native_.visionLibrary=visionLibrary;native_.pcMode=pcMode;
        if(native_.start(session,origin)){manual_=true;binding=native_.binding;diagnostics=native_.diagnostics;status=native_.status;return true;}
        if(pcMode){status=native_.status;diagnostics=native_.diagnostics;return false;}
        public_.system=system;public_.api=api;
        try{publicMatcher_.open(visionLibrary);
            public_.stereoMatcher=[this](const auto& left,const auto& right,float focal,float baseline,const auto& support){return publicMatcher_.match(left,right,focal,baseline,support);};
            if(public_.start(session,origin)){binding=public_.binding;diagnostics=public_.diagnostics;status=public_.status;return true;}
        }catch(const std::exception&){public_.status="room-vision-unavailable";}
        status=native_.status;diagnostics=native_.diagnostics;diagnostics["public_camera"]=public_.diagnostics;return false;
    }
    bool active()const{return manual_?native_.active():public_.handle!=0;}
    Json next(){return manual_?native_.next():public_.next();}
    void park(){if(manual_)native_.park();}
    void stop(){public_.stop();native_.stop();publicMatcher_.close();manual_=false;}
};
} // namespace room_native
