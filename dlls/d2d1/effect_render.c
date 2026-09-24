/* Builtin image effect rendering.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include "d2d1_private.h"
#include "d3dcompiler.h"
#include "wincodec.h"
#include <float.h>

WINE_DEFAULT_DEBUG_CHANNEL(d2d);

static struct d2d_lookup_table *lookup_impl(ID2D1LookupTable3D *iface)
{
    return CONTAINING_RECORD(iface, struct d2d_lookup_table, ID2D1LookupTable3D_iface);
}
static HRESULT STDMETHODCALLTYPE lookup_QueryInterface(ID2D1LookupTable3D *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_ID2D1Resource) || IsEqualGUID(iid, &IID_ID2D1LookupTable3D))
    { *out = iface; ID2D1LookupTable3D_AddRef(iface); return S_OK; }
    *out = NULL; return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE lookup_AddRef(ID2D1LookupTable3D *iface)
{
    return InterlockedIncrement(&lookup_impl(iface)->refcount);
}
static ULONG STDMETHODCALLTYPE lookup_Release(ID2D1LookupTable3D *iface)
{
    struct d2d_lookup_table *lut = lookup_impl(iface);
    ULONG ref = InterlockedDecrement(&lut->refcount);
    if (!ref) { ID3D11ShaderResourceView_Release(lut->view); ID2D1Factory_Release(lut->factory); free(lut); }
    return ref;
}
static void STDMETHODCALLTYPE lookup_GetFactory(ID2D1LookupTable3D *iface, ID2D1Factory **factory)
{
    ID2D1Factory_AddRef(*factory = lookup_impl(iface)->factory);
}
static const ID2D1LookupTable3DVtbl lookup_vtbl =
{
    lookup_QueryInterface, lookup_AddRef, lookup_Release, lookup_GetFactory,
};
struct d2d_lookup_table *d2d_lookup_table_from_iface(ID2D1LookupTable3D *iface)
{
    return iface && iface->lpVtbl == &lookup_vtbl ? lookup_impl(iface) : NULL;
}
HRESULT d2d_lookup_table_create(struct d2d_device_context *context, D2D1_BUFFER_PRECISION precision,
        const UINT32 *extents, const BYTE *data, UINT32 size, const UINT32 *strides, ID2D1LookupTable3D **result)
{
    struct d2d_lookup_table *lut;
    D3D11_TEXTURE3D_DESC desc = {0};
    D3D11_SUBRESOURCE_DATA initial;
    ID3D11Texture3D *texture;
    UINT32 bytes;
    UINT64 required;
    HRESULT hr;
    if (!result) return E_INVALIDARG;
    *result = NULL;
    if (!extents || !data || !strides) return E_INVALIDARG;
    if (precision == D2D1_BUFFER_PRECISION_8BPC_UNORM) { desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM; bytes=4; }
    else if (precision == D2D1_BUFFER_PRECISION_16BPC_UNORM) { desc.Format=DXGI_FORMAT_R16G16B16A16_UNORM; bytes=8; }
    else if (precision == D2D1_BUFFER_PRECISION_16BPC_FLOAT) { desc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT; bytes=8; }
    else if (precision == D2D1_BUFFER_PRECISION_32BPC_FLOAT) { desc.Format=DXGI_FORMAT_R32G32B32A32_FLOAT; bytes=16; }
    else return E_INVALIDARG;
    if (extents[0]<2 || extents[1]<2 || extents[2]<2 || extents[0]>256 || extents[1]>256 || extents[2]>256)
        return E_INVALIDARG;
    if (strides[0] < extents[0]*bytes || (UINT64)strides[1] < (UINT64)strides[0]*extents[1]) return E_INVALIDARG;
    required=(UINT64)(extents[2]-1)*strides[1]+(UINT64)(extents[1]-1)*strides[0]+extents[0]*bytes;
    if (required>size) return E_INVALIDARG;
    if (!(lut=calloc(1,sizeof(*lut)))) return E_OUTOFMEMORY;
    desc.Width=extents[0]; desc.Height=extents[1]; desc.Depth=extents[2]; desc.MipLevels=1;
    desc.Usage=D3D11_USAGE_IMMUTABLE; desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    initial.pSysMem=data; initial.SysMemPitch=strides[0]; initial.SysMemSlicePitch=strides[1];
    if (FAILED(hr=ID3D11Device1_CreateTexture3D(context->d3d_device,&desc,&initial,&texture))) { free(lut); return hr; }
    hr=ID3D11Device1_CreateShaderResourceView(context->d3d_device,(ID3D11Resource *)texture,NULL,&lut->view);
    ID3D11Texture3D_Release(texture);
    if (FAILED(hr)) { free(lut); return hr; }
    lut->ID2D1LookupTable3D_iface.lpVtbl=&lookup_vtbl; lut->refcount=1;
    ID2D1Factory_AddRef(lut->factory=context->factory);
    memcpy(lut->extents,extents,sizeof(lut->extents));
    *result=&lut->ID2D1LookupTable3D_iface;
    return S_OK;
}

struct d2d_effect_renderer
{
    ID3D11VertexShader *vs;
    ID3D11PixelShader *ps;
    ID3D11ComputeShader *histogram;
    ID3D11PixelShader *blur_ps;
    ID3D11Buffer *blur_constants;
    ID3D11Buffer *constants;
    ID3D11SamplerState *samplers[4];
};

struct blur_constants
{
    float output[4], input[4], direction[4], colour[4];
    UINT count, shadow, ignore_alpha, hard_border;
    float taps[1024][4];
};

static const char blur_ps_code[] =
    "cbuffer constants {float4 output_rect,input_rect,direction,colour;"
    "uint count,shadow,ignore_alpha,hard_border;float4 taps[1024];};"
    "Texture2D source:register(t0);SamplerState linear_sampler:register(s0);"
    "float4 read_image(float2 p){float2 uv=(p-input_rect.xy)/input_rect.zw;"
    "float4 c=source.SampleLevel(linear_sampler,uv,0);"
    "if(ignore_alpha){uint w,h;source.GetDimensions(w,h);float2 size=float2(w,h);"
    "float2 a=saturate(min(uv*size+.5,size+.5-uv*size));c.a=hard_border?1:a.x*a.y;}return c;}"
    "float4 main(float4 position:SV_Position):SV_Target{"
    "float2 p=output_rect.xy+position.xy*output_rect.zw;"
    "float4 sum=read_image(p)*direction.z;"
    "for(uint i=0;i<count;++i){float2 offset=direction.xy*taps[i].x;"
    "sum+=(read_image(p+offset)+read_image(p-offset))*taps[i].y;}"
    "return shadow?float4(colour.rgb*colour.a,colour.a)*sum.a:sum;}";

struct d2d_effect_bounds_node
{
    ID2D1Image *image;
    D2D1_RECT_F bounds;
    HRESULT status;
    BOOL visiting;
};

struct d2d_effect_bounds_evaluation
{
    struct d2d_effect_bounds_node *nodes;
    size_t count, capacity;
};

struct d2d_effect_image_node
{
    ID2D1Image *image;
    struct d2d_bitmap *bitmap;
    D2D1_RECT_F requested, bounds;
};

struct d2d_effect_image_evaluation
{
    struct d2d_effect_image_node *nodes;
    size_t count, capacity, allocated;
};

struct effect_constants
{
    float output[4], input[4], second[4];
    float row_x[4], row_y[4], blur[4], colour[4];
    float matrix[5][4];
    float clip[4];
    UINT op, composite, ignore_alpha, clamp_output;
    UINT second_ignore_alpha, straight_alpha, interpolation, hard_border;
    float texel[4];
    float tables[256][4];
    UINT table_sizes[4];
    UINT table_disabled[4];
    UINT table_offsets[4];
    float noise_x[256][4], noise_y[256][4];
    ID3D11ShaderResourceView *lut_view;
    ID3D11ShaderResourceView *weights_view;
};

static const char effect_vs[] =
    "float4 main(uint id : SV_VertexID) : SV_Position {"
    "float2 p = float2((id << 1) & 2, id & 2);"
    "return float4(p * float2(2,-2) + float2(-1,1),0,1); }";

static const char histogram_cs[] =
    "cbuffer params { uint width,height,bins,channel; uint ignore_alpha; };"
    "Texture2D<float4> source : register(t0); RWBuffer<uint> counts : register(u0);"
    "[numthreads(16,16,1)] void main(uint3 id:SV_DispatchThreadID) {"
    "if(id.x>=width || id.y>=height)return; float4 c=source.Load(int3(id.xy,0));"
    "if(ignore_alpha)c.a=1; if(channel<3 && c.a>0)c.rgb/=c.a;"
    "float v=saturate(c[channel]); uint bin=min((uint)max(ceil(v*bins)-1,0),bins-1);"
    "InterlockedAdd(counts[bin],1); }";

static const char effect_ps[] =
    "cbuffer constants { float4 output_rect, input_rect, second_rect;"
    "float4 row_x, row_y, blur, colour; float4 colour_matrix[5];"
    "float4 clip_rect;"
    "uint op, composite, ignore_alpha, clamp_output;"
    "uint second_ignore_alpha, straight_alpha, interpolation, hard_border; float4 texel;"
    "float4 tables[256]; uint4 table_sizes; uint4 table_disabled; uint4 table_offsets; float4 noise_x[256],noise_y[256]; };"
    "Texture2D source : register(t0); Texture2D other : register(t1);"
    "Texture3D lookup_table : register(t2);"
    "Buffer<float> weights : register(t3);"
    "SamplerState sampler0 : register(s0);"
    "float4 perlin(float2 p,float2 period) { float2 cell=floor(p),f=frac(p); int2 a=(int2)cell,b=a+1;"
    "if(period.x>0) {a.x=(int)(cell.x-floor(cell.x/period.x)*period.x); b.x=(a.x+1)%(int)period.x;}"
    "if(period.y>0) {a.y=(int)(cell.y-floor(cell.y/period.y)*period.y); b.y=(a.y+1)%(int)period.y;}"
    "uint ax=(uint)tables[a.x&255].x,bx=(uint)tables[b.x&255].x;"
    "uint i00=(uint)tables[(ax+a.y)&255].x,i10=(uint)tables[(bx+a.y)&255].x;"
    "uint i01=(uint)tables[(ax+b.y)&255].x,i11=(uint)tables[(bx+b.y)&255].x;"
    "float4 n00=noise_x[i00]*f.x+noise_y[i00]*f.y,n10=noise_x[i10]*(f.x-1)+noise_y[i10]*f.y;"
    "float4 n01=noise_x[i01]*f.x+noise_y[i01]*(f.y-1),n11=noise_x[i11]*(f.x-1)+noise_y[i11]*(f.y-1);"
    "float2 u=f*f*(3-2*f);return lerp(lerp(n00,n10,u.x),lerp(n01,n11,u.x),u.y);}"
    "float lum(float3 c) { return dot(c,float3(.3,.59,.11)); }"
    "float sat(float3 c) { return max(c.r,max(c.g,c.b))-min(c.r,min(c.g,c.b)); }"
    "float3 setlum(float3 c,float l) { c+=l-lum(c); float n=min(c.r,min(c.g,c.b)),x=max(c.r,max(c.g,c.b));"
    "if(n<0)c=l+(c-l)*l/(l-n); if(x>1)c=l+(c-l)*(1-l)/(x-l); return c; }"
    "float3 setsat(float3 c,float s) { float n=min(c.r,min(c.g,c.b)),d=sat(c); return d>0?(c-n)*s/d:float3(0,0,0); }"
    "float3 blend_colour(float3 b,float3 f,uint mode) {"
    "if(mode==0)return b*f; if(mode==1)return b+f-b*f; if(mode==2)return min(b,f); if(mode==3)return max(b,f);"
    "if(mode==5)return 1-min(1,(1-b)/max(f,1e-8)); if(mode==6)return max(0,b+f-1);"
    "if(mode==7)return lum(b)<lum(f)?b:f; if(mode==8)return lum(b)>lum(f)?b:f;"
    "if(mode==9)return min(1,b/max(1-f,1e-8)); if(mode==10)return min(1,b+f);"
    "if(mode==11)return lerp(2*b*f,1-2*(1-b)*(1-f),step(.5,b));"
    "if(mode==12) {float3 d=lerp(((16*b-12)*b+4)*b,sqrt(max(b,0)),step(.25,b));"
    "return lerp(b-(1-2*f)*b*(1-b),b+(2*f-1)*(d-b),step(.5,f));}"
    "if(mode==13)return lerp(2*b*f,1-2*(1-b)*(1-f),step(.5,f));"
    "if(mode==14)return lerp(1-min(1,(1-b)/max(2*f,1e-8)),min(1,b/max(2*(1-f),1e-8)),step(.5,f));"
    "if(mode==15)return saturate(b+2*f-1);"
    "if(mode==16)return lerp(min(b,2*f),max(b,2*f-1),step(.5,f));"
    "if(mode==17)return step(1,b+f); if(mode==18)return abs(f-b); if(mode==19)return f+b-2*f*b;"
    "if(mode==20)return setlum(setsat(f,sat(b)),lum(b));"
    "if(mode==21)return setlum(setsat(b,sat(f)),lum(b));"
    "if(mode==22)return setlum(f,lum(b)); if(mode==23)return setlum(b,lum(f));"
    "if(mode==24)return max(0,b-f); if(mode==25)return min(1,b/max(f,1e-8)); return f;}"
    "float4 sample_linear(float2 p) {"
    "float2 uv = (p-input_rect.xy)/input_rect.zw;"
    "float4 c = source.SampleLevel(sampler0,uv,0);"
    "if(ignore_alpha) {float2 px=uv*texel.zw;"
    "float2 coverage=saturate(min(px+.5,texel.zw+.5-px));"
    "c.a=hard_border?1:coverage.x*coverage.y;} return c; }"
    "float cubic_weight(float x) {x=abs(x); if(x<1)return 1.5*x*x*x-2.5*x*x+1;"
    "if(x<2)return -.5*x*x*x+2.5*x*x-4*x+2;return 0;}"
    "float4 fetch_pixel(int2 p) {bool inside=all(p>=0)&&all(p<int2(texel.zw));"
    "if(hard_border)p=clamp(p,int2(0,0),int2(texel.zw)-1);else if(!inside)return 0;"
    "float4 c=source.Load(int3(p,0));if(ignore_alpha)c.a=1;return c;}"
    "float4 sample_cubic(float2 p,float2 footprint) {float2 px=(p-input_rect.xy)/texel.xy-.5;"
    "float2 scale=max(footprint,1); int2 lo=(int2)ceil(px-2*scale),hi=(int2)floor(px+2*scale);"
    "float4 sum=0;float total=0;for(int y=lo.y;y<=hi.y;++y)for(int x=lo.x;x<=hi.x;++x){"
    "float w=cubic_weight((px.x-x)/scale.x)*cubic_weight((px.y-y)/scale.y);"
    "sum+=fetch_pixel(int2(x,y))*w;total+=w;}return total!=0?sum/total:0;}"
    "float4 sample_image(float2 p) {if(interpolation==2)return sample_cubic(p,float2(1,1));return sample_linear(p);}"
    "float4 sample_kernel(float2 p) {"
    "float2 dx=float2(row_x.x,row_y.x)*output_rect.z,dy=float2(row_x.y,row_y.y)*output_rect.w;"
    "if(interpolation==3)return (sample_linear(p+dx*.25+dy*.25)+sample_linear(p-dx*.25+dy*.25)"
    "+sample_linear(p+dx*.25-dy*.25)+sample_linear(p-dx*.25-dy*.25))*.25;"
    "if(interpolation==4){float2 major=length(dx/texel.xy)>length(dy/texel.xy)?dx:dy;"
    "float ratio=max(length(dx/texel.xy),length(dy/texel.xy))/max(min(length(dx/texel.xy),length(dy/texel.xy)),1);"
    "uint count=(uint)clamp(ceil(ratio),1,16);float4 value=0;"
    "for(uint i=0;i<count;++i)value+=sample_linear(p+major*((i+.5)/count-.5));return value/count;}"
    "if(interpolation==5)return sample_cubic(p,min(max(abs(dx)+abs(dy),texel.xy)/texel.xy,16));"
    "return sample_image(p);}"
    "float4 main(float4 pos : SV_Position) : SV_Target {"
    "if(op==9) return float4(colour.rgb*colour.a,colour.a);"
    "float2 p = output_rect.xy + pos.xy*output_rect.zw;"
    "if(op==39) {float2 n=p*blur.xy+4096,period=clip_rect.zw; float4 sum=0; float amplitude=1;"
    "for(uint octave=0;octave<composite;++octave) {float4 v=perlin(n,period);sum+=(straight_alpha?abs(v):v)*amplitude;"
    "n*=2;period*=2;amplitude*=.5;} if(!straight_alpha)sum=sum*.5+.5;sum.rgb*=sum.a;return sum;}"
    "if(op==16 && (any(p<clip_rect.xy)||any(p>=clip_rect.zw))) return 0;"
    "float2 q = float2(dot(float3(p,1),row_x.xyz),dot(float3(p,1),row_y.xyz));"
    "if(op==31) {float w=dot(float3(p,1),blur.xyz); if(w<=0)return 0; q/=w;"
    "if(hard_border && (any(q<input_rect.xy)||any(q>=input_rect.xy+input_rect.zw)))return 0;}"
    "if(op==22 || op==23) { float2 origin=op==22?clip_rect.xy:input_rect.xy;"
    "float2 size=op==22?clip_rect.zw-clip_rect.xy:input_rect.zw;"
    "float2 uv=(q-origin)/size;"
    "for(uint j=0;j<2;++j) { uint mode=op==22?1:(uint)colour[j];"
    "if(mode==1)uv[j]=frac(uv[j]);"
    "else if(mode==2)uv[j]=1-abs(frac(uv[j]*.5)*2-1);"
    "else uv[j]=saturate(uv[j]); } q=origin+uv*size; }"
    "float4 c = sample_image(q);"
    "if(interpolation==3 || interpolation==4 || interpolation==5) {"
    "float2 dx=ddx(q),dy=ddy(q);"
    "if(interpolation==3)c=(sample_linear(q+dx*.25+dy*.25)+sample_linear(q-dx*.25+dy*.25)"
    "+sample_linear(q+dx*.25-dy*.25)+sample_linear(q-dx*.25-dy*.25))*.25;"
    "if(interpolation==4){float2 major=length(dx/texel.xy)>length(dy/texel.xy)?dx:dy;"
    "float ratio=max(length(dx/texel.xy),length(dy/texel.xy))/max(min(length(dx/texel.xy),length(dy/texel.xy)),1);"
    "uint count=(uint)clamp(ceil(ratio),1,16);c=0;for(uint i=0;i<count;++i)c+=sample_linear(q+major*((i+.5)/count-.5));c/=count;}"
    "if(interpolation==5)c=sample_cubic(q,min(max(abs(dx)+abs(dy),texel.xy)/texel.xy,16));}"
    "if(op==36) {float2 uv=(q-input_rect.xy)/input_rect.zw;"
    "float2 chroma=other.SampleLevel(sampler0,uv,0).rg-float2(128.0/255,128.0/255);"
    "return float4(saturate(float3(c.r+1.402*chroma.y,c.r-.344136*chroma.x-.714136*chroma.y,c.r+1.772*chroma.x)),1);}"
    "if(op==1 || op==2) {"
    "if(blur.z>0) { float4 sum=0; float weight=0;"
    "for(int i=-(int)blur.w;i<=(int)blur.w;++i) {"
    "float w=exp(-0.5*i*i/(blur.z*blur.z));"
    "sum+=sample_image(q+i*blur.xy)*w; weight+=w; } c=sum/weight; }"
    "if(op==2) c=float4(colour.rgb*colour.a,colour.a)*c.a; }"
    "if(op==3) { if(straight_alpha && c.a>0) c.rgb/=c.a;"
    "c=c.r*colour_matrix[0]+c.g*colour_matrix[1]+c.b*colour_matrix[2]+c.a*colour_matrix[3]+colour_matrix[4];"
    "if(clamp_output) c=saturate(c); if(straight_alpha) c.rgb*=c.a; }"
    "if(op==4 || op==13 || op==14 || op==15 || op==26 || op==30) { float2 uv=(p-second_rect.xy)/second_rect.zw;"
    "float4 d=other.SampleLevel(sampler0,uv,0);"
    "if(second_ignore_alpha) d.a=all(uv>=0)&&all(uv<=1)?1:0;"
    "if(op==26) return sample_image(q+colour.x*(float2(d[(uint)colour.y],d[(uint)colour.z])-.5));"
    "if(op==30) {float3 b=c.a>0?c.rgb/c.a:float3(0,0,0),f=d.a>0?d.rgb/d.a:float3(0,0,0);"
    "if(composite==4) { float n=frac(sin(dot(floor(p),float2(12.9898,78.233)))*43758.5453);"
    "return n<d.a?float4(f,1):c; }"
    "return float4(blend_colour(b,f,composite)*c.a*d.a+c.rgb*(1-d.a)+d.rgb*(1-c.a),d.a+c.a*(1-d.a));}"
    "if(op==13) return c*d.a;"
    "if(op==14) return colour.x*c+(1-colour.x)*d;"
    "if(op==15) { c=colour.x*c*d+colour.y*c+colour.z*d+colour.w;"
    "return clamp_output?saturate(c):c; }"
    "if(composite==0) c=c+d*(1-c.a);"
    "else if(composite==1) c=d+c*(1-d.a);"
    "else if(composite==2) c=c*d.a;"
    "else if(composite==3) c=d*c.a;"
    "else if(composite==4) c=c*(1-d.a);"
    "else if(composite==5) c=d*(1-c.a);"
    "else if(composite==6) c=c*d.a+d*(1-c.a);"
    "else if(composite==7) c=d*c.a+c*(1-d.a);"
    "else if(composite==8) c=c*(1-d.a)+d*(1-c.a);"
    "else if(composite==9) c=c+d;"
    "else if(composite==11) {if(any(p<input_rect.xy)||any(p>=input_rect.xy+input_rect.zw))c=d;}"
    "else if(composite==12) c=float4((1-d.rgb)*c.rgb+(1-c.a)*d.rgb,d.a); }"
    "if(op==5) c.rgb*=c.a;"
    "if(op==6) c.rgb=c.a>0?c.rgb/c.a:float3(0,0,0);"
    "if(op==7) c=float4(0,0,0,dot(c.rgb,float3(.2126,.7152,.0722)));"
    "if(op==8) { float3 rgb=c.a>0?c.rgb/c.a:float3(0,0,0);"
    "rgb=saturate((rgb-colour.x)*colour.z+colour.y); c.rgb=rgb*c.a; }"
    "if(op==10) c.rgb=c.a-c.rgb;"
    "if(op==11) c*=colour.x;"
    "if(op==12) c.rgb*=exp2(colour.x);"
    "if(op==17 || op==18) { if(c.a>0)c.rgb/=c.a;"
    "float4 t=op==17?c*colour_matrix[0]+colour_matrix[2]:colour_matrix[0]*pow(max(c,0),colour_matrix[1])+colour_matrix[2];"
    "c=lerp(t,c,colour_matrix[3]); if(clamp_output)c=saturate(c); c.rgb*=c.a; }"
    "if(op==19 || op==20) { if(c.a>0)c.rgb/=c.a;"
    "for(uint j=0;j<4;++j) { if(!table_disabled[j]) {"
    "float at=saturate(c[j])*(op==19?table_sizes[j]-1:table_sizes[j]);"
    "uint lo=min((uint)at,table_sizes[j]-1); uint hi=min(lo+1,table_sizes[j]-1);"
    "c[j]=op==19?lerp(weights[table_offsets[j]+lo],weights[table_offsets[j]+hi],frac(at)):weights[table_offsets[j]+lo]; } }"
    "if(clamp_output)c=saturate(c); c.rgb*=c.a; }"
    "if(op==21) { float4 v=c;"
    "for(int y=0;y<(int)colour.y;++y) for(int x=0;x<(int)colour.x;++x) {"
    "float2 d=float2(x-floor((colour.x-1)*.5),y-floor((colour.y-1)*.5))*blur.xy;"
    "float4 t=sample_image(q+d); v=colour.z>0?max(v,t):min(v,t); } c=v; }"
    "if(op==24) { c*=float4(colour.rgb*colour.a,colour.a); if(clamp_output)c=saturate(c); }"
    "if(op==25) { float3 rgb=c.a>0?c.rgb/c.a:float3(0,0,0);"
    "rgb=round(saturate(rgb)*(colour.rgb-1))/(colour.rgb-1); c.rgb=rgb*c.a; }"
    "if(op==27) { float4 sum=0;"
    "for(uint y=0;y<table_sizes.y;++y) for(uint x=0;x<table_sizes.x;++x) {"
    "float2 d=(float2(x,y)-(float2(table_sizes.xy)-1)*.5+blur.zw)*blur.xy;"
    "float weight=weights[y*table_sizes.x+x]; if(weight!=0)sum+=sample_kernel(q+d)*weight; }"
    "sum=sum/colour.x+colour.y; if(straight_alpha)sum.a=c.a; c=clamp_output?saturate(sum):sum; }"
    "if(op==28) { float3 rgb=c.a>0?c.rgb/c.a:float3(0,0,0);"
    "float hi=max(rgb.r,max(rgb.g,rgb.b)),lo=min(rgb.r,min(rgb.g,rgb.b));"
    "float delta=hi-lo,h=0,s=0,v=hi; if(delta>0) {"
    "if(hi==rgb.r)h=(rgb.g-rgb.b)/delta; else if(hi==rgb.g)h=2+(rgb.b-rgb.r)/delta; else h=4+(rgb.r-rgb.g)/delta;"
    "h=frac(h/6); s=hi>0?delta/hi:0; }"
    "if(colour.x>0) { v=(hi+lo)*.5; s=delta>0?delta/(1-abs(2*v-1)):0; } c.rgb=float3(h,s,v)*c.a; }"
    "if(op==29) { float3 hsv=c.a>0?c.rgb/c.a:float3(0,0,0);"
    "float chroma=colour.x>0?(1-abs(2*hsv.z-1))*hsv.y:hsv.z*hsv.y;"
    "float3 rgb=saturate(abs(frac(hsv.x+float3(0,2.0/3,1.0/3))*6-3)-1);"
    "float m=colour.x>0?hsv.z-chroma*.5:hsv.z-chroma; c.rgb=(rgb*chroma+m)*c.a; }"
    "if(op==32) { float dx=blur.x,dy=blur.y;"
    "float a00=sample_image(q+float2(-dx,-dy)).a,a10=sample_image(q+float2(0,-dy)).a,a20=sample_image(q+float2(dx,-dy)).a;"
    "float a01=sample_image(q+float2(-dx,0)).a,a21=sample_image(q+float2(dx,0)).a;"
    "float a02=sample_image(q+float2(-dx,dy)).a,a12=sample_image(q+float2(0,dy)).a,a22=sample_image(q+float2(dx,dy)).a;"
    "float gx=(a20+2*a21+a22-a00-2*a01-a02)/(4*dx);"
    "float gy=(a02+2*a12+a22-a00-2*a10-a20)/(4*dy);"
    "float3 n=normalize(float3(-gx*blur.z,-gy*blur.z,1));"
    "float3 light=colour_matrix[0].w==0?colour_matrix[0].xyz:colour_matrix[0].xyz-float3(q,c.a*blur.z);"
    "float length2=dot(light,light); light=length2>0?light*rsqrt(length2):float3(0,0,0);"
    "float amount=max(0,dot(n,light));"
    "if(straight_alpha) {float3 h=light+float3(0,0,1); float h2=dot(h,h);"
    "amount=h2>0?pow(max(0,dot(n,h*rsqrt(h2))),colour_matrix[2].x):0;}"
    "if(colour_matrix[0].w==2) {float3 axis=colour_matrix[1].xyz-colour_matrix[0].xyz;"
    "float axis2=dot(axis,axis); float cone=axis2>0?dot(-light,axis*rsqrt(axis2)):0;"
    "amount*=cone>colour_matrix[2].z?pow(max(cone,0),colour_matrix[2].y):0;}"
    "float3 rgb=colour.rgb*colour.a*amount; c=float4(rgb,straight_alpha?max(rgb.r,max(rgb.g,rgb.b)):1); }"
    "if(op==33) {float3 rgb=c.a>0?c.rgb/c.a:float3(0,0,0);"
    "float distance=length(rgb-colour.rgb); float a=distance>colour.a?1:0;"
    "if(clamp_output)a=colour.a>0?saturate(distance/colour.a):a; if(straight_alpha)a=1-a; c*=a;}"
    "if(op==34)c.rgb*=colour.x;"
    "if(op==35) {float3 rgb=c.a>0?c.rgb/c.a:float3(0,0,0); if(clamp_output)rgb=saturate(rgb);"
    "float3 lo=rgb+colour.x*rgb*(2*rgb-1);"
    "float3 hi=rgb+colour.x*(1-rgb)*(2*rgb-1); c.rgb=lerp(lo,hi,step(.5,rgb))*c.a;}"
    "if(op==37) {float3 rgb=c.rgb; if(straight_alpha && c.a>0)rgb/=c.a;"
    "float3 uv=(saturate(rgb.bgr)*(colour.xyz-1)+.5)/colour.xyz;"
    "float4 mapped=lookup_table.SampleLevel(sampler0,uv,0);"
    "mapped.a*=c.a; if(straight_alpha)mapped.rgb*=mapped.a; c=mapped;}"
    "if(op==38) {float3 rgb=c.rgb; if(straight_alpha && c.a>0)rgb/=c.a;"
    "if(colour.x==1 && colour.y==2)rgb=lerp(rgb/12.92,pow(max((rgb+.055)/1.055,0),2.4),step(.04045,rgb));"
    "if(colour.x==2 && colour.y==1)rgb=lerp(rgb*12.92,1.055*pow(max(rgb,0),1.0/2.4)-.055,step(.0031308,rgb));"
    "c.rgb=straight_alpha?rgb*c.a:rgb;}"
    "if(op==40) {float4 avg=(sample_image(q+float2(blur.x,0))+sample_image(q-float2(blur.x,0))"
    "+sample_image(q+float2(0,blur.y))+sample_image(q-float2(0,blur.y)))*.25;"
    "float3 delta=c.rgb-avg.rgb; c.rgb+=colour.x*delta*step(colour.y,abs(delta));}"
    "if(op==41) {float2 direction=float2(cos(colour.y),sin(colour.y))*blur.xy;"
    "float3 a=sample_image(q+direction).rgb,b=sample_image(q-direction).rgb;"
    "float gray=.5+dot(a-b,float3(.299,.587,.114))*colour.x;c.rgb=saturate(gray)*c.a;}"
    "if(op==42) {float3 gx=0,gy=0;"
    "for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){float3 rgb=sample_image(q+float2(x,y)*blur.xy).rgb;"
    "gx+=rgb*x*(y==0?colour.y:1);gy+=rgb*y*(x==0?colour.y:1);}"
    "float3 edges=sqrt(gx*gx+gy*gy)*colour.x; c.rgb=clamp_output?c.rgb+edges:edges;}"
    "if(op==43 && blur.x>0) {float2 closest=clamp(q,input_rect.xy+blur.x,input_rect.xy+input_rect.zw-blur.x);"
    "float t=saturate(length(q-closest)/blur.x-blur.y);t=t*t*(3-2*t);"
    "c=lerp(c,float4(colour.rgb*colour.a,colour.a),t);}"
    "if(op==44) {float3 rgb=c.rgb; float4 mask=other.SampleLevel(sampler0,(q-second_rect.xy)/second_rect.zw,0);"
    "if(straight_alpha){rgb=pow(max(rgb,0),1.0/2.2);mask.rgb=pow(max(mask.rgb,0),1.0/2.2);}"
    "float y=dot(rgb,float3(.21266,.71515,.07219)),m=dot(mask.rgb,float3(.21266,.71515,.07219));"
    "float delta=y-m; m+=mask.a*delta; float mid=y+colour.z*delta;"
    "float shadow=y*colour.y/(1+y*(colour.y-1)),highlight=y*colour.x/(1+y*(colour.x-1));"
    "float yy=saturate(shadow*(1-m)*(1-m)+highlight*m*m+mid*2*m*(1-m));"
    "float u=dot(rgb,float3(-.09991,-.33609,.436)),v=dot(rgb,float3(.615,-.55861,-.05639));"
    "rgb=float3(yy+1.28033*v,yy-.21482*u-.38059*v,yy+2.12798*u);"
    "float peak=max(rgb.r,max(rgb.g,rgb.b)); if(peak>1)rgb/=peak;"
    "if(straight_alpha)rgb=pow(max(rgb,0),2.2);c.rgb=rgb*c.a;}"
    "if(op==45) {float3 a=sample_image(q+float2(blur.x,0)).rgb,b=sample_image(q-float2(blur.x,0)).rgb;"
    "float3 d=sample_image(q+float2(0,blur.y)).rgb,e=sample_image(q-float2(0,blur.y)).rgb;"
    "float gx=dot(a-b,float3(.21266,.71515,.07219)),gy=dot(d-e,float3(.21266,.71515,.07219));"
    "c.a=(gx*gx+gy*gy)*colour.x;}"
    "if(op==46) {float3 rgb=c.a>0?c.rgb/c.a:float3(0,0,0);"
    "float3 lms=float3(dot(rgb,noise_x[0].xyz),dot(rgb,noise_x[1].xyz),dot(rgb,noise_x[2].xyz));"
    "float exponent=colour.z,power2=exp2(exponent);float3 v=pow(abs(lms),exponent);"
    "v=sign(lms)*246*v/(power2+v)+.02;"
    "float intensity=dot(v,float3(.4,.4,.2));"
    "float pt=dot(v,float3(4.455,-4.851,.396)),tt=dot(v,float3(.8056,.3572,-1.1628));"
    "float mapped;"
    "if(blur.y>0 && intensity<=blur.y) {float t=max(intensity/blur.y,0),u=1-t;"
    "mapped=((blur.y+.05)*t*t+u*t*blur.y)/(.35*u*u+2*u*t+t*t);}"
    "else if(intensity<=blur.x)mapped=intensity+.05;"
    "else if(intensity<blur.z) {float t=(intensity-blur.x)/(blur.z-blur.x+.00001),u=1-t;"
    "float a=blur.w/(blur.x+.05),b=.4*colour.x/colour.y;"
    "mapped=(a*u*u*(blur.x+.05)+b*2*u*t*blur.w+b*t*t*blur.w)/(a*u*u+b*2*u*t+b*t*t);}"
    "else mapped=blur.w;"
    "float chroma=pow(min(abs(mapped/max(abs(intensity),1e-8)),1),1.2);pt*=chroma;tt*=chroma;"
    "v=float3(mapped+.09756893*pt+.20522644*tt,mapped-.113876484*pt+.133217156*tt,mapped+.03261511*pt-.676887155*tt);"
    "lms=sign(v)*pow(abs(power2*(50*v-1)/(50*v-12301)),1/exponent);"
    "rgb=float3(dot(lms,noise_y[0].xyz),dot(lms,noise_y[1].xyz),dot(lms,noise_y[2].xyz));c.rgb=rgb*c.a;}"
    "return c; }";

void d2d_effect_renderer_destroy(struct d2d_effect_renderer *r)
{
    unsigned int i;
    if (!r) return;
    if (r->vs) ID3D11VertexShader_Release(r->vs);
    if (r->ps) ID3D11PixelShader_Release(r->ps);
    if (r->histogram) ID3D11ComputeShader_Release(r->histogram);
    if (r->blur_ps) ID3D11PixelShader_Release(r->blur_ps);
    if (r->blur_constants) ID3D11Buffer_Release(r->blur_constants);
    if (r->constants) ID3D11Buffer_Release(r->constants);
    for (i = 0; i < ARRAY_SIZE(r->samplers); ++i)
        if (r->samplers[i]) ID3D11SamplerState_Release(r->samplers[i]);
    free(r);
}

static HRESULT effect_renderer_init(struct d2d_device_context *context)
{
    struct d2d_effect_renderer *r;
    D3D11_BUFFER_DESC desc = {offsetof(struct effect_constants, lut_view), D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER};
    D3D11_SAMPLER_DESC sampler = {0};
    ID3DBlob *code = NULL, *errors = NULL;
    unsigned int i;
    HRESULT hr;

    if (context->effect_renderer) return S_OK;
    if (!(r = calloc(1, sizeof(*r)))) return E_OUTOFMEMORY;
    hr = D3DCompile(effect_vs, sizeof(effect_vs) - 1, "effect_vs", NULL, NULL, "main", "vs_4_0", 0, 0, &code, &errors);
    if (errors) { WARN("%s\n", (char *)ID3D10Blob_GetBufferPointer(errors)); ID3D10Blob_Release(errors); errors = NULL; }
    if (FAILED(hr)) goto failed;
    hr = ID3D11Device1_CreateVertexShader(context->d3d_device, ID3D10Blob_GetBufferPointer(code),
            ID3D10Blob_GetBufferSize(code), NULL, &r->vs);
    ID3D10Blob_Release(code);
    if (FAILED(hr)) goto failed;
    hr = D3DCompile(effect_ps, sizeof(effect_ps) - 1, "effect_ps", NULL, NULL, "main", "ps_4_0", 0, 0, &code, &errors);
    if (errors) { WARN("%s\n", (char *)ID3D10Blob_GetBufferPointer(errors)); ID3D10Blob_Release(errors); }
    if (FAILED(hr)) goto failed;
    hr = ID3D11Device1_CreatePixelShader(context->d3d_device, ID3D10Blob_GetBufferPointer(code),
            ID3D10Blob_GetBufferSize(code), NULL, &r->ps);
    ID3D10Blob_Release(code);
    if (FAILED(hr)) goto failed;
    if (FAILED(hr = ID3D11Device1_CreateBuffer(context->d3d_device, &desc, NULL, &r->constants))) goto failed;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    for (i = 0; i < ARRAY_SIZE(r->samplers); ++i)
    {
        sampler.Filter = i == 0 || i == 3 ? D3D11_FILTER_MIN_MAG_MIP_POINT : D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = i >= 2 ? D3D11_TEXTURE_ADDRESS_CLAMP : D3D11_TEXTURE_ADDRESS_BORDER;
        if (FAILED(hr = ID3D11Device1_CreateSamplerState(context->d3d_device, &sampler, &r->samplers[i]))) goto failed;
    }
    context->effect_renderer = r;
    return S_OK;
failed:
    d2d_effect_renderer_destroy(r);
    return hr;
}

static BOOL empty_rect(const D2D1_RECT_F *r)
{
    return r->right <= r->left || r->bottom <= r->top;
}

static void union_rect(D2D1_RECT_F *r, const D2D1_RECT_F *s)
{
    if (empty_rect(s)) return;
    if (empty_rect(r)) { *r = *s; return; }
    r->left = min(r->left, s->left); r->top = min(r->top, s->top);
    r->right = max(r->right, s->right); r->bottom = max(r->bottom, s->bottom);
}

static void transform_rect(D2D1_RECT_F *r, const D2D1_MATRIX_3X2_F *m)
{
    D2D1_RECT_F s = *r;
    D2D1_POINT_2F p;
    unsigned int i;
    if (empty_rect(r)) return;
    for (i = 0; i < 4; ++i)
    {
        d2d_point_transform(&p, m, i & 1 ? s.right : s.left, i & 2 ? s.bottom : s.top);
        if (!i) { r->left = r->right = p.x; r->top = r->bottom = p.y; }
        else { r->left = min(r->left, p.x); r->right = max(r->right, p.x);
            r->top = min(r->top, p.y); r->bottom = max(r->bottom, p.y); }
    }
}

static HRESULT property(struct d2d_effect *effect, UINT32 index, void *value, UINT32 size)
{
    return ID2D1Effect_GetValue(&effect->ID2D1Effect_iface, index, D2D1_PROPERTY_TYPE_UNKNOWN, value, size);
}

struct effect_description
{
    GUID id;
    D2D1_MATRIX_3X2_F transform;
    D2D1_RECT_F crop;
    D2D1_VECTOR_4F colour;
    D2D1_MATRIX_5X4_F matrix;
    float sigma, angle;
    UINT32 interpolation, border, mode, alpha;
    BOOL clamp;
    float projection[9];
};

static BOOL invert_projection(const float *m, float *inverse)
{
    float det = m[0]*(m[4]*m[8]-m[5]*m[7])-m[1]*(m[3]*m[8]-m[5]*m[6])+m[2]*(m[3]*m[7]-m[4]*m[6]);
    if (!isfinite(det) || fabsf(det) < 1e-12f) return FALSE;
    inverse[0]=(m[4]*m[8]-m[5]*m[7])/det; inverse[1]=(m[2]*m[7]-m[1]*m[8])/det;
    inverse[2]=(m[1]*m[5]-m[2]*m[4])/det; inverse[3]=(m[5]*m[6]-m[3]*m[8])/det;
    inverse[4]=(m[0]*m[8]-m[2]*m[6])/det; inverse[5]=(m[2]*m[3]-m[0]*m[5])/det;
    inverse[6]=(m[3]*m[7]-m[4]*m[6])/det; inverse[7]=(m[1]*m[6]-m[0]*m[7])/det;
    inverse[8]=(m[0]*m[4]-m[1]*m[3])/det;
    return TRUE;
}

static void perspective_point(float x, float y, const D2D1_VECTOR_3F *local,
        const D2D1_VECTOR_3F *global, const D2D1_VECTOR_3F *origin,
        const D2D1_VECTOR_3F *rotation, float *point)
{
    float z = local->z - origin->z, c, s, a, b;
    x += local->x - origin->x;
    y += local->y - origin->y;
    c = cosf(rotation->x * M_PI / 180); s = sinf(rotation->x * M_PI / 180);
    a = y * c - z * s; b = y * s + z * c; y = a; z = b;
    c = cosf(rotation->y * M_PI / 180); s = sinf(rotation->y * M_PI / 180);
    a = x * c + z * s; b = -x * s + z * c; x = a; z = b;
    c = cosf(rotation->z * M_PI / 180); s = sinf(rotation->z * M_PI / 180);
    a = x * c - y * s; b = x * s + y * c;
    point[0] = a + origin->x + global->x;
    point[1] = b + origin->y + global->y;
    point[2] = z + origin->z + global->z;
}

static void temperature_white_point(float temperature, float tint, float *red, float *blue)
{
    /* Robertson isotemperature lines in CIE 1960 uv: u, v, slope, reciprocal kelvin. */
    static const float lines[][4] =
    {
        {.18006f,.26352f,-.24341f,1e-10f}, {.18066f,.26589f,-.25479f,.00001f},
        {.18133f,.26846f,-.26876f,.00002f}, {.18208f,.27119f,-.28539f,.00003f},
        {.18293f,.27407f,-.30470f,.00004f}, {.18388f,.27709f,-.32675f,.00005f},
        {.18494f,.28021f,-.35156f,.00006f}, {.18611f,.28342f,-.37915f,.00007f},
        {.18740f,.28668f,-.40955f,.00008f}, {.18880f,.28997f,-.44278f,.00009f},
        {.19032f,.29326f,-.47888f,.00010f}, {.19462f,.30141f,-.58204f,.000125f},
        {.19962f,.30921f,-.70471f,.00015f}, {.20525f,.31647f,-.84901f,.000175f},
        {.21142f,.32312f,-1.0182f,.00020f}, {.21807f,.32909f,-1.2168f,.000225f},
        {.22511f,.33439f,-1.4512f,.00025f}, {.23247f,.33904f,-1.7298f,.000275f},
        {.24010f,.34308f,-2.0637f,.00030f}, {.24792f,.34655f,-2.4681f,.000325f},
        {.25591f,.34951f,-2.9641f,.00035f}, {.26400f,.35200f,-3.5814f,.000375f},
        {.27218f,.35407f,-4.3633f,.00040f}, {.28039f,.35577f,-5.3762f,.000425f},
        {.28863f,.35714f,-6.7262f,.00045f}, {.29685f,.35823f,-8.5955f,.000475f},
        {.30505f,.35907f,-11.324f,.00050f}, {.31320f,.35968f,-15.628f,.000525f},
        {.32129f,.36011f,-23.325f,.00055f}, {.32931f,.36038f,-40.770f,.000575f},
        {.33724f,.36051f,-116.45f,.00060f},
    };
    float reciprocal=1/fmaxf(1666.6666f,temperature), t, nx0, nx1, slope, u0, v0, u, v, shift, x, z, green;
    const float *a,*b;
    unsigned int i;
    for(i=1;i<ARRAY_SIZE(lines)-1;++i)if(reciprocal>=lines[i-1][3]&&reciprocal<=lines[i][3])break;
    a=lines[i-1];b=lines[i];t=(reciprocal-a[3])/(b[3]-a[3]);
    nx0=1/sqrtf(a[2]*a[2]+1);nx1=1/sqrtf(b[2]*b[2]+1);
    slope=(a[2]*nx0+(b[2]*nx1-a[2]*nx0)*t)/(nx0+(nx1-nx0)*t);
    u0=a[0]+(b[0]-a[0])*t;v0=a[1]+(b[1]-a[1])*t;
    shift=-fminf(.1f,fmaxf(-.1f,tint))/sqrtf(slope*slope+1);u=u0+shift;v=v0+shift*slope;
    if(v>.4f-.1f*u)
    {
        float factor=(.4f-.1f*u-v)/(v0-(u-u0)*.1f-v);
        u+=(u0-u)*factor;v+=(v0-v)*factor;
    }
    x=v>=1e-7f?1.5f*u/v:0;z=v>=1e-7f?(24-6*u-60*v)/(12*v):0;
    green=1.8760108f-.969266f*x+.041556001f*z;
    *red=(3.2404542f*x-1.5371385f-.4985314f*z)/green;
    *blue=(.055643398f*x-.20402589f+1.0572252f*z)/green;
}

