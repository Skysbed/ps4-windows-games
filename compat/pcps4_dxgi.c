#include "pcps4_dxgi.h"
#include "pcps4_d3d11_com.h"

#include <stdlib.h>
#include <string.h>

#if defined(__x86_64__) && (defined(__clang__) || defined(__GNUC__))
#define PCPS4_MS_ABI __attribute__((ms_abi))
#else
#define PCPS4_MS_ABI
#endif

typedef struct {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t data4[8];
} PCPS4Guid;

static const PCPS4Guid IID_IUnknown = {
    0x00000000u,0x0000u,0x0000u,{0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}
};
static const PCPS4Guid IID_IDXGIFactory = {
    0x7b7166ecu,0x21c7u,0x44aeu,{0xb2,0x1a,0xc9,0xae,0x32,0x1a,0xe3,0x69}
};
static const PCPS4Guid IID_IDXGIFactory1 = {
    0x770aae78u,0xf26fu,0x4dba,{0xa8,0x29,0x25,0x3c,0x83,0xd1,0xb3,0x87}
};
static const PCPS4Guid IID_IDXGIAdapter = {
    0x2411e7e1u,0x12acu,0x4ccfu,{0xbd,0x14,0x97,0x98,0xe8,0x53,0x4d,0xc0}
};
static const PCPS4Guid IID_IDXGIDevice = {
    0x54ec77fau,0x1377u,0x44e6u,{0x8c,0x32,0x88,0xfd,0x5f,0x44,0xc8,0x4c}
};

static const PCPS4Guid IID_IDXGIAdapter1 = {
    0x29038f61u,0x3839u,0x4626u,{0x91,0xfd,0x08,0x68,0x79,0x01,0x1a,0x05}
};

static int guid_eq(const void *p, const PCPS4Guid *g)
{
    return p != NULL && g != NULL && memcmp(p,g,sizeof(*g))==0;
}

typedef struct PCPS4DxgiAdapter PCPS4DxgiAdapter;
typedef struct PCPS4DxgiFactory PCPS4DxgiFactory;

struct PCPS4DxgiAdapter {
    const void **vtbl;
    uint32_t refs;
    PCPS4DxgiFactory *factory;
};

struct PCPS4DxgiFactory {
    const void **vtbl;
    uint32_t refs;
    HANDLE window;
    PCPS4DxgiAdapter *adapter;
};

#define FACTORY_SLOTS 14
#define ADAPTER_SLOTS 11

static const void *g_factory_vtbl[FACTORY_SLOTS];
static const void *g_adapter_vtbl[ADAPTER_SLOTS];
static int g_vtbl_ready;

static PCPS4_MS_ABI HRESULT factory_query(
    PCPS4DxgiFactory *s, const void *riid, void **out)
{
    if (!s || !out) return PCPS4_DXGI_ERROR_INVALID_CALL;
    *out=NULL;
    if (guid_eq(riid,&IID_IUnknown) ||
        guid_eq(riid,&IID_IDXGIFactory) ||
        guid_eq(riid,&IID_IDXGIFactory1)) {
        ++s->refs;
        *out=s;
        return S_OK;
    }
    return E_NOINTERFACE;
}
static PCPS4_MS_ABI UINT factory_addref(PCPS4DxgiFactory *s){return ++s->refs;}
static PCPS4_MS_ABI UINT adapter_release(PCPS4DxgiAdapter *s);

static PCPS4_MS_ABI UINT factory_release(PCPS4DxgiFactory *s)
{
    if (!s || s->refs==0) return 0;
    if (--s->refs) return s->refs;
    if (s->adapter) { PCPS4DxgiAdapter *a=s->adapter; s->adapter=NULL; a->factory=NULL; adapter_release(a); }
    free(s);
    return 0;
}
static PCPS4_MS_ABI HRESULT factory_private(PCPS4DxgiFactory *s,const void*a,UINT b,const void*c){(void)s;(void)a;(void)b;(void)c;return E_NOTIMPL;}
static PCPS4_MS_ABI HRESULT factory_private_i(PCPS4DxgiFactory *s,const void*a,const void*b){(void)s;(void)a;(void)b;return E_NOTIMPL;}
static PCPS4_MS_ABI HRESULT factory_get_private(PCPS4DxgiFactory *s,const void*a,UINT*b,void*c){(void)s;(void)a;(void)b;(void)c;return E_NOTIMPL;}
static PCPS4_MS_ABI HRESULT factory_get_parent(PCPS4DxgiFactory *s,const void*a,void**b){(void)a;if(b)*b=NULL;(void)s;return E_NOINTERFACE;}

