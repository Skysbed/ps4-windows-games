#version 450
layout(location=0) in vec2 pos;
layout(push_constant) uniform P { vec4 v; } pc;
void main(){ gl_Position=vec4(pos*pc.v.x+vec2(pc.v.y,0.0)*(1.0-step(1.0,pc.v.x)),0,1); }