static float hdr_luminance_intensity(float luminance, float maximum, float exponent)
{
    static const float xyz_lms[9]={.4002f,.7075f,-.0807f,-.228f,1.15f,.0612f,0,0,.9184f};
    const float white[3]={.95042855f,1,1.0889004f};
    float values[3],power2=powf(2,exponent);
    unsigned int i;
    for(i=0;i<3;++i)
    {
        float x=(xyz_lms[i*3]*white[0]+xyz_lms[i*3+1]*white[1]+xyz_lms[i*3+2]*white[2])*luminance/maximum;
        float v=powf(fabsf(x),exponent);
        values[i]=(x<0?-1:1)*246*v/(power2+v)+.02f;
    }
    return .4f*values[0]+.4f*values[1]+.2f*values[2];
}

static HRESULT describe_effect(struct d2d_effect *e, struct effect_description *d)
{
    HRESULT hr;
    memset(d, 0, sizeof(*d));
    d->transform._11 = d->transform._22 = 1;
    d->interpolation = D2D1_INTERPOLATION_MODE_LINEAR;
    if (FAILED(hr = property(e, D2D1_PROPERTY_CLSID, &d->id, sizeof(d->id)))) return hr;
#define GET(index, field) do { if (FAILED(hr = property(e, index, &d->field, sizeof(d->field)))) return hr; } while (0)
    if (IsEqualGUID(&d->id, &CLSID_D2D1Crop) || IsEqualGUID(&d->id, &CLSID_D2D1Atlas))
    {
        GET(D2D1_CROP_PROP_RECT, crop);
        if (IsEqualGUID(&d->id, &CLSID_D2D1Crop)) GET(D2D1_CROP_PROP_BORDER_MODE, border);
    }
    else if (IsEqualGUID(&d->id, &CLSID_D2D1Scale))
    {
        D2D1_VECTOR_2F scale, center;
        if (FAILED(hr = property(e, D2D1_SCALE_PROP_SCALE, &scale, sizeof(scale)))) return hr;
        if (FAILED(hr = property(e, D2D1_SCALE_PROP_CENTER_POINT, &center, sizeof(center)))) return hr;
        if (scale.x <= 0 || scale.y <= 0 || !isfinite(scale.x) || !isfinite(scale.y)) return E_INVALIDARG;
        d->transform._11 = scale.x; d->transform._22 = scale.y;
        d->transform._31 = center.x * (1 - scale.x); d->transform._32 = center.y * (1 - scale.y);
        GET(D2D1_SCALE_PROP_INTERPOLATION_MODE, interpolation);
        GET(D2D1_SCALE_PROP_BORDER_MODE, border);
    }
    else if (IsEqualGUID(&d->id, &CLSID_D2D12DAffineTransform))
    {
        GET(D2D1_2DAFFINETRANSFORM_PROP_TRANSFORM_MATRIX, transform);
        GET(D2D1_2DAFFINETRANSFORM_PROP_INTERPOLATION_MODE, interpolation);
        GET(D2D1_2DAFFINETRANSFORM_PROP_BORDER_MODE, border);
    }
    else if (IsEqualGUID(&d->id, &CLSID_D2D1GaussianBlur))
    {
        GET(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, sigma);
        GET(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, border);
    }
    else if (IsEqualGUID(&d->id, &CLSID_D2D1DirectionalBlur))
    {
        GET(D2D1_DIRECTIONALBLUR_PROP_STANDARD_DEVIATION, sigma);
        GET(D2D1_DIRECTIONALBLUR_PROP_ANGLE, angle);
        GET(D2D1_DIRECTIONALBLUR_PROP_BORDER_MODE, border);
    }
    else if (IsEqualGUID(&d->id, &CLSID_D2D1Shadow))
    {
        GET(D2D1_SHADOW_PROP_BLUR_STANDARD_DEVIATION, sigma);
        GET(D2D1_SHADOW_PROP_COLOR, colour);
    }
    else if (IsEqualGUID(&d->id, &CLSID_D2D1Composite))
    {
        GET(D2D1_COMPOSITE_PROP_MODE, mode);
        if (d->mode > D2D1_COMPOSITE_MODE_MASK_INVERT) return E_INVALIDARG;
    }
    else if (IsEqualGUID(&d->id, &CLSID_D2D1ColorMatrix))
    {
        GET(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, matrix);
        GET(D2D1_COLORMATRIX_PROP_ALPHA_MODE, alpha);
        GET(D2D1_COLORMATRIX_PROP_CLAMP_OUTPUT, clamp);
    }
    else if (IsEqualGUID(&d->id, &CLSID_D2D1Saturation) || IsEqualGUID(&d->id, &CLSID_D2D1Grayscale))
    {
        float saturation = 0, *m = (float *)&d->matrix;
        const float luminance[] = {.2125f, .7154f, .0721f};
        unsigned int i, j;
        if (IsEqualGUID(&d->id, &CLSID_D2D1Saturation))
        {
            if (FAILED(hr = property(e, D2D1_SATURATION_PROP_SATURATION, &saturation, sizeof(saturation)))) return hr;
            if (!isfinite(saturation) || saturation < 0 || saturation > 2) return E_INVALIDARG;
        }
        for (i = 0; i < 3; ++i)
            for (j = 0; j < 3; ++j)
                m[i * 4 + j] = luminance[i] * (1 - saturation) + (i == j ? saturation : 0);
        m[15] = 1;
        d->alpha = D2D1_COLORMATRIX_ALPHA_MODE_PREMULTIPLIED;
    }
    else if (IsEqualGUID(&d->id, &CLSID_D2D1HueRotation))
    {
        float angle, s, c, *m = (float *)&d->matrix;
        if (FAILED(hr = property(e, D2D1_HUEROTATION_PROP_ANGLE, &angle, sizeof(angle)))) return hr;
        if (!isfinite(angle)) return E_INVALIDARG;
        c = cosf(angle * M_PI / 180); s = sinf(angle * M_PI / 180);
        m[0] = .213f + c * .787f - s * .213f;
        m[1] = .213f - c * .213f + s * .143f;
        m[2] = .213f - c * .213f - s * .787f;
        m[4] = .715f - c * .715f - s * .715f;
        m[5] = .715f + c * .285f + s * .140f;
        m[6] = .715f - c * .715f + s * .715f;
        m[8] = .072f - c * .072f + s * .928f;
        m[9] = .072f - c * .072f - s * .283f;
        m[10] = .072f + c * .928f + s * .072f;
        m[15] = 1;
        d->alpha = D2D1_COLORMATRIX_ALPHA_MODE_PREMULTIPLIED;
    }
    else if (IsEqualGUID(&d->id, &CLSID_D2D1Brightness))
    {
        D2D1_VECTOR_2F black, white;
        if (FAILED(hr = property(e, D2D1_BRIGHTNESS_PROP_BLACK_POINT, &black, sizeof(black)))) return hr;
        if (FAILED(hr = property(e, D2D1_BRIGHTNESS_PROP_WHITE_POINT, &white, sizeof(white)))) return hr;
        if (!isfinite(black.x) || !isfinite(black.y) || !isfinite(white.x) || !isfinite(white.y)
                || white.x <= black.x) return E_INVALIDARG;
        d->colour.x = black.x; d->colour.y = black.y;
        d->colour.z = (white.y - black.y) / (white.x - black.x);
    }
    else if (IsEqualGUID(&d->id, &CLSID_D2D1Flood))
    {
        GET(D2D1_FLOOD_PROP_COLOR, colour);
    }
    else if (IsEqualGUID(&d->id, &CLSID_D2D1Opacity) || IsEqualGUID(&d->id, &CLSID_D2D1Exposure))
    {
        if (FAILED(hr = property(e, 0, &d->colour.x, sizeof(float)))) return hr;
        if (!isfinite(d->colour.x)) return E_INVALIDARG;
        if (IsEqualGUID(&d->id, &CLSID_D2D1Opacity) && (d->colour.x < 0 || d->colour.x > 1)) return E_INVALIDARG;
        if (IsEqualGUID(&d->id, &CLSID_D2D1Exposure) && (d->colour.x < -2 || d->colour.x > 2)) return E_INVALIDARG;
    }
    else if (IsEqualGUID(&d->id, &CLSID_D2D1CrossFade))
    {
        if (FAILED(hr = property(e, D2D1_CROSSFADE_PROP_WEIGHT, &d->colour.x, sizeof(float)))) return hr;
    }
    else if (IsEqualGUID(&d->id, &CLSID_D2D1ArithmeticComposite))
    {
        GET(D2D1_ARITHMETICCOMPOSITE_PROP_COEFFICIENTS, colour);
        GET(D2D1_ARITHMETICCOMPOSITE_PROP_CLAMP_OUTPUT, clamp);
    }
    else if (IsEqualGUID(&d->id, &CLSID_D2D1LinearTransfer) || IsEqualGUID(&d->id, &CLSID_D2D1GammaTransfer))
    {
        BOOL gamma = IsEqualGUID(&d->id, &CLSID_D2D1GammaTransfer), disabled;
        float *m = (float *)&d->matrix;
        unsigned int i, stride = gamma ? 4 : 3;
        for (i = 0; i < 4; ++i)
        {
            if (FAILED(hr = property(e, i * stride + (gamma ? 0 : 1), &m[i], sizeof(float)))) return hr;
            m[4 + i] = 1;
            if (gamma && FAILED(hr = property(e, i * stride + 1, &m[4 + i], sizeof(float)))) return hr;
            if (FAILED(hr = property(e, i * stride + (gamma ? 2 : 0), &m[8 + i], sizeof(float)))) return hr;
            if (FAILED(hr = property(e, i * stride + stride - 1, &disabled, sizeof(disabled)))) return hr;
            m[12 + i] = !!disabled;
        }
        if (FAILED(hr = property(e, stride * 4, &d->clamp, sizeof(d->clamp)))) return hr;
    }
    else if (IsEqualGUID(&d->id, &CLSID_D2D1Morphology))
    {
        UINT32 width, height;
        if (FAILED(hr = property(e, 0, &d->mode, sizeof(d->mode)))) return hr;
        if (FAILED(hr = property(e, 1, &width, sizeof(width)))) return hr;
        if (FAILED(hr = property(e, 2, &height, sizeof(height)))) return hr;
        if (d->mode > 1 || !width || width > 100 || !height || height > 100) return E_INVALIDARG;
        d->colour.x = width; d->colour.y = height; d->colour.z = d->mode;
    }
    else if (IsEqualGUID(&d->id, &CLSID_D2D1Tile))
    {
        if (FAILED(hr = property(e, 0, &d->crop, sizeof(d->crop)))) return hr;
        if (empty_rect(&d->crop)) return E_INVALIDARG;
    }
    else if (IsEqualGUID(&d->id, &CLSID_D2D1Border))
    {
        UINT32 x, y;
        if (FAILED(hr = property(e, 0, &x, sizeof(x)))) return hr;
        if (FAILED(hr = property(e, 1, &y, sizeof(y)))) return hr;
        if (x > 2 || y > 2) return E_INVALIDARG;
        d->colour.x = x; d->colour.y = y;
    }
    else if (IsEqualGUID(&d->id, &CLSID_D2D1Sepia))
    {
        static const float sepia[3][3] = {{.393f,.349f,.272f},{.769f,.686f,.534f},{.189f,.168f,.131f}};
        float intensity, *m = (float *)&d->matrix;
        unsigned int i, j;
        if (FAILED(hr = property(e, 0, &intensity, sizeof(intensity)))) return hr;
        if (FAILED(hr = property(e, 1, &d->alpha, sizeof(d->alpha)))) return hr;
        if (!isfinite(intensity) || intensity < 0 || intensity > 1) return E_INVALIDARG;
        for (i = 0; i < 3; ++i) for (j = 0; j < 3; ++j)
            m[i * 4 + j] = sepia[i][j] * intensity + (i == j ? 1 - intensity : 0);
        m[15] = 1;
    }
    else if (IsEqualGUID(&d->id, &CLSID_D2D1Tint))
    {
        if (FAILED(hr = property(e, 0, &d->colour, sizeof(d->colour)))) return hr;
        if (FAILED(hr = property(e, 1, &d->clamp, sizeof(d->clamp)))) return hr;
    }
    else if (IsEqualGUID(&d->id, &CLSID_D2D1Posterize))
    {
        UINT32 counts[3];
        unsigned int i;
        for (i = 0; i < 3; ++i)
        {
            if (FAILED(hr = property(e, i, &counts[i], sizeof(counts[i])))) return hr;
            if (counts[i] < 2 || counts[i] > 16) return E_INVALIDARG;
            ((float *)&d->colour)[i] = counts[i];
        }
    }
    else if (IsEqualGUID(&d->id, &CLSID_D2D1DisplacementMap))
    {
        UINT32 x, y;
        if (FAILED(hr = property(e, 0, &d->colour.x, sizeof(float)))) return hr;
        if (FAILED(hr = property(e, 1, &x, sizeof(x)))) return hr;
        if (FAILED(hr = property(e, 2, &y, sizeof(y)))) return hr;
        if (!isfinite(d->colour.x) || x > 3 || y > 3) return E_INVALIDARG;
        d->colour.y = x; d->colour.z = y;
    }
    else if (IsEqualGUID(&d->id, &CLSID_D2D1RgbToHue) || IsEqualGUID(&d->id, &CLSID_D2D1HueToRgb))
    {
        if (FAILED(hr = property(e, 0, &d->mode, sizeof(d->mode)))) return hr;
        if (d->mode > 1) return E_INVALIDARG;
    }
    else if (IsEqualGUID(&d->id, &CLSID_D2D1Blend))
    {
        if (FAILED(hr = property(e, D2D1_BLEND_PROP_MODE, &d->mode, sizeof(d->mode)))) return hr;
        if (d->mode > 25) return E_INVALIDARG;
    }
    else if (IsEqualGUID(&d->id, &CLSID_D2D1DpiCompensation))
    {
        D2D1_VECTOR_2F dpi;
        if (FAILED(hr = property(e, 0, &d->interpolation, sizeof(d->interpolation)))) return hr;
        if (FAILED(hr = property(e, 1, &d->border, sizeof(d->border)))) return hr;
        if (FAILED(hr = property(e, 2, &dpi, sizeof(dpi)))) return hr;
        if (!isfinite(dpi.x) || !isfinite(dpi.y) || dpi.x <= 0 || dpi.y <= 0) return E_INVALIDARG;
        d->transform._11 = 96 / dpi.x; d->transform._22 = 96 / dpi.y;
    }
    else if (IsEqualGUID(&d->id, &CLSID_D2D13DTransform))
    {
        D2D1_MATRIX_4X4_F m;
        if (FAILED(hr = property(e, 0, &d->interpolation, sizeof(d->interpolation)))) return hr;
        if (FAILED(hr = property(e, 1, &d->border, sizeof(d->border)))) return hr;
        if (FAILED(hr = property(e, 2, &m, sizeof(m)))) return hr;
        d->projection[0]=m._11; d->projection[1]=m._21; d->projection[2]=m._41;
        d->projection[3]=m._12; d->projection[4]=m._22; d->projection[5]=m._42;
        d->projection[6]=m._14; d->projection[7]=m._24; d->projection[8]=m._44;
    }
    else if (IsEqualGUID(&d->id, &CLSID_D2D13DPerspectiveTransform))
    {
        D2D1_VECTOR_3F local, global, origin, rotation;
        D2D1_VECTOR_2F perspective;
        float depth, p[3][3];
        unsigned int i;
        GET(D2D1_3DPERSPECTIVETRANSFORM_PROP_INTERPOLATION_MODE, interpolation);
        GET(D2D1_3DPERSPECTIVETRANSFORM_PROP_BORDER_MODE, border);
        if (FAILED(hr = property(e, 2, &depth, sizeof(depth)))) return hr;
        if (FAILED(hr = property(e, 3, &perspective, sizeof(perspective)))) return hr;
        if (FAILED(hr = property(e, 4, &local, sizeof(local)))) return hr;
        if (FAILED(hr = property(e, 5, &global, sizeof(global)))) return hr;
        if (FAILED(hr = property(e, 6, &origin, sizeof(origin)))) return hr;
        if (FAILED(hr = property(e, 7, &rotation, sizeof(rotation)))) return hr;
        if (!isfinite(depth) || depth <= 0) return E_INVALIDARG;
        perspective_point(0, 0, &local, &global, &origin, &rotation, p[0]);
        perspective_point(1, 0, &local, &global, &origin, &rotation, p[1]);
        perspective_point(0, 1, &local, &global, &origin, &rotation, p[2]);
        for (i = 0; i < 3; ++i)
        {
            float z = p[i][2] / depth;
            p[i][0] += perspective.x * z;
            p[i][1] += perspective.y * z;
            p[i][2] = 1 + z;
        }
        for (i = 0; i < 3; ++i)
        {
            d->projection[i * 3] = p[1][i] - p[0][i];
            d->projection[i * 3 + 1] = p[2][i] - p[0][i];
            d->projection[i * 3 + 2] = p[0][i];
        }
    }
    else if (IsEqualGUID(&d->id, &CLSID_D2D1DistantDiffuse) || IsEqualGUID(&d->id, &CLSID_D2D1DistantSpecular)
            || IsEqualGUID(&d->id, &CLSID_D2D1PointDiffuse) || IsEqualGUID(&d->id, &CLSID_D2D1PointSpecular)
            || IsEqualGUID(&d->id, &CLSID_D2D1SpotDiffuse) || IsEqualGUID(&d->id, &CLSID_D2D1SpotSpecular))
    {
        BOOL distant = IsEqualGUID(&d->id, &CLSID_D2D1DistantDiffuse) || IsEqualGUID(&d->id, &CLSID_D2D1DistantSpecular);
        BOOL spot = IsEqualGUID(&d->id, &CLSID_D2D1SpotDiffuse) || IsEqualGUID(&d->id, &CLSID_D2D1SpotSpecular);
        BOOL specular = IsEqualGUID(&d->id, &CLSID_D2D1DistantSpecular)
                || IsEqualGUID(&d->id, &CLSID_D2D1PointSpecular) || IsEqualGUID(&d->id, &CLSID_D2D1SpotSpecular);
        float *m = (float *)&d->matrix, azimuth, elevation, cone;
        UINT32 index = distant ? 2 : spot ? 4 : 1;
        if (distant)
        {
            if (FAILED(hr = property(e, 0, &azimuth, sizeof(float)))) return hr;
            if (FAILED(hr = property(e, 1, &elevation, sizeof(float)))) return hr;
            azimuth *= M_PI/180; elevation *= M_PI/180;
            m[0]=cosf(azimuth)*cosf(elevation); m[1]=sinf(azimuth)*cosf(elevation); m[2]=sinf(elevation);
        }
        else if (FAILED(hr = property(e, 0, m, 3*sizeof(float)))) return hr;
        m[3] = distant ? 0 : spot ? 2 : 1;
        m[8] = 1;
        if (spot)
        {
            if (FAILED(hr = property(e, 1, m+4, 3*sizeof(float)))) return hr;
            if (FAILED(hr = property(e, 2, m+9, sizeof(float)))) return hr;
            if (FAILED(hr = property(e, 3, &cone, sizeof(float)))) return hr;
            m[10] = cosf(cone*M_PI/180);
        }
        if (specular && FAILED(hr = property(e, index++, m+8, sizeof(float)))) return hr;
        if (FAILED(hr = property(e, index++, &d->colour.w, sizeof(float)))) return hr;
        if (FAILED(hr = property(e, index++, m+14, sizeof(float)))) return hr;
        if (FAILED(hr = property(e, index++, &d->colour, 3*sizeof(float)))) return hr;
        if (FAILED(hr = property(e, index++, m+12, 2*sizeof(float)))) return hr;
        if (FAILED(hr = property(e, index, &d->interpolation, sizeof(d->interpolation)))) return hr;
        d->mode = 32; d->alpha = specular;
        if (m[12] <= 0 || m[13] <= 0 || m[14] < 0 || d->colour.w < 0 || m[8] < 1 || m[8] > 128) return E_INVALIDARG;
    }
    else if (IsEqualGUID(&d->id, &CLSID_D2D1ChromaKey))
    {
        if (FAILED(hr = property(e, 0, &d->colour, 3*sizeof(float)))) return hr;
        if (FAILED(hr = property(e, 1, &d->colour.w, sizeof(float)))) return hr;
        if (FAILED(hr = property(e, 2, &d->alpha, sizeof(UINT)))) return hr;
        if (FAILED(hr = property(e, 3, &d->clamp, sizeof(BOOL)))) return hr;
        if (!isfinite(d->colour.w) || d->colour.w < 0 || d->colour.w > 1) return E_INVALIDARG;
    }
    else if (IsEqualGUID(&d->id, &CLSID_D2D1WhiteLevelAdjustment))
    {
        float input, output;
        if (FAILED(hr = property(e, 0, &input, sizeof(float)))) return hr;
        if (FAILED(hr = property(e, 1, &output, sizeof(float)))) return hr;
        if (!isfinite(input) || !isfinite(output) || input <= 0 || output <= 0) return E_INVALIDARG;
        d->colour.x = input / output;
    }
    else if (IsEqualGUID(&d->id, &CLSID_D2D1BitmapSource))
    {
        D2D1_VECTOR_2F scale;
        if (FAILED(hr = property(e, 1, &scale, sizeof(scale)))) return hr;
        if (FAILED(hr = property(e, 2, &d->interpolation, sizeof(d->interpolation)))) return hr;
        if (FAILED(hr = property(e, 3, &d->clamp, sizeof(d->clamp)))) return hr;
        if (FAILED(hr = property(e, 4, &d->alpha, sizeof(d->alpha)))) return hr;
        if (FAILED(hr = property(e, 5, &d->mode, sizeof(d->mode)))) return hr;
        if (!isfinite(scale.x) || !isfinite(scale.y) || scale.x <= 0 || scale.y <= 0) return E_INVALIDARG;
        d->transform._11 = scale.x; d->transform._22 = scale.y;
        if (d->mode < 1 || d->mode > 8) return E_INVALIDARG;
    }
    else if (IsEqualGUID(&d->id, &CLSID_D2D1Contrast))
    {
        if (FAILED(hr = property(e, 0, &d->colour.x, sizeof(float)))) return hr;
        if (FAILED(hr = property(e, 1, &d->clamp, sizeof(BOOL)))) return hr;
        if (!isfinite(d->colour.x) || d->colour.x < -1 || d->colour.x > 1) return E_INVALIDARG;
    }
    else if (IsEqualGUID(&d->id, &CLSID_D2D1Turbulence))
    {
        D2D1_VECTOR_2F offset, size;
        if(FAILED(hr=property(e,0,&offset,sizeof(offset))))return hr;
        if(FAILED(hr=property(e,1,&size,sizeof(size))))return hr;
        if(!isfinite(size.x)||!isfinite(size.y)||size.x<0||size.y<0)return E_INVALIDARG;
        d->crop=(D2D1_RECT_F){offset.x,offset.y,offset.x+size.x,offset.y+size.y};
    }
    else if (IsEqualGUID(&d->id, &CLSID_D2D1Straighten))
    {
        if (FAILED(hr = property(e, 0, &d->angle, sizeof(float)))) return hr;
        if (FAILED(hr = property(e, 1, &d->clamp, sizeof(BOOL)))) return hr;
        if (FAILED(hr = property(e, 2, &d->interpolation, sizeof(UINT)))) return hr;
        if (!isfinite(d->angle) || d->angle < -45 || d->angle > 45) return E_INVALIDARG;
    }
    else if (IsEqualGUID(&d->id, &CLSID_D2D1YCbCr))
    {
        if (FAILED(hr = property(e, 0, &d->mode, sizeof(UINT)))) return hr;
        if (FAILED(hr = property(e, 1, &d->transform, sizeof(d->transform)))) return hr;
        if (FAILED(hr = property(e, 2, &d->interpolation, sizeof(UINT)))) return hr;
        if (d->mode > 4) return E_INVALIDARG;
    }
    else if (IsEqualGUID(&d->id, &CLSID_D2D1ColorManagement))
    {
        ID2D1ColorContext *source=NULL,*destination=NULL;
        if (FAILED(hr=property(e,0,&source,sizeof(source)))) return hr;
        hr=property(e,2,&destination,sizeof(destination));
        if (SUCCEEDED(hr))
        {
            d->colour.x=source?ID2D1ColorContext_GetColorSpace(source):D2D1_COLOR_SPACE_SRGB;
            d->colour.y=destination?ID2D1ColorContext_GetColorSpace(destination):D2D1_COLOR_SPACE_SRGB;
        }
        if(source)ID2D1ColorContext_Release(source);
        if(destination)ID2D1ColorContext_Release(destination);
        if(FAILED(hr))return hr;
        if(FAILED(hr=property(e,4,&d->alpha,sizeof(UINT))))return hr;
    }
    else if (IsEqualGUID(&d->id,&CLSID_D2D1Sharpen) || IsEqualGUID(&d->id,&CLSID_D2D1Emboss))
    {
        if(FAILED(hr=property(e,0,&d->colour.x,sizeof(float))))return hr;
        if(FAILED(hr=property(e,1,&d->colour.y,sizeof(float))))return hr;
        if(!isfinite(d->colour.x)||!isfinite(d->colour.y))return E_INVALIDARG;
        if(IsEqualGUID(&d->id,&CLSID_D2D1Emboss))d->colour.y*=M_PI/180;
    }
    else if(IsEqualGUID(&d->id,&CLSID_D2D1EdgeDetection))
    {
        if(FAILED(hr=property(e,0,&d->colour.x,sizeof(float))))return hr;
        if(FAILED(hr=property(e,1,&d->sigma,sizeof(float))))return hr;
        if(FAILED(hr=property(e,2,&d->mode,sizeof(UINT))))return hr;
        if(FAILED(hr=property(e,3,&d->clamp,sizeof(BOOL))))return hr;
        if(FAILED(hr=property(e,4,&d->alpha,sizeof(UINT))))return hr;
        if(d->mode>1)return E_INVALIDARG;
        d->colour.y=d->mode?1:2;
        if(!isfinite(d->sigma) || d->sigma < 0 || d->sigma > 10) return E_INVALIDARG;
        d->border = D2D1_BORDER_MODE_HARD;
    }
    else if(IsEqualGUID(&d->id,&CLSID_D2D1TemperatureTint))
    {
        float temperature,tint,reference=6502.0781f,adjusted=reference,red0,blue0,red1,blue1,*m=(float *)&d->matrix;
        if(FAILED(hr=property(e,0,&temperature,sizeof(float))))return hr;
        if(FAILED(hr=property(e,1,&tint,sizeof(float))))return hr;
        temperature*=1.25f;
        if(temperature>0)adjusted=1/(.00015379698f+temperature*.000068425245f);
        else if(temperature<0)
        {
            adjusted=1/(.00015379698f+temperature*.000053796983f);
            reference=1/(.00015379698f-temperature*.000084298255f);
        }
        temperature_white_point(reference,.0032560001f,&red0,&blue0);
        temperature_white_point(adjusted,.0032560001f+tint*.0125f,&red1,&blue1);
        m[0]=red1/red0;m[5]=1;m[10]=blue1/blue0;m[15]=1;d->alpha=1;
    }
    else if(IsEqualGUID(&d->id,&CLSID_D2D1Vignette))
    {
        if(FAILED(hr=property(e,0,&d->colour,sizeof(d->colour))))return hr;
        if(FAILED(hr=property(e,1,&d->angle,sizeof(float))))return hr;
        if(FAILED(hr=property(e,2,&d->sigma,sizeof(float))))return hr;
        d->angle=fminf(1,fmaxf(0,d->angle));
        d->sigma=fminf(1,fmaxf(0,d->sigma));
        d->border=D2D1_BORDER_MODE_HARD;
    }
    else if(IsEqualGUID(&d->id,&CLSID_D2D1HighlightsShadows))
    {
        float h,s,clarity;
        if(FAILED(hr=property(e,0,&h,sizeof(float))))return hr;
        if(FAILED(hr=property(e,1,&s,sizeof(float))))return hr;
        if(FAILED(hr=property(e,2,&clarity,sizeof(float))))return hr;
        if(FAILED(hr=property(e,3,&d->alpha,sizeof(UINT))))return hr;
        if(FAILED(hr=property(e,4,&d->angle,sizeof(float))))return hr;
        h=fminf(1,fmaxf(-1,h));s=fminf(1,fmaxf(-1,s));clarity=fminf(1,fmaxf(-1,clarity));
        d->colour.x=powf(4,h*(h>0?2:1.5f));d->colour.y=powf(5,s*(s>0?.8f:1.5f));
        d->colour.z=clarity*(clarity>0?2:1);
        if(d->alpha>1)return E_INVALIDARG;
        d->angle=fminf(10,fmaxf(0,d->angle));
    }
    else if(IsEqualGUID(&d->id,&CLSID_D2D1HdrToneMap))
    {
        if(FAILED(hr=property(e,0,&d->colour.x,sizeof(float))))return hr;
        if(FAILED(hr=property(e,1,&d->colour.y,sizeof(float))))return hr;
        if(FAILED(hr=property(e,2,&d->mode,sizeof(UINT))))return hr;
        if(!isfinite(d->colour.x)||!isfinite(d->colour.y)||d->colour.x<=1||d->colour.y<=0||d->mode>1)return E_INVALIDARG;
    }
    else if (!IsEqualGUID(&d->id, &CLSID_D2D1Premultiply)
            && !IsEqualGUID(&d->id, &CLSID_D2D1UnPremultiply)
            && !IsEqualGUID(&d->id, &CLSID_D2D1Invert)
            && !IsEqualGUID(&d->id, &CLSID_D2D1AlphaMask)
            && !IsEqualGUID(&d->id, &CLSID_D2D1TableTransfer)
            && !IsEqualGUID(&d->id, &CLSID_D2D1DiscreteTransfer)
            && !IsEqualGUID(&d->id, &CLSID_D2D1ConvolveMatrix)
            && !IsEqualGUID(&d->id, &CLSID_D2D1OpacityMetadata)
            && !IsEqualGUID(&d->id, &CLSID_D2D1Histogram)
            && !IsEqualGUID(&d->id, &CLSID_D2D1LookupTable3D)
            && !IsEqualGUID(&d->id, &CLSID_D2D1LuminanceToAlpha)) return E_NOTIMPL;
#undef GET
    if (!isfinite(d->sigma) || d->sigma < 0 || d->sigma > 250 || !isfinite(d->angle)) return E_INVALIDARG;
    if (d->border > D2D1_BORDER_MODE_HARD) return E_INVALIDARG;
    if (d->interpolation > D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC) return E_INVALIDARG;
    return S_OK;
}