static PCPS4_MS_ABI HRESULT adapter_query(PCPS4DxgiAdapter *s,const void *riid,void **out)
{
    if(!s||!out)return PCPS4_DXGI_ERROR_INVALID_CALL;
    *out=NULL;
    if(guid_eq(riid,&IID_IUnknown)||guid_eq(riid,&IID_IDXGIAdapter)||guid_eq(riid,&IID_IDXGIAdapter1)){
        ++s->refs;*out=s;return S_OK;
    }
    return E_NOINTERFACE;
}
static PCPS4_MS_ABI UINT adapter_addref(PCPS4DxgiAdapter*s){return ++s->refs;}
static PCPS4_MS_ABI HRESULT adapter_get_parent(PCPS4DxgiAdapter*s,const void*riid,void**out)
{
    if(!out)return PCPS4_DXGI_ERROR_INVALID_CALL;*out=NULL;
    if(!s||!s->factory)return E_FAIL;
    return factory_query(s->factory,riid,out);
}
static PCPS4_MS_ABI HRESULT adapter_enum_outputs(PCPS4DxgiAdapter*s,UINT index,void**output)
{
    (void)s;
    if(!output)return PCPS4_DXGI_ERROR_INVALID_CALL;
    *output=NULL;
    if(index!=0)return PCPS4_DXGI_ERROR_NOT_FOUND;
    return E_NOTIMPL;
}
static PCPS4_MS_ABI HRESULT adapter_get_desc(PCPS4DxgiAdapter*s,void*desc)
{
    (void)s;
    if(!desc)return PCPS4_DXGI_ERROR_INVALID_CALL;
    struct Desc {
        WCHAR Description[128];
        UINT VendorId,DeviceId,SubSysId,Revision;
        SIZE_T DedicatedVideoMemory,DedicatedSystemMemory,SharedSystemMemory;
        uint64_t Luid;
    } d;
    memset(&d,0,sizeof(d));
    const char *name="PcPs4 PS4 GPU";
    for(size_t i=0;i<127&&name[i];++i)d.Description[i]=(WCHAR)(unsigned char)name[i];
    d.VendorId=0x1002u;
    d.DedicatedVideoMemory=8ULL*1024ULL*1024ULL*1024ULL;
    d.SharedSystemMemory=8ULL*1024ULL*1024ULL*1024ULL;
    memcpy(desc,&d,sizeof(d));
    return S_OK;
}
static PCPS4_MS_ABI HRESULT adapter_check_interface(PCPS4DxgiAdapter*s,const void*riid,void*version)
{
    (void)s;(void)riid;
    if(!version)return PCPS4_DXGI_ERROR_INVALID_CALL;
    *(uint64_t*)version=1;
    return S_OK;
}
static PCPS4_MS_ABI HRESULT adapter_get_desc1(PCPS4DxgiAdapter*s,void*desc)
{
    if(!desc)return PCPS4_DXGI_ERROR_INVALID_CALL;
    struct Desc1 {
        WCHAR Description[128];
        UINT VendorId,DeviceId,SubSysId,Revision;
        SIZE_T DedicatedVideoMemory,DedicatedSystemMemory,SharedSystemMemory;
        uint64_t Luid;
        UINT Flags;
    } d;
    memset(&d,0,sizeof(d));
    const char *name="PcPs4 PS4 GPU";
    for(size_t i=0;i<127&&name[i];++i)d.Description[i]=(WCHAR)(unsigned char)name[i];
    d.VendorId=0x1002u;
    d.DedicatedVideoMemory=8ULL*1024ULL*1024ULL*1024ULL;
    d.SharedSystemMemory=8ULL*1024ULL*1024ULL*1024ULL;
    d.Flags=0;
    memcpy(desc,&d,sizeof(d));
    return S_OK;
}

