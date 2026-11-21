#version 450
layout(location=0) in vec4 o[7]; layout(location=0) out vec4 c;
void main(){ vec4 s=vec4(0); for(int k=0;k<7;k++) s+=o[k]; c=s/7.0; }
