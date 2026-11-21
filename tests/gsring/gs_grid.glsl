#version 450
layout(triangles) in;
layout(triangle_strip, max_vertices=4) out;
layout(location=0) flat in int id[];
layout(location=0) out vec4 o[7];
void put(float px, float py, vec4 col){ gl_Position=vec4(px,py,0,1); for(int k=0;k<7;k++) o[k]=col; EmitVertex(); }
void main(){
  int x=id[0]%64, y=(id[0]/64)%64;
  vec2 a=vec2(x,y)/32.0-1.0, b=vec2(x+1,y+1)/32.0-1.0;
  vec4 col=vec4((float(x)+0.5)/64.0,(float(y)+0.5)/64.0,0.25,1.0);
  put(a.x,a.y,col); put(b.x,a.y,col); put(a.x,b.y,col); put(b.x,b.y,col);
  EndPrimitive();
}
