#include "../compat/pcps4_pe.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); ++failures; } } while (0)

static void put16(uint8_t *b, size_t o, uint16_t v)
{ b[o]= (uint8_t)v; b[o+1]=(uint8_t)(v>>8); }
static void put32(uint8_t *b, size_t o, uint32_t v)
{ b[o]=(uint8_t)v; b[o+1]=(uint8_t)(v>>8); b[o+2]=(uint8_t)(v>>16); b[o+3]=(uint8_t)(v>>24); }

enum { IMG_SIZE=0x700, PE_OFF=0x80, OPT_OFF=0xF8, OPT_SIZE=0xF0, SEC_OFF=0x1E8, SEC_RAW=0x200, SEC_VA=0x1000 };
static size_t roff(uint32_t rva) { return SEC_RAW + (size_t)(rva-SEC_VA); }

static void build_pe(uint8_t *b)
{
    memset(b,0,IMG_SIZE);
    b[0]='M'; b[1]='Z'; put32(b,0x3c,PE_OFF);
    b[PE_OFF]='P'; b[PE_OFF+1]='E';
    put16(b,PE_OFF+4,PCPS4_PE_MACHINE_AMD64); put16(b,PE_OFF+6,1); put16(b,PE_OFF+20,OPT_SIZE);
    put16(b,OPT_OFF,PCPS4_PE_MAGIC_PE32PLUS); put32(b,OPT_OFF+16,0);
    put32(b,OPT_OFF+56,0x3000); put32(b,OPT_OFF+60,0x200); put32(b,OPT_OFF+108,PCPS4_PE_NUM_DATA_DIRECTORIES);
    put32(b,OPT_OFF+112+8*PCPS4_PE_DIRECTORY_EXPORT,0x1000);
    put32(b,OPT_OFF+112+8*PCPS4_PE_DIRECTORY_EXPORT+4,0xA0);
    memcpy(b+SEC_OFF,".edata",6); put32(b,SEC_OFF+8,0x300); put32(b,SEC_OFF+12,SEC_VA);
    put32(b,SEC_OFF+16,0x300); put32(b,SEC_OFF+20,SEC_RAW);
    put32(b,SEC_OFF+36,PCPS4_PE_SCN_MEM_READ|PCPS4_PE_SCN_MEM_EXECUTE);

    /* Export directory at 0x1000. Base=10, functions Foo/Bar/ordinal-only. */
    put32(b,roff(0x1000)+16,10); put32(b,roff(0x1000)+20,3); put32(b,roff(0x1000)+24,2);
    put32(b,roff(0x1000)+28,0x1050); put32(b,roff(0x1000)+32,0x1060); put32(b,roff(0x1000)+36,0x1068);
    put32(b,roff(0x1050)+0,0x2000);
    put32(b,roff(0x1050)+4,0x1090); /* Bar is forwarded to OTHER.Baz. */
    put32(b,roff(0x1050)+8,0x2100);
    put32(b,roff(0x1060)+0,0x1080); put32(b,roff(0x1060)+4,0x1088);
    put16(b,roff(0x1068)+0,0); put16(b,roff(0x1068)+2,1);
    memcpy(b+roff(0x1080),"Foo",4); memcpy(b+roff(0x1088),"Bar",4);
    memcpy(b+roff(0x1090),"OTHER.Baz",10);
}

struct seen { int count; int named; int forwarded; uint32_t ord[3]; uint32_t rva[3]; char names[3][16]; char fwd[16]; };
static int on_export(void *user,const char *name,uint32_t ordinal,uint32_t rva,const char *forwarder)
{
    struct seen *s=(struct seen *)user; int i=s->count++;
    if (i>=3) return 1;
    s->ord[i]=ordinal; s->rva[i]=rva;
    if (name) { ++s->named; strncpy(s->names[i],name,sizeof(s->names[i])-1); }
    if (forwarder) { ++s->forwarded; strncpy(s->fwd,forwarder,sizeof(s->fwd)-1); }
    return 0;
}

int main(void)
{
    uint8_t b[IMG_SIZE]; pcps4_pe_image img; struct seen s={0};
    build_pe(b);
    CHECK(pcps4_pe_parse(b,sizeof(b),&img)==PCPS4_PE_OK);
    CHECK(pcps4_pe_iterate_exports(&img,on_export,&s)==PCPS4_PE_OK);
    CHECK(s.count==3); CHECK(s.named==2); CHECK(s.forwarded==1);
    CHECK(s.ord[0]==10 && s.ord[1]==11 && s.ord[2]==12);
    CHECK(s.rva[0]==0x2000 && s.rva[1]==0x1090 && s.rva[2]==0x2100);
    CHECK(strcmp(s.names[0],"Foo")==0); CHECK(strcmp(s.names[1],"Bar")==0);
    CHECK(strcmp(s.fwd,"OTHER.Baz")==0);
    if (failures) { printf("pe_export_host_test: %d check(s) failed\n",failures); return 1; }
    printf("pe_export_host_test: all checks passed\n"); return 0;
}