static void straighten_transform(struct effect_description *d, const D2D1_RECT_F *bounds)
{
    float angle = d->angle * M_PI / 180, c = cosf(angle), s = sinf(angle), scale = 1;
    float width = bounds->right - bounds->left, height = bounds->bottom - bounds->top;
    float x = (bounds->left + bounds->right) * .5f, y = (bounds->top + bounds->bottom) * .5f;
    if (d->clamp && width > 0 && height > 0)
        scale = max(fabsf(c) + fabsf(s)*height/width, fabsf(c) + fabsf(s)*width/height);
    d->transform._11 = c*scale; d->transform._12 = s*scale;
    d->transform._21 = -s*scale; d->transform._22 = c*scale;
    d->transform._31 = x - x*d->transform._11 - y*d->transform._21;
    d->transform._32 = y - x*d->transform._12 - y*d->transform._22;
}

static HRESULT image_get_bounds(struct d2d_device_context *context, ID2D1Image *image,
        D2D1_RECT_F *bounds, unsigned int depth)
{
    struct d2d_effect *effect = d2d_effect_from_image(image);
    struct effect_description d;
    ID2D1CommandList *list;
    ID2D1Bitmap *bitmap;
    D2D1_RECT_F input;
    unsigned int i;
    HRESULT hr;
    if (!image || !bounds) return E_INVALIDARG;
    if (depth >= 32) return D2DERR_CYCLIC_GRAPH;
    memset(bounds, 0, sizeof(*bounds));
    if (!effect)
    {
        if (SUCCEEDED(ID2D1Image_QueryInterface(image, &IID_ID2D1Bitmap, (void **)&bitmap)))
        {
            D2D1_SIZE_F size = ID2D1Bitmap_GetSize(bitmap);
            bounds->right = size.width; bounds->bottom = size.height;
            ID2D1Bitmap_Release(bitmap);
            return S_OK;
        }
        if (SUCCEEDED(ID2D1Image_QueryInterface(image, &IID_ID2D1CommandList, (void **)&list)))
        {
            hr = d2d_command_list_get_bounds(context, unsafe_impl_from_ID2D1CommandList(list), bounds, depth + 1);
            ID2D1CommandList_Release(list);
            return hr;
        }
        return E_NOTIMPL;
    }
    if (FAILED(hr = describe_effect(effect, &d))) return hr;
    if (IsEqualGUID(&d.id,&CLSID_D2D1Turbulence)) { *bounds=d.crop; return S_OK; }
    if (IsEqualGUID(&d.id, &CLSID_D2D1BitmapSource))
    {
        IWICBitmapSource *source;
        UINT width, height;
        double dpi_x = 96, dpi_y = 96;
        if (FAILED(hr = property(effect, 0, &source, sizeof(source)))) return hr;
        if (!source) return D2DERR_WRONG_STATE;
        hr = IWICBitmapSource_GetSize(source, &width, &height);
        if (SUCCEEDED(hr) && d.clamp) hr = IWICBitmapSource_GetResolution(source, &dpi_x, &dpi_y);
        IWICBitmapSource_Release(source);
        if (FAILED(hr)) return hr;
        if (dpi_x <= 0 || dpi_y <= 0) return E_INVALIDARG;
        bounds->right = width * d.transform._11 * 96 / dpi_x;
        bounds->bottom = height * d.transform._22 * 96 / dpi_y;
        if (d.mode >= 5)
        {
            float swap = bounds->right;
            bounds->right = bounds->bottom;
            bounds->bottom = swap;
        }
        return S_OK;
    }
    if (IsEqualGUID(&d.id, &CLSID_D2D1Flood) || IsEqualGUID(&d.id, &CLSID_D2D1Tile)
            || IsEqualGUID(&d.id, &CLSID_D2D1Border))
    {
        bounds->left = bounds->top = -(float)INT_MAX;
        bounds->right = bounds->bottom = (float)INT_MAX;
        return S_OK;
    }
    for (i = 0; i < effect->input_count; ++i)
    {
        if (!effect->inputs[i]) return D2DERR_WRONG_STATE;
        if (FAILED(hr = d2d_image_get_bounds(context, effect->inputs[i], &input, depth + 1))) return hr;
        if(IsEqualGUID(&d.id,&CLSID_D2D1Composite)&&i)
        {
            switch(d.mode)
            {
                case D2D1_COMPOSITE_MODE_SOURCE_IN:
                case D2D1_COMPOSITE_MODE_DESTINATION_IN:
                    bounds->left=max(bounds->left,input.left);bounds->top=max(bounds->top,input.top);
                    bounds->right=min(bounds->right,input.right);bounds->bottom=min(bounds->bottom,input.bottom);
                    if(empty_rect(bounds))memset(bounds,0,sizeof(*bounds));
                    break;
                case D2D1_COMPOSITE_MODE_SOURCE_OUT:
                case D2D1_COMPOSITE_MODE_DESTINATION_ATOP:
                case D2D1_COMPOSITE_MODE_SOURCE_COPY:
                    *bounds=input;
                    break;
                case D2D1_COMPOSITE_MODE_DESTINATION_OUT:
                case D2D1_COMPOSITE_MODE_SOURCE_ATOP:
                    break;
                default:union_rect(bounds,&input);break;
            }
        }
        else union_rect(bounds, &input);
    }
    if (IsEqualGUID(&d.id, &CLSID_D2D1YCbCr))
    {
        if (effect->input_count != 2) return D2DERR_WRONG_STATE;
        if (FAILED(hr = d2d_image_get_bounds(context, effect->inputs[0], bounds, depth+1))) return hr;
        transform_rect(bounds, &d.transform);
    }
    else if (IsEqualGUID(&d.id, &CLSID_D2D1Straighten))
    {
        straighten_transform(&d, bounds);
        if (!d.clamp) transform_rect(bounds, &d.transform);
    }
    else if (IsEqualGUID(&d.id, &CLSID_D2D1Crop) || IsEqualGUID(&d.id, &CLSID_D2D1Atlas))
    {
        bounds->left = max(bounds->left, d.crop.left); bounds->top = max(bounds->top, d.crop.top);
        bounds->right = min(bounds->right, d.crop.right); bounds->bottom = min(bounds->bottom, d.crop.bottom);
        if (empty_rect(bounds)) memset(bounds,0,sizeof(*bounds));
    }
    else if (IsEqualGUID(&d.id, &CLSID_D2D1Scale) || IsEqualGUID(&d.id, &CLSID_D2D12DAffineTransform)
            || IsEqualGUID(&d.id, &CLSID_D2D1DpiCompensation))
        transform_rect(bounds, &d.transform);
    else if ((IsEqualGUID(&d.id, &CLSID_D2D13DTransform)
            || IsEqualGUID(&d.id, &CLSID_D2D13DPerspectiveTransform)) && !empty_rect(bounds))
    {
        D2D1_RECT_F original = *bounds;
        const float *m = d.projection;
        float min_w = FLT_MAX, max_w = -FLT_MAX;

        for (i = 0; i < ARRAY_SIZE(d.projection); ++i)
            if (!isfinite(m[i])) return E_INVALIDARG;
        /* Homogeneous w is affine across the source plane. A plane entirely
         * behind the viewer is empty; one crossing w=0 may project to infinity.
         * Keep those logical bounds unbounded and rasterize only the requested
         * viewport. The inverse projection in the shader rejects w <= 0. */
        for (i = 0; i < 4; ++i)
        {
            float x = i & 1 ? original.right : original.left, y = i & 2 ? original.bottom : original.top;
            float w = m[6] * x + m[7] * y + m[8];
            if (!isfinite(w)) return E_INVALIDARG;
            min_w = min(min_w, w);
            max_w = max(max_w, w);
        }
        if (max_w <= 0)
        {
            memset(bounds, 0, sizeof(*bounds));
            return S_OK;
        }
        if (min_w <= 0)
        {
            bounds->left = bounds->top = -(float)INT_MAX;
            bounds->right = bounds->bottom = (float)INT_MAX;
            return S_OK;
        }
        for (i = 0; i < 4; ++i)
        {
            float x = i & 1 ? original.right : original.left, y = i & 2 ? original.bottom : original.top;
            float w = m[6]*x+m[7]*y+m[8], px, py;
            px=(m[0]*x+m[1]*y+m[2])/w; py=(m[3]*x+m[4]*y+m[5])/w;
            if (!isfinite(px) || !isfinite(py)) return E_INVALIDARG;
            if (!i) { bounds->left=bounds->right=px; bounds->top=bounds->bottom=py; }
            else { bounds->left=min(bounds->left,px); bounds->right=max(bounds->right,px);
                bounds->top=min(bounds->top,py); bounds->bottom=max(bounds->bottom,py); }
        }
    }
    else if (d.sigma && d.border == D2D1_BORDER_MODE_SOFT && !empty_rect(bounds))
    {
        float x = 3 * d.sigma, y = x;
        if (IsEqualGUID(&d.id, &CLSID_D2D1DirectionalBlur))
        {
            x *= fabsf(cosf(d.angle * M_PI / 180)); y *= fabsf(sinf(d.angle * M_PI / 180));
        }
        bounds->left -= x; bounds->right += x; bounds->top -= y; bounds->bottom += y;
    }
    return S_OK;
}

