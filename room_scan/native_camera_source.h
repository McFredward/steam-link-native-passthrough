#pragma once
#include "native_arcturus_source.h"
namespace room_native {
class RoomStereoCamera {
    StereoCamera public_;NativeArcturusSource native_;bool manual_=false;
public:
    VR_IVRSystem_FnTable* system=nullptr;VR_IVRTrackedCamera_FnTable* api=nullptr;
    Json binding,diagnostics;std::string status;
    bool start(const std::string& session,const std::string& origin){
        stop();native_.system=system;
        if(native_.start(session,origin)){manual_=true;binding=native_.binding;diagnostics=native_.diagnostics;status=native_.status;return true;}
        public_.system=system;public_.api=api;
        if(public_.start(session,origin)){binding=public_.binding;diagnostics=public_.diagnostics;status=public_.status;return true;}
        status=native_.status;diagnostics=native_.diagnostics;diagnostics["public_camera"]=public_.diagnostics;return false;
    }
    bool active()const{return manual_?native_.active():public_.handle!=0;}
    Json next(){return manual_?native_.next():public_.next();}
    void stop(){public_.stop();native_.stop();manual_=false;}
};
} // namespace room_native
