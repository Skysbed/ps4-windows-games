#version 450
layout(triangles) in;
layout(triangle_strip, max_vertices=3) out;
layout(location=0) out vec4 o[7];
void main(){ vec2 p[3]=vec2[](vec2(-1,-1),vec2(3,-1),vec2(-1,3));
  for(int i=0;i<3;i++){ gl_Position=vec4(p[i],0,1); for(int k=0;k<7;k++) o[k]=vec4(float(k)/7.0,float(i)/3.0,0.25,1.0); EmitVertex(); }
  EndPrimitive(); }