HRESULT d2d_image_get_bounds(struct d2d_device_context *context, ID2D1Image *image,
        D2D1_RECT_F *bounds, unsigned int depth)
{
    struct d2d_effect_bounds_evaluation local = {0}, *evaluation = context->effect_bounds_evaluation;
    size_t index;
    HRESULT hr;
    if (!image || !bounds) return E_INVALIDARG;
    if (!evaluation) context->effect_bounds_evaluation = evaluation = &local;
    for (index = 0; index < evaluation->count; ++index)
    {
        if (evaluation->nodes[index].image != image) continue;
        if (evaluation->nodes[index].visiting) return D2DERR_CYCLIC_GRAPH;
        *bounds = evaluation->nodes[index].bounds;
        return evaluation->nodes[index].status;
    }
    if (evaluation->count >= 4096)
    {
        hr = D2DERR_INSUFFICIENT_DEVICE_CAPABILITIES;
        goto done;
    }
    if (!d2d_array_reserve((void **)&evaluation->nodes, &evaluation->capacity,
            evaluation->count + 1, sizeof(*evaluation->nodes)))
    {
        hr = E_OUTOFMEMORY;
        goto done;
    }
    index = evaluation->count++;
    evaluation->nodes[index].image = image;
    evaluation->nodes[index].visiting = TRUE;
    memset(bounds, 0, sizeof(*bounds));
    hr = image_get_bounds(context, image, bounds, depth);
    evaluation->nodes[index].bounds = *bounds;
    evaluation->nodes[index].status = hr;
    evaluation->nodes[index].visiting = FALSE;
done:
    if (evaluation == &local)
    {
        context->effect_bounds_evaluation = NULL;
        free(local.nodes);
    }
    return hr;
}

