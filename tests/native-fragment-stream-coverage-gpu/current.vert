#version 450
// Owned offscreen fragment input. This does not replace the native vertex
// stage in the product and establishes no headset frame or eye association.
layout(push_constant) uniform FixtureInput { vec4 uv; } fixture;
layout(location=0) noperspective out vec4 innerInput;
layout(location=1) noperspective out vec4 outerInput;
layout(location=2) flat out uint flags;
void main() {
    vec2 corner=vec2((gl_VertexIndex<<1)&2,gl_VertexIndex&2);
    gl_Position=vec4(corner*2.0-1.0,0.0,1.0);
    outerInput=vec4(fixture.uv.xy,vec2(fixture.uv.y*2.0-1.0));
    innerInput=vec4(fixture.uv.zw,0.0,0.0);
    flags=0u;
}