static PCPS4_MS_ABI HRESULT factory_enum_adapters(PCPS4DxgiFactory*s,UINT index,void**out)
{
    if(!s||!out)return PCPS4_DXGI_ERROR_INVALID_CALL;
    *out=NULL;
    if(index!=0||!s->adapter)return PCPS4_DXGI_ERROR_NOT_FOUND;
    ++s->adapter->refs;*out=s->adapter;return S_OK;
}
static PCPS4_MS_ABI HRESULT factory_make_window_association(PCPS4DxgiFactory*s,HANDLE hwnd,UINT flags)
{(void)flags;if(!s)return PCPS4_DXGI_ERROR_INVALID_CALL;s->window=hwnd;return S_OK;}
static PCPS4_MS_ABI HRESULT factory_get_window_association(PCPS4DxgiFactory*s,HANDLE*hwnd)
{if(!s||!hwnd)return PCPS4_DXGI_ERROR_INVALID_CALL;*hwnd=s->window;return S_OK;}

static PCPS4_MS_ABI HRESULT factory_create_swap_chain(
    PCPS4DxgiFactory*s,void*device,const PCPS4_DXGI_SWAP_CHAIN_DESC*desc,void**swap)
{
    (void)s;
    if(!device||!desc||!swap)return PCPS4_DXGI_ERROR_INVALID_CALL;
    *swap=NULL;
    return pcps4_d3d11_com_create_swap_chain(device,desc,swap);
}
static PCPS4_MS_ABI HRESULT factory_create_software_adapter(PCPS4DxgiFactory*s,HANDLE module,void**adapter)
{(void)s;(void)module;if(adapter)*adapter=NULL;return PCPS4_DXGI_ERROR_UNSUPPORTED;}
static PCPS4_MS_ABI HRESULT factory_enum_adapters1(PCPS4DxgiFactory*s,UINT index,void**out)
{
    return factory_enum_adapters(s,index,out);
}
static PCPS4_MS_ABI BOOL factory_is_current(PCPS4DxgiFactory*s){(void)s;return TRUE;}


typedef struct PCPS4DxgiDevice {
    const void **vtbl;
    uint32_t refs;
    PCPS4DxgiFactory *factory;
} PCPS4DxgiDevice;

#define DEVICE_SLOTS 12
static const void *g_device_vtbl[DEVICE_SLOTS];