static HRESULT create_image(struct d2d_device_context *context, D2D1_RECT_F *bounds, struct d2d_bitmap **bitmap)
{
    D2D1_BITMAP_PROPERTIES1 props = {{DXGI_FORMAT_R16G16B16A16_FLOAT, D2D1_ALPHA_MODE_PREMULTIPLIED},
            0, 0, D2D1_BITMAP_OPTIONS_TARGET, NULL};
    D2D1_SIZE_U size;
    float sx = context->desc.dpiX / 96, sy = context->desc.dpiY / 96;
    props.dpiX = context->desc.dpiX; props.dpiY = context->desc.dpiY;
    if (empty_rect(bounds)) memset(bounds, 0, sizeof(*bounds));
    bounds->left = floorf(bounds->left * sx) / sx; bounds->top = floorf(bounds->top * sy) / sy;
    bounds->right = ceilf(bounds->right * sx) / sx; bounds->bottom = ceilf(bounds->bottom * sy) / sy;
    if (!isfinite(bounds->left) || !isfinite(bounds->right) || !isfinite(bounds->top) || !isfinite(bounds->bottom)
            || (bounds->right - bounds->left) * sx > 16384 || (bounds->bottom - bounds->top) * sy > 16384)
        return D2DERR_EXCEEDS_MAX_BITMAP_SIZE;
    size.width = max(1, (unsigned int)roundf((bounds->right - bounds->left) * sx));
    size.height = max(1, (unsigned int)roundf((bounds->bottom - bounds->top) * sy));
    if (context->effect_image_evaluation)
    {
        size_t bytes = (size_t)size.width * size.height * 8;
        if (bytes > 256 * 1024 * 1024 - context->effect_image_evaluation->allocated)
            return E_OUTOFMEMORY;
        context->effect_image_evaluation->allocated += bytes;
    }
    return d2d_bitmap_create(context, size, NULL, 0, &props, bitmap);
}

static HRESULT create_empty_image(struct d2d_device_context *context,
        struct d2d_bitmap **bitmap, D2D1_RECT_F *bounds)
{
    static const float transparent[4] = {0};
    ID3D11DeviceContext *dc;
    HRESULT hr;

    memset(bounds, 0, sizeof(*bounds));
    if (FAILED(hr = create_image(context, bounds, bitmap))) return hr;
    if (!(*bitmap)->rtv)
    {
        ID2D1Bitmap1_Release(&(*bitmap)->ID2D1Bitmap1_iface);
        *bitmap = NULL;
        return E_FAIL;
    }
    if (context->cs) EnterCriticalSection(context->cs);
    ID3D11Device1_GetImmediateContext(context->d3d_device, &dc);
    ID3D11DeviceContext_ClearRenderTargetView(dc, (*bitmap)->rtv, transparent);
    ID3D11DeviceContext_Release(dc);
    if (context->cs) LeaveCriticalSection(context->cs);
    return S_OK;
}

