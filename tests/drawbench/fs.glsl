#version 450
layout(set=0,binding=0) uniform U { vec4 c; } u;
layout(location=0) out vec4 o;
void main(){ o=u.c*GAIN; }
