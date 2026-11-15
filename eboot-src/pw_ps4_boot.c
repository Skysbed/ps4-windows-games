#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "pw_wine_start.h"
#include "pw_wine_dl.h"
int sceKernelLoadStartModule(const char*,size_t,const void*,uint32_t,const void*,int*);
int sceKernelGetModuleInfo(int32_t,void*);
int scePthreadCreate(void**,const void*,void*(*)(void*),void*,const char*);
int scePthreadDetach(void*);
void sceKernelUsleep(unsigned);
int sceKernelDlsym(int,const char*,void**);
static FILE* L;
static void log2(const char*m,int v){ if(L){fprintf(L,"%s=%d\n",m,v);fflush(L);} fprintf(stderr,"[pcps4] %s=%d\n",m,v); }
static int32_t op_load(const char*p,size_t ac,const void*av,uint32_t fl,const void*opt,int*res){ return sceKernelLoadStartModule(p,ac,av,fl,opt,res); }
static int op_modinfo(int32_t h,void*i){ return sceKernelGetModuleInfo(h,i); }
static int op_setenv(const char*n,const char*v){ return setenv(n,v,1); }
struct thunk{ void(*e)(void*); void*a; };
static void* tramp(void*a){ struct thunk*t=a; t->e(t->a); return 0; }
static int op_thread(void(*e)(void*),void*a,size_t st){ (void)st; void*th; struct thunk*t=malloc(sizeof*t); t->e=e;t->a=a; int r=scePthreadCreate(&th,0,tramp,t,"wine"); if(!r)scePthreadDetach(th); return r; }
/* The launcher (launcher.prx) shows the game library before Wine starts.
 * A copy in /data/pcps4/sce_module replaces the packaged one.
 * Returns 0 = a game was chosen, 1 = quit, anything else = start Wine with
 * the existing settings. */
static int run_launcher(void){
  static const PwWineDlOps dl_ops={op_load,op_modinfo,0,0};
  /* the loader turns "name.so" into "name.prx" (and would add .prx to a .prx name) */
  const char*path="/app0/sce_module/launcher.so";
  FILE*o=fopen("/data/pcps4/sce_module/launcher.prx","rb");
  if(o){ fclose(o); path="/data/pcps4/sce_module/launcher.so"; }
  pw_wine_dl_configure(&dl_ops,"/app0/sce_module");
  void*h=pw_wine_dl_open(path);
  if(!h){ fprintf(stderr,"[pcps4] launcher not loaded: %s\n",pw_wine_dl_error()?pw_wine_dl_error():"?"); return -1; }
  int(*run)(void)=(int(*)(void))pw_wine_dl_sym(h,"pcps4_launcher_run");
  if(!run) return -1;
  return run();
}
static int launcher_used;
/* End the application the way a console title does; after a game started
 * from the launcher, start the app again so the library comes back. */
static void end_app(int back_to_launcher){
  int res=0,h=sceKernelLoadStartModule("libSceSystemService.sprx",0,0,0,0,&res);
  int (*load_exec)(const char*,const char**)=0;
  if(h>=0) sceKernelDlsym(h,"sceSystemServiceLoadExec",(void**)&load_exec);
  fprintf(stderr,"[pcps4] stop requested: module=%d load_exec=%p relaunch=%d\n",h,(void*)load_exec,back_to_launcher);
  if(load_exec && back_to_launcher){ static const char*args[]={0}; load_exec("/app0/eboot.bin",args); sceKernelUsleep(3000000); }
  if(load_exec) load_exec("exit",0);
  sceKernelUsleep(3000000);
  _Exit(0);
}
int main(void){
  remove("/data/pcps4_stop");
  L=fopen("/data/pcps4_wine_boot.txt","w");
  { int lr=run_launcher(); log2("launcher",lr);
    if(lr==1){ if(L)fclose(L); end_app(0); }
    launcher_used=(lr==0); }
  static const char*av[]={"wine","/app0/test.exe"};
  PwWineStartOps ops={op_load,op_modinfo,op_setenv,op_thread};
  PwWineStartConfig cfg; memset(&cfg,0,sizeof cfg);
  cfg.ntdll_path="/app0/sce_module/ntdll.prx"; cfg.ntdll_dir="/app0/sce_module";
  { /* development overlay: an ntdll uploaded to /data replaces the packaged one */
    FILE*o=fopen("/data/pcps4/sce_module/ntdll.prx","rb");
    if(o){ fclose(o); cfg.ntdll_path="/data/pcps4/sce_module/ntdll.prx"; }
    log2(cfg.ntdll_path,0); }
  cfg.prefix="/data/wine"; cfg.argc=2; cfg.argv=av; cfg.stack_bytes=8u*1024*1024;
  PwWineStart st; memset(&st,0,sizeof st);
  int r=pw_wine_start_load(&st,&cfg,&ops); log2("wine_start_load",r); log2("stage",st.stage); log2("status",st.status);
  if(r==0){ r=pw_wine_start_environment(&st,&cfg,&ops); log2("environment",r);
            r=pw_wine_start_run(&st,&cfg,&ops); log2("run",r); }
  /* Wine runs on its own thread.  Returning from main() would start process
   * exit underneath a running program (this used to return after 10 minutes),
   * so stay here for as long as the process lives. */
  if(L){fclose(L);L=0;}
  /* Stop switch: when /data/pcps4_stop appears (Wine creates it when the
   * Windows program exits; it can also be uploaded by hand to end a stuck
   * session), end the application the way a console title does. */
  for(;;){
    FILE*s=fopen("/data/pcps4_stop","rb");
    if(s){
      fclose(s); remove("/data/pcps4_stop");
      end_app(launcher_used);
    }
    sceKernelUsleep(250000);
  }
  return 0;
}
