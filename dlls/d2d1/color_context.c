/* Direct2D color contexts.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */
#include "d2d1_private.h"
#include "wincodec.h"
#include "icm.h"

struct d2d_color_context
{
    ID2D1ColorContext ID2D1ColorContext_iface;
    LONG refcount;
    ID2D1Factory *factory;
    D2D1_COLOR_SPACE space;
    BYTE *profile;
    UINT32 size;
};

static struct d2d_color_context *impl_from_color(ID2D1ColorContext *iface)
{
    return CONTAINING_RECORD(iface, struct d2d_color_context, ID2D1ColorContext_iface);
}
static HRESULT STDMETHODCALLTYPE color_QueryInterface(ID2D1ColorContext *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid,&IID_IUnknown) || IsEqualGUID(iid,&IID_ID2D1Resource) || IsEqualGUID(iid,&IID_ID2D1ColorContext))
    { *out=iface; ID2D1ColorContext_AddRef(iface); return S_OK; }
    *out=NULL; return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE color_AddRef(ID2D1ColorContext *iface)
{
    return InterlockedIncrement(&impl_from_color(iface)->refcount);
}
static ULONG STDMETHODCALLTYPE color_Release(ID2D1ColorContext *iface)
{
    struct d2d_color_context *context=impl_from_color(iface);
    ULONG ref=InterlockedDecrement(&context->refcount);
    if (!ref) { ID2D1Factory_Release(context->factory); free(context->profile); free(context); }
    return ref;
}
static void STDMETHODCALLTYPE color_GetFactory(ID2D1ColorContext *iface, ID2D1Factory **factory)
{
    ID2D1Factory_AddRef(*factory=impl_from_color(iface)->factory);
}
static D2D1_COLOR_SPACE STDMETHODCALLTYPE color_GetColorSpace(ID2D1ColorContext *iface)
{
    return impl_from_color(iface)->space;
}
static UINT32 STDMETHODCALLTYPE color_GetProfileSize(ID2D1ColorContext *iface)
{
    return impl_from_color(iface)->size;
}
static HRESULT STDMETHODCALLTYPE color_GetProfile(ID2D1ColorContext *iface, BYTE *profile, UINT32 size)
{
    struct d2d_color_context *context=impl_from_color(iface);
    if (size<context->size) return E_NOT_SUFFICIENT_BUFFER;
    if (context->size && !profile) return E_INVALIDARG;
    if (context->size) memcpy(profile,context->profile,context->size);
    return S_OK;
}
static const ID2D1ColorContextVtbl color_vtbl =
{
    color_QueryInterface,color_AddRef,color_Release,color_GetFactory,
    color_GetColorSpace,color_GetProfileSize,color_GetProfile,
};

HRESULT d2d_color_context_create(ID2D1Factory *factory, D2D1_COLOR_SPACE space,
        const BYTE *profile, UINT32 size, ID2D1ColorContext **result)
{
    struct d2d_color_context *context;
    if (!result) return E_INVALIDARG;
    *result=NULL;
    if (space>D2D1_COLOR_SPACE_SCRGB || (space==D2D1_COLOR_SPACE_CUSTOM && (!profile || size<128))) return E_INVALIDARG;
    if (space==D2D1_COLOR_SPACE_CUSTOM && memcmp(profile+36,"acsp",4)) return E_INVALIDARG;
    if (!(context=calloc(1,sizeof(*context)))) return E_OUTOFMEMORY;
    if (space==D2D1_COLOR_SPACE_CUSTOM)
    {
        if (!(context->profile=malloc(size))) { free(context); return E_OUTOFMEMORY; }
        memcpy(context->profile,profile,size); context->size=size;
    }
    context->ID2D1ColorContext_iface.lpVtbl=&color_vtbl;
    context->refcount=1; context->space=space;
    ID2D1Factory_AddRef(context->factory=factory);
    *result=&context->ID2D1ColorContext_iface;
    return S_OK;
}

HRESULT d2d_color_context_from_wic(ID2D1Factory *factory, IWICColorContext *wic, ID2D1ColorContext **result)
{
    WICColorContextType type;
    UINT size, actual, space;
    BYTE *profile;
    HRESULT hr;
    if(!wic||!result)return E_INVALIDARG;
    *result=NULL;
    if(FAILED(hr=IWICColorContext_GetType(wic,&type)))return hr;
    if(type==WICColorContextExifColorSpace)
    {
        if(FAILED(hr=IWICColorContext_GetExifColorSpace(wic,&space)))return hr;
        if(space!=1)return E_NOTIMPL;
        return d2d_color_context_create(factory,D2D1_COLOR_SPACE_SRGB,NULL,0,result);
    }
    if(type!=WICColorContextProfile)return E_INVALIDARG;
    if(FAILED(hr=IWICColorContext_GetProfileBytes(wic,0,NULL,&size)))return hr;
    if(!(profile=malloc(size)))return E_OUTOFMEMORY;
    hr=IWICColorContext_GetProfileBytes(wic,size,profile,&actual);
    if(SUCCEEDED(hr))hr=d2d_color_context_create(factory,D2D1_COLOR_SPACE_CUSTOM,profile,actual,result);
    free(profile);return hr;
}