static void set_rect_constants(float *v, const D2D1_RECT_F *rect)
{
    v[0] = rect->left; v[1] = rect->top;
    v[2] = max(0.00001f, rect->right - rect->left); v[3] = max(0.00001f, rect->bottom - rect->top);
}

static HRESULT create_weights(struct d2d_device_context *context, const float *values,
        UINT32 count, ID3D11ShaderResourceView **view)
{
    D3D11_BUFFER_DESC desc = {0};
    D3D11_SUBRESOURCE_DATA data = {values, 0, 0};
    D3D11_SHADER_RESOURCE_VIEW_DESC srv = {0};
    ID3D11Buffer *buffer;
    HRESULT hr;

    *view = NULL;
    if (!count || count > (1u << D3D11_REQ_BUFFER_RESOURCE_TEXEL_COUNT_2_TO_EXP))
        return E_INVALIDARG;
    /* D3D11's buffer limit and the evaluation allocation budget apply, rather
     * than the old 256-element constant-buffer limit. */
    if (count > UINT_MAX / sizeof(float)) return E_OUTOFMEMORY;
    desc.ByteWidth = count * sizeof(float);
    if (context->effect_image_evaluation)
    {
        if (desc.ByteWidth > 256 * 1024 * 1024 - context->effect_image_evaluation->allocated)
            return E_OUTOFMEMORY;
        context->effect_image_evaluation->allocated += desc.ByteWidth;
    }
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(hr = ID3D11Device1_CreateBuffer(context->d3d_device, &desc, &data, &buffer))) return hr;
    srv.Format = DXGI_FORMAT_R32_FLOAT;
    srv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srv.Buffer.NumElements = count;
    hr = ID3D11Device1_CreateShaderResourceView(context->d3d_device, (ID3D11Resource *)buffer, &srv, view);
    ID3D11Buffer_Release(buffer);
    return hr;
}

static HRESULT prepare_blur(struct d2d_device_context *context, const struct effect_constants *c,
        ID3D11DeviceContext1 *dc)
{
    struct d2d_effect_renderer *r = context->effect_renderer;
    struct blur_constants params = {0};
    D3D11_BUFFER_DESC desc = {sizeof(params), D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER};
    ID3DBlob *code = NULL, *errors = NULL;
    unsigned int i, radius = (unsigned int)c->blur[3];
    float sigma = c->blur[2], normalization = 1;
    HRESULT hr;

    if (!r->blur_ps)
    {
        hr = D3DCompile(blur_ps_code, sizeof(blur_ps_code) - 1, "d2d_blur", NULL, NULL,
                "main", "ps_4_0", 0, 0, &code, &errors);
        if (errors) { WARN("%s\n", (char *)ID3D10Blob_GetBufferPointer(errors)); ID3D10Blob_Release(errors); }
        if (FAILED(hr)) return hr;
        hr = ID3D11Device1_CreatePixelShader(context->d3d_device, ID3D10Blob_GetBufferPointer(code),
                ID3D10Blob_GetBufferSize(code), NULL, &r->blur_ps);
        ID3D10Blob_Release(code);
        if (FAILED(hr)) return hr;
    }
    if (!r->blur_constants && FAILED(hr = ID3D11Device1_CreateBuffer(context->d3d_device,
            &desc, NULL, &r->blur_constants))) return hr;
    memcpy(params.output, c->output, sizeof(params.output));
    memcpy(params.input, c->input, sizeof(params.input));
    memcpy(params.colour, c->colour, sizeof(params.colour));
    params.direction[0] = c->blur[0];
    params.direction[1] = c->blur[1];
    params.shadow = c->op == 2;
    params.ignore_alpha = c->ignore_alpha;
    params.hard_border = c->hard_border;
    if (sigma > 0)
    {
        if ((radius + 1) / 2 > ARRAY_SIZE(params.taps)) return E_INVALIDARG;
        /* Bilinear filtering exactly combines adjacent taps for axis-aligned,
         * texel-aligned Gaussian passes. Compute weights once per draw rather
         * than evaluating exp() at every pixel and every sample. */
        for (i = 1; i <= radius; i += 2)
        {
            float a = expf(-.5f * i * i / (sigma * sigma));
            float b = i < radius ? expf(-.5f * (i + 1) * (i + 1) / (sigma * sigma)) : 0;
            float weight = a + b;
            if (!weight) continue;
            params.taps[params.count][0] = i + b / weight;
            params.taps[params.count++][1] = weight;
            normalization += 2 * weight;
        }
    }
    params.direction[2] = 1 / normalization;
    for (i = 0; i < params.count; ++i) params.taps[i][1] /= normalization;
    ID3D11DeviceContext1_UpdateSubresource(dc, (ID3D11Resource *)r->blur_constants, 0, NULL, &params, 0, 0);
    return S_OK;
}

static HRESULT render_pass(struct d2d_device_context *context, struct d2d_bitmap *output,
        const D2D1_RECT_F *bounds, struct d2d_bitmap *input, const D2D1_RECT_F *input_bounds,
        struct d2d_bitmap *second, const D2D1_RECT_F *second_bounds, struct effect_constants *c, unsigned int sampler)
{
    struct d2d_effect_renderer *r;
    ID3D11DeviceContext1 *dc;
    ID3DDeviceContextState *previous;
    ID3D11ShaderResourceView *views[4] = {input ? input->srv : NULL, second ? second->srv : NULL, c->lut_view, c->weights_view};
    D3D11_VIEWPORT viewport = {0, 0, output->pixel_size.width, output->pixel_size.height, 0, 1};
    D3D11_RECT scissor = {0, 0, output->pixel_size.width, output->pixel_size.height};
    BOOL fast_blur;
    HRESULT hr;
    if ((input && !input->srv) || !output->rtv || (second && !second->srv)) return D2DERR_BITMAP_CANNOT_DRAW;
    if (FAILED(hr = effect_renderer_init(context))) return hr;
    r = context->effect_renderer;
    if (input) set_rect_constants(c->input, input_bounds);
    if (input)
    {
        c->texel[0]=c->input[2]/input->pixel_size.width;
        c->texel[1]=c->input[3]/input->pixel_size.height;
        c->texel[2]=input->pixel_size.width;c->texel[3]=input->pixel_size.height;
    }
    c->hard_border=sampler>=2;
    if (second) set_rect_constants(c->second, second_bounds);
    c->output[0] = bounds->left; c->output[1] = bounds->top;
    c->output[2] = 96 / context->desc.dpiX; c->output[3] = 96 / context->desc.dpiY;
    c->ignore_alpha = input && input->format.alphaMode == D2D1_ALPHA_MODE_IGNORE;
    c->second_ignore_alpha = second && second->format.alphaMode == D2D1_ALPHA_MODE_IGNORE;
    fast_blur = input && (c->op == 1 || c->op == 2) && c->interpolation <= 1
            && sampler != 0 && sampler != 3
            && ((c->blur[0] == 0 && fabsf(c->blur[1] - c->texel[1]) < .00001f)
                || (c->blur[1] == 0 && fabsf(c->blur[0] - c->texel[0]) < .00001f))
            && fabsf(c->output[2] - c->texel[0]) < .00001f
            && fabsf(c->output[3] - c->texel[1]) < .00001f
            && fabsf((bounds->left - input_bounds->left) / c->texel[0]
                - roundf((bounds->left - input_bounds->left) / c->texel[0])) < .0001f
            && fabsf((bounds->top - input_bounds->top) / c->texel[1]
                - roundf((bounds->top - input_bounds->top) / c->texel[1])) < .0001f
            && c->blur[3] <= 2048;
    if (context->cs) EnterCriticalSection(context->cs);
    ID3D11Device1_GetImmediateContext1(context->d3d_device, &dc);
    ID3D11DeviceContext1_SwapDeviceContextState(dc, context->d3d_state, &previous);
    if (fast_blur)
    {
        if (FAILED(hr = prepare_blur(context, c, dc))) goto restore;
    }
    else ID3D11DeviceContext1_UpdateSubresource(dc, (ID3D11Resource *)r->constants, 0, NULL, c, 0, 0);
    ID3D11DeviceContext1_IASetInputLayout(dc, NULL);
    ID3D11DeviceContext1_IASetPrimitiveTopology(dc, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext1_VSSetShader(dc, r->vs, NULL, 0);
    ID3D11DeviceContext1_GSSetShader(dc, NULL, NULL, 0);
    ID3D11DeviceContext1_PSSetShader(dc, fast_blur ? r->blur_ps : r->ps, NULL, 0);
    ID3D11DeviceContext1_PSSetConstantBuffers(dc, 0, 1, fast_blur ? &r->blur_constants : &r->constants);
    ID3D11DeviceContext1_OMSetRenderTargets(dc, 1, &output->rtv, NULL);
    ID3D11DeviceContext1_PSSetShaderResources(dc, 0, ARRAY_SIZE(views), views);
    ID3D11DeviceContext1_PSSetSamplers(dc, 0, 1, &r->samplers[sampler]);
    ID3D11DeviceContext1_RSSetViewports(dc, 1, &viewport);
    ID3D11DeviceContext1_RSSetScissorRects(dc, 1, &scissor);
    ID3D11DeviceContext1_RSSetState(dc, context->rs);
    ID3D11DeviceContext1_OMSetBlendState(dc, NULL, NULL, ~0u);
    ID3D11DeviceContext1_OMSetDepthStencilState(dc, NULL, 0);
    ID3D11DeviceContext1_Draw(dc, 3, 0);
    memset(views, 0, sizeof(views));
    ID3D11DeviceContext1_PSSetShaderResources(dc, 0, ARRAY_SIZE(views), views);
    hr = S_OK;
restore:
    ID3D11DeviceContext1_SwapDeviceContextState(dc, previous, NULL);
    ID3DDeviceContextState_Release(previous);
    ID3D11DeviceContext1_Release(dc);
    if (context->cs) LeaveCriticalSection(context->cs);
    return hr;
}

static HRESULT render_image(struct d2d_device_context *context, ID2D1Image *image,
        struct d2d_bitmap **result, D2D1_RECT_F *bounds, const D2D1_RECT_F *requested, unsigned int depth);

static HRESULT render_histogram(struct d2d_device_context *context, struct d2d_effect *effect, struct d2d_bitmap *input)
{
    D3D11_BUFFER_DESC desc = {0};
    D3D11_UNORDERED_ACCESS_VIEW_DESC view = {0};
    ID3D11Buffer *counts = NULL, *staging = NULL, *constants = NULL;
    ID3D11UnorderedAccessView *uav = NULL, *null_uav = NULL;
    ID3D11ShaderResourceView *null_srv = NULL;
    ID3D11DeviceContext1 *dc;
    ID3DDeviceContextState *previous;
    ID3DBlob *code, *errors = NULL;
    D3D11_MAPPED_SUBRESOURCE map;
    UINT params[8] = {input->pixel_size.width,input->pixel_size.height}, zeros[4] = {0};
    float *values = NULL;
    unsigned int i;
    HRESULT hr;
    if (FAILED(hr = property(effect, 0, &params[2], sizeof(UINT)))) return hr;
    if (FAILED(hr = property(effect, 1, &params[3], sizeof(UINT)))) return hr;
    if (!input->srv || params[2] < 2 || params[2] > 1024 || params[3] > 3) return E_INVALIDARG;
    params[4] = input->format.alphaMode == D2D1_ALPHA_MODE_IGNORE;
    if (FAILED(hr = effect_renderer_init(context))) return hr;
    if (!context->effect_renderer->histogram)
    {
        hr = D3DCompile(histogram_cs, sizeof(histogram_cs)-1, "histogram", NULL, NULL, "main", "cs_5_0", 0, 0, &code, &errors);
        if (errors) { WARN("%s\n", (char *)ID3D10Blob_GetBufferPointer(errors)); ID3D10Blob_Release(errors); }
        if (FAILED(hr)) return hr;
        hr = ID3D11Device1_CreateComputeShader(context->d3d_device, ID3D10Blob_GetBufferPointer(code),
                ID3D10Blob_GetBufferSize(code), NULL, &context->effect_renderer->histogram);
        ID3D10Blob_Release(code);
        if (FAILED(hr)) return hr;
    }
    desc.ByteWidth = params[2]*sizeof(UINT); desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    if (FAILED(hr = ID3D11Device1_CreateBuffer(context->d3d_device, &desc, NULL, &counts))) goto done;
    view.Format = DXGI_FORMAT_R32_UINT; view.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    view.Buffer.NumElements = params[2];
    if (FAILED(hr = ID3D11Device1_CreateUnorderedAccessView(context->d3d_device, (ID3D11Resource *)counts, &view, &uav))) goto done;
    desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(hr = ID3D11Device1_CreateBuffer(context->d3d_device, &desc, NULL, &staging))) goto done;
    desc.ByteWidth = sizeof(params); desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER; desc.CPUAccessFlags = 0;
    if (FAILED(hr = ID3D11Device1_CreateBuffer(context->d3d_device, &desc, NULL, &constants))) goto done;
    if (!(values = malloc(params[2]*sizeof(float)))) { hr = E_OUTOFMEMORY; goto done; }
    if (context->cs) EnterCriticalSection(context->cs);
    ID3D11Device1_GetImmediateContext1(context->d3d_device, &dc);
    ID3D11DeviceContext1_SwapDeviceContextState(dc, context->d3d_state, &previous);
    ID3D11DeviceContext1_ClearUnorderedAccessViewUint(dc, uav, zeros);
    ID3D11DeviceContext1_UpdateSubresource(dc, (ID3D11Resource *)constants, 0, NULL, params, 0, 0);
    ID3D11DeviceContext1_OMSetRenderTargets(dc, 0, NULL, NULL);
    ID3D11DeviceContext1_CSSetShader(dc, context->effect_renderer->histogram, NULL, 0);
    ID3D11DeviceContext1_CSSetConstantBuffers(dc, 0, 1, &constants);
    ID3D11DeviceContext1_CSSetShaderResources(dc, 0, 1, &input->srv);
    ID3D11DeviceContext1_CSSetUnorderedAccessViews(dc, 0, 1, &uav, NULL);
    ID3D11DeviceContext1_Dispatch(dc, (params[0]+15)/16, (params[1]+15)/16, 1);
    ID3D11DeviceContext1_CSSetUnorderedAccessViews(dc, 0, 1, &null_uav, NULL);
    ID3D11DeviceContext1_CSSetShaderResources(dc, 0, 1, &null_srv);
    ID3D11DeviceContext1_CopyResource(dc, (ID3D11Resource *)staging, (ID3D11Resource *)counts);
    hr = ID3D11DeviceContext1_Map(dc, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &map);
    if (SUCCEEDED(hr))
    {
        for (i = 0; i < params[2]; ++i) values[i] = ((UINT *)map.pData)[i] / ((float)params[0]*params[1]);
        ID3D11DeviceContext1_Unmap(dc, (ID3D11Resource *)staging, 0);
        hr = d2d_effect_set_histogram(effect, values, params[2]);
    }
    ID3D11DeviceContext1_SwapDeviceContextState(dc, previous, NULL);
    ID3DDeviceContextState_Release(previous); ID3D11DeviceContext1_Release(dc);
    if (context->cs) LeaveCriticalSection(context->cs);
done:
    free(values);
    if (uav) ID3D11UnorderedAccessView_Release(uav);
    if (constants) ID3D11Buffer_Release(constants);
    if (counts) ID3D11Buffer_Release(counts);
    if (staging) ID3D11Buffer_Release(staging);
    return hr;
}

