// Metric intersection of rays with their measured capture-time camera origins.
#pragma once
#include "native_math.h"
#include <optional>
namespace room_native {
struct RayIntersection {V3 point;float separation=0;};
inline std::optional<RayIntersection> intersectStereoRays(V3 left,V3 a,V3 right,V3 b){
    a=normalized(a);b=normalized(b);const V3 offset=left-right;
    const float ab=dot(a,b),denominator=1-ab*ab;if(denominator<1e-6f)return {};
    const float da=dot(a,offset),db=dot(b,offset);
    const float ta=(ab*db-da)/denominator,tb=(db-ab*da)/denominator;
    if(ta<=0||tb<=0||ta>12||tb>12)return {};
    const V3 p=left+a*ta,q=right+b*tb;const float gap=length(p-q);
    if(!std::isfinite(gap)||gap>.006f)return {};
    return RayIntersection{(p+q)*.5f,gap};
}
} // namespace room_native