HRESULT d2d_color_context_from_filename(ID2D1Factory *factory, const WCHAR *filename, ID2D1ColorContext **result)
{
    IWICImagingFactory *wic;
    IWICColorContext *color;
    HRESULT hr;
    if(!filename||!result)return E_INVALIDARG;
    *result=NULL;
    if(FAILED(hr=CoCreateInstance(&CLSID_WICImagingFactory,NULL,CLSCTX_INPROC_SERVER,
            &IID_IWICImagingFactory,(void **)&wic)))return hr;
    hr=IWICImagingFactory_CreateColorContext(wic,&color);
    if(SUCCEEDED(hr))
    {
        hr=IWICColorContext_InitializeFromFilename(color,filename);
        if(SUCCEEDED(hr))hr=d2d_color_context_from_wic(factory,color,result);
        IWICColorContext_Release(color);
    }
    IWICImagingFactory_Release(wic);return hr;
}

static HRESULT open_color_profile(ID2D1ColorContext *context, HPROFILE *profile)
{
    PROFILE desc;
    WCHAR filename[MAX_PATH];
    BYTE *data=NULL;
    DWORD size=sizeof(filename);
    HRESULT hr=S_OK;
    D2D1_COLOR_SPACE space=context?ID2D1ColorContext_GetColorSpace(context):D2D1_COLOR_SPACE_SRGB;
    *profile=NULL;
    if(space==D2D1_COLOR_SPACE_CUSTOM)
    {
        size=ID2D1ColorContext_GetProfileSize(context);
        if(!(data=malloc(size)))return E_OUTOFMEMORY;
        hr=ID2D1ColorContext_GetProfile(context,data,size);
        desc.dwType=PROFILE_MEMBUFFER;desc.pProfileData=data;desc.cbDataSize=size;
    }
    else
    {
        if(space!=D2D1_COLOR_SPACE_SRGB)return E_NOTIMPL;
        if(!GetStandardColorSpaceProfileW(NULL,LCS_sRGB,filename,&size))return HRESULT_FROM_WIN32(GetLastError());
        desc.dwType=PROFILE_FILENAME;desc.pProfileData=filename;desc.cbDataSize=(wcslen(filename)+1)*sizeof(WCHAR);
    }
    if(SUCCEEDED(hr))
    {
        *profile=OpenColorProfileW(&desc,PROFILE_READ,FILE_SHARE_READ,OPEN_EXISTING);
        if(!*profile)hr=HRESULT_FROM_WIN32(GetLastError());
    }
    free(data);return hr;
}

HRESULT d2d_color_transform_lut(struct d2d_device_context *context, ID2D1ColorContext *source,
        ID2D1ColorContext *destination, UINT32 source_intent, UINT32 destination_intent, ID2D1LookupTable3D **result)
{
    const UINT32 extent=33,count=33*33*33;
    UINT32 extents[3]={33,33,33},strides[2]={33*16,33*33*16};
    DWORD intents[2]={source_intent,destination_intent};
    HPROFILE profiles[2]={NULL,NULL};
    HTRANSFORM transform=NULL;
    COLOR *input=NULL,*output=NULL;
    float *data=NULL;
    unsigned int r,g,b,i;
    HRESULT hr;
    *result=NULL;
    if(source_intent>3||destination_intent>3)return E_INVALIDARG;
    if(FAILED(hr=open_color_profile(source,&profiles[0])))goto done;
    if(FAILED(hr=open_color_profile(destination,&profiles[1])))goto done;
    transform=CreateMultiProfileTransform(profiles,2,intents,2,BEST_MODE,0);
    if(!transform){hr=HRESULT_FROM_WIN32(GetLastError());goto done;}
    if(!(input=calloc(count,sizeof(*input)))||!(output=calloc(count,sizeof(*output)))||!(data=malloc(count*16)))
    {hr=E_OUTOFMEMORY;goto done;}
    for(r=0;r<extent;++r)for(g=0;g<extent;++g)for(b=0;b<extent;++b)
    {
        i=(r*extent+g)*extent+b;
        input[i].rgb.red=roundf(r*65535.0f/(extent-1));
        input[i].rgb.green=roundf(g*65535.0f/(extent-1));
        input[i].rgb.blue=roundf(b*65535.0f/(extent-1));
    }
    if(!TranslateColors(transform,input,count,COLOR_RGB,output,COLOR_RGB))
    {hr=HRESULT_FROM_WIN32(GetLastError());goto done;}
    for(i=0;i<count;++i)
    {
        data[i*4]=output[i].rgb.red/65535.0f;data[i*4+1]=output[i].rgb.green/65535.0f;
        data[i*4+2]=output[i].rgb.blue/65535.0f;data[i*4+3]=1;
    }
    hr=d2d_lookup_table_create(context,D2D1_BUFFER_PRECISION_32BPC_FLOAT,extents,(BYTE *)data,count*16,strides,result);
done:
    free(input);free(output);free(data);
    if(transform)DeleteColorTransform(transform);
    if(profiles[0])CloseColorProfile(profiles[0]);
    if(profiles[1])CloseColorProfile(profiles[1]);
    return hr;
}
