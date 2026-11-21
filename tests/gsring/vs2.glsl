#version 450
layout(location=0) out vec4 o[7];
void main(){ vec2 p[3]=vec2[](vec2(-1,-1),vec2(3,-1),vec2(-1,3)); gl_Position=vec4(p[gl_VertexIndex],0,1); for(int k=0;k<7;k++) o[k]=vec4(float(k)/7.0, float(gl_VertexIndex)/3.0, 0.25, 1.0); }