static PCPS4_MS_ABI UINT dxgi_device_addref(PCPS4DxgiDevice *s)
{
    return ++s->refs;
}
static PCPS4_MS_ABI UINT dxgi_device_release(PCPS4DxgiDevice *s)
{
    if (!s || s->refs == 0) return 0;
    if (--s->refs) return s->refs;
    if (s->factory) {
        PCPS4DxgiFactory *f=s->factory;
        s->factory=NULL;
        factory_release(f);
    }
    free(s);
    return 0;
}
static PCPS4_MS_ABI HRESULT dxgi_device_query(
    PCPS4DxgiDevice *s,const void *riid,void **out)
{
    if(!s||!out)return PCPS4_DXGI_ERROR_INVALID_CALL;
    *out=NULL;
    if(guid_eq(riid,&IID_IUnknown)||guid_eq(riid,&IID_IDXGIDevice)){
        ++s->refs;*out=s;return S_OK;
    }
    return E_NOINTERFACE;
}
static PCPS4_MS_ABI HRESULT dxgi_device_private(PCPS4DxgiDevice*s,const void*a,UINT b,const void*c)
{(void)s;(void)a;(void)b;(void)c;return E_NOTIMPL;}
static PCPS4_MS_ABI HRESULT dxgi_device_private_i(PCPS4DxgiDevice*s,const void*a,const void*b)
{(void)s;(void)a;(void)b;return E_NOTIMPL;}
static PCPS4_MS_ABI HRESULT dxgi_device_get_private(PCPS4DxgiDevice*s,const void*a,UINT*b,void*c)
{(void)s;(void)a;(void)b;(void)c;return E_NOTIMPL;}
static PCPS4_MS_ABI HRESULT dxgi_device_get_parent(PCPS4DxgiDevice*s,const void*riid,void**out)
{
    if(!s||!out)return PCPS4_DXGI_ERROR_INVALID_CALL;
    *out=NULL;
    return s->factory ? factory_query(s->factory,riid,out) : E_NOINTERFACE;
}
static PCPS4_MS_ABI HRESULT dxgi_device_get_adapter(
    PCPS4DxgiDevice*s,void**out)
{
    if(!s||!out)return PCPS4_DXGI_ERROR_INVALID_CALL;
    *out=NULL;
    if(!s->factory||!s->factory->adapter)return E_FAIL;
    ++s->factory->adapter->refs;
    *out=s->factory->adapter;
    return S_OK;
}
static PCPS4_MS_ABI HRESULT dxgi_device_create_surface(
    PCPS4DxgiDevice*s,const void*desc,UINT count,UINT usage,const void*shared,void**surface)
{
    (void)s;(void)desc;(void)count;(void)usage;(void)shared;
    if(surface)*surface=NULL;
    return PCPS4_DXGI_ERROR_UNSUPPORTED;
}
static PCPS4_MS_ABI HRESULT dxgi_device_query_residency(
    PCPS4DxgiDevice*s,const void*resources,void*residency,UINT count)
{
    (void)s;(void)resources;
    if(!residency)return PCPS4_DXGI_ERROR_INVALID_CALL;
    memset(residency,1,count*4u);
    return S_OK;
}
static PCPS4_MS_ABI HRESULT dxgi_device_set_priority(PCPS4DxgiDevice*s,int priority)
{(void)s;(void)priority;return S_OK;}
static PCPS4_MS_ABI HRESULT dxgi_device_get_priority(PCPS4DxgiDevice*s,int*priority)
{(void)s;if(!priority)return PCPS4_DXGI_ERROR_INVALID_CALL;*priority=0;return S_OK;}

PCPS4_MS_ABI HRESULT pcps4_dxgi_create_device(const void *riid, void **out)
{
    if(!out)return PCPS4_DXGI_ERROR_INVALID_CALL;
    *out=NULL;
    if(!guid_eq(riid,&IID_IUnknown)&&!guid_eq(riid,&IID_IDXGIDevice))
        return E_NOINTERFACE;
    init_vtbls();
    for(int i=0;i<DEVICE_SLOTS;++i)g_device_vtbl[i]=(const void*)dxgi_device_get_private;
    g_device_vtbl[0]=(const void*)dxgi_device_query;
    g_device_vtbl[1]=(const void*)dxgi_device_addref;
    g_device_vtbl[2]=(const void*)dxgi_device_release;
    g_device_vtbl[3]=(const void*)dxgi_device_private;
    g_device_vtbl[4]=(const void*)dxgi_device_private_i;
    g_device_vtbl[5]=(const void*)dxgi_device_get_private;
    g_device_vtbl[6]=(const void*)dxgi_device_get_parent;
    g_device_vtbl[7]=(const void*)dxgi_device_get_adapter;
    g_device_vtbl[8]=(const void*)dxgi_device_create_surface;
    g_device_vtbl[9]=(const void*)dxgi_device_query_residency;
    g_device_vtbl[10]=(const void*)dxgi_device_set_priority;
    g_device_vtbl[11]=(const void*)dxgi_device_get_priority;

    /*
     * A standalone DXGI device gets the same one-adapter factory. This keeps
     * the object graph coherent for enumeration and adapter queries.
     */
    void *factory_ptr=NULL;
    HRESULT hr=create_factory(&IID_IDXGIFactory1,&factory_ptr);
    if(hr!=S_OK)return hr;
    PCPS4DxgiDevice*d=calloc(1,sizeof(*d));
    if(!d){factory_release((PCPS4DxgiFactory*)factory_ptr);return E_OUTOFMEMORY;}
    d->vtbl=g_device_vtbl;d->refs=1;d->factory=(PCPS4DxgiFactory*)factory_ptr;
    *out=d;
    return S_OK;
}