static HRESULT render_image_internal(struct d2d_device_context *context, ID2D1Image *image,
        struct d2d_bitmap **result, D2D1_RECT_F *bounds, const D2D1_RECT_F *requested, unsigned int depth)
{
    struct d2d_effect *effect = d2d_effect_from_image(image);
    struct d2d_bitmap *input = NULL, *output = NULL, *temp = NULL, *next = NULL;
    struct effect_description d;
    struct effect_constants c = {0};
    D2D1_RECT_F input_bounds, temp_bounds, next_bounds;
    ID2D1CommandList *list;
    ID2D1Bitmap *bitmap;
    unsigned int i, sampler;
    HRESULT hr;
    *result = NULL;
    if (FAILED(hr = d2d_image_get_bounds(context, image, bounds, depth))) return hr;
    /* A collapsed animation frame has no coverage. Do not invert its singular
     * transform and poison the enclosing draw session with E_INVALIDARG. */
    if (empty_rect(bounds)) return create_empty_image(context, result, bounds);
    if (bounds->left <= -(float)INT_MAX || bounds->top <= -(float)INT_MAX
            || bounds->right >= (float)INT_MAX || bounds->bottom >= (float)INT_MAX)
    {
        bounds->left = max(bounds->left, requested->left);
        bounds->top = max(bounds->top, requested->top);
        bounds->right = min(bounds->right, requested->right);
        bounds->bottom = min(bounds->bottom, requested->bottom);
    }
    if (!effect)
    {
        if (SUCCEEDED(ID2D1Image_QueryInterface(image, &IID_ID2D1Bitmap, (void **)&bitmap)))
        {
            *result = unsafe_impl_from_ID2D1Bitmap(bitmap);
            return S_OK;
        }
        if (FAILED(hr = ID2D1Image_QueryInterface(image, &IID_ID2D1CommandList, (void **)&list))) return hr;
        if (SUCCEEDED(hr = create_image(context, bounds, &output)))
            hr = d2d_device_context_rasterize_command_list(context, list, output, bounds);
        ID2D1CommandList_Release(list);
        if (FAILED(hr)) { if (output) ID2D1Bitmap1_Release(&output->ID2D1Bitmap1_iface); return hr; }
        *result = output;
        return S_OK;
    }
    if (FAILED(hr = describe_effect(effect, &d))) return hr;
    if (IsEqualGUID(&d.id,&CLSID_D2D1Turbulence))
    {
        UINT32 seed, octaves, noise;
        BOOL stitch;
        INT64 random;
        unsigned int channel,j,permutation[256];
        float size_x=bounds->right-bounds->left,size_y=bounds->bottom-bounds->top;
        if(FAILED(hr=property(effect,2,c.blur,2*sizeof(float))))return hr;
        if(FAILED(hr=property(effect,3,&octaves,sizeof(octaves))))return hr;
        if(FAILED(hr=property(effect,4,&seed,sizeof(seed))))return hr;
        if(FAILED(hr=property(effect,5,&noise,sizeof(noise))))return hr;
        if(FAILED(hr=property(effect,6,&stitch,sizeof(stitch))))return hr;
        if(!octaves||octaves>16||noise>1||c.blur[0]<=0||c.blur[1]<=0)return E_INVALIDARG;
        random=(INT32)seed;
        if(random<=0)random=-(random%2147483646)+1;
        if(random>2147483646)random=2147483646;
        for(channel=0;channel<4;++channel)for(i=0;i<256;++i)
        {
            float x,y,length;
            random=(random*16807)%2147483647; x=((int)(random%512)-256)/256.0f;
            random=(random*16807)%2147483647; y=((int)(random%512)-256)/256.0f;
            length=sqrtf(x*x+y*y);
            c.noise_x[i][channel]=length?x/length:1;
            c.noise_y[i][channel]=length?y/length:0;
            permutation[i]=i;
        }
        for(i=255;i;i--)
        {
            unsigned int swap;
            random=(random*16807)%2147483647;j=random%256;
            swap=permutation[i];permutation[i]=permutation[j];permutation[j]=swap;
        }
        for(i=0;i<256;++i)c.tables[i][0]=permutation[i];
        if(stitch)
        {
            float size[2]={size_x,size_y};
            for(i=0;i<2;++i)
            {
                float lo=floorf(size[i]*c.blur[i])/max(size[i],.00001f),hi=ceilf(size[i]*c.blur[i])/max(size[i],.00001f);
                c.blur[i]=lo>0&&c.blur[i]/lo<hi/c.blur[i]?lo:hi;
                c.clip[2+i]=roundf(size[i]*c.blur[i]);
            }
        }
        c.op=39;c.composite=octaves;c.straight_alpha=noise;
        if(FAILED(hr=create_image(context,bounds,&output)))return hr;
        hr=render_pass(context,output,bounds,NULL,NULL,NULL,NULL,&c,1);
        goto done;
    }
    if (IsEqualGUID(&d.id, &CLSID_D2D1BitmapSource))
    {
        D2D1_BITMAP_PROPERTIES1 props = {{DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED},96,96,0,NULL};
        IWICBitmapSource *source;
        if (FAILED(hr = property(effect, 0, &source, sizeof(source)))) return hr;
        if (!source) return D2DERR_WRONG_STATE;
        if (d.alpha == 2) props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_IGNORE;
        hr = d2d_bitmap_create_from_wic_bitmap(context, source, &props, &input);
        IWICBitmapSource_Release(source);
        if (FAILED(hr)) return hr;
        input_bounds = (D2D1_RECT_F){0,0,input->pixel_size.width,input->pixel_size.height};
        if (FAILED(hr = create_image(context, bounds, &output))) goto done;
        /* Inverse EXIF orientation, applied after scaling in the source axes. */
        switch (d.mode)
        {
            case 1: c.row_x[0] = 1; c.row_y[1] = 1; break;
            case 2: c.row_x[0] = -1; c.row_x[2] = 1; c.row_y[1] = 1; break;
            case 3: c.row_x[0] = -1; c.row_x[2] = 1; c.row_y[1] = -1; c.row_y[2] = 1; break;
            case 4: c.row_x[0] = 1; c.row_y[1] = -1; c.row_y[2] = 1; break;
            case 5: c.row_x[1] = 1; c.row_y[0] = 1; break;
            case 6: c.row_x[1] = 1; c.row_y[0] = -1; c.row_y[2] = 1; break;
            case 7: c.row_x[1] = -1; c.row_x[2] = 1; c.row_y[0] = -1; c.row_y[2] = 1; break;
            case 8: c.row_x[1] = -1; c.row_x[2] = 1; c.row_y[0] = 1; break;
        }
        c.row_x[0] *= input_bounds.right / max(.00001f, bounds->right);
        c.row_x[1] *= input_bounds.right / max(.00001f, bounds->bottom);
        c.row_x[2] *= input_bounds.right;
        c.row_y[0] *= input_bounds.bottom / max(.00001f, bounds->right);
        c.row_y[1] *= input_bounds.bottom / max(.00001f, bounds->bottom);
        c.row_y[2] *= input_bounds.bottom;
        c.interpolation = d.interpolation;
        hr = render_pass(context, output, bounds, input, &input_bounds, NULL, NULL, &c, d.interpolation ? 1 : 0);
        goto done;
    }
    if (IsEqualGUID(&d.id, &CLSID_D2D1Flood))
    {
        if (FAILED(hr = create_image(context, bounds, &output))) return hr;
        c.op = 9;
        memcpy(c.colour, &d.colour, sizeof(c.colour));
        hr = render_pass(context, output, bounds, NULL, NULL, NULL, NULL, &c, 1);
        goto done;
    }
    if (!effect->input_count) return D2DERR_WRONG_STATE;
    TRACE("effect %p, id %s, depth %u, bounds %s.\n", effect, debugstr_guid(&d.id), depth, debug_d2d_rect_f(bounds));
    temp_bounds = *bounds;
    if (IsEqualGUID(&d.id, &CLSID_D2D1Scale) || IsEqualGUID(&d.id, &CLSID_D2D12DAffineTransform)
            || IsEqualGUID(&d.id, &CLSID_D2D1DpiCompensation))
    {
        D2D1_MATRIX_3X2_F inverse = d.transform;
        if (!D2D1InvertMatrix(&inverse)) return E_INVALIDARG;
        transform_rect(&temp_bounds, &inverse);
    }
    if (d.sigma)
    {
        temp_bounds.left -= 3 * d.sigma; temp_bounds.right += 3 * d.sigma;
        temp_bounds.top -= 3 * d.sigma; temp_bounds.bottom += 3 * d.sigma;
    }
    if (FAILED(hr = render_image(context, effect->inputs[0], &input, &input_bounds, &temp_bounds, depth + 1))) return hr;
    if (IsEqualGUID(&d.id, &CLSID_D2D1EdgeDetection) && d.sigma > 0)
    {
        struct d2d_bitmap *horizontal = NULL, *vertical = NULL;
        struct effect_constants pass = {0};
        D2D1_RECT_F allocation = input_bounds;

        hr = create_image(context, &allocation, &horizontal);
        if (SUCCEEDED(hr)) hr = create_image(context, &allocation, &vertical);
        pass.row_x[0] = pass.row_y[1] = 1;
        pass.op = 1;
        pass.blur[0] = 96 / context->desc.dpiX;
        pass.blur[2] = d.sigma * context->desc.dpiX / 96;
        pass.blur[3] = ceilf(3 * pass.blur[2]);
        if (SUCCEEDED(hr)) hr = render_pass(context, horizontal, &allocation, input, &input_bounds,
                NULL, NULL, &pass, 2);
        pass.blur[0] = 0;
        pass.blur[1] = 96 / context->desc.dpiY;
        pass.blur[2] = d.sigma * context->desc.dpiY / 96;
        pass.blur[3] = ceilf(3 * pass.blur[2]);
        if (SUCCEEDED(hr)) hr = render_pass(context, vertical, &allocation, horizontal, &allocation,
                NULL, NULL, &pass, 2);
        if (horizontal) ID2D1Bitmap1_Release(&horizontal->ID2D1Bitmap1_iface);
        if (FAILED(hr))
        {
            if (vertical) ID2D1Bitmap1_Release(&vertical->ID2D1Bitmap1_iface);
            goto done;
        }
        ID2D1Bitmap1_Release(&input->ID2D1Bitmap1_iface);
        input = vertical;
        input_bounds = allocation;
    }
    if (IsEqualGUID(&d.id, &CLSID_D2D1Straighten)) straighten_transform(&d, &input_bounds);
    if (IsEqualGUID(&d.id, &CLSID_D2D1Histogram))
    {
        hr = render_histogram(context, effect, input);
        if (SUCCEEDED(hr)) { output = input; input = NULL; *bounds = input_bounds; }
        goto done;
    }
    c.row_x[0] = c.row_y[1] = 1;
    c.interpolation=d.interpolation;
    sampler = d.interpolation == D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR ? 0 : 1;
    if (d.border == D2D1_BORDER_MODE_HARD) sampler = 2;
    if(IsEqualGUID(&d.id,&CLSID_D2D1ColorManagement)&&(d.colour.x==0||d.colour.y==0))
    {
        ID2D1ColorContext *source=NULL,*destination=NULL;
        ID2D1LookupTable3D *iface=NULL;
        struct d2d_lookup_table *lut;
        UINT32 source_intent,destination_intent;
        if(FAILED(hr=property(effect,0,&source,sizeof(source))))goto done;
        if(SUCCEEDED(hr=property(effect,2,&destination,sizeof(destination)))
                &&SUCCEEDED(hr=property(effect,1,&source_intent,sizeof(UINT)))
                &&SUCCEEDED(hr=property(effect,3,&destination_intent,sizeof(UINT))))
            hr=d2d_color_transform_lut(context,source,destination,source_intent,destination_intent,&iface);
        if(source)ID2D1ColorContext_Release(source);
        if(destination)ID2D1ColorContext_Release(destination);
        if(FAILED(hr))goto done;
        lut=d2d_lookup_table_from_iface(iface);
        c.op=37;c.lut_view=lut->view;c.straight_alpha=d.alpha==1;
        for(i=0;i<3;++i)c.colour[i]=lut->extents[i];
        if(SUCCEEDED(hr=create_image(context,bounds,&output)))
            hr=render_pass(context,output,bounds,input,&input_bounds,NULL,NULL,&c,2);
        ID2D1LookupTable3D_Release(iface);
        goto done;
    }
    if(IsEqualGUID(&d.id,&CLSID_D2D1HighlightsShadows))
    {
        struct d2d_bitmap *horizontal=NULL,*vertical=NULL,*mask_image=NULL,*straight=NULL;
        struct effect_constants pass={0};
        D2D1_RECT_F mask_bounds=input_bounds;
        if(FAILED(hr=create_image(context,&mask_bounds,&straight)))goto done;
        pass.row_x[0]=pass.row_y[1]=1;pass.op=6;
        hr=render_pass(context,straight,&mask_bounds,input,&input_bounds,NULL,NULL,&pass,2);
        if(SUCCEEDED(hr)&&d.angle>0)
        {
            if(SUCCEEDED(hr=create_image(context,&mask_bounds,&horizontal)))
                hr=create_image(context,&mask_bounds,&vertical);
            if(SUCCEEDED(hr))hr=create_image(context,&mask_bounds,&mask_image);
            if(SUCCEEDED(hr))
            {
                pass.op=1;pass.blur[0]=96/context->desc.dpiX;
                pass.blur[2]=d.angle*context->desc.dpiX/96;pass.blur[3]=ceilf(3*pass.blur[2]);
                hr=render_pass(context,horizontal,&mask_bounds,input,&input_bounds,NULL,NULL,&pass,2);
            }
            if(SUCCEEDED(hr))
            {
                pass.blur[0]=0;pass.blur[1]=96/context->desc.dpiY;
                pass.blur[2]=d.angle*context->desc.dpiY/96;pass.blur[3]=ceilf(3*pass.blur[2]);
                hr=render_pass(context,vertical,&mask_bounds,horizontal,&mask_bounds,NULL,NULL,&pass,2);
            }
            if(SUCCEEDED(hr))
            {
                pass.op=6;
                hr=render_pass(context,horizontal,&mask_bounds,vertical,&mask_bounds,NULL,NULL,&pass,2);
            }
            if(SUCCEEDED(hr))
            {
                pass.op=45;pass.blur[0]=96/context->desc.dpiX;pass.blur[1]=96/context->desc.dpiY;
                pass.colour[0]=d.angle*d.angle*.5f*M_PI*.7f;
                hr=render_pass(context,mask_image,&mask_bounds,horizontal,&mask_bounds,NULL,NULL,&pass,2);
            }
        }
        if(SUCCEEDED(hr))hr=create_image(context,bounds,&output);
        if(SUCCEEDED(hr))
        {
            c.op=44;memcpy(c.colour,&d.colour,sizeof(c.colour));c.straight_alpha=d.alpha;
            hr=render_pass(context,output,bounds,straight,&input_bounds,mask_image?mask_image:straight,&mask_bounds,&c,2);
        }
        if(horizontal)ID2D1Bitmap1_Release(&horizontal->ID2D1Bitmap1_iface);
        if(vertical)ID2D1Bitmap1_Release(&vertical->ID2D1Bitmap1_iface);
        if(mask_image)ID2D1Bitmap1_Release(&mask_image->ID2D1Bitmap1_iface);
        ID2D1Bitmap1_Release(&straight->ID2D1Bitmap1_iface);
        goto done;
    }
    if (IsEqualGUID(&d.id, &CLSID_D2D1LookupTable3D))
    {
        ID2D1LookupTable3D *iface;
        struct d2d_lookup_table *lut;
        if (FAILED(hr=property(effect,0,&iface,sizeof(iface)))) goto done;
        if (!(lut=d2d_lookup_table_from_iface(iface)))
        { if (iface) ID2D1LookupTable3D_Release(iface); hr=E_INVALIDARG; goto done; }
        if (SUCCEEDED(hr=property(effect,1,&c.straight_alpha,sizeof(UINT))))
        {
            c.straight_alpha=c.straight_alpha==D2D1_ALPHA_MODE_PREMULTIPLIED;
            c.op=37; c.lut_view=lut->view;
            for (i=0;i<3;++i)c.colour[i]=lut->extents[i];
            if (SUCCEEDED(hr=create_image(context,bounds,&output)))
                hr=render_pass(context,output,bounds,input,&input_bounds,NULL,NULL,&c,2);
        }
        ID2D1LookupTable3D_Release(iface);
        goto done;
    }
    if (IsEqualGUID(&d.id, &CLSID_D2D1AlphaMask) || IsEqualGUID(&d.id, &CLSID_D2D1CrossFade)
            || IsEqualGUID(&d.id, &CLSID_D2D1ArithmeticComposite) || IsEqualGUID(&d.id, &CLSID_D2D1DisplacementMap)
            || IsEqualGUID(&d.id, &CLSID_D2D1Blend) || IsEqualGUID(&d.id, &CLSID_D2D1YCbCr))
    {
        if (effect->input_count != 2) { hr = D2DERR_WRONG_STATE; goto done; }
        if (FAILED(hr = render_image(context, effect->inputs[1], &next, &next_bounds, bounds, depth + 1))) goto done;
        if (FAILED(hr = create_image(context, bounds, &output))) goto done;
        c.op = IsEqualGUID(&d.id, &CLSID_D2D1AlphaMask) ? 13 : IsEqualGUID(&d.id, &CLSID_D2D1CrossFade) ? 14 : 15;
        if (IsEqualGUID(&d.id, &CLSID_D2D1DisplacementMap)) c.op = 26;
        if (IsEqualGUID(&d.id, &CLSID_D2D1Blend)) { c.op = 30; c.composite = d.mode; }
        if (IsEqualGUID(&d.id, &CLSID_D2D1YCbCr))
        {
            if (input->format.format != DXGI_FORMAT_R8_UNORM || next->format.format != DXGI_FORMAT_R8G8_UNORM)
            { hr = D2DERR_UNSUPPORTED_PIXEL_FORMAT; goto done; }
            if (!D2D1InvertMatrix(&d.transform)) { hr = E_INVALIDARG; goto done; }
            c.op = 36;
            c.row_x[0]=d.transform._11; c.row_x[1]=d.transform._21; c.row_x[2]=d.transform._31;
            c.row_y[0]=d.transform._12; c.row_y[1]=d.transform._22; c.row_y[2]=d.transform._32;
        }
        memcpy(c.colour, &d.colour, sizeof(c.colour));
        c.clamp_output = d.clamp;
        hr = render_pass(context, output, bounds, input, &input_bounds, next, &next_bounds, &c,
                IsEqualGUID(&d.id, &CLSID_D2D1YCbCr) ? 2 : 1);
        goto done;
    }
    if (IsEqualGUID(&d.id, &CLSID_D2D1Composite))
    {
        for (i = 1; i < effect->input_count; ++i)
        {
            if (FAILED(hr = render_image(context, effect->inputs[i], &next, &next_bounds, bounds, depth + 1))) goto done;
            temp_bounds = *bounds;
            if (FAILED(hr = create_image(context, &temp_bounds, &output))) goto done;
            c.op = 4; c.composite = d.mode;
            hr = render_pass(context, output, &temp_bounds, next, &next_bounds, input, &input_bounds, &c, 1);
            if (FAILED(hr)) goto done;
            ID2D1Bitmap1_Release(&input->ID2D1Bitmap1_iface);
            ID2D1Bitmap1_Release(&next->ID2D1Bitmap1_iface); next = NULL;
            input = output; output = NULL; input_bounds = temp_bounds;
        }
        output = input; input = NULL; *bounds = input_bounds;
        goto done;
    }
    if (FAILED(hr = create_image(context, bounds, &output))) goto done;
    if (IsEqualGUID(&d.id, &CLSID_D2D1Scale) || IsEqualGUID(&d.id, &CLSID_D2D12DAffineTransform)
            || IsEqualGUID(&d.id, &CLSID_D2D1DpiCompensation) || IsEqualGUID(&d.id, &CLSID_D2D1Straighten))
    {
        if (!D2D1InvertMatrix(&d.transform)) { hr = E_INVALIDARG; goto done; }
        c.row_x[0] = d.transform._11; c.row_x[1] = d.transform._21; c.row_x[2] = d.transform._31;
        c.row_y[0] = d.transform._12; c.row_y[1] = d.transform._22; c.row_y[2] = d.transform._32;
    }
    if (IsEqualGUID(&d.id, &CLSID_D2D1ColorMatrix) || IsEqualGUID(&d.id, &CLSID_D2D1Saturation)
            || IsEqualGUID(&d.id, &CLSID_D2D1Grayscale) || IsEqualGUID(&d.id, &CLSID_D2D1HueRotation)
            || IsEqualGUID(&d.id, &CLSID_D2D1Sepia) || IsEqualGUID(&d.id,&CLSID_D2D1TemperatureTint))
    {
        c.op = 3; memcpy(c.matrix, &d.matrix, sizeof(c.matrix));
        /* PREMULTIPLIED means unpremultiply before the matrix, then premultiply
         * its output. STRAIGHT applies the matrix directly to the channels. */
        c.straight_alpha = d.alpha == D2D1_COLORMATRIX_ALPHA_MODE_PREMULTIPLIED;
        c.clamp_output = d.clamp;
    }
    if (IsEqualGUID(&d.id, &CLSID_D2D1Premultiply)) c.op = 5;
    if (IsEqualGUID(&d.id, &CLSID_D2D1UnPremultiply)) c.op = 6;
    if (IsEqualGUID(&d.id, &CLSID_D2D1LuminanceToAlpha)) c.op = 7;
    if (IsEqualGUID(&d.id, &CLSID_D2D1Brightness)) { c.op = 8; memcpy(c.colour, &d.colour, sizeof(c.colour)); }
    if (IsEqualGUID(&d.id, &CLSID_D2D1Crop) || IsEqualGUID(&d.id, &CLSID_D2D1Atlas))
    { c.op = 16; memcpy(c.clip, &d.crop, sizeof(c.clip)); }
    if (IsEqualGUID(&d.id, &CLSID_D2D1Invert)) c.op = 10;
    if (IsEqualGUID(&d.id, &CLSID_D2D1Opacity)) { c.op = 11; c.colour[0] = d.colour.x; }
    if (IsEqualGUID(&d.id, &CLSID_D2D1Exposure)) { c.op = 12; c.colour[0] = d.colour.x; }
    if (IsEqualGUID(&d.id, &CLSID_D2D1LinearTransfer) || IsEqualGUID(&d.id, &CLSID_D2D1GammaTransfer))
    {
        c.op = IsEqualGUID(&d.id, &CLSID_D2D1LinearTransfer) ? 17 : 18;
        memcpy(c.matrix, &d.matrix, sizeof(c.matrix));
        c.clamp_output = d.clamp;
    }
    if (IsEqualGUID(&d.id, &CLSID_D2D1TableTransfer) || IsEqualGUID(&d.id, &CLSID_D2D1DiscreteTransfer))
    {
        unsigned int channel;
        UINT32 sizes[4], count = 0;
        float *values;
        c.op = IsEqualGUID(&d.id, &CLSID_D2D1TableTransfer) ? 19 : 20;
        for (channel = 0; channel < 4; ++channel)
        {
            UINT32 size = ID2D1Effect_GetValueSize(&effect->ID2D1Effect_iface, channel * 2);
            if (!size || size % sizeof(float)) { hr = E_INVALIDARG; goto done; }
            if (size > 256 * 1024 * 1024 - count * sizeof(float)) { hr = E_OUTOFMEMORY; goto done; }
            sizes[channel] = size;
            c.table_offsets[channel] = count;
            c.table_sizes[channel] = size / sizeof(float);
            count += c.table_sizes[channel];
            if (FAILED(hr = property(effect, channel * 2 + 1, &c.table_disabled[channel], sizeof(UINT)))) goto done;
        }
        if (!(values = malloc(count * sizeof(float)))) { hr = E_OUTOFMEMORY; goto done; }
        for (channel = 0; channel < 4; ++channel)
            if (FAILED(hr = property(effect, channel * 2, values + c.table_offsets[channel], sizes[channel]))) break;
        if (SUCCEEDED(hr)) hr = create_weights(context, values, count, &c.weights_view);
        free(values);
        if (FAILED(hr)) goto done;
        if (FAILED(hr = property(effect, 8, &c.clamp_output, sizeof(c.clamp_output)))) goto done;
    }
    if (IsEqualGUID(&d.id, &CLSID_D2D1Morphology))
    {
        c.op = 21;
        memcpy(c.colour, &d.colour, sizeof(c.colour));
        c.blur[0] = 96 / context->desc.dpiX; c.blur[1] = 96 / context->desc.dpiY;
        sampler = 0;
    }
    if (IsEqualGUID(&d.id, &CLSID_D2D1Tile))
    {
        c.op = 22; memcpy(c.clip, &d.crop, sizeof(c.clip)); sampler = 0;
    }
    if (IsEqualGUID(&d.id, &CLSID_D2D1Border))
    {
        c.op = 23; memcpy(c.colour, &d.colour, sizeof(c.colour)); sampler = 2;
    }
    if (IsEqualGUID(&d.id, &CLSID_D2D1Tint)) { c.op = 24; memcpy(c.colour, &d.colour, sizeof(c.colour)); c.clamp_output = d.clamp; }
    if (IsEqualGUID(&d.id, &CLSID_D2D1Posterize)) { c.op = 25; memcpy(c.colour, &d.colour, sizeof(c.colour)); }
    if (IsEqualGUID(&d.id, &CLSID_D2D1RgbToHue)) { c.op = 28; c.colour[0] = d.mode; }
    if (IsEqualGUID(&d.id, &CLSID_D2D1HueToRgb)) { c.op = 29; c.colour[0] = d.mode; }
    if (IsEqualGUID(&d.id, &CLSID_D2D13DTransform) || IsEqualGUID(&d.id, &CLSID_D2D13DPerspectiveTransform))
    {
        float inverse[9];
        if (!invert_projection(d.projection, inverse)) { hr = E_INVALIDARG; goto done; }
        c.op = 31;
        memcpy(c.row_x, inverse, 3*sizeof(float));
        memcpy(c.row_y, inverse+3, 3*sizeof(float));
        memcpy(c.blur, inverse+6, 3*sizeof(float));
    }
    if (d.mode == 32)
    {
        c.op = 32;
        memcpy(c.matrix, &d.matrix, sizeof(c.matrix));
        memcpy(c.colour, &d.colour, sizeof(c.colour));
        memcpy(c.blur, c.matrix[3], 3*sizeof(float));
        c.straight_alpha = d.alpha;
    }
    if (IsEqualGUID(&d.id, &CLSID_D2D1ChromaKey))
    {
        c.op = 33; memcpy(c.colour, &d.colour, sizeof(c.colour));
        c.straight_alpha = d.alpha; c.clamp_output = d.clamp;
    }
    if (IsEqualGUID(&d.id, &CLSID_D2D1WhiteLevelAdjustment)) { c.op = 34; c.colour[0] = d.colour.x; }
    if (IsEqualGUID(&d.id, &CLSID_D2D1Contrast)) { c.op = 35; c.colour[0] = d.colour.x; c.clamp_output = d.clamp; }
    if (IsEqualGUID(&d.id, &CLSID_D2D1ColorManagement))
    {
        c.op=38; memcpy(c.colour,&d.colour,sizeof(c.colour)); c.straight_alpha=d.alpha==1;
    }
    if(IsEqualGUID(&d.id,&CLSID_D2D1Sharpen)||IsEqualGUID(&d.id,&CLSID_D2D1Emboss)||IsEqualGUID(&d.id,&CLSID_D2D1EdgeDetection))
    {
        c.op=IsEqualGUID(&d.id,&CLSID_D2D1Sharpen)?40:IsEqualGUID(&d.id,&CLSID_D2D1Emboss)?41:42;
        memcpy(c.colour,&d.colour,sizeof(c.colour)); c.clamp_output=d.clamp;
        c.blur[0]=96/context->desc.dpiX;c.blur[1]=96/context->desc.dpiY;sampler=2;
    }
    if(IsEqualGUID(&d.id,&CLSID_D2D1Vignette))
    {
        c.op=43;memcpy(c.colour,&d.colour,sizeof(c.colour));
        c.blur[0]=.5f*min(input_bounds.right-input_bounds.left,input_bounds.bottom-input_bounds.top)*d.angle;
        c.blur[1]=.75f*(1-d.sigma);
    }
    if(IsEqualGUID(&d.id,&CLSID_D2D1HdrToneMap))
    {
        static const float rgb_xyz[9]={.4123908f,.35758434f,.1804808f,.21263901f,.71516868f,.07219232f,.01933082f,.11919478f,.95053215f};
        static const float xyz_lms[9]={.4002f,.7075f,-.0807f,-.228f,1.15f,.0612f,0,0,.9184f};
        float forward[9]={0},inverse[9],input=d.colour.x,output=d.colour.y,exponent;
        unsigned int row,col,k;
        if(d.mode==0&&input<=160){input=max(4000,input);output=max(150,output*.75f);}
        exponent=.58f/((logf(318)/logf(input))*(1.25f-(20/input)*1.35869563f));
        if(!isfinite(exponent)||exponent<=0){hr=E_INVALIDARG;goto done;}
        for(row=0;row<3;++row)for(col=0;col<3;++col)for(k=0;k<3;++k)
            forward[row*3+col]+=xyz_lms[row*3+k]*rgb_xyz[k*3+col];
        if(!invert_projection(forward,inverse)){hr=E_FAIL;goto done;}
        for(row=0;row<3;++row)for(col=0;col<3;++col)
        {
            c.noise_x[row][col]=forward[row*3+col]*80/input;
            c.noise_y[row][col]=inverse[row*3+col]*input/(output/d.colour.y*80);
        }
        c.op=46;c.colour[0]=input;c.colour[1]=output;c.colour[2]=exponent;
        c.blur[0]=hdr_luminance_intensity(output*.6f,input,exponent)-.05f;
        c.blur[1]=d.mode?0:hdr_luminance_intensity(output*.1f,input,exponent);
        c.blur[2]=hdr_luminance_intensity(input,input,exponent);
        c.blur[3]=hdr_luminance_intensity(output,input,exponent);
    }
    if (IsEqualGUID(&d.id, &CLSID_D2D1ConvolveMatrix))
    {
        float *values;
        UINT32 size, mode, border;
        c.op = 27;
        if (FAILED(hr = property(effect, 0, c.blur, 2 * sizeof(float)))) goto done;
        if (FAILED(hr = property(effect, 1, &mode, sizeof(mode)))) goto done;
        if (FAILED(hr = property(effect, 2, &c.table_sizes[0], sizeof(UINT)))) goto done;
        if (FAILED(hr = property(effect, 3, &c.table_sizes[1], sizeof(UINT)))) goto done;
        if (!c.table_sizes[0] || !c.table_sizes[1] || c.table_sizes[0] > 100 || c.table_sizes[1] > 100)
        { hr = E_INVALIDARG; goto done; }
        size = ID2D1Effect_GetValueSize(&effect->ID2D1Effect_iface, 4);
        if (size != c.table_sizes[0] * c.table_sizes[1] * sizeof(float)) { hr = E_INVALIDARG; goto done; }
        if (!(values = malloc(size))) { hr = E_OUTOFMEMORY; goto done; }
        hr = property(effect, 4, values, size);
        if (SUCCEEDED(hr)) hr = create_weights(context, values, size / sizeof(float), &c.weights_view);
        free(values);
        if (FAILED(hr)) goto done;
        if (FAILED(hr = property(effect, 5, &c.colour[0], sizeof(float)))) goto done;
        if (FAILED(hr = property(effect, 6, &c.colour[1], sizeof(float)))) goto done;
        if (FAILED(hr = property(effect, 7, &c.blur[2], 2 * sizeof(float)))) goto done;
        if (FAILED(hr = property(effect, 8, &c.straight_alpha, sizeof(UINT)))) goto done;
        if (FAILED(hr = property(effect, 9, &border, sizeof(border)))) goto done;
        if (FAILED(hr = property(effect, 10, &c.clamp_output, sizeof(UINT)))) goto done;
        if (!c.colour[0] || c.blur[0] <= 0 || c.blur[1] <= 0) { hr = E_INVALIDARG; goto done; }
        if (mode > D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC || border > 1) { hr = E_INVALIDARG; goto done; }
        c.interpolation = mode;
        sampler = border ? (mode ? 2 : 3) : mode ? 1 : 0;
    }
    if (IsEqualGUID(&d.id, &CLSID_D2D1GaussianBlur) || IsEqualGUID(&d.id, &CLSID_D2D1Shadow))
    {
        temp_bounds = *bounds;
        if (FAILED(hr = create_image(context, &temp_bounds, &temp))) goto done;
        c.op = 1; c.blur[0] = 96 / context->desc.dpiX;
        c.blur[2] = d.sigma * context->desc.dpiX / 96; c.blur[3] = ceilf(3 * c.blur[2]);
        if (FAILED(hr = render_pass(context, temp, &temp_bounds, input, &input_bounds, NULL, NULL, &c, sampler))) goto done;
        c.blur[0] = 0; c.blur[1] = 96 / context->desc.dpiY;
        c.blur[2] = d.sigma * context->desc.dpiY / 96; c.blur[3] = ceilf(3 * c.blur[2]);
        if (IsEqualGUID(&d.id, &CLSID_D2D1Shadow)) { c.op = 2; memcpy(c.colour, &d.colour, sizeof(c.colour)); }
        hr = render_pass(context, output, bounds, temp, &temp_bounds, NULL, NULL, &c, sampler);
    }
    else
    {
        if (IsEqualGUID(&d.id, &CLSID_D2D1DirectionalBlur))
        {
            float step = 96 / max(context->desc.dpiX, context->desc.dpiY);
            c.op = 1; c.blur[0] = cosf(d.angle * M_PI / 180) * step;
            c.blur[1] = sinf(d.angle * M_PI / 180) * step;
            c.blur[2] = d.sigma / step; c.blur[3] = ceilf(3 * c.blur[2]);
        }
        hr = render_pass(context, output, bounds, input, &input_bounds, NULL, NULL, &c, sampler);
    }
done:
    if (c.weights_view) ID3D11ShaderResourceView_Release(c.weights_view);
    if (input) ID2D1Bitmap1_Release(&input->ID2D1Bitmap1_iface);
    if (temp) ID2D1Bitmap1_Release(&temp->ID2D1Bitmap1_iface);
    if (next) ID2D1Bitmap1_Release(&next->ID2D1Bitmap1_iface);
    if (FAILED(hr)) { if (output) ID2D1Bitmap1_Release(&output->ID2D1Bitmap1_iface); return hr; }
    *result = output;
    return S_OK;
}

