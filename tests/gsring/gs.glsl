#version 450
layout(triangles, invocations=INV) in;
layout(triangle_strip, max_vertices=MAXV) out;
layout(location=0) out vec4 o[7];
void main(){
  for(int i=0;i<3;i++){
    gl_Position=gl_in[i].gl_Position;
    for(int k=0;k<7;k++) o[k]=vec4(float(k)/7.0, float(i)/3.0, BLUE, 1.0) + gl_in[i].gl_Position.xyxy*0.0001*float(k+1);
    EmitVertex();
  }
  EndPrimitive();
}
