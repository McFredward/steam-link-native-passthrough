#version 450
layout(push_constant) uniform Scene { mat4 mvp; float time; float revealStart; } scene;
layout(location=0) in vec3 position;
layout(location=1) in vec4 color;
layout(location=2) in vec3 barycentric;
layout(location=3) in float firstSeen;
layout(location=4) in float style;
layout(location=0) out vec4 surfaceColor;
layout(location=1) out vec3 edge;
layout(location=2) out vec3 roomPosition;
layout(location=3) out float age;
layout(location=4) flat out float surfaceStyle;
void main(){
    gl_Position=scene.mvp*vec4(position,1);
    surfaceColor=color;edge=barycentric;roomPosition=position;
    age=scene.time-max(firstSeen,scene.revealStart);surfaceStyle=style;
}