static void init_vtbls(void)
{
    if(g_vtbl_ready)return;
    g_vtbl_ready=1;
    for(int i=0;i<FACTORY_SLOTS;++i)g_factory_vtbl[i]=(const void *)factory_get_private;
    for(int i=0;i<ADAPTER_SLOTS;++i)g_adapter_vtbl[i]=(const void *)factory_get_private;

    g_factory_vtbl[0]=(const void*)factory_query;
    g_factory_vtbl[1]=(const void*)factory_addref;
    g_factory_vtbl[2]=(const void*)factory_release;
    g_factory_vtbl[3]=(const void*)factory_private;
    g_factory_vtbl[4]=(const void*)factory_private_i;
    g_factory_vtbl[5]=(const void*)factory_get_private;
    g_factory_vtbl[6]=(const void*)factory_get_parent;
    g_factory_vtbl[7]=(const void*)factory_enum_adapters;
    g_factory_vtbl[8]=(const void*)factory_make_window_association;
    g_factory_vtbl[9]=(const void*)factory_get_window_association;
    g_factory_vtbl[10]=(const void*)factory_create_swap_chain;
    g_factory_vtbl[11]=(const void*)factory_create_software_adapter;
    g_factory_vtbl[12]=(const void*)factory_enum_adapters1;
    g_factory_vtbl[13]=(const void*)factory_is_current;

    g_adapter_vtbl[0]=(const void*)adapter_query;
    g_adapter_vtbl[1]=(const void*)adapter_addref;
    g_adapter_vtbl[2]=(const void*)adapter_release;
    g_adapter_vtbl[3]=(const void*)factory_private;
    g_adapter_vtbl[4]=(const void*)factory_private_i;
    g_adapter_vtbl[5]=(const void*)factory_get_private;
    g_adapter_vtbl[6]=(const void*)adapter_get_parent;
    g_adapter_vtbl[7]=(const void*)adapter_enum_outputs;
    g_adapter_vtbl[8]=(const void*)adapter_get_desc;
    g_adapter_vtbl[9]=(const void*)adapter_check_interface;
    g_adapter_vtbl[10]=(const void*)adapter_get_desc1;
}
static PCPS4_MS_ABI HRESULT create_factory(const void*riid,void**out)
{
    if(!out)return PCPS4_DXGI_ERROR_INVALID_CALL;
    *out=NULL;
    if(!(guid_eq(riid,&IID_IUnknown)||guid_eq(riid,&IID_IDXGIFactory)||guid_eq(riid,&IID_IDXGIFactory1)))
        return E_NOINTERFACE;
    init_vtbls();
    PCPS4DxgiFactory*f=calloc(1,sizeof(*f));
    PCPS4DxgiAdapter*a=calloc(1,sizeof(*a));
    if(!f||!a){free(f);free(a);return E_OUTOFMEMORY;}
    f->vtbl=g_factory_vtbl;f->refs=1;f->adapter=a;
    a->vtbl=g_adapter_vtbl;a->refs=1;a->factory=f;
    ++f->refs; /* adapter owns one factory ref */
    *out=f;
    return S_OK;
}
static PCPS4_MS_ABI UINT adapter_release(PCPS4DxgiAdapter*s)
{
    if(!s||s->refs==0)return 0;
    if(--s->refs)return s->refs;
    if(s->factory){PCPS4DxgiFactory*f=s->factory;s->factory=NULL;factory_release(f);}
    free(s);return 0;
}
PCPS4_MS_ABI HRESULT pcps4_abi_CreateDXGIFactory(const void*riid,void**factory){return create_factory(riid,factory);}
PCPS4_MS_ABI HRESULT pcps4_abi_CreateDXGIFactory1(const void*riid,void**factory){return create_factory(riid,factory);}
