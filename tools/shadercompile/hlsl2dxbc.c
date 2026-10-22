#include <windows.h>
#include <d3dcompiler.h>
#include <stdio.h>
#include <stdlib.h>
/* usage: hlsl2dxbc <in.hlsl> <entry> <profile> <out.dxbc> */
int main(int argc, char **argv){
    if(argc<5){fprintf(stderr,"usage: %s in.hlsl entry profile out.dxbc\n",argv[0]);return 2;}
    FILE*f=fopen(argv[1],"rb"); if(!f){fprintf(stderr,"open %s failed\n",argv[1]);return 2;}
    fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
    char*src=malloc(n+1); fread(src,1,n,f); src[n]=0; fclose(f);
    ID3DBlob*code=NULL,*err=NULL;
    HRESULT hr=D3DCompile(src,(SIZE_T)n,argv[1],NULL,NULL,argv[2],argv[3],0,0,&code,&err);
    if(err){fprintf(stderr,"%.*s\n",(int)err->lpVtbl->GetBufferSize(err),(char*)err->lpVtbl->GetBufferPointer(err));}
    if(FAILED(hr)){fprintf(stderr,"D3DCompile 0x%08lx\n",(unsigned long)hr);return 1;}
    SIZE_T sz=code->lpVtbl->GetBufferSize(code);
    FILE*o=fopen(argv[4],"wb"); fwrite(code->lpVtbl->GetBufferPointer(code),1,sz,o); fclose(o);
    fprintf(stderr,"OK wrote %lu bytes\n",(unsigned long)sz);
    return 0;
}
