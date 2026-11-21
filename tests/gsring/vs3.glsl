#version 450
layout(location=0) flat out int id;
void main(){ id=gl_InstanceIndex; gl_Position=vec4(0,0,0,1); }