static HRESULT render_image(struct d2d_device_context *context, ID2D1Image *image,
        struct d2d_bitmap **result, D2D1_RECT_F *bounds, const D2D1_RECT_F *requested, unsigned int depth)
{
    struct d2d_effect_image_evaluation *evaluation = context->effect_image_evaluation;
    size_t i;
    HRESULT hr;
    for (i = 0; i < evaluation->count; ++i)
    {
        struct d2d_effect_image_node *node = &evaluation->nodes[i];
        if (node->image != image || memcmp(&node->requested, requested, sizeof(*requested))) continue;
        *result = node->bitmap;
        ID2D1Bitmap1_AddRef(&(*result)->ID2D1Bitmap1_iface);
        *bounds = node->bounds;
        return S_OK;
    }
    if (evaluation->count >= 4096) return D2DERR_INSUFFICIENT_DEVICE_CAPABILITIES;
    if (FAILED(hr = render_image_internal(context, image, result, bounds, requested, depth))) return hr;
    if (!d2d_array_reserve((void **)&evaluation->nodes, &evaluation->capacity,
            evaluation->count + 1, sizeof(*evaluation->nodes)))
    {
        ID2D1Bitmap1_Release(&(*result)->ID2D1Bitmap1_iface);
        *result = NULL;
        return E_OUTOFMEMORY;
    }
    evaluation->nodes[evaluation->count++] = (struct d2d_effect_image_node){image, *result, *requested, *bounds};
    ID2D1Bitmap1_AddRef(&(*result)->ID2D1Bitmap1_iface);
    return S_OK;
}

HRESULT d2d_effect_render(struct d2d_device_context *context, ID2D1Image *image,
        const D2D1_POINT_2F *offset, const D2D1_RECT_F *source_rect,
        struct d2d_bitmap **bitmap, D2D1_RECT_F *bounds)
{
    struct d2d_effect_image_evaluation local = {0}, *evaluation = context->effect_image_evaluation;
    D2D1_RECT_F request = {0, 0, context->pixel_size.width * 96.0f / context->desc.dpiX,
            context->pixel_size.height * 96.0f / context->desc.dpiY};
    D2D1_MATRIX_3X2_F inverse = context->drawing_state.transform;
    size_t i;
    HRESULT hr;
    if (context->effect_depth >= 32) return D2DERR_CYCLIC_GRAPH;
    if (!D2D1InvertMatrix(&inverse))
    {
        const D2D1_MATRIX_3X2_F *m = &context->drawing_state.transform;
        if (!isfinite(m->_11) || !isfinite(m->_12) || !isfinite(m->_21)
                || !isfinite(m->_22) || !isfinite(m->_31) || !isfinite(m->_32))
            return E_INVALIDARG;
        return create_empty_image(context, bitmap, bounds);
    }
    transform_rect(&request, &inverse);
    {
        float x=offset?offset->x:0,y=offset?offset->y:0;
        if(source_rect){x-=source_rect->left;y-=source_rect->top;}
        request.left-=x;request.right-=x;request.top-=y;request.bottom-=y;
        if(source_rect)
        {
            request.left=max(request.left,source_rect->left);request.top=max(request.top,source_rect->top);
            request.right=min(request.right,source_rect->right);request.bottom=min(request.bottom,source_rect->bottom);
        }
    }
    if (!evaluation) context->effect_image_evaluation = evaluation = &local;
    ++context->effect_depth;
    hr = render_image(context, image, bitmap, bounds, &request, context->effect_depth);
    --context->effect_depth;
    if (evaluation == &local)
    {
        context->effect_image_evaluation = NULL;
        for (i = 0; i < local.count; ++i)
            ID2D1Bitmap1_Release(&local.nodes[i].bitmap->ID2D1Bitmap1_iface);
        free(local.nodes);
    }
    return hr;
}
