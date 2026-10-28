#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "pw_wine_start.h"
int sceKernelLoadStartModule(const char*,size_t,const void*,uint32_t,const void*,int*);
int sceKernelGetModuleInfo(int32_t,void*);
int scePthreadCreate(void**,const void*,void*(*)(void*),void*,const char*);
int scePthreadDetach(void*);
void sceKernelUsleep(unsigned);
static FILE* L;
static void log2(const char*m,int v){ if(L){fprintf(L,"%s=%d\n",m,v);fflush(L);} fprintf(stderr,"[pcps4] %s=%d\n",m,v); }
static int32_t op_load(const char*p,size_t ac,const void*av,uint32_t fl,const void*opt,int*res){ return sceKernelLoadStartModule(p,ac,av,fl,opt,res); }
static int op_modinfo(int32_t h,void*i){ return sceKernelGetModuleInfo(h,i); }
static int op_setenv(const char*n,const char*v){ return setenv(n,v,1); }
struct thunk{ void(*e)(void*); void*a; };
static void* tramp(void*a){ struct thunk*t=a; t->e(t->a); return 0; }
static int op_thread(void(*e)(void*),void*a,size_t st){ (void)st; void*th; struct thunk*t=malloc(sizeof*t); t->e=e;t->a=a; int r=scePthreadCreate(&th,0,tramp,t,"wine"); if(!r)scePthreadDetach(th); return r; }
int main(void){
  L=fopen("/data/pcps4_wine_boot.txt","w");
  static const char*av[]={"wine","/app0/test.exe"};
  PwWineStartOps ops={op_load,op_modinfo,op_setenv,op_thread};
  PwWineStartConfig cfg; memset(&cfg,0,sizeof cfg);
  cfg.ntdll_path="/app0/sce_module/ntdll.prx"; cfg.ntdll_dir="/app0/sce_module";
  cfg.prefix="/data/wine"; cfg.argc=2; cfg.argv=av; cfg.stack_bytes=8u*1024*1024;
  PwWineStart st; memset(&st,0,sizeof st);
  int r=pw_wine_start_load(&st,&cfg,&ops); log2("wine_start_load",r); log2("stage",st.stage); log2("status",st.status);
  if(r==0){ r=pw_wine_start_environment(&st,&cfg,&ops); log2("environment",r);
            r=pw_wine_start_run(&st,&cfg,&ops); log2("run",r); }
  for(int i=0;i<600;i++) sceKernelUsleep(1000000);
  if(L)fclose(L); return 0;
}
