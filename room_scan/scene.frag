#version 450
layout(location=0) in vec4 surfaceColor;
layout(location=1) in vec3 edge;
layout(location=2) in vec3 roomPosition;
layout(location=3) in float age;
layout(location=4) flat in float surfaceStyle;
layout(location=0) out vec4 outputColor;
void shade(){
    if(surfaceStyle>5.5){
        // A continuous world grid replaces subpixel triangle edges. Fade its
        // lines before they become smaller than a pixel at distance/grazing.
        vec3 normal=abs(cross(dFdx(roomPosition),dFdy(roomPosition)));
        vec2 uv=normal.y>normal.x&&normal.y>normal.z?roomPosition.xz:
                normal.x>normal.z?roomPosition.yz:roomPosition.xy;
        vec2 cell=uv/.25,width=max(fwidth(cell),vec2(.00001));
        vec2 distance=abs(fract(cell-.5)-.5)/width;
        float grid=(1-smoothstep(.35,1.05,min(distance.x,distance.y)))
                   *(1-smoothstep(.10,.35,max(width.x,width.y)));
        float reveal=smoothstep(0,.5,age),settled=smoothstep(.2,1.2,age);
        bool reliable=surfaceStyle>6.5;
        vec3 fill=mix(vec3(.08,.87,.84),vec3(.19,.60,.78),settled);
        float front=1-smoothstep(.04,.22,abs(age-.30));
        float alpha=mix(reliable?.065:.035,reliable?.15:.11,grid)+front*.025;
        outputColor=vec4(mix(fill,vec3(.33,.94,.96),grid*.55+front*.2),alpha*reveal);return;
    }
    if(surfaceStyle>4.5){
        float weight=exp(-.5*dot(edge.xy,edge.xy));
        if(weight<.055)discard;
        float reveal=smoothstep(0,.55,age),settled=smoothstep(.3,1.4,age);
        vec3 target=edge.z>1.5?vec3(.62,.86,.87):vec3(.94,.67,.29);
        vec3 color=mix(vec3(.08,.96,.79),target,settled);
        float ring=1-smoothstep(.06,.14,abs(length(edge.xy)-1.8));
        float pulse=1-smoothstep(.04,.2,abs(age-.30-length(edge.xy)*.06));
        outputColor=vec4(color,surfaceColor.a*weight*reveal+ring*pulse*.25*reveal);return;
    }
    if(surfaceStyle>=3){outputColor=surfaceColor;return;}
    vec3 pixels=edge/max(fwidth(edge),vec3(.000001));
    float wire=1-smoothstep(.6,1.5,min(pixels.x,min(pixels.y,pixels.z)));
    bool uncertain=surfaceStyle>.5&&surfaceStyle<1.5;
    bool reliable=surfaceStyle>1.5;
    float delay=fract(roomPosition.x*1.7+roomPosition.y*.5+roomPosition.z*2.1)*.18;
    float reveal=smoothstep(0,.65,age-delay),settled=smoothstep(.25,1.2,age-delay);
    vec3 wireColor=mix(vec3(.20,.88,.80),vec3(.88,.92,.94),settled);
    vec3 restingFill=uncertain?vec3(.77,.75,.68):vec3(.70,.80,.83);
    vec3 fill=mix(vec3(.12,.84,.75),restingFill,settled);
    float alpha=mix(reliable?.26:uncertain?.16:.20,reliable?.72:uncertain?.42:.58,wire)*reveal;
    if(uncertain&&fract((roomPosition.x+roomPosition.y*.37+roomPosition.z*.71)*24)>.86)alpha*=.75;
    float front=1-smoothstep(.03,.12,abs(age-delay-.42));
    alpha=max(alpha,front*.35*reveal);
    fill=mix(fill,vec3(.2,.95,.84),front*.5);
    outputColor=vec4(mix(fill,wireColor,wire),alpha);
}

void main(){
    shade();
    // The nearest-surface pass writes premultiplied colour directly. Repeated
    // coplanar triangles cannot accumulate alpha into an opaque, noisy sheet.
    outputColor.rgb*=outputColor.a;
}
