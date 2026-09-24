/* Builtin Direct2D effect rendering tests.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#define COBJMACROS
#include <math.h>
#include "d2d1_1.h"
#include "d2d1effects.h"
#include "d2d1effects_1.h"
#include "d2d1effects_2.h"
#include "d2d1_3.h"
#include "d3d11.h"
#include "wincodec.h"
#include "icm.h"
#include "wine/test.h"

struct effect_test_context
{
    ID3D11Device *d3d;
    ID3D11DeviceContext *dc;
    ID2D1Factory1 *factory;
    ID2D1Device *device;
    ID2D1DeviceContext *context;
    ID2D1Bitmap1 *target, *readback;
};

static void cleanup_effect_context(struct effect_test_context *ctx)
{
    ULONG refcount;
    if (ctx->context) ID2D1DeviceContext_SetTarget(ctx->context, NULL);
    if (ctx->readback) ID2D1Bitmap1_Release(ctx->readback);
    if (ctx->target) ID2D1Bitmap1_Release(ctx->target);
    if (ctx->context) ID2D1DeviceContext_Release(ctx->context);
    if (ctx->device) ID2D1Device_Release(ctx->device);
    if (ctx->factory)
    {
        refcount = ID2D1Factory1_Release(ctx->factory);
        ok(!refcount, "Factory has %lu references left.\n", refcount);
    }
    if (ctx->dc) ID3D11DeviceContext_Release(ctx->dc);
    if (ctx->d3d)
    {
        refcount = ID3D11Device_Release(ctx->d3d);
        ok(!refcount, "D3D device has %lu references left.\n", refcount);
    }
}

static BOOL init_effect_context(struct effect_test_context *ctx)
{
    D2D1_BITMAP_PROPERTIES1 props = {{DXGI_FORMAT_R32G32B32A32_FLOAT, D2D1_ALPHA_MODE_PREMULTIPLIED},
            96, 96, D2D1_BITMAP_OPTIONS_TARGET, NULL};
    D2D1_SIZE_U size = {32, 32};
    IDXGIDevice *dxgi;
    HRESULT hr;
    memset(ctx, 0, sizeof(*ctx));
    hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            NULL, 0, D3D11_SDK_VERSION, &ctx->d3d, NULL, &ctx->dc);
    if (FAILED(hr))
    {
        skip("D3D11 device creation failed, hr %#lx.\n", hr);
        return FALSE;
    }
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &IID_ID2D1Factory1, NULL, (void **)&ctx->factory);
    if (FAILED(hr)) goto failed;
    hr = ID3D11Device_QueryInterface(ctx->d3d, &IID_IDXGIDevice, (void **)&dxgi);
    if (FAILED(hr)) goto failed;
    hr = ID2D1Factory1_CreateDevice(ctx->factory, dxgi, &ctx->device);
    IDXGIDevice_Release(dxgi);
    if (FAILED(hr)) goto failed;
    hr = ID2D1Device_CreateDeviceContext(ctx->device, D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &ctx->context);
    if (FAILED(hr)) goto failed;
    hr = ID2D1DeviceContext_CreateBitmap(ctx->context, size, NULL, 0, &props, &ctx->target);
    if (FAILED(hr)) goto failed;
    props.bitmapOptions = D2D1_BITMAP_OPTIONS_CANNOT_DRAW | D2D1_BITMAP_OPTIONS_CPU_READ;
    hr = ID2D1DeviceContext_CreateBitmap(ctx->context, size, NULL, 0, &props, &ctx->readback);
    if (FAILED(hr)) goto failed;
    ID2D1DeviceContext_SetTarget(ctx->context, (ID2D1Image *)ctx->target);
    ID2D1DeviceContext_SetAntialiasMode(ctx->context, D2D1_ANTIALIAS_MODE_ALIASED);
    return TRUE;
failed:
    ok(0, "Fixture creation failed, hr %#lx.\n", hr);
    cleanup_effect_context(ctx);
    return FALSE;
}

static ID2D1Bitmap1 *create_float_bitmap(struct effect_test_context *ctx,
        UINT width, UINT height, const D2D1_VECTOR_4F *pixels)
{
    D2D1_BITMAP_PROPERTIES1 props = {{DXGI_FORMAT_R32G32B32A32_FLOAT, D2D1_ALPHA_MODE_PREMULTIPLIED},
            96, 96, D2D1_BITMAP_OPTIONS_NONE, NULL};
    D2D1_SIZE_U size = {width, height};
    ID2D1Bitmap1 *bitmap = NULL;
    HRESULT hr = ID2D1DeviceContext_CreateBitmap(ctx->context, size, pixels,
            width * sizeof(*pixels), &props, &bitmap);
    ok(hr == S_OK, "Bitmap creation failed, hr %#lx.\n", hr);
    return bitmap;
}

static D2D1_VECTOR_4F read_float_pixel(struct effect_test_context *ctx, UINT x, UINT y)
{
    D2D1_VECTOR_4F value = {0};
    D2D1_MAPPED_RECT map;
    HRESULT hr;
    hr = ID2D1Bitmap1_CopyFromBitmap(ctx->readback, NULL, (ID2D1Bitmap *)ctx->target, NULL);
    ok(hr == S_OK, "Copy failed, hr %#lx.\n", hr);
    if (FAILED(hr)) return value;
    hr = ID2D1Bitmap1_Map(ctx->readback, D2D1_MAP_OPTIONS_READ, &map);
    ok(hr == S_OK, "Map failed, hr %#lx.\n", hr);
    if (FAILED(hr)) return value;
    value = ((D2D1_VECTOR_4F *)(map.bits + y * map.pitch))[x];
    hr = ID2D1Bitmap1_Unmap(ctx->readback);
    ok(hr == S_OK, "Unmap failed, hr %#lx.\n", hr);
    return value;
}

static void check_vector(D2D1_VECTOR_4F actual, D2D1_VECTOR_4F expected, float tolerance)
{
    ok(fabsf(actual.x - expected.x) <= tolerance && fabsf(actual.y - expected.y) <= tolerance
            && fabsf(actual.z - expected.z) <= tolerance && fabsf(actual.w - expected.w) <= tolerance,
            "Got {%g,%g,%g,%g}, expected {%g,%g,%g,%g}.\n", actual.x, actual.y, actual.z, actual.w,
            expected.x, expected.y, expected.z, expected.w);
}

static HRESULT draw_effect(struct effect_test_context *ctx, ID2D1Effect *effect,
        const D2D1_POINT_2F *offset, const D2D1_RECT_F *source_rect)
{
    ID2D1Image *output;
    HRESULT hr;
    ID2D1Effect_GetOutput(effect, &output);
    ID2D1DeviceContext_BeginDraw(ctx->context);
    ID2D1DeviceContext_Clear(ctx->context, NULL);
    ID2D1DeviceContext_DrawImage(ctx->context, output, offset, source_rect,
            D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR, D2D1_COMPOSITE_MODE_SOURCE_OVER);
    hr = ID2D1DeviceContext_EndDraw(ctx->context, NULL, NULL);
    ID2D1Image_Release(output);
    return hr;
}

static void test_fixture(void)
{
    static const D2D1_VECTOR_4F pixels[] = {{1,0,0,1}, {.5f,0,0,.5f}, {0,0,0,0}, {2,1,.5f,1}};
    static const D2D1_COLOR_F green = {0,1,0,1};
    struct effect_test_context ctx;
    ID2D1Bitmap1 *bitmap;
    unsigned int i;
    HRESULT hr;
    if (!init_effect_context(&ctx)) return;
    bitmap = create_float_bitmap(&ctx, 4, 1, pixels);
    if (!bitmap) { cleanup_effect_context(&ctx); return; }
    ID2D1DeviceContext_BeginDraw(ctx.context);
    ID2D1DeviceContext_Clear(ctx.context, NULL);
    ID2D1DeviceContext_DrawImage(ctx.context, (ID2D1Image *)bitmap, NULL, NULL,
            D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR, D2D1_COMPOSITE_MODE_SOURCE_OVER);
    hr = ID2D1DeviceContext_EndDraw(ctx.context, NULL, NULL);
    ok(hr == S_OK, "Fixture draw failed, hr %#lx.\n", hr);
    for (i = 0; i < ARRAY_SIZE(pixels); ++i) check_vector(read_float_pixel(&ctx, i, 0), pixels[i], 0.0001f);
    ID2D1DeviceContext_BeginDraw(ctx.context);
    ID2D1DeviceContext_Clear(ctx.context, &green);
    ID2D1DeviceContext_DrawImage(ctx.context, (ID2D1Image *)bitmap, NULL, NULL,
            D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR, D2D1_COMPOSITE_MODE_SOURCE_OVER);
    hr = ID2D1DeviceContext_EndDraw(ctx.context, NULL, NULL);
    ok(hr == S_OK, "Fixture blend failed, hr %#lx.\n", hr);
    check_vector(read_float_pixel(&ctx, 1, 0), (D2D1_VECTOR_4F){.5f,.5f,0,1}, 0.0001f);
    ID2D1Bitmap1_Release(bitmap);
    cleanup_effect_context(&ctx);
}

static void test_color_matrix_alpha(void)
{
    static const D2D1_VECTOR_4F pixel = {.5f, 0, 0, .5f};
    D2D1_MATRIX_5X4_F matrix = {{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,.5f, 0,0,0,0}};
    struct effect_test_context ctx;
    ID2D1Bitmap1 *bitmap;
    ID2D1Effect *effect;
    HRESULT hr;
    if (!init_effect_context(&ctx)) return;
    bitmap = create_float_bitmap(&ctx, 1, 1, &pixel);
    if (!bitmap) { cleanup_effect_context(&ctx); return; }
    hr = ID2D1DeviceContext_CreateEffect(ctx.context, &CLSID_D2D1ColorMatrix, &effect);
    ok(hr == S_OK, "CreateEffect failed, hr %#lx.\n", hr);
    if (SUCCEEDED(hr))
    {
        ID2D1Effect_SetInput(effect, 0, (ID2D1Image *)bitmap, TRUE);
        hr = ID2D1Effect_SetValue(effect, D2D1_COLORMATRIX_PROP_COLOR_MATRIX, D2D1_PROPERTY_TYPE_MATRIX_5X4,
                (const BYTE *)&matrix, sizeof(matrix));
        ok(hr == S_OK, "SetValue failed, hr %#lx.\n", hr);
        hr = draw_effect(&ctx, effect, NULL, NULL);
        ok(hr == S_OK, "Draw failed, hr %#lx.\n", hr);
        if (SUCCEEDED(hr)) check_vector(read_float_pixel(&ctx, 0, 0), (D2D1_VECTOR_4F){.25f,0,0,.25f}, .001f);
        ID2D1Effect_Release(effect);
    }
    ID2D1Bitmap1_Release(bitmap);
    cleanup_effect_context(&ctx);
}

static void test_pointwise_effects(void)
{
    static const D2D1_VECTOR_4F pixels[] = {{1,0,0,1}, {.5f,0,0,.5f}, {0,0,0,0}, {2,1,.5f,1}};
    static const struct
    {
        const GUID *id;
        float property_value;
        UINT32 property_index;
        D2D1_VECTOR_4F expected;
    } cases[] =
    {
        {&CLSID_D2D1Saturation, 0, D2D1_SATURATION_PROP_SATURATION, {.2125f,.2125f,.2125f,1}},
        {&CLSID_D2D1HueRotation, 360, D2D1_HUEROTATION_PROP_ANGLE, {1,0,0,1}},
        {&CLSID_D2D1Opacity, .25f, D2D1_OPACITY_PROP_OPACITY, {.25f,0,0,.25f}},
        {&CLSID_D2D1Exposure, 1, D2D1_EXPOSURE_PROP_EXPOSURE_VALUE, {2,0,0,1}},
    };
    struct effect_test_context ctx;
    ID2D1Bitmap1 *bitmap;
    ID2D1Effect *effect;
    unsigned int i;
    HRESULT hr;
    if (!init_effect_context(&ctx)) return;
    bitmap = create_float_bitmap(&ctx, 4, 1, pixels);
    if (!bitmap) { cleanup_effect_context(&ctx); return; }
    for (i = 0; i < ARRAY_SIZE(cases); ++i)
    {
        winetest_push_context("case %u", i);
        hr = ID2D1DeviceContext_CreateEffect(ctx.context, cases[i].id, &effect);
        ok(hr == S_OK, "CreateEffect failed, hr %#lx.\n", hr);
        if (SUCCEEDED(hr))
        {
            ID2D1Effect_SetInput(effect, 0, (ID2D1Image *)bitmap, TRUE);
            hr = ID2D1Effect_SetValue(effect, cases[i].property_index, D2D1_PROPERTY_TYPE_FLOAT,
                    (const BYTE *)&cases[i].property_value, sizeof(float));
            ok(hr == S_OK, "SetValue failed, hr %#lx.\n", hr);
            if (IsEqualGUID(cases[i].id, &CLSID_D2D1Opacity) || IsEqualGUID(cases[i].id, &CLSID_D2D1Exposure))
            {
                float invalid = 100, returned = 0;
                hr = ID2D1Effect_SetValue(effect, cases[i].property_index, D2D1_PROPERTY_TYPE_FLOAT,
                        (const BYTE *)&invalid, sizeof(invalid));
                ok(hr == E_INVALIDARG, "Invalid property accepted, hr %#lx.\n", hr);
                hr = ID2D1Effect_GetValue(effect, cases[i].property_index, D2D1_PROPERTY_TYPE_FLOAT,
                        (BYTE *)&returned, sizeof(returned));
                ok(hr == S_OK && returned == cases[i].property_value, "Rejected property changed old value to %g.\n", returned);
            }
            hr = draw_effect(&ctx, effect, NULL, NULL);
            ok(hr == S_OK, "Draw failed, hr %#lx.\n", hr);
            if (SUCCEEDED(hr)) check_vector(read_float_pixel(&ctx, 0, 0), cases[i].expected, .002f);
            ID2D1Effect_Release(effect);
        }
        winetest_pop_context();
    }
    ID2D1Bitmap1_Release(bitmap);
    cleanup_effect_context(&ctx);
}

static void test_alpha_effects(void)
{
    static const D2D1_VECTOR_4F pixel = {.2f, .1f, .05f, .5f};
    static const struct
    {
        const GUID *id;
        D2D1_VECTOR_4F expected;
    } cases[] =
    {
        {&CLSID_D2D1Premultiply, {.1f, .05f, .025f, .5f}},
        {&CLSID_D2D1UnPremultiply, {.4f, .2f, .1f, .5f}},
        {&CLSID_D2D1LuminanceToAlpha, {0, 0, 0, .1177f}},
        {&CLSID_D2D1Invert, {.3f, .4f, .45f, .5f}},
    };
    struct effect_test_context ctx;
    ID2D1Bitmap1 *bitmap;
    ID2D1Effect *effect;
    unsigned int i;
    HRESULT hr;
    if (!init_effect_context(&ctx)) return;
    bitmap = create_float_bitmap(&ctx, 1, 1, &pixel);
    if (!bitmap) { cleanup_effect_context(&ctx); return; }
    for (i = 0; i < ARRAY_SIZE(cases); ++i)
    {
        winetest_push_context("alpha effect %u", i);
        hr = ID2D1DeviceContext_CreateEffect(ctx.context, cases[i].id, &effect);
        ok(hr == S_OK, "CreateEffect failed, hr %#lx.\n", hr);
        if (SUCCEEDED(hr))
        {
            ID2D1Effect_SetInput(effect, 0, (ID2D1Image *)bitmap, TRUE);
            hr = draw_effect(&ctx, effect, NULL, NULL);
            ok(hr == S_OK, "Draw failed, hr %#lx.\n", hr);
            if (SUCCEEDED(hr)) check_vector(read_float_pixel(&ctx, 0, 0), cases[i].expected, .002f);
            ID2D1Effect_Release(effect);
        }
        winetest_pop_context();
    }
    ID2D1Bitmap1_Release(bitmap);
    cleanup_effect_context(&ctx);
}

static void test_effect_bounds(void)
{
    static const D2D1_VECTOR_4F pixels[4] = {{1,0,0,1}, {1,0,0,1}, {1,0,0,1}, {1,0,0,1}};
    D2D1_RECT_F crop_rect = {.25f, .5f, 1.75f, 1.5f}, bounds;
    D2D1_MATRIX_3X2_F transform = {{{1,0,0,1,8,4}}};
    struct effect_test_context ctx;
    ID2D1Bitmap1 *bitmap;
    ID2D1Effect *crop, *blur;
    ID2D1Image *output, *blur_output;
    float zero = 0;
    HRESULT hr;
    if (!init_effect_context(&ctx)) return;
    bitmap = create_float_bitmap(&ctx, 2, 2, pixels);
    if (!bitmap) { cleanup_effect_context(&ctx); return; }
    hr = ID2D1DeviceContext_CreateEffect(ctx.context, &CLSID_D2D1Crop, &crop);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    if (FAILED(hr)) goto done;
    ID2D1Effect_SetInput(crop, 0, (ID2D1Image *)bitmap, TRUE);
    hr = ID2D1Effect_SetValue(crop, D2D1_CROP_PROP_RECT, D2D1_PROPERTY_TYPE_VECTOR4,
            (const BYTE *)&crop_rect, sizeof(crop_rect));
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    ID2D1Effect_GetOutput(crop, &output);
    ID2D1DeviceContext_SetDpi(ctx.context, 144, 192);
    hr = ID2D1DeviceContext_GetImageLocalBounds(ctx.context, output, &bounds);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    ok(!memcmp(&bounds, &crop_rect, sizeof(bounds)), "Fractional logical bounds changed.\n");
    ID2D1DeviceContext_SetTransform(ctx.context, &transform);
    hr = ID2D1DeviceContext_GetImageWorldBounds(ctx.context, output, &bounds);
    ok(hr == S_OK, "World bounds failed, hr %#lx.\n", hr);
    if (SUCCEEDED(hr))
        ok(bounds.left == 8.25f && bounds.top == 4.5f && bounds.right == 9.75f && bounds.bottom == 5.5f,
                "Incorrect world bounds {%g,%g,%g,%g}.\n", bounds.left, bounds.top, bounds.right, bounds.bottom);
    transform._31 = transform._32 = 0;
    ID2D1DeviceContext_SetTransform(ctx.context, &transform);
    ID2D1DeviceContext_SetDpi(ctx.context, 96, 96);
    hr = ID2D1DeviceContext_CreateEffect(ctx.context, &CLSID_D2D1GaussianBlur, &blur);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    if (SUCCEEDED(hr))
    {
        hr = ID2D1Effect_SetValue(blur, D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION,
                D2D1_PROPERTY_TYPE_FLOAT, (const BYTE *)&zero, sizeof(zero));
        ok(hr == S_OK, "Got hr %#lx.\n", hr);
        ID2D1Effect_SetInput(blur, 0, output, TRUE);
        ID2D1Effect_GetOutput(blur, &blur_output);
        ID2D1Effect_SetInput(crop, 0, blur_output, TRUE);
        hr = ID2D1DeviceContext_GetImageLocalBounds(ctx.context, output, &bounds);
        ok(hr == D2DERR_CYCLIC_GRAPH, "Expected cycle error, hr %#lx.\n", hr);
        hr = draw_effect(&ctx, crop, NULL, NULL);
        ok(hr == D2DERR_CYCLIC_GRAPH, "Expected cycle error on draw, hr %#lx.\n", hr);
        /* Break the COM cycle before releasing our references. */
        ID2D1Effect_SetInput(crop, 0, (ID2D1Image *)bitmap, TRUE);
        ID2D1Image_Release(blur_output);
        hr = draw_effect(&ctx, blur, NULL, NULL);
        ok(hr == S_OK, "Drawing after cycle repair failed, hr %#lx.\n", hr);
        ID2D1Effect_Release(blur);
    }
    ID2D1Image_Release(output);
    ID2D1Effect_Release(crop);
done:
    ID2D1Bitmap1_Release(bitmap);
    cleanup_effect_context(&ctx);
}

static void test_brightness(void)
{
    static const D2D1_VECTOR_4F pixel = {.25f,.5f,.75f,1};
    D2D1_VECTOR_2F black = {.25f,0}, white = {.75f,1};
    struct effect_test_context ctx;
    ID2D1Bitmap1 *bitmap;
    ID2D1Effect *effect;
    HRESULT hr;
    if (!init_effect_context(&ctx)) return;
    bitmap = create_float_bitmap(&ctx, 1, 1, &pixel);
    if (!bitmap) { cleanup_effect_context(&ctx); return; }
    hr = ID2D1DeviceContext_CreateEffect(ctx.context, &CLSID_D2D1Brightness, &effect);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    if (SUCCEEDED(hr))
    {
        ID2D1Effect_SetInput(effect, 0, (ID2D1Image *)bitmap, TRUE);
        hr = ID2D1Effect_SetValue(effect, D2D1_BRIGHTNESS_PROP_BLACK_POINT, D2D1_PROPERTY_TYPE_VECTOR2,
                (const BYTE *)&black, sizeof(black));
        ok(hr == S_OK, "Got hr %#lx.\n", hr);
        hr = ID2D1Effect_SetValue(effect, D2D1_BRIGHTNESS_PROP_WHITE_POINT, D2D1_PROPERTY_TYPE_VECTOR2,
                (const BYTE *)&white, sizeof(white));
        ok(hr == S_OK, "Got hr %#lx.\n", hr);
        hr = draw_effect(&ctx, effect, NULL, NULL);
        ok(hr == S_OK, "Got hr %#lx.\n", hr);
        if (SUCCEEDED(hr)) check_vector(read_float_pixel(&ctx, 0, 0), (D2D1_VECTOR_4F){0,.5f,1,1}, .002f);
        ID2D1Effect_Release(effect);
    }
    ID2D1Bitmap1_Release(bitmap);
    cleanup_effect_context(&ctx);
}

static void test_flood_crop(void)
{
    D2D1_VECTOR_4F colour = {.8f,.4f,.2f,.5f};
    D2D1_RECT_F rect = {4, 8, 12, 16};
    struct effect_test_context ctx;
    ID2D1Effect *flood, *crop;
    ID2D1Image *input;
    HRESULT hr;
    if (!init_effect_context(&ctx)) return;
    hr = ID2D1DeviceContext_CreateEffect(ctx.context, &CLSID_D2D1Flood, &flood);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    if (FAILED(hr)) { cleanup_effect_context(&ctx); return; }
    hr = ID2D1Effect_SetValue(flood, D2D1_FLOOD_PROP_COLOR, D2D1_PROPERTY_TYPE_VECTOR4,
            (const BYTE *)&colour, sizeof(colour));
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    hr = draw_effect(&ctx, flood, NULL, NULL);
    ok(hr == S_OK, "Flood draw failed, hr %#lx.\n", hr);
    if (SUCCEEDED(hr)) check_vector(read_float_pixel(&ctx, 31, 31), (D2D1_VECTOR_4F){.4f,.2f,.1f,.5f}, .002f);
    hr = ID2D1DeviceContext_CreateEffect(ctx.context, &CLSID_D2D1Crop, &crop);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    if (SUCCEEDED(hr))
    {
        ID2D1Effect_GetOutput(flood, &input);
        ID2D1Effect_SetInput(crop, 0, input, TRUE);
        ID2D1Image_Release(input);
        hr = ID2D1Effect_SetValue(crop, D2D1_CROP_PROP_RECT, D2D1_PROPERTY_TYPE_VECTOR4,
                (const BYTE *)&rect, sizeof(rect));
        ok(hr == S_OK, "Got hr %#lx.\n", hr);
        hr = draw_effect(&ctx, crop, NULL, NULL);
        ok(hr == S_OK, "Crop of Flood failed, hr %#lx.\n", hr);
        if (SUCCEEDED(hr))
        {
            check_vector(read_float_pixel(&ctx, 8, 12), (D2D1_VECTOR_4F){.4f,.2f,.1f,.5f}, .002f);
            check_vector(read_float_pixel(&ctx, 0, 0), (D2D1_VECTOR_4F){0}, .002f);
        }
        ID2D1Effect_Release(crop);
    }
    ID2D1Effect_Release(flood);
    cleanup_effect_context(&ctx);
}

static void test_shared_graph(void)
{
    static const D2D1_VECTOR_4F red = {1,0,0,1};
    D2D1_MATRIX_5X4_F matrix = {{0,0,1,0, 0,1,0,0, 1,0,0,0, 0,0,0,1, 0,0,0,0}};
    struct effect_test_context ctx;
    ID2D1Effect *leaf, *nodes[12] = {0};
    ID2D1Image *input;
    ID2D1Bitmap1 *bitmap;
    unsigned int i;
    HRESULT hr;
    if (!init_effect_context(&ctx)) return;
    bitmap = create_float_bitmap(&ctx, 1, 1, &red);
    if (!bitmap) { cleanup_effect_context(&ctx); return; }
    hr = ID2D1DeviceContext_CreateEffect(ctx.context, &CLSID_D2D1ColorMatrix, &leaf);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    if (FAILED(hr)) goto done;
    ID2D1Effect_SetInput(leaf, 0, (ID2D1Image *)bitmap, TRUE);
    for (i = 0; i < ARRAY_SIZE(nodes); ++i)
    {
        hr = ID2D1DeviceContext_CreateEffect(ctx.context, &CLSID_D2D1Composite, &nodes[i]);
        ok(hr == S_OK, "Got hr %#lx.\n", hr);
        if (FAILED(hr)) goto release;
        ID2D1Effect_GetOutput(i ? nodes[i - 1] : leaf, &input);
        ID2D1Effect_SetInput(nodes[i], 0, input, TRUE);
        ID2D1Effect_SetInput(nodes[i], 1, input, TRUE);
        ID2D1Image_Release(input);
    }
    hr = draw_effect(&ctx, nodes[ARRAY_SIZE(nodes) - 1], NULL, NULL);
    ok(hr == S_OK, "Shared graph failed, hr %#lx.\n", hr);
    if (SUCCEEDED(hr)) check_vector(read_float_pixel(&ctx, 0, 0), red, .001f);
    hr = ID2D1Effect_SetValue(leaf, D2D1_COLORMATRIX_PROP_COLOR_MATRIX, D2D1_PROPERTY_TYPE_MATRIX_5X4,
            (const BYTE *)&matrix, sizeof(matrix));
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    hr = draw_effect(&ctx, nodes[ARRAY_SIZE(nodes) - 1], NULL, NULL);
    ok(hr == S_OK, "Mutated shared graph failed, hr %#lx.\n", hr);
    if (SUCCEEDED(hr)) check_vector(read_float_pixel(&ctx, 0, 0), (D2D1_VECTOR_4F){0,0,1,1}, .001f);
release:
    for (i = ARRAY_SIZE(nodes); i; --i) if (nodes[i - 1]) ID2D1Effect_Release(nodes[i - 1]);
    ID2D1Effect_Release(leaf);
done:
    ID2D1Bitmap1_Release(bitmap);
    cleanup_effect_context(&ctx);
}

static void test_two_input_effects(void)
{
    static const D2D1_VECTOR_4F first = {.5f,0,0,.5f}, second = {0,.25f,0,.25f};
    static const struct { const GUID *id; D2D1_VECTOR_4F expected; } cases[] =
    {
        {&CLSID_D2D1AlphaMask, {.125f,0,0,.125f}},
        {&CLSID_D2D1CrossFade, {.125f,.1875f,0,.3125f}},
        {&CLSID_D2D1ArithmeticComposite, {.25f,.25f,0,.625f}},
    };
    D2D1_VECTOR_4F coefficients = {1,.5f,1,0};
    struct effect_test_context ctx;
    ID2D1Bitmap1 *a, *b;
    ID2D1Effect *effect;
    float weight = .25f;
    unsigned int i;
    HRESULT hr;
    if (!init_effect_context(&ctx)) return;
    a = create_float_bitmap(&ctx, 1, 1, &first);
    b = create_float_bitmap(&ctx, 1, 1, &second);
    if (!a || !b) goto done;
    for (i = 0; i < ARRAY_SIZE(cases); ++i)
    {
        winetest_push_context("two input %u", i);
        hr = ID2D1DeviceContext_CreateEffect(ctx.context, cases[i].id, &effect);
        ok(hr == S_OK, "CreateEffect failed, hr %#lx.\n", hr);
        if (SUCCEEDED(hr))
        {
            ID2D1Effect_SetInput(effect, 0, (ID2D1Image *)a, TRUE);
            ID2D1Effect_SetInput(effect, 1, (ID2D1Image *)b, TRUE);
            if (i == 1)
            {
                hr = ID2D1Effect_SetValue(effect, D2D1_CROSSFADE_PROP_WEIGHT, D2D1_PROPERTY_TYPE_FLOAT,
                        (const BYTE *)&weight, sizeof(weight));
                ok(hr == S_OK, "Got hr %#lx.\n", hr);
            }
            if (i == 2)
            {
                hr = ID2D1Effect_SetValue(effect, D2D1_ARITHMETICCOMPOSITE_PROP_COEFFICIENTS,
                        D2D1_PROPERTY_TYPE_VECTOR4, (const BYTE *)&coefficients, sizeof(coefficients));
                ok(hr == S_OK, "Got hr %#lx.\n", hr);
            }
            hr = draw_effect(&ctx, effect, NULL, NULL);
            ok(hr == S_OK, "Draw failed, hr %#lx.\n", hr);
            if (SUCCEEDED(hr)) check_vector(read_float_pixel(&ctx, 0, 0), cases[i].expected, .002f);
            ID2D1Effect_Release(effect);
        }
        winetest_pop_context();
    }
done:
    if (a) ID2D1Bitmap1_Release(a);
    if (b) ID2D1Bitmap1_Release(b);
    cleanup_effect_context(&ctx);
}

static void test_fractional_crop_pixels(void)
{
    static const D2D1_VECTOR_4F pixels[4] = {{1,0,0,1},{0,1,0,1},{0,0,1,1},{1,1,1,1}};
    D2D1_RECT_F rect = {.75f, 0, 2.25f, 1};
    struct effect_test_context ctx;
    ID2D1Bitmap1 *bitmap;
    ID2D1Effect *effect;
    HRESULT hr;
    if (!init_effect_context(&ctx)) return;
    bitmap = create_float_bitmap(&ctx, 4, 1, pixels);
    if (!bitmap) { cleanup_effect_context(&ctx); return; }
    hr = ID2D1DeviceContext_CreateEffect(ctx.context, &CLSID_D2D1Crop, &effect);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    if (SUCCEEDED(hr))
    {
        ID2D1Effect_SetInput(effect, 0, (ID2D1Image *)bitmap, TRUE);
        hr = ID2D1Effect_SetValue(effect, D2D1_CROP_PROP_RECT, D2D1_PROPERTY_TYPE_VECTOR4,
                (const BYTE *)&rect, sizeof(rect));
        ok(hr == S_OK, "Got hr %#lx.\n", hr);
        hr = draw_effect(&ctx, effect, NULL, NULL);
        ok(hr == S_OK, "Got hr %#lx.\n", hr);
        if (SUCCEEDED(hr))
        {
            check_vector(read_float_pixel(&ctx, 0, 0), (D2D1_VECTOR_4F){0}, .001f);
            check_vector(read_float_pixel(&ctx, 1, 0), pixels[1], .001f);
            check_vector(read_float_pixel(&ctx, 2, 0), (D2D1_VECTOR_4F){0}, .001f);
        }
        ID2D1Effect_Release(effect);
    }
    ID2D1Bitmap1_Release(bitmap);
    cleanup_effect_context(&ctx);
}

static void test_transfer_effects(void)
{
    static const D2D1_VECTOR_4F pixel = {.25f,.125f,.375f,.5f};
    const GUID *ids[] = {&CLSID_D2D1LinearTransfer, &CLSID_D2D1GammaTransfer};
    struct effect_test_context ctx;
    ID2D1Bitmap1 *bitmap;
    ID2D1Effect *effect;
    float value = 2;
    BOOL disabled = TRUE;
    unsigned int i;
    HRESULT hr;
    if (!init_effect_context(&ctx)) return;
    bitmap = create_float_bitmap(&ctx, 1, 1, &pixel);
    if (!bitmap) { cleanup_effect_context(&ctx); return; }
    for (i = 0; i < ARRAY_SIZE(ids); ++i)
    {
        winetest_push_context("transfer %u", i);
        hr = ID2D1DeviceContext_CreateEffect(ctx.context, ids[i], &effect);
        ok(hr == S_OK, "CreateEffect failed, hr %#lx.\n", hr);
        if (SUCCEEDED(hr))
        {
            ID2D1Effect_SetInput(effect, 0, (ID2D1Image *)bitmap, TRUE);
            /* Index 1 is red slope for linear, red exponent for gamma. */
            hr = ID2D1Effect_SetValue(effect, 1, D2D1_PROPERTY_TYPE_FLOAT, (const BYTE *)&value, sizeof(value));
            ok(hr == S_OK, "Got hr %#lx.\n", hr);
            hr = draw_effect(&ctx, effect, NULL, NULL);
            ok(hr == S_OK, "Draw failed, hr %#lx.\n", hr);
            if (SUCCEEDED(hr)) check_vector(read_float_pixel(&ctx, 0, 0),
                    (D2D1_VECTOR_4F){i ? .125f : .5f,.125f,.375f,.5f}, .002f);
            hr = ID2D1Effect_SetValue(effect, i ? 3 : 2, D2D1_PROPERTY_TYPE_BOOL,
                    (const BYTE *)&disabled, sizeof(disabled));
            ok(hr == S_OK, "Got hr %#lx.\n", hr);
            hr = draw_effect(&ctx, effect, NULL, NULL);
            ok(hr == S_OK, "Draw failed, hr %#lx.\n", hr);
            if (SUCCEEDED(hr)) check_vector(read_float_pixel(&ctx, 0, 0), pixel, .002f);
            ID2D1Effect_Release(effect);
        }
        winetest_pop_context();
    }
    ID2D1Bitmap1_Release(bitmap);
    cleanup_effect_context(&ctx);
}

static void test_table_transfer(void)
{
    static const D2D1_VECTOR_4F pixel = {.25f,.5f,.75f,1};
    const GUID *ids[] = {&CLSID_D2D1TableTransfer, &CLSID_D2D1DiscreteTransfer};
    float table[] = {1,0}, returned[2];
    struct effect_test_context ctx;
    ID2D1Effect *effect;
    ID2D1Bitmap1 *bitmap;
    unsigned int i;
    HRESULT hr;
    if (!init_effect_context(&ctx)) return;
    bitmap = create_float_bitmap(&ctx, 1, 1, &pixel);
    if (!bitmap) { cleanup_effect_context(&ctx); return; }
    for (i = 0; i < ARRAY_SIZE(ids); ++i)
    {
        winetest_push_context("table %u", i);
        hr = ID2D1DeviceContext_CreateEffect(ctx.context, ids[i], &effect);
        ok(hr == S_OK, "CreateEffect failed, hr %#lx.\n", hr);
        if (SUCCEEDED(hr))
        {
            ID2D1Effect_SetInput(effect, 0, (ID2D1Image *)bitmap, TRUE);
            hr = ID2D1Effect_SetValue(effect, 0, D2D1_PROPERTY_TYPE_BLOB, (const BYTE *)table, sizeof(table));
            ok(hr == S_OK, "Set table failed, hr %#lx.\n", hr);
            hr = ID2D1Effect_GetValue(effect, 0, D2D1_PROPERTY_TYPE_BLOB, (BYTE *)returned, sizeof(returned));
            ok(hr == S_OK && returned[0] == 1 && returned[1] == 0, "Table did not round trip.\n");
            hr = ID2D1Effect_SetValue(effect, 0, D2D1_PROPERTY_TYPE_BLOB, (const BYTE *)table, 3);
            ok(hr == E_INVALIDARG, "Malformed table accepted, hr %#lx.\n", hr);
            hr = draw_effect(&ctx, effect, NULL, NULL);
            ok(hr == S_OK, "Draw failed, hr %#lx.\n", hr);
            if (SUCCEEDED(hr)) check_vector(read_float_pixel(&ctx, 0, 0),
                    (D2D1_VECTOR_4F){i ? 1 : .75f, i ? 1 : .5f, i ? 1 : .75f,1}, .002f);
            ID2D1Effect_Release(effect);
        }
        winetest_pop_context();
    }
    ID2D1Bitmap1_Release(bitmap);
    cleanup_effect_context(&ctx);
}

static void test_morphology(void)
{
    D2D1_VECTOR_4F pixels[25] = {{0}};
    struct effect_test_context ctx;
    ID2D1Bitmap1 *bitmap;
    ID2D1Effect *effect;
    UINT32 mode = 1, width = 3, height = 1;
    HRESULT hr;
    if (!init_effect_context(&ctx)) return;
    pixels[12] = (D2D1_VECTOR_4F){1,0,0,1};
    bitmap = create_float_bitmap(&ctx, 5, 5, pixels);
    if (!bitmap) { cleanup_effect_context(&ctx); return; }
    hr = ID2D1DeviceContext_CreateEffect(ctx.context, &CLSID_D2D1Morphology, &effect);
    ok(hr == S_OK, "CreateEffect failed, hr %#lx.\n", hr);
    if (SUCCEEDED(hr))
    {
        ID2D1Effect_SetInput(effect, 0, (ID2D1Image *)bitmap, TRUE);
        hr = ID2D1Effect_SetValue(effect, 0, D2D1_PROPERTY_TYPE_ENUM, (BYTE *)&mode, sizeof(mode));
        ok(hr == S_OK, "Got hr %#lx.\n", hr);
        hr = ID2D1Effect_SetValue(effect, 1, D2D1_PROPERTY_TYPE_UINT32, (BYTE *)&width, sizeof(width));
        ok(hr == S_OK, "Got hr %#lx.\n", hr);
        hr = ID2D1Effect_SetValue(effect, 2, D2D1_PROPERTY_TYPE_UINT32, (BYTE *)&height, sizeof(height));
        ok(hr == S_OK, "Got hr %#lx.\n", hr);
        hr = draw_effect(&ctx, effect, NULL, NULL);
        ok(hr == S_OK, "Draw failed, hr %#lx.\n", hr);
        if (SUCCEEDED(hr))
        {
            check_vector(read_float_pixel(&ctx, 1, 2), pixels[12], .001f);
            check_vector(read_float_pixel(&ctx, 3, 2), pixels[12], .001f);
            check_vector(read_float_pixel(&ctx, 2, 1), pixels[0], .001f);
        }
        mode = 0;
        hr = ID2D1Effect_SetValue(effect, 0, D2D1_PROPERTY_TYPE_ENUM, (BYTE *)&mode, sizeof(mode));
        ok(hr == S_OK, "Got hr %#lx.\n", hr);
        hr = draw_effect(&ctx, effect, NULL, NULL);
        ok(hr == S_OK, "Draw failed, hr %#lx.\n", hr);
        if (SUCCEEDED(hr)) check_vector(read_float_pixel(&ctx, 2, 2), pixels[0], .001f);
        ID2D1Effect_Release(effect);
    }
    ID2D1Bitmap1_Release(bitmap);
    cleanup_effect_context(&ctx);
}

static void test_tile_border(void)
{
    static const D2D1_VECTOR_4F pixels[2] = {{1,0,0,1},{0,0,1,1}};
    const GUID *ids[] = {&CLSID_D2D1Tile, &CLSID_D2D1Border};
    D2D1_RECT_F tile_rect = {0,0,2,1}, crop_rect = {0,0,8,1};
    UINT32 wrap = 1;
    struct effect_test_context ctx;
    ID2D1Bitmap1 *bitmap;
    ID2D1Effect *effect, *crop;
    ID2D1Image *output;
    unsigned int i, x;
    HRESULT hr;
    if (!init_effect_context(&ctx)) return;
    bitmap = create_float_bitmap(&ctx, 2, 1, pixels);
    if (!bitmap) { cleanup_effect_context(&ctx); return; }
    for (i = 0; i < ARRAY_SIZE(ids); ++i)
    {
        winetest_push_context("tile/border %u", i);
        hr = ID2D1DeviceContext_CreateEffect(ctx.context, ids[i], &effect);
        ok(hr == S_OK, "CreateEffect failed, hr %#lx.\n", hr);
        if (SUCCEEDED(hr))
        {
            ID2D1Effect_SetInput(effect, 0, (ID2D1Image *)bitmap, TRUE);
            hr = ID2D1Effect_SetValue(effect, 0, i ? D2D1_PROPERTY_TYPE_ENUM : D2D1_PROPERTY_TYPE_VECTOR4,
                    i ? (const BYTE *)&wrap : (const BYTE *)&tile_rect, i ? sizeof(wrap) : sizeof(tile_rect));
            ok(hr == S_OK, "Got hr %#lx.\n", hr);
            hr = ID2D1DeviceContext_CreateEffect(ctx.context, &CLSID_D2D1Crop, &crop);
            ok(hr == S_OK, "Got hr %#lx.\n", hr);
            if (SUCCEEDED(hr))
            {
                ID2D1Effect_GetOutput(effect, &output);
                ID2D1Effect_SetInput(crop, 0, output, TRUE);
                ID2D1Image_Release(output);
                hr = ID2D1Effect_SetValue(crop, D2D1_CROP_PROP_RECT, D2D1_PROPERTY_TYPE_VECTOR4,
                        (const BYTE *)&crop_rect, sizeof(crop_rect));
                ok(hr == S_OK, "Got hr %#lx.\n", hr);
                hr = draw_effect(&ctx, crop, NULL, NULL);
                ok(hr == S_OK, "Draw failed, hr %#lx.\n", hr);
                if (SUCCEEDED(hr)) for (x = 0; x < 8; ++x) check_vector(read_float_pixel(&ctx, x, 0), pixels[x % 2], .001f);
                ID2D1Effect_Release(crop);
            }
            ID2D1Effect_Release(effect);
        }
        winetest_pop_context();
    }
    ID2D1Bitmap1_Release(bitmap);
    cleanup_effect_context(&ctx);
}

static void test_color_filters(void)
{
    static const D2D1_VECTOR_4F pixel = {.2f,.4f,.8f,1};
    const GUID *ids[] = {&CLSID_D2D1Sepia, &CLSID_D2D1Tint, &CLSID_D2D1Posterize};
    const D2D1_VECTOR_4F expected[] = {{.5374f,.4786f,.3728f,1}, {.1f,.1f,.8f,1}, {0,1.0f/3,2.0f/3,1}};
    D2D1_VECTOR_4F tint = {.5f,.25f,1,1};
    UINT32 levels = 2;
    float intensity = 1;
    struct effect_test_context ctx;
    ID2D1Bitmap1 *bitmap;
    ID2D1Effect *effect;
    unsigned int i;
    HRESULT hr;
    if (!init_effect_context(&ctx)) return;
    bitmap = create_float_bitmap(&ctx, 1, 1, &pixel);
    if (!bitmap) { cleanup_effect_context(&ctx); return; }
    for (i = 0; i < ARRAY_SIZE(ids); ++i)
    {
        winetest_push_context("color filter %u", i);
        hr = ID2D1DeviceContext_CreateEffect(ctx.context, ids[i], &effect);
        ok(hr == S_OK, "CreateEffect failed, hr %#lx.\n", hr);
        if (SUCCEEDED(hr))
        {
            ID2D1Effect_SetInput(effect, 0, (ID2D1Image *)bitmap, TRUE);
            if (!i)
            {
                hr = ID2D1Effect_SetValue(effect, 0, D2D1_PROPERTY_TYPE_FLOAT,
                        (const BYTE *)&intensity, sizeof(intensity));
                ok(hr == S_OK, "Got hr %#lx.\n", hr);
            }
            if (i)
            {
                hr = ID2D1Effect_SetValue(effect, 0, i == 1 ? D2D1_PROPERTY_TYPE_VECTOR4 : D2D1_PROPERTY_TYPE_UINT32,
                        i == 1 ? (const BYTE *)&tint : (const BYTE *)&levels, i == 1 ? sizeof(tint) : sizeof(levels));
                ok(hr == S_OK, "Got hr %#lx.\n", hr);
            }
            hr = draw_effect(&ctx, effect, NULL, NULL);
            ok(hr == S_OK, "Draw failed, hr %#lx.\n", hr);
            if (SUCCEEDED(hr)) check_vector(read_float_pixel(&ctx, 0, 0), expected[i], .003f);
            ID2D1Effect_Release(effect);
        }
        winetest_pop_context();
    }
    ID2D1Bitmap1_Release(bitmap);
    cleanup_effect_context(&ctx);
}

static void test_displacement(void)
{
    static const D2D1_VECTOR_4F pixels[2] = {{1,0,0,1},{0,0,1,1}};
    static const D2D1_VECTOR_4F map[2] = {{1,.5f,0,1},{1,.5f,0,1}};
    struct effect_test_context ctx;
    ID2D1Bitmap1 *source, *displacement;
    ID2D1Effect *effect;
    UINT32 x = 0, y = 1;
    float scale = 2;
    HRESULT hr;
    if (!init_effect_context(&ctx)) return;
    source = create_float_bitmap(&ctx, 2, 1, pixels);
    displacement = create_float_bitmap(&ctx, 2, 1, map);
    if (!source || !displacement) goto done;
    hr = ID2D1DeviceContext_CreateEffect(ctx.context, &CLSID_D2D1DisplacementMap, &effect);
    ok(hr == S_OK, "CreateEffect failed, hr %#lx.\n", hr);
    if (SUCCEEDED(hr))
    {
        ID2D1Effect_SetInput(effect, 0, (ID2D1Image *)source, TRUE);
        ID2D1Effect_SetInput(effect, 1, (ID2D1Image *)displacement, TRUE);
        hr = ID2D1Effect_SetValue(effect, 0, D2D1_PROPERTY_TYPE_FLOAT, (BYTE *)&scale, sizeof(scale));
        ok(hr == S_OK, "Got hr %#lx.\n", hr);
        hr = ID2D1Effect_SetValue(effect, 1, D2D1_PROPERTY_TYPE_ENUM, (BYTE *)&x, sizeof(x));
        ok(hr == S_OK, "Got hr %#lx.\n", hr);
        hr = ID2D1Effect_SetValue(effect, 2, D2D1_PROPERTY_TYPE_ENUM, (BYTE *)&y, sizeof(y));
        ok(hr == S_OK, "Got hr %#lx.\n", hr);
        hr = draw_effect(&ctx, effect, NULL, NULL);
        ok(hr == S_OK, "Draw failed, hr %#lx.\n", hr);
        if (SUCCEEDED(hr)) check_vector(read_float_pixel(&ctx, 0, 0), pixels[1], .002f);
        ID2D1Effect_Release(effect);
    }
done:
    if (source) ID2D1Bitmap1_Release(source);
    if (displacement) ID2D1Bitmap1_Release(displacement);
    cleanup_effect_context(&ctx);
}

static void test_convolution(void)
{
    static const D2D1_VECTOR_4F pixels[3] = {{1,0,0,1},{0,1,0,1},{0,0,1,1}};
    const float kernel[9] = {0,0,0, 0,2,0, 0,0,0};
    struct effect_test_context ctx;
    ID2D1Effect *effect;
    ID2D1Bitmap1 *bitmap;
    float divisor = 2;
    HRESULT hr;
    if (!init_effect_context(&ctx)) return;
    bitmap = create_float_bitmap(&ctx, 3, 1, pixels);
    if (!bitmap) { cleanup_effect_context(&ctx); return; }
    hr = ID2D1DeviceContext_CreateEffect(ctx.context, &CLSID_D2D1ConvolveMatrix, &effect);
    ok(hr == S_OK, "CreateEffect failed, hr %#lx.\n", hr);
    if (SUCCEEDED(hr))
    {
        ID2D1Effect_SetInput(effect, 0, (ID2D1Image *)bitmap, TRUE);
        hr = ID2D1Effect_SetValue(effect, 4, D2D1_PROPERTY_TYPE_BLOB, (const BYTE *)kernel, sizeof(kernel));
        ok(hr == S_OK, "Got hr %#lx.\n", hr);
        hr = ID2D1Effect_SetValue(effect, 5, D2D1_PROPERTY_TYPE_FLOAT, (const BYTE *)&divisor, sizeof(divisor));
        ok(hr == S_OK, "Got hr %#lx.\n", hr);
        hr = draw_effect(&ctx, effect, NULL, NULL);
        ok(hr == S_OK, "Draw failed, hr %#lx.\n", hr);
        if (SUCCEEDED(hr)) check_vector(read_float_pixel(&ctx, 1, 0), pixels[1], .002f);
        ID2D1Effect_Release(effect);
    }
    ID2D1Bitmap1_Release(bitmap);
    cleanup_effect_context(&ctx);
}

static void test_hue_conversion(void)
{
    static const D2D1_VECTOR_4F pixel = {0,1,0,1};
    struct effect_test_context ctx;
    ID2D1Bitmap1 *bitmap;
    ID2D1Effect *forward, *reverse;
    ID2D1Image *input;
    HRESULT hr;
    if (!init_effect_context(&ctx)) return;
    bitmap = create_float_bitmap(&ctx, 1, 1, &pixel);
    if (!bitmap) { cleanup_effect_context(&ctx); return; }
    hr = ID2D1DeviceContext_CreateEffect(ctx.context, &CLSID_D2D1RgbToHue, &forward);
    ok(hr == S_OK, "CreateEffect failed, hr %#lx.\n", hr);
    if (SUCCEEDED(hr))
    {
        ID2D1Effect_SetInput(forward, 0, (ID2D1Image *)bitmap, TRUE);
        hr = draw_effect(&ctx, forward, NULL, NULL);
        ok(hr == S_OK, "Draw failed, hr %#lx.\n", hr);
        if (SUCCEEDED(hr)) check_vector(read_float_pixel(&ctx, 0, 0), (D2D1_VECTOR_4F){1.0f/3,1,1,1}, .002f);
        hr = ID2D1DeviceContext_CreateEffect(ctx.context, &CLSID_D2D1HueToRgb, &reverse);
        ok(hr == S_OK, "CreateEffect failed, hr %#lx.\n", hr);
        if (SUCCEEDED(hr))
        {
            ID2D1Effect_GetOutput(forward, &input);
            ID2D1Effect_SetInput(reverse, 0, input, TRUE);
            ID2D1Image_Release(input);
            hr = draw_effect(&ctx, reverse, NULL, NULL);
            ok(hr == S_OK, "Draw failed, hr %#lx.\n", hr);
            if (SUCCEEDED(hr)) check_vector(read_float_pixel(&ctx, 0, 0), pixel, .003f);
            ID2D1Effect_Release(reverse);
        }
        ID2D1Effect_Release(forward);
    }
    ID2D1Bitmap1_Release(bitmap);
    cleanup_effect_context(&ctx);
}

static void test_atlas_metadata_dpi(void)
{
    static const D2D1_VECTOR_4F pixels[4] = {{1,0,0,1},{0,1,0,1},{0,0,1,1},{1,1,1,1}};
    const GUID *ids[] = {&CLSID_D2D1Atlas, &CLSID_D2D1OpacityMetadata, &CLSID_D2D1DpiCompensation};
    D2D1_RECT_F rect = {1,0,3,1};
    D2D1_VECTOR_2F dpi = {192,96};
    struct effect_test_context ctx;
    ID2D1Effect *effect;
    ID2D1Bitmap1 *bitmap;
    unsigned int i;
    HRESULT hr;
    if (!init_effect_context(&ctx)) return;
    bitmap = create_float_bitmap(&ctx, 4, 1, pixels);
    if (!bitmap) { cleanup_effect_context(&ctx); return; }
    for (i = 0; i < ARRAY_SIZE(ids); ++i)
    {
        winetest_push_context("atlas/metadata/dpi %u", i);
        hr = ID2D1DeviceContext_CreateEffect(ctx.context, ids[i], &effect);
        ok(hr == S_OK, "CreateEffect failed, hr %#lx.\n", hr);
        if (SUCCEEDED(hr))
        {
            ID2D1Effect_SetInput(effect, 0, (ID2D1Image *)bitmap, TRUE);
            hr = ID2D1Effect_SetValue(effect, i == 2 ? 2 : 0,
                    i == 2 ? D2D1_PROPERTY_TYPE_VECTOR2 : D2D1_PROPERTY_TYPE_VECTOR4,
                    i == 2 ? (const BYTE *)&dpi : (const BYTE *)&rect, i == 2 ? sizeof(dpi) : sizeof(rect));
            ok(hr == S_OK, "Got hr %#lx.\n", hr);
            hr = draw_effect(&ctx, effect, NULL, NULL);
            ok(hr == S_OK, "Draw failed, hr %#lx.\n", hr);
            if (SUCCEEDED(hr) && i != 2)
            {
                check_vector(read_float_pixel(&ctx, 1, 0), pixels[1], .002f);
                check_vector(read_float_pixel(&ctx, 0, 0), i == 0 ? (D2D1_VECTOR_4F){0} : pixels[0], .002f);
            }
            if (SUCCEEDED(hr) && i == 2)
                check_vector(read_float_pixel(&ctx, 0, 0), (D2D1_VECTOR_4F){.5f,.5f,0,1}, .002f);
            ID2D1Effect_Release(effect);
        }
        winetest_pop_context();
    }
    ID2D1Bitmap1_Release(bitmap);
    cleanup_effect_context(&ctx);
}

static void test_blend_modes(void)
{
    static const D2D1_VECTOR_4F a = {.25f,.5f,.75f,1}, b = {.5f,.25f,.5f,1};
    static const struct { UINT32 mode; D2D1_VECTOR_4F value; } cases[] =
    {
        {0, {.125f,.125f,.375f,1}}, {1, {.625f,.625f,.875f,1}},
        {2, {.25f,.25f,.5f,1}}, {3, {.5f,.5f,.75f,1}},
        {5, {0,0,.5f,1}}, {6, {0,0,.25f,1}},
        {7, {.5f,.25f,.5f,1}}, {8, {.25f,.5f,.75f,1}},
        {9, {.5f,2.0f/3,1,1}}, {10, {.75f,.75f,1,1}},
        {11, {.25f,.25f,.75f,1}}, {12, {.25f,.375f,.75f,1}},
        {13, {.25f,.25f,.75f,1}}, {14, {.25f,0,.75f,1}},
        {15, {.25f,0,.75f,1}}, {16, {.25f,.5f,.75f,1}},
        {17, {0,0,1,1}}, {18, {.25f,.25f,.25f,1}}, {19, {.5f,.5f,.5f,1}},
        {24, {0,.25f,.25f,1}}, {25, {.5f,1,1,1}},
    };
    struct effect_test_context ctx;
    ID2D1Bitmap1 *first, *second;
    ID2D1Effect *effect;
    unsigned int i;
    HRESULT hr;
    if (!init_effect_context(&ctx)) return;
    first = create_float_bitmap(&ctx, 1, 1, &a);
    second = create_float_bitmap(&ctx, 1, 1, &b);
    if (!first || !second) goto done;
    hr = ID2D1DeviceContext_CreateEffect(ctx.context, &CLSID_D2D1Blend, &effect);
    ok(hr == S_OK, "CreateEffect failed, hr %#lx.\n", hr);
    if (SUCCEEDED(hr))
    {
        ID2D1Effect_SetInput(effect, 0, (ID2D1Image *)first, TRUE);
        ID2D1Effect_SetInput(effect, 1, (ID2D1Image *)second, TRUE);
        for (i = 0; i < ARRAY_SIZE(cases); ++i)
        {
            winetest_push_context("blend %u", cases[i].mode);
            hr = ID2D1Effect_SetValue(effect, 0, D2D1_PROPERTY_TYPE_ENUM, (const BYTE *)&cases[i].mode, sizeof(UINT32));
            ok(hr == S_OK, "Got hr %#lx.\n", hr);
            hr = draw_effect(&ctx, effect, NULL, NULL);
            ok(hr == S_OK, "Draw failed, hr %#lx.\n", hr);
            if (SUCCEEDED(hr)) check_vector(read_float_pixel(&ctx, 0, 0), cases[i].value, .002f);
            winetest_pop_context();
        }
        {
            D2D1_VECTOR_4F a_alpha = {.125f,.25f,.375f,.5f}, b_alpha = {.125f,.0625f,.125f,.25f};
            UINT32 mode = D2D1_BLEND_MODE_MULTIPLY;
            hr = ID2D1Bitmap1_CopyFromMemory(first, NULL, &a_alpha, sizeof(a_alpha));
            ok(hr == S_OK, "Got hr %#lx.\n", hr);
            hr = ID2D1Bitmap1_CopyFromMemory(second, NULL, &b_alpha, sizeof(b_alpha));
            ok(hr == S_OK, "Got hr %#lx.\n", hr);
            hr = ID2D1Effect_SetValue(effect, 0, D2D1_PROPERTY_TYPE_ENUM, (const BYTE *)&mode, sizeof(mode));
            ok(hr == S_OK, "Got hr %#lx.\n", hr);
            hr = draw_effect(&ctx, effect, NULL, NULL);
            ok(hr == S_OK, "Half-alpha blend failed, hr %#lx.\n", hr);
            if (SUCCEEDED(hr)) check_vector(read_float_pixel(&ctx, 0, 0),
                    (D2D1_VECTOR_4F){.171875f,.234375f,.390625f,.625f}, .002f);
        }
        ID2D1Effect_Release(effect);
    }
done:
    if (first) ID2D1Bitmap1_Release(first);
    if (second) ID2D1Bitmap1_Release(second);
    cleanup_effect_context(&ctx);
}

static void test_projective_transform(void)
{
    static const D2D1_VECTOR_4F pixels[2] = {{1,0,0,1},{0,0,1,1}};
    D2D1_MATRIX_4X4_F matrix = {{1,0,0,0, 0,1,0,0, 0,0,1,0, 8,4,0,1}};
    struct effect_test_context ctx;
    ID2D1Effect *effect;
    ID2D1Bitmap1 *bitmap;
    ID2D1Image *output;
    D2D1_RECT_F bounds;
    HRESULT hr;
    if (!init_effect_context(&ctx)) return;
    bitmap = create_float_bitmap(&ctx, 2, 1, pixels);
    if (!bitmap) { cleanup_effect_context(&ctx); return; }
    hr = ID2D1DeviceContext_CreateEffect(ctx.context, &CLSID_D2D13DTransform, &effect);
    ok(hr == S_OK, "CreateEffect failed, hr %#lx.\n", hr);
    if (SUCCEEDED(hr))
    {
        ID2D1Effect_SetInput(effect, 0, (ID2D1Image *)bitmap, TRUE);
        hr = ID2D1Effect_SetValue(effect, 2, D2D1_PROPERTY_TYPE_MATRIX_4X4, (BYTE *)&matrix, sizeof(matrix));
        ok(hr == S_OK, "Got hr %#lx.\n", hr);
        ID2D1Effect_GetOutput(effect, &output);
        hr = ID2D1DeviceContext_GetImageLocalBounds(ctx.context, output, &bounds);
        ok(hr == S_OK, "Bounds failed, hr %#lx.\n", hr);
        if (SUCCEEDED(hr)) ok(bounds.left == 8 && bounds.top == 4 && bounds.right == 10 && bounds.bottom == 5,
                "Wrong projected bounds.\n");
        ID2D1Image_Release(output);
        hr = draw_effect(&ctx, effect, NULL, NULL);
        ok(hr == S_OK, "Draw failed, hr %#lx.\n", hr);
        if (SUCCEEDED(hr)) check_vector(read_float_pixel(&ctx, 8, 4), pixels[0], .002f);
        ID2D1Effect_Release(effect);
    }
    ID2D1Bitmap1_Release(bitmap);
    cleanup_effect_context(&ctx);
}

static void test_perspective_transform(void)
{
    static const D2D1_VECTOR_4F red = {1,0,0,1};
    D2D1_VECTOR_3F offset = {8,4,0};
    struct effect_test_context ctx;
    ID2D1Effect *effect;
    ID2D1Bitmap1 *bitmap;
    HRESULT hr;
    if (!init_effect_context(&ctx)) return;
    bitmap = create_float_bitmap(&ctx, 1, 1, &red);
    if (!bitmap) { cleanup_effect_context(&ctx); return; }
    hr = ID2D1DeviceContext_CreateEffect(ctx.context, &CLSID_D2D13DPerspectiveTransform, &effect);
    ok(hr == S_OK, "CreateEffect failed, hr %#lx.\n", hr);
    if (SUCCEEDED(hr))
    {
        ID2D1Effect_SetInput(effect, 0, (ID2D1Image *)bitmap, TRUE);
        hr = ID2D1Effect_SetValue(effect, D2D1_3DPERSPECTIVETRANSFORM_PROP_GLOBAL_OFFSET,
                D2D1_PROPERTY_TYPE_VECTOR3, (const BYTE *)&offset, sizeof(offset));
        ok(hr == S_OK, "Got hr %#lx.\n", hr);
        hr = draw_effect(&ctx, effect, NULL, NULL);
        ok(hr == S_OK, "Draw failed, hr %#lx.\n", hr);
        if (SUCCEEDED(hr)) check_vector(read_float_pixel(&ctx, 8, 4), red, .002f);
        ID2D1Effect_Release(effect);
    }
    ID2D1Bitmap1_Release(bitmap);
    cleanup_effect_context(&ctx);
}

static void test_lighting(void)
{
    const GUID *ids[] = {&CLSID_D2D1DistantDiffuse, &CLSID_D2D1DistantSpecular,
            &CLSID_D2D1PointDiffuse, &CLSID_D2D1PointSpecular, &CLSID_D2D1SpotDiffuse, &CLSID_D2D1SpotSpecular};
    D2D1_VECTOR_4F pixels[25];
    D2D1_VECTOR_3F position = {2.5f,2.5f,11}, at = {2.5f,2.5f,0};
    struct effect_test_context ctx;
    ID2D1Bitmap1 *bitmap;
    ID2D1Effect *effect;
    unsigned int i;
    float elevation = 90;
    HRESULT hr;
    if (!init_effect_context(&ctx)) return;
    for (i = 0; i < ARRAY_SIZE(pixels); ++i) pixels[i] = (D2D1_VECTOR_4F){0,0,0,1};
    bitmap = create_float_bitmap(&ctx, 5, 5, pixels);
    if (!bitmap) { cleanup_effect_context(&ctx); return; }
    for (i = 0; i < ARRAY_SIZE(ids); ++i)
    {
        winetest_push_context("lighting %u", i);
        hr = ID2D1DeviceContext_CreateEffect(ctx.context, ids[i], &effect);
        ok(hr == S_OK, "CreateEffect failed, hr %#lx.\n", hr);
        if (SUCCEEDED(hr))
        {
            ID2D1Effect_SetInput(effect, 0, (ID2D1Image *)bitmap, TRUE);
            hr = ID2D1Effect_SetValue(effect, i < 2 ? 1 : 0,
                    i < 2 ? D2D1_PROPERTY_TYPE_FLOAT : D2D1_PROPERTY_TYPE_VECTOR3,
                    i < 2 ? (BYTE *)&elevation : (BYTE *)&position, i < 2 ? sizeof(elevation) : sizeof(position));
            ok(hr == S_OK, "Got hr %#lx.\n", hr);
            if (i >= 4)
            {
                hr = ID2D1Effect_SetValue(effect, 1, D2D1_PROPERTY_TYPE_VECTOR3, (BYTE *)&at, sizeof(at));
                ok(hr == S_OK, "Got hr %#lx.\n", hr);
            }
            hr = draw_effect(&ctx, effect, NULL, NULL);
            ok(hr == S_OK, "Draw failed, hr %#lx.\n", hr);
            if (SUCCEEDED(hr)) check_vector(read_float_pixel(&ctx, 2, 2), (D2D1_VECTOR_4F){1,1,1,1}, .002f);
            ID2D1Effect_Release(effect);
        }
        winetest_pop_context();
    }
    ID2D1Bitmap1_Release(bitmap);
    cleanup_effect_context(&ctx);
}

static void test_chroma_white_level(void)
{
    static const D2D1_VECTOR_4F pixels[2] = {{0,1,0,1},{1,0,0,1}};
    D2D1_VECTOR_3F key = {0,1,0};
    struct effect_test_context ctx;
    ID2D1Effect *effect;
    ID2D1Bitmap1 *bitmap;
    float input_white = 160, output_white = 80;
    HRESULT hr;
    if (!init_effect_context(&ctx)) return;
    bitmap = create_float_bitmap(&ctx, 2, 1, pixels);
    if (!bitmap) { cleanup_effect_context(&ctx); return; }
    hr = ID2D1DeviceContext_CreateEffect(ctx.context, &CLSID_D2D1ChromaKey, &effect);
    ok(hr == S_OK, "CreateEffect failed, hr %#lx.\n", hr);
    if (SUCCEEDED(hr))
    {
        ID2D1Effect_SetInput(effect, 0, (ID2D1Image *)bitmap, TRUE);
        hr = ID2D1Effect_SetValue(effect, 0, D2D1_PROPERTY_TYPE_VECTOR3, (BYTE *)&key, sizeof(key));
        ok(hr == S_OK, "Got hr %#lx.\n", hr);
        hr = draw_effect(&ctx, effect, NULL, NULL);
        ok(hr == S_OK, "Draw failed, hr %#lx.\n", hr);
        if (SUCCEEDED(hr))
        {
            check_vector(read_float_pixel(&ctx, 0, 0), (D2D1_VECTOR_4F){0}, .002f);
            check_vector(read_float_pixel(&ctx, 1, 0), pixels[1], .002f);
        }
        ID2D1Effect_Release(effect);
    }
    hr = ID2D1DeviceContext_CreateEffect(ctx.context, &CLSID_D2D1WhiteLevelAdjustment, &effect);
    ok(hr == S_OK, "CreateEffect failed, hr %#lx.\n", hr);
    if (SUCCEEDED(hr))
    {
        ID2D1Effect_SetInput(effect, 0, (ID2D1Image *)bitmap, TRUE);
        hr = ID2D1Effect_SetValue(effect, 0, D2D1_PROPERTY_TYPE_FLOAT, (BYTE *)&input_white, sizeof(float));
        ok(hr == S_OK, "Got hr %#lx.\n", hr);
        hr = ID2D1Effect_SetValue(effect, 1, D2D1_PROPERTY_TYPE_FLOAT, (BYTE *)&output_white, sizeof(float));
        ok(hr == S_OK, "Got hr %#lx.\n", hr);
        hr = draw_effect(&ctx, effect, NULL, NULL);
        ok(hr == S_OK, "Draw failed, hr %#lx.\n", hr);
        if (SUCCEEDED(hr)) check_vector(read_float_pixel(&ctx, 1, 0), (D2D1_VECTOR_4F){2,0,0,1}, .002f);
        ID2D1Effect_Release(effect);
    }
    ID2D1Bitmap1_Release(bitmap);
    cleanup_effect_context(&ctx);
}

static void test_bitmap_source(void)
{
    BYTE pixels[] = {0,0,255,255, 255,0,0,255};
    struct effect_test_context ctx;
    IWICImagingFactory *factory;
    IWICBitmap *bitmap;
    ID2D1Effect *effect;
    HRESULT hr;
    if (!init_effect_context(&ctx)) return;
    hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
            &IID_IWICImagingFactory, (void **)&factory);
    ok(hr == S_OK, "WIC factory failed, hr %#lx.\n", hr);
    if (FAILED(hr)) { cleanup_effect_context(&ctx); return; }
    hr = IWICImagingFactory_CreateBitmapFromMemory(factory, 2, 1, &GUID_WICPixelFormat32bppPBGRA,
            sizeof(pixels), sizeof(pixels), pixels, &bitmap);
    ok(hr == S_OK, "WIC bitmap failed, hr %#lx.\n", hr);
    if (SUCCEEDED(hr))
    {
        hr = ID2D1DeviceContext_CreateEffect(ctx.context, &CLSID_D2D1BitmapSource, &effect);
        ok(hr == S_OK, "CreateEffect failed, hr %#lx.\n", hr);
        if (SUCCEEDED(hr))
        {
            hr = ID2D1Effect_SetValue(effect, 0, D2D1_PROPERTY_TYPE_IUNKNOWN, (const BYTE *)&bitmap, sizeof(bitmap));
            ok(hr == S_OK, "Set source failed, hr %#lx.\n", hr);
            hr = draw_effect(&ctx, effect, NULL, NULL);
            ok(hr == S_OK, "Draw failed, hr %#lx.\n", hr);
            if (SUCCEEDED(hr))
            {
                check_vector(read_float_pixel(&ctx, 0, 0), (D2D1_VECTOR_4F){1,0,0,1}, .001f);
                check_vector(read_float_pixel(&ctx, 1, 0), (D2D1_VECTOR_4F){0,0,1,1}, .001f);
            }
            ID2D1Effect_Release(effect);
        }
        IWICBitmap_Release(bitmap);
    }
    IWICImagingFactory_Release(factory);
    cleanup_effect_context(&ctx);
}

static void test_histogram(void)
{
    static const D2D1_VECTOR_4F pixels[4] = {{0,0,0,1},{.3f,0,0,1},{.6f,0,0,1},{1,0,0,1}};
    struct effect_test_context ctx;
    ID2D1Effect *effect;
    ID2D1Bitmap1 *bitmap;
    UINT32 bins = 4;
    float histogram[4] = {0};
    unsigned int i;
    HRESULT hr;
    if (!init_effect_context(&ctx)) return;
    bitmap = create_float_bitmap(&ctx, 4, 1, pixels);
    if (!bitmap) { cleanup_effect_context(&ctx); return; }
    hr = ID2D1DeviceContext_CreateEffect(ctx.context, &CLSID_D2D1Histogram, &effect);
    ok(hr == S_OK, "CreateEffect failed, hr %#lx.\n", hr);
    if (SUCCEEDED(hr))
    {
        ID2D1Effect_SetInput(effect, 0, (ID2D1Image *)bitmap, TRUE);
        hr = ID2D1Effect_SetValue(effect, 0, D2D1_PROPERTY_TYPE_UINT32, (BYTE *)&bins, sizeof(bins));
        ok(hr == S_OK, "Got hr %#lx.\n", hr);
        hr = draw_effect(&ctx, effect, NULL, NULL);
        ok(hr == S_OK, "Draw failed, hr %#lx.\n", hr);
        hr = ID2D1Effect_GetValue(effect, 2, D2D1_PROPERTY_TYPE_BLOB, (BYTE *)histogram, sizeof(histogram));
        ok(hr == S_OK, "Histogram retrieval failed, hr %#lx.\n", hr);
        if (SUCCEEDED(hr)) for (i = 0; i < 4; ++i)
            ok(fabsf(histogram[i] - .25f) < .0001f, "Bin %u is %g.\n", i, histogram[i]);
        ID2D1Effect_Release(effect);
    }
    ID2D1Bitmap1_Release(bitmap);
    cleanup_effect_context(&ctx);
}

static void test_contrast(void)
{
    static const D2D1_VECTOR_4F pixel = {.25f,.5f,.75f,1};
    struct effect_test_context ctx;
    ID2D1Effect *effect;
    ID2D1Bitmap1 *bitmap;
    float contrast = 1;
    HRESULT hr;
    if (!init_effect_context(&ctx)) return;
    bitmap = create_float_bitmap(&ctx, 1, 1, &pixel);
    if (!bitmap) { cleanup_effect_context(&ctx); return; }
    hr = ID2D1DeviceContext_CreateEffect(ctx.context, &CLSID_D2D1Contrast, &effect);
    ok(hr == S_OK, "CreateEffect failed, hr %#lx.\n", hr);
    if (SUCCEEDED(hr))
    {
        ID2D1Effect_SetInput(effect, 0, (ID2D1Image *)bitmap, TRUE);
        hr = ID2D1Effect_SetValue(effect, 0, D2D1_PROPERTY_TYPE_FLOAT, (BYTE *)&contrast, sizeof(float));
        ok(hr == S_OK, "Got hr %#lx.\n", hr);
        hr = draw_effect(&ctx, effect, NULL, NULL);
        ok(hr == S_OK, "Draw failed, hr %#lx.\n", hr);
        if (SUCCEEDED(hr)) check_vector(read_float_pixel(&ctx, 0, 0), (D2D1_VECTOR_4F){.125f,.5f,.875f,1}, .002f);
        ID2D1Effect_Release(effect);
    }
    ID2D1Bitmap1_Release(bitmap);
    cleanup_effect_context(&ctx);
}

static void test_straighten(void)
{
    static const D2D1_VECTOR_4F pixels[4] = {{1,0,0,1},{0,1,0,1},{0,0,1,1},{1,1,1,1}};
    struct effect_test_context ctx;
    ID2D1Bitmap1 *bitmap;
    ID2D1Effect *effect;
    ID2D1Image *output;
    D2D1_RECT_F bounds;
    float angle = 45;
    BOOL maintain = FALSE;
    HRESULT hr;
    if (!init_effect_context(&ctx)) return;
    bitmap = create_float_bitmap(&ctx, 2, 2, pixels);
    if (!bitmap) { cleanup_effect_context(&ctx); return; }
    hr = ID2D1DeviceContext_CreateEffect(ctx.context, &CLSID_D2D1Straighten, &effect);
    ok(hr == S_OK, "CreateEffect failed, hr %#lx.\n", hr);
    if (SUCCEEDED(hr))
    {
        ID2D1Effect_SetInput(effect, 0, (ID2D1Image *)bitmap, TRUE);
        hr = draw_effect(&ctx, effect, NULL, NULL);
        ok(hr == S_OK, "Draw failed, hr %#lx.\n", hr);
        if (SUCCEEDED(hr)) check_vector(read_float_pixel(&ctx, 0, 0), pixels[0], .002f);
        hr = ID2D1Effect_SetValue(effect, 0, D2D1_PROPERTY_TYPE_FLOAT, (BYTE *)&angle, sizeof(angle));
        ok(hr == S_OK, "Got hr %#lx.\n", hr);
        hr = ID2D1Effect_SetValue(effect, 1, D2D1_PROPERTY_TYPE_BOOL, (BYTE *)&maintain, sizeof(maintain));
        ok(hr == S_OK, "Got hr %#lx.\n", hr);
        ID2D1Effect_GetOutput(effect, &output);
        hr = ID2D1DeviceContext_GetImageLocalBounds(ctx.context, output, &bounds);
        ok(hr == S_OK, "Bounds failed, hr %#lx.\n", hr);
        if (SUCCEEDED(hr)) ok(fabsf(bounds.right - bounds.left - sqrtf(8)) < .001f, "Wrong rotated width.\n");
        ID2D1Image_Release(output);
        ID2D1Effect_Release(effect);
    }
    ID2D1Bitmap1_Release(bitmap);
    cleanup_effect_context(&ctx);
}

static void test_ycbcr(void)
{
    BYTE luma[2] = {0,255}, chroma[2] = {128,128};
    D2D1_BITMAP_PROPERTIES1 props = {{DXGI_FORMAT_R8_UNORM,D2D1_ALPHA_MODE_IGNORE},96,96,0,NULL};
    struct effect_test_context ctx;
    ID2D1Bitmap1 *y = NULL, *cbcr = NULL;
    ID2D1Effect *effect;
    HRESULT hr;
    if (!init_effect_context(&ctx)) return;
    hr = ID2D1DeviceContext_CreateBitmap(ctx.context, (D2D1_SIZE_U){2,1}, luma, 2, &props, &y);
    ok(hr == S_OK, "Luma bitmap failed, hr %#lx.\n", hr);
    props.pixelFormat.format = DXGI_FORMAT_R8G8_UNORM;
    hr = ID2D1DeviceContext_CreateBitmap(ctx.context, (D2D1_SIZE_U){1,1}, chroma, 2, &props, &cbcr);
    ok(hr == S_OK, "Chroma bitmap failed, hr %#lx.\n", hr);
    if (y && cbcr)
    {
        hr = ID2D1DeviceContext_CreateEffect(ctx.context, &CLSID_D2D1YCbCr, &effect);
        ok(hr == S_OK, "CreateEffect failed, hr %#lx.\n", hr);
        if (SUCCEEDED(hr))
        {
            ID2D1Effect_SetInput(effect, 0, (ID2D1Image *)y, TRUE);
            ID2D1Effect_SetInput(effect, 1, (ID2D1Image *)cbcr, TRUE);
            hr = draw_effect(&ctx, effect, NULL, NULL);
            ok(hr == S_OK, "Draw failed, hr %#lx.\n", hr);
            if (SUCCEEDED(hr))
            {
                check_vector(read_float_pixel(&ctx, 0, 0), (D2D1_VECTOR_4F){0,0,0,1}, .005f);
                check_vector(read_float_pixel(&ctx, 1, 0), (D2D1_VECTOR_4F){1,1,1,1}, .005f);
            }
            ID2D1Effect_Release(effect);
        }
    }
    if (y) ID2D1Bitmap1_Release(y);
    if (cbcr) ID2D1Bitmap1_Release(cbcr);
    cleanup_effect_context(&ctx);
}

static void test_lookup_table(void)
{
    D2D1_VECTOR_4F data[8], pixel = {1,0,0,1};
    UINT32 extents[3] = {2,2,2}, strides[2] = {32,64};
    struct effect_test_context ctx;
    ID2D1DeviceContext2 *context2;
    ID2D1LookupTable3D *lut;
    ID2D1Bitmap1 *bitmap;
    ID2D1Effect *effect;
    unsigned int r, g, b;
    HRESULT hr;
    if (!init_effect_context(&ctx)) return;
    hr = ID2D1DeviceContext_QueryInterface(ctx.context, &IID_ID2D1DeviceContext2, (void **)&context2);
    if (FAILED(hr)) { win_skip("DeviceContext2 unavailable.\n"); cleanup_effect_context(&ctx); return; }
    for (r=0;r<2;++r) for (g=0;g<2;++g) for (b=0;b<2;++b)
        data[r*4+g*2+b] = (D2D1_VECTOR_4F){b,g,r,1};
    hr = ID2D1DeviceContext2_CreateLookupTable3D(context2, D2D1_BUFFER_PRECISION_32BPC_FLOAT,
            extents, (BYTE *)data, sizeof(data), strides, &lut);
    ok(hr == S_OK, "CreateLookupTable3D failed, hr %#lx.\n", hr);
    if (SUCCEEDED(hr))
    {
        bitmap = create_float_bitmap(&ctx, 1, 1, &pixel);
        hr = ID2D1DeviceContext_CreateEffect(ctx.context, &CLSID_D2D1LookupTable3D, &effect);
        ok(hr == S_OK, "CreateEffect failed, hr %#lx.\n", hr);
        if (SUCCEEDED(hr))
        {
            ID2D1Effect_SetInput(effect, 0, (ID2D1Image *)bitmap, TRUE);
            hr = ID2D1Effect_SetValue(effect, 0, D2D1_PROPERTY_TYPE_IUNKNOWN, (BYTE *)&lut, sizeof(lut));
            ok(hr == S_OK, "Set LUT failed, hr %#lx.\n", hr);
            hr = draw_effect(&ctx, effect, NULL, NULL);
            ok(hr == S_OK, "Draw failed, hr %#lx.\n", hr);
            if (SUCCEEDED(hr)) check_vector(read_float_pixel(&ctx, 0, 0), (D2D1_VECTOR_4F){0,0,1,1}, .002f);
            ID2D1Effect_Release(effect);
        }
        if (bitmap) ID2D1Bitmap1_Release(bitmap);
        ID2D1LookupTable3D_Release(lut);
    }
    ID2D1DeviceContext2_Release(context2);
    cleanup_effect_context(&ctx);
}

static void test_color_management(void)
{
    D2D1_VECTOR_4F pixel = {.5f,.5f,.5f,1};
    struct effect_test_context ctx;
    ID2D1ColorContext *srgb = NULL, *linear = NULL;
    ID2D1Bitmap1 *bitmap;
    ID2D1Effect *effect;
    HRESULT hr;
    if (!init_effect_context(&ctx)) return;
    hr = ID2D1DeviceContext_CreateColorContext(ctx.context, D2D1_COLOR_SPACE_SRGB, NULL, 0, &srgb);
    ok(hr == S_OK, "sRGB context failed, hr %#lx.\n", hr);
    hr = ID2D1DeviceContext_CreateColorContext(ctx.context, D2D1_COLOR_SPACE_SCRGB, NULL, 0, &linear);
    ok(hr == S_OK, "scRGB context failed, hr %#lx.\n", hr);
    if (srgb && linear)
    {
        bitmap = create_float_bitmap(&ctx, 1, 1, &pixel);
        hr = ID2D1DeviceContext_CreateEffect(ctx.context, &CLSID_D2D1ColorManagement, &effect);
        ok(hr == S_OK, "CreateEffect failed, hr %#lx.\n", hr);
        if (SUCCEEDED(hr))
        {
            ID2D1Effect_SetInput(effect, 0, (ID2D1Image *)bitmap, TRUE);
            hr = ID2D1Effect_SetValue(effect, 0, D2D1_PROPERTY_TYPE_COLOR_CONTEXT, (BYTE *)&srgb, sizeof(srgb));
            ok(hr == S_OK, "Set source context failed, hr %#lx.\n", hr);
            hr = ID2D1Effect_SetValue(effect, 2, D2D1_PROPERTY_TYPE_COLOR_CONTEXT, (BYTE *)&linear, sizeof(linear));
            ok(hr == S_OK, "Set destination context failed, hr %#lx.\n", hr);
            hr = draw_effect(&ctx, effect, NULL, NULL);
            ok(hr == S_OK, "Draw failed, hr %#lx.\n", hr);
            if (SUCCEEDED(hr)) check_vector(read_float_pixel(&ctx, 0, 0),
                    (D2D1_VECTOR_4F){.214041f,.214041f,.214041f,1}, .002f);
            ID2D1Effect_Release(effect);
        }
        if (bitmap) ID2D1Bitmap1_Release(bitmap);
    }
    if (srgb) ID2D1ColorContext_Release(srgb);
    if (linear) ID2D1ColorContext_Release(linear);
    cleanup_effect_context(&ctx);
}

static void test_color_context_wic(void)
{
    struct effect_test_context ctx;
    IWICImagingFactory *wic;
    IWICColorContext *wic_context;
    ID2D1ColorContext *context;
    HRESULT hr;
    if(!init_effect_context(&ctx))return;
    hr=CoCreateInstance(&CLSID_WICImagingFactory,NULL,CLSCTX_INPROC_SERVER,&IID_IWICImagingFactory,(void **)&wic);
    ok(hr==S_OK,"WIC factory failed, hr %#lx.\n",hr);
    if(SUCCEEDED(hr))
    {
        hr=IWICImagingFactory_CreateColorContext(wic,&wic_context);
        ok(hr==S_OK,"WIC context failed, hr %#lx.\n",hr);
        if(SUCCEEDED(hr))
        {
            hr=IWICColorContext_InitializeFromExifColorSpace(wic_context,1);
            ok(hr==S_OK,"EXIF initialize failed, hr %#lx.\n",hr);
            hr=ID2D1DeviceContext_CreateColorContextFromWicColorContext(ctx.context,wic_context,&context);
            ok(hr==S_OK,"D2D WIC context failed, hr %#lx.\n",hr);
            if(SUCCEEDED(hr))
            {
                ok(ID2D1ColorContext_GetColorSpace(context)==D2D1_COLOR_SPACE_SRGB,"Expected sRGB context.\n");
                ID2D1ColorContext_Release(context);
            }
            IWICColorContext_Release(wic_context);
        }
        IWICImagingFactory_Release(wic);
    }
    cleanup_effect_context(&ctx);
}

static void test_turbulence(void)
{
    D2D1_VECTOR_2F size = {16,16}, frequency = {.125f,.125f};
    struct effect_test_context ctx;
    ID2D1Effect *effect;
    D2D1_VECTOR_4F first, repeated, changed;
    UINT32 seed = 17;
    HRESULT hr;
    if (!init_effect_context(&ctx)) return;
    hr = ID2D1DeviceContext_CreateEffect(ctx.context, &CLSID_D2D1Turbulence, &effect);
    ok(hr == S_OK, "CreateEffect failed, hr %#lx.\n", hr);
    if (SUCCEEDED(hr))
    {
        hr = ID2D1Effect_SetValue(effect, 1, D2D1_PROPERTY_TYPE_VECTOR2, (BYTE *)&size, sizeof(size));
        ok(hr == S_OK, "Got hr %#lx.\n", hr);
        hr = ID2D1Effect_SetValue(effect, 2, D2D1_PROPERTY_TYPE_VECTOR2, (BYTE *)&frequency, sizeof(frequency));
        ok(hr == S_OK, "Got hr %#lx.\n", hr);
        hr = draw_effect(&ctx, effect, NULL, NULL);
        ok(hr == S_OK, "Draw failed, hr %#lx.\n", hr);
        if (SUCCEEDED(hr))
        {
            first = read_float_pixel(&ctx, 4, 4);
            hr = draw_effect(&ctx, effect, NULL, NULL);
            ok(hr == S_OK, "Draw failed, hr %#lx.\n", hr);
            repeated = read_float_pixel(&ctx, 4, 4);
            check_vector(repeated, first, .0001f);
            ok(first.w > 0 && first.w < 1 && first.x <= first.w && first.y <= first.w && first.z <= first.w,
                    "Expected nonconstant premultiplied noise.\n");
            hr = ID2D1Effect_SetValue(effect, 4, D2D1_PROPERTY_TYPE_UINT32, (BYTE *)&seed, sizeof(seed));
            ok(hr == S_OK, "Got hr %#lx.\n", hr);
            hr = draw_effect(&ctx, effect, NULL, NULL);
            ok(hr == S_OK, "Draw failed, hr %#lx.\n", hr);
            changed = read_float_pixel(&ctx, 4, 4);
            ok(memcmp(&changed, &first, sizeof(first)), "Changing seed did not change output.\n");
        }
        ID2D1Effect_Release(effect);
    }
    cleanup_effect_context(&ctx);
}

static void test_edge_filters(void)
{
    const GUID *ids[] = {&CLSID_D2D1Sharpen, &CLSID_D2D1Emboss, &CLSID_D2D1EdgeDetection};
    D2D1_VECTOR_4F pixels[25];
    struct effect_test_context ctx;
    ID2D1Effect *effect;
    ID2D1Bitmap1 *bitmap;
    unsigned int i;
    HRESULT hr;
    if (!init_effect_context(&ctx)) return;
    for(i=0;i<25;++i)pixels[i]=(D2D1_VECTOR_4F){.25f,.25f,.25f,1};
    bitmap=create_float_bitmap(&ctx,5,5,pixels);
    if(!bitmap){cleanup_effect_context(&ctx);return;}
    for(i=0;i<ARRAY_SIZE(ids);++i)
    {
        winetest_push_context("edge filter %u",i);
        hr=ID2D1DeviceContext_CreateEffect(ctx.context,ids[i],&effect);
        ok(hr==S_OK,"CreateEffect failed, hr %#lx.\n",hr);
        if(SUCCEEDED(hr))
        {
            ID2D1Effect_SetInput(effect,0,(ID2D1Image *)bitmap,TRUE);
            hr=draw_effect(&ctx,effect,NULL,NULL);
            ok(hr==S_OK,"Draw failed, hr %#lx.\n",hr);
            if(SUCCEEDED(hr))check_vector(read_float_pixel(&ctx,2,2),
                    i==0?pixels[0]:i==1?(D2D1_VECTOR_4F){.5f,.5f,.5f,1}:(D2D1_VECTOR_4F){0,0,0,1},.002f);
            ID2D1Effect_Release(effect);
        }
        winetest_pop_context();
    }
    ID2D1Bitmap1_Release(bitmap);
    cleanup_effect_context(&ctx);
}

static void test_empty_and_unbounded_bounds(void)
{
    static const D2D1_VECTOR_4F pixel = {1,0,0,1};
    struct effect_test_context ctx;
    ID2D1Effect *effect;
    ID2D1Bitmap1 *bitmap;
    ID2D1Image *output;
    D2D1_RECT_F rect = {-4,-4,-2,-2}, bounds;
    HRESULT hr;
    if (!init_effect_context(&ctx)) return;
    bitmap = create_float_bitmap(&ctx,1,1,&pixel);
    if (!bitmap) { cleanup_effect_context(&ctx); return; }
    hr = ID2D1DeviceContext_CreateEffect(ctx.context,&CLSID_D2D1Crop,&effect);
    ok(hr == S_OK,"CreateEffect failed, hr %#lx.\n",hr);
    if (SUCCEEDED(hr))
    {
        ID2D1Effect_SetInput(effect,0,(ID2D1Image *)bitmap,TRUE);
        hr = ID2D1Effect_SetValue(effect,0,D2D1_PROPERTY_TYPE_VECTOR4,(BYTE *)&rect,sizeof(rect));
        ok(hr == S_OK,"Got hr %#lx.\n",hr);
        ID2D1Effect_GetOutput(effect,&output);
        hr = ID2D1DeviceContext_GetImageLocalBounds(ctx.context,output,&bounds);
        ok(hr == S_OK,"Got hr %#lx.\n",hr);
        ok(bounds.left == 0 && bounds.top == 0 && bounds.right == 0 && bounds.bottom == 0,"Empty bounds not canonical.\n");
        ID2D1Image_Release(output);
        ID2D1Effect_Release(effect);
    }
    hr = ID2D1DeviceContext_CreateEffect(ctx.context,&CLSID_D2D1Flood,&effect);
    ok(hr == S_OK,"CreateEffect failed, hr %#lx.\n",hr);
    if (SUCCEEDED(hr))
    {
        ID2D1Effect_GetOutput(effect,&output);
        hr = ID2D1DeviceContext_GetImageLocalBounds(ctx.context,output,&bounds);
        ok(hr == S_OK,"Got hr %#lx.\n",hr);
        ok(bounds.left == -(float)INT_MAX && bounds.top == -(float)INT_MAX
                && bounds.right == (float)INT_MAX && bounds.bottom == (float)INT_MAX,"Unexpected unbounded representation.\n");
        ID2D1Image_Release(output);
        ID2D1Effect_Release(effect);
    }
    ID2D1Bitmap1_Release(bitmap);
    cleanup_effect_context(&ctx);
}

static void test_temperature_tint(void)
{
    static const struct { float temperature, tint, red, blue; } cases[] =
    {
        {0,0,1,1}, {.5f,0,1.1886869f,.7568971f}, {0,.5f,.8897246f,.9033844f},
        {-.5f,-.5f,.8089710f,1.8849901f}, {1,1,1.2166918f,.3683208f},
    };
    D2D1_VECTOR_4F pixel = {.25f,.25f,.25f,.5f};
    struct effect_test_context ctx;
    ID2D1Bitmap1 *bitmap;
    ID2D1Effect *effect;
    unsigned int i;
    HRESULT hr;
    if (!init_effect_context(&ctx)) return;
    bitmap=create_float_bitmap(&ctx,1,1,&pixel);
    if(!bitmap){cleanup_effect_context(&ctx);return;}
    hr=ID2D1DeviceContext_CreateEffect(ctx.context,&CLSID_D2D1TemperatureTint,&effect);
    ok(hr==S_OK,"CreateEffect failed, hr %#lx.\n",hr);
    if(SUCCEEDED(hr))
    {
        ID2D1Effect_SetInput(effect,0,(ID2D1Image *)bitmap,TRUE);
        for(i=0;i<ARRAY_SIZE(cases);++i)
        {
            winetest_push_context("temperature %u",i);
            hr=ID2D1Effect_SetValue(effect,0,D2D1_PROPERTY_TYPE_FLOAT,(const BYTE *)&cases[i].temperature,sizeof(float));
            ok(hr==S_OK,"Got hr %#lx.\n",hr);
            hr=ID2D1Effect_SetValue(effect,1,D2D1_PROPERTY_TYPE_FLOAT,(const BYTE *)&cases[i].tint,sizeof(float));
            ok(hr==S_OK,"Got hr %#lx.\n",hr);
            hr=draw_effect(&ctx,effect,NULL,NULL);
            ok(hr==S_OK,"Draw failed, hr %#lx.\n",hr);
            if(SUCCEEDED(hr))check_vector(read_float_pixel(&ctx,0,0),
                    (D2D1_VECTOR_4F){.25f*cases[i].red,.25f,.25f*cases[i].blue,.5f},.001f);
            winetest_pop_context();
        }
        ID2D1Effect_Release(effect);
    }
    ID2D1Bitmap1_Release(bitmap);cleanup_effect_context(&ctx);
}

static void test_vignette(void)
{
    D2D1_VECTOR_4F pixels[64];
    struct effect_test_context ctx;
    ID2D1Bitmap1 *bitmap;
    ID2D1Effect *effect;
    float transition=1,strength=1;
    unsigned int i;
    HRESULT hr;
    if(!init_effect_context(&ctx))return;
    for(i=0;i<64;++i)pixels[i]=(D2D1_VECTOR_4F){1,1,1,1};
    bitmap=create_float_bitmap(&ctx,8,8,pixels);
    if(!bitmap){cleanup_effect_context(&ctx);return;}
    hr=ID2D1DeviceContext_CreateEffect(ctx.context,&CLSID_D2D1Vignette,&effect);
    ok(hr==S_OK,"CreateEffect failed, hr %#lx.\n",hr);
    if(SUCCEEDED(hr))
    {
        ID2D1Effect_SetInput(effect,0,(ID2D1Image *)bitmap,TRUE);
        hr=ID2D1Effect_SetValue(effect,1,D2D1_PROPERTY_TYPE_FLOAT,(BYTE *)&transition,sizeof(float));
        ok(hr==S_OK,"Got hr %#lx.\n",hr);
        hr=ID2D1Effect_SetValue(effect,2,D2D1_PROPERTY_TYPE_FLOAT,(BYTE *)&strength,sizeof(float));
        ok(hr==S_OK,"Got hr %#lx.\n",hr);
        hr=draw_effect(&ctx,effect,NULL,NULL);
        ok(hr==S_OK,"Draw failed, hr %#lx.\n",hr);
        if(SUCCEEDED(hr))
        {
            float t=sqrtf(.5f)/4,expected=1-t*t*(3-2*t);
            check_vector(read_float_pixel(&ctx,0,0),(D2D1_VECTOR_4F){0,0,0,1},.002f);
            check_vector(read_float_pixel(&ctx,3,3),(D2D1_VECTOR_4F){expected,expected,expected,1},.002f);
        }
        ID2D1Effect_Release(effect);
    }
    ID2D1Bitmap1_Release(bitmap);cleanup_effect_context(&ctx);
}

static void test_highlights_shadows(void)
{
    D2D1_VECTOR_4F pixel={.25f,.25f,.25f,1};
    struct effect_test_context ctx;
    ID2D1Effect *effect;
    ID2D1Bitmap1 *bitmap;
    float highlights=.5f,shadows=.5f,radius=0;
    UINT32 gamma=0;
    HRESULT hr;
    if(!init_effect_context(&ctx))return;
    bitmap=create_float_bitmap(&ctx,1,1,&pixel);
    if(!bitmap){cleanup_effect_context(&ctx);return;}
    hr=ID2D1DeviceContext_CreateEffect(ctx.context,&CLSID_D2D1HighlightsShadows,&effect);
    ok(hr==S_OK,"CreateEffect failed, hr %#lx.\n",hr);
    if(SUCCEEDED(hr))
    {
        ID2D1Effect_SetInput(effect,0,(ID2D1Image *)bitmap,TRUE);
        hr=ID2D1Effect_SetValue(effect,0,D2D1_PROPERTY_TYPE_FLOAT,(BYTE *)&highlights,sizeof(float));
        ok(hr==S_OK,"Got hr %#lx.\n",hr);
        hr=ID2D1Effect_SetValue(effect,1,D2D1_PROPERTY_TYPE_FLOAT,(BYTE *)&shadows,sizeof(float));
        ok(hr==S_OK,"Got hr %#lx.\n",hr);
        hr=ID2D1Effect_SetValue(effect,3,D2D1_PROPERTY_TYPE_ENUM,(BYTE *)&gamma,sizeof(gamma));
        ok(hr==S_OK,"Got hr %#lx.\n",hr);
        hr=ID2D1Effect_SetValue(effect,4,D2D1_PROPERTY_TYPE_FLOAT,(BYTE *)&radius,sizeof(float));
        ok(hr==S_OK,"Got hr %#lx.\n",hr);
        hr=draw_effect(&ctx,effect,NULL,NULL);
        ok(hr==S_OK,"Draw failed, hr %#lx.\n",hr);
        if(SUCCEEDED(hr))
        {
            float y=.25f,s=powf(5,.4f),h=4;
            float expected=(y*s/(y*(s-1)+1))*(1-y)*(1-y)+(y*h/(y*(h-1)+1))*y*y+y*2*y*(1-y);
            check_vector(read_float_pixel(&ctx,0,0),(D2D1_VECTOR_4F){expected,expected,expected,1},.003f);
        }
        radius=1.25f;
        hr=ID2D1Effect_SetValue(effect,4,D2D1_PROPERTY_TYPE_FLOAT,(BYTE *)&radius,sizeof(float));
        ok(hr==S_OK,"Got hr %#lx.\n",hr);
        hr=draw_effect(&ctx,effect,NULL,NULL);
        ok(hr==S_OK,"Blurred mask failed, hr %#lx.\n",hr);
        if(SUCCEEDED(hr))
        {
            float y=.25f,s=powf(5,.4f),h=4;
            float expected=(y*s/(y*(s-1)+1))*(1-y)*(1-y)+(y*h/(y*(h-1)+1))*y*y+y*2*y*(1-y);
            check_vector(read_float_pixel(&ctx,0,0),(D2D1_VECTOR_4F){expected,expected,expected,1},.003f);
        }
        ID2D1Effect_Release(effect);
    }
    ID2D1Bitmap1_Release(bitmap);cleanup_effect_context(&ctx);
}

static void test_hdr_tone_map(void)
{
    D2D1_VECTOR_4F pixels[3]={{.1f,.1f,.1f,1},{1,1,1,1},{4,4,4,1}};
    static const float expected[]={.1009004f,1.003004f,2.2003f};
    struct effect_test_context ctx;
    ID2D1Effect *effect;
    ID2D1Bitmap1 *bitmap;
    float input=1000,output=200;
    UINT32 display=1;
    unsigned int i;
    HRESULT hr;
    if(!init_effect_context(&ctx))return;
    bitmap=create_float_bitmap(&ctx,3,1,pixels);
    if(!bitmap){cleanup_effect_context(&ctx);return;}
    hr=ID2D1DeviceContext_CreateEffect(ctx.context,&CLSID_D2D1HdrToneMap,&effect);
    ok(hr==S_OK,"CreateEffect failed, hr %#lx.\n",hr);
    if(SUCCEEDED(hr))
    {
        ID2D1Effect_SetInput(effect,0,(ID2D1Image *)bitmap,TRUE);
        hr=ID2D1Effect_SetValue(effect,0,D2D1_PROPERTY_TYPE_FLOAT,(BYTE *)&input,sizeof(float));
        ok(hr==S_OK,"Got hr %#lx.\n",hr);
        hr=ID2D1Effect_SetValue(effect,1,D2D1_PROPERTY_TYPE_FLOAT,(BYTE *)&output,sizeof(float));
        ok(hr==S_OK,"Got hr %#lx.\n",hr);
        hr=ID2D1Effect_SetValue(effect,2,D2D1_PROPERTY_TYPE_ENUM,(BYTE *)&display,sizeof(display));
        ok(hr==S_OK,"Got hr %#lx.\n",hr);
        hr=draw_effect(&ctx,effect,NULL,NULL);
        ok(hr==S_OK,"Draw failed, hr %#lx.\n",hr);
        if(SUCCEEDED(hr))for(i=0;i<3;++i)check_vector(read_float_pixel(&ctx,i,0),
                (D2D1_VECTOR_4F){expected[i],expected[i],expected[i],1},.005f);
        ID2D1Effect_Release(effect);
    }
    ID2D1Bitmap1_Release(bitmap);cleanup_effect_context(&ctx);
}

static void test_icc_color_management(void)
{
    struct effect_test_context ctx;
    WCHAR filename[MAX_PATH];
    DWORD size=sizeof(filename);
    ID2D1ColorContext *profile;
    ID2D1Bitmap1 *bitmap;
    ID2D1Effect *effect;
    D2D1_VECTOR_4F pixel={.2f,.4f,.6f,1};
    HRESULT hr;
    if(!GetStandardColorSpaceProfileW(NULL,LCS_sRGB,filename,&size))
    {win_skip("Standard sRGB profile unavailable, error %lu.\n",GetLastError());return;}
    if(!init_effect_context(&ctx))return;
    hr=ID2D1DeviceContext_CreateColorContextFromFilename(ctx.context,filename,&profile);
    ok(hr==S_OK,"Profile context failed, hr %#lx.\n",hr);
    if(SUCCEEDED(hr))
    {
        bitmap=create_float_bitmap(&ctx,1,1,&pixel);
        hr=ID2D1DeviceContext_CreateEffect(ctx.context,&CLSID_D2D1ColorManagement,&effect);
        ok(hr==S_OK,"CreateEffect failed, hr %#lx.\n",hr);
        if(SUCCEEDED(hr))
        {
            ID2D1Effect_SetInput(effect,0,(ID2D1Image *)bitmap,TRUE);
            hr=ID2D1Effect_SetValue(effect,0,D2D1_PROPERTY_TYPE_COLOR_CONTEXT,(BYTE *)&profile,sizeof(profile));
            ok(hr==S_OK,"Got hr %#lx.\n",hr);
            hr=ID2D1Effect_SetValue(effect,2,D2D1_PROPERTY_TYPE_COLOR_CONTEXT,(BYTE *)&profile,sizeof(profile));
            ok(hr==S_OK,"Got hr %#lx.\n",hr);
            hr=draw_effect(&ctx,effect,NULL,NULL);
            ok(hr==S_OK,"ICC draw failed, hr %#lx.\n",hr);
            if(SUCCEEDED(hr))check_vector(read_float_pixel(&ctx,0,0),pixel,.005f);
            ID2D1Effect_Release(effect);
        }
        if(bitmap)ID2D1Bitmap1_Release(bitmap);
        ID2D1ColorContext_Release(profile);
    }
    cleanup_effect_context(&ctx);
}

static void test_transform_interpolation(void)
{
    D2D1_VECTOR_4F pixels[16];
    D2D1_MATRIX_3X2_F matrix={{{1,0,0,1,8,8}}};
    struct effect_test_context ctx;
    ID2D1Effect *effect;
    ID2D1Bitmap1 *bitmap;
    UINT32 mode;
    unsigned int i;
    HRESULT hr;
    if(!init_effect_context(&ctx))return;
    for(i=0;i<16;++i)pixels[i]=(D2D1_VECTOR_4F){.25f,.5f,.75f,1};
    bitmap=create_float_bitmap(&ctx,4,4,pixels);
    if(!bitmap){cleanup_effect_context(&ctx);return;}
    hr=ID2D1DeviceContext_CreateEffect(ctx.context,&CLSID_D2D12DAffineTransform,&effect);
    ok(hr==S_OK,"CreateEffect failed, hr %#lx.\n",hr);
    if(SUCCEEDED(hr))
    {
        ID2D1Effect_SetInput(effect,0,(ID2D1Image *)bitmap,TRUE);
        hr=ID2D1Effect_SetValue(effect,2,D2D1_PROPERTY_TYPE_MATRIX_3X2,(BYTE *)&matrix,sizeof(matrix));
        ok(hr==S_OK,"Got hr %#lx.\n",hr);
        for(mode=0;mode<6;++mode)
        {
            winetest_push_context("interpolation %u",mode);
            hr=ID2D1Effect_SetValue(effect,0,D2D1_PROPERTY_TYPE_ENUM,(BYTE *)&mode,sizeof(mode));
            ok(hr==S_OK,"Got hr %#lx.\n",hr);
            hr=draw_effect(&ctx,effect,NULL,NULL);
            ok(hr==S_OK,"Draw failed, hr %#lx.\n",hr);
            if(SUCCEEDED(hr))check_vector(read_float_pixel(&ctx,9,9),pixels[0],.004f);
            winetest_pop_context();
        }
        ID2D1Effect_Release(effect);
    }
    ID2D1Bitmap1_Release(bitmap);cleanup_effect_context(&ctx);
}

static void test_unbounded_offset(void)
{
    struct effect_test_context ctx;
    ID2D1Effect *effect;
    D2D1_POINT_2F offset={-16,-16};
    D2D1_RECT_F source={100,100,132,132};
    D2D1_VECTOR_4F red={1,0,0,1};
    HRESULT hr;
    if(!init_effect_context(&ctx))return;
    hr=ID2D1DeviceContext_CreateEffect(ctx.context,&CLSID_D2D1Flood,&effect);
    ok(hr==S_OK,"CreateEffect failed, hr %#lx.\n",hr);
    if(SUCCEEDED(hr))
    {
        hr=ID2D1Effect_SetValue(effect,0,D2D1_PROPERTY_TYPE_VECTOR4,(BYTE *)&red,sizeof(red));
        ok(hr==S_OK,"Got hr %#lx.\n",hr);
        hr=draw_effect(&ctx,effect,&offset,NULL);
        ok(hr==S_OK,"Draw failed, hr %#lx.\n",hr);
        if(SUCCEEDED(hr))check_vector(read_float_pixel(&ctx,31,31),red,.001f);
        hr=draw_effect(&ctx,effect,NULL,&source);
        ok(hr==S_OK,"Source rectangle draw failed, hr %#lx.\n",hr);
        if(SUCCEEDED(hr))check_vector(read_float_pixel(&ctx,16,16),red,.001f);
        ID2D1Effect_Release(effect);
    }
    cleanup_effect_context(&ctx);
}

static void test_composite_mask_invert(void)
{
    D2D1_VECTOR_4F a={.5f,0,0,.5f},b={0,.25f,0,.25f};
    struct effect_test_context ctx;
    ID2D1Effect *effect;
    ID2D1Bitmap1 *first,*second;
    UINT32 mode=D2D1_COMPOSITE_MODE_MASK_INVERT;
    HRESULT hr;
    if(!init_effect_context(&ctx))return;
    first=create_float_bitmap(&ctx,1,1,&a);second=create_float_bitmap(&ctx,1,1,&b);
    if(!first||!second)goto done;
    hr=ID2D1DeviceContext_CreateEffect(ctx.context,&CLSID_D2D1Composite,&effect);
    ok(hr==S_OK,"CreateEffect failed, hr %#lx.\n",hr);
    if(SUCCEEDED(hr))
    {
        ID2D1Effect_SetInput(effect,0,(ID2D1Image *)first,TRUE);
        ID2D1Effect_SetInput(effect,1,(ID2D1Image *)second,TRUE);
        hr=ID2D1Effect_SetValue(effect,0,D2D1_PROPERTY_TYPE_ENUM,(BYTE *)&mode,sizeof(mode));
        ok(hr==S_OK,"Got hr %#lx.\n",hr);
        hr=draw_effect(&ctx,effect,NULL,NULL);
        ok(hr==S_OK,"MASK_INVERT failed, hr %#lx.\n",hr);
        if(SUCCEEDED(hr))check_vector(read_float_pixel(&ctx,0,0),(D2D1_VECTOR_4F){.375f,.25f,0,.5f},.002f);
        ID2D1Effect_Release(effect);
    }
done:
    if(first)ID2D1Bitmap1_Release(first);
    if(second)ID2D1Bitmap1_Release(second);
    cleanup_effect_context(&ctx);
}

static void test_composite_bounds(void)
{
    D2D1_VECTOR_4F pixels[2]={{1,0,0,1},{1,0,0,1}};
    struct effect_test_context ctx;
    ID2D1Effect *effect;
    ID2D1Bitmap1 *wide,*narrow;
    ID2D1Image *output;
    D2D1_RECT_F bounds;
    UINT32 mode;
    HRESULT hr;
    if(!init_effect_context(&ctx))return;
    wide=create_float_bitmap(&ctx,2,1,pixels);narrow=create_float_bitmap(&ctx,1,1,pixels);
    if(!wide||!narrow)goto done;
    hr=ID2D1DeviceContext_CreateEffect(ctx.context,&CLSID_D2D1Composite,&effect);
    ok(hr==S_OK,"CreateEffect failed, hr %#lx.\n",hr);
    if(SUCCEEDED(hr))
    {
        ID2D1Effect_SetInput(effect,0,(ID2D1Image *)wide,TRUE);
        ID2D1Effect_SetInput(effect,1,(ID2D1Image *)narrow,TRUE);
        ID2D1Effect_GetOutput(effect,&output);
        for(mode=0;mode<=D2D1_COMPOSITE_MODE_MASK_INVERT;++mode)
        {
            float width=mode==2||mode==3||mode==4||mode==7||mode==10?1:2;
            winetest_push_context("composite bounds %u",mode);
            hr=ID2D1Effect_SetValue(effect,0,D2D1_PROPERTY_TYPE_ENUM,(BYTE *)&mode,sizeof(mode));
            ok(hr==S_OK,"Got hr %#lx.\n",hr);
            hr=ID2D1DeviceContext_GetImageLocalBounds(ctx.context,output,&bounds);
            ok(hr==S_OK,"Bounds failed, hr %#lx.\n",hr);
            if(SUCCEEDED(hr))ok(bounds.left==0&&bounds.top==0&&bounds.right==width&&bounds.bottom==1,"Wrong output bounds.\n");
            winetest_pop_context();
        }
        ID2D1Image_Release(output);ID2D1Effect_Release(effect);
    }
done:
    if(wide)ID2D1Bitmap1_Release(wide);
    if(narrow)ID2D1Bitmap1_Release(narrow);
    cleanup_effect_context(&ctx);
}

static void test_bitmap_source_orientations(void)
{
    static const DWORD pixels[6] = {0xffff0000, 0xff00ff00, 0xff0000ff, 0xffffff00, 0xffff00ff, 0xff00ffff};
    static const unsigned int expected[8][6] =
    {
        {0,1,2,3,4,5}, {2,1,0,5,4,3}, {5,4,3,2,1,0}, {3,4,5,0,1,2},
        {0,3,1,4,2,5}, {3,0,4,1,5,2}, {5,2,4,1,3,0}, {2,5,1,4,0,3},
    };
    struct effect_test_context ctx;
    IWICImagingFactory *wic;
    IWICBitmap *bitmap;
    ID2D1Effect *effect;
    ID2D1Image *output;
    D2D1_RECT_F bounds;
    UINT32 orientation;
    unsigned int width, height, i;
    HRESULT hr;

    if (!init_effect_context(&ctx)) return;
    hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, (void **)&wic);
    ok(hr == S_OK, "WIC factory failed, hr %#lx.\n", hr);
    if (FAILED(hr)) goto done;
    hr = IWICImagingFactory_CreateBitmapFromMemory(wic, 3, 2, &GUID_WICPixelFormat32bppPBGRA,
            3 * sizeof(DWORD), sizeof(pixels), (BYTE *)pixels, &bitmap);
    ok(hr == S_OK, "WIC bitmap failed, hr %#lx.\n", hr);
    if (SUCCEEDED(hr))
    {
        hr = ID2D1DeviceContext_CreateEffect(ctx.context, &CLSID_D2D1BitmapSource, &effect);
        ok(hr == S_OK, "CreateEffect failed, hr %#lx.\n", hr);
        if (SUCCEEDED(hr))
        {
            hr = ID2D1Effect_SetValue(effect, 0, D2D1_PROPERTY_TYPE_IUNKNOWN, (BYTE *)&bitmap, sizeof(bitmap));
            ok(hr == S_OK, "Set source failed, hr %#lx.\n", hr);
            ID2D1Effect_GetOutput(effect, &output);
            for (orientation = 1; orientation <= 8; ++orientation)
            {
                winetest_push_context("orientation %u", orientation);
                width = orientation >= 5 ? 2 : 3; height = orientation >= 5 ? 3 : 2;
                hr = ID2D1Effect_SetValue(effect, 5, D2D1_PROPERTY_TYPE_ENUM, (BYTE *)&orientation, sizeof(orientation));
                ok(hr == S_OK, "Set orientation failed, hr %#lx.\n", hr);
                hr = ID2D1DeviceContext_GetImageLocalBounds(ctx.context, output, &bounds);
                ok(hr == S_OK, "Bounds failed, hr %#lx.\n", hr);
                if (SUCCEEDED(hr)) ok(bounds.left == 0 && bounds.top == 0 && bounds.right == width && bounds.bottom == height,
                        "Unexpected oriented bounds {%g,%g,%g,%g}.\n", bounds.left, bounds.top, bounds.right, bounds.bottom);
                hr = draw_effect(&ctx, effect, NULL, NULL);
                ok(hr == S_OK, "Draw failed, hr %#lx.\n", hr);
                if (SUCCEEDED(hr)) for (i = 0; i < 6; ++i)
                {
                    DWORD p = pixels[expected[orientation - 1][i]];
                    check_vector(read_float_pixel(&ctx, i % width, i / width),
                            (D2D1_VECTOR_4F){((p >> 16) & 255) / 255.0f, ((p >> 8) & 255) / 255.0f, (p & 255) / 255.0f, 1}, .002f);
                }
                winetest_pop_context();
            }
            ID2D1Image_Release(output);
            ID2D1Effect_Release(effect);
        }
        IWICBitmap_Release(bitmap);
    }
    IWICImagingFactory_Release(wic);
done:
    cleanup_effect_context(&ctx);
}

static void test_edge_detection_blur(void)
{
    D2D1_VECTOR_4F pixels[81], sharp, blurred;
    struct effect_test_context ctx;
    ID2D1Bitmap1 *bitmap;
    ID2D1Effect *effect;
    float radius = 0;
    unsigned int x, y;
    HRESULT hr;

    if (!init_effect_context(&ctx)) return;
    for (y = 0; y < 9; ++y) for (x = 0; x < 9; ++x)
        pixels[y * 9 + x] = (D2D1_VECTOR_4F){x >= 4, x >= 4, x >= 4, 1};
    bitmap = create_float_bitmap(&ctx, 9, 9, pixels);
    if (!bitmap) { cleanup_effect_context(&ctx); return; }
    hr = ID2D1DeviceContext_CreateEffect(ctx.context, &CLSID_D2D1EdgeDetection, &effect);
    ok(hr == S_OK, "CreateEffect failed, hr %#lx.\n", hr);
    if (SUCCEEDED(hr))
    {
        ID2D1Effect_SetInput(effect, 0, (ID2D1Image *)bitmap, TRUE);
        hr = draw_effect(&ctx, effect, NULL, NULL);
        ok(hr == S_OK, "Sharp edge draw failed, hr %#lx.\n", hr);
        sharp = read_float_pixel(&ctx, 2, 4);
        radius = 1;
        hr = ID2D1Effect_SetValue(effect, 1, D2D1_PROPERTY_TYPE_FLOAT, (BYTE *)&radius, sizeof(radius));
        ok(hr == S_OK, "Set radius failed, hr %#lx.\n", hr);
        hr = draw_effect(&ctx, effect, NULL, NULL);
        ok(hr == S_OK, "Blurred edge draw failed, hr %#lx.\n", hr);
        if (SUCCEEDED(hr))
        {
            blurred = read_float_pixel(&ctx, 2, 4);
            ok(sharp.x < .001f && blurred.x > .05f, "Blur did not spread edge: %g -> %g.\n", sharp.x, blurred.x);
            check_vector(read_float_pixel(&ctx, 0, 4), (D2D1_VECTOR_4F){0,0,0,1}, .015f);
        }
        ID2D1Effect_Release(effect);
    }
    ID2D1Bitmap1_Release(bitmap);
    cleanup_effect_context(&ctx);
}

static void test_large_effect_buffers(void)
{
    struct effect_test_context ctx;
    ID2D1Bitmap1 *bitmap;
    ID2D1Effect *effect;
    D2D1_VECTOR_4F pixel = {.25f,.5f,.75f,1};
    float table[1025], kernel[17 * 17] = {0};
    UINT32 dimension = 17, mode;
    unsigned int i;
    HRESULT hr;

    if (!init_effect_context(&ctx)) return;
    bitmap = create_float_bitmap(&ctx, 1, 1, &pixel);
    if (!bitmap) { cleanup_effect_context(&ctx); return; }
    for (i = 0; i < ARRAY_SIZE(table); ++i) table[i] = 1 - i / 1024.0f;
    hr = ID2D1DeviceContext_CreateEffect(ctx.context, &CLSID_D2D1TableTransfer, &effect);
    ok(hr == S_OK, "CreateEffect failed, hr %#lx.\n", hr);
    if (SUCCEEDED(hr))
    {
        ID2D1Effect_SetInput(effect, 0, (ID2D1Image *)bitmap, TRUE);
        hr = ID2D1Effect_SetValue(effect, 0, D2D1_PROPERTY_TYPE_BLOB, (BYTE *)table, sizeof(table));
        ok(hr == S_OK, "Set large table failed, hr %#lx.\n", hr);
        hr = draw_effect(&ctx, effect, NULL, NULL);
        ok(hr == S_OK, "Large table draw failed, hr %#lx.\n", hr);
        if (SUCCEEDED(hr)) check_vector(read_float_pixel(&ctx, 0, 0), (D2D1_VECTOR_4F){.75f,.5f,.75f,1}, .002f);
        ID2D1Effect_Release(effect);
    }
    kernel[8 * 17 + 8] = 1;
    hr = ID2D1DeviceContext_CreateEffect(ctx.context, &CLSID_D2D1ConvolveMatrix, &effect);
    ok(hr == S_OK, "CreateEffect failed, hr %#lx.\n", hr);
    if (SUCCEEDED(hr))
    {
        ID2D1Effect_SetInput(effect, 0, (ID2D1Image *)bitmap, TRUE);
        hr = ID2D1Effect_SetValue(effect, 2, D2D1_PROPERTY_TYPE_UINT32, (BYTE *)&dimension, sizeof(dimension));
        ok(hr == S_OK, "Set width failed, hr %#lx.\n", hr);
        hr = ID2D1Effect_SetValue(effect, 3, D2D1_PROPERTY_TYPE_UINT32, (BYTE *)&dimension, sizeof(dimension));
        ok(hr == S_OK, "Set height failed, hr %#lx.\n", hr);
        hr = ID2D1Effect_SetValue(effect, 4, D2D1_PROPERTY_TYPE_BLOB, (BYTE *)kernel, sizeof(kernel));
        ok(hr == S_OK, "Set large kernel failed, hr %#lx.\n", hr);
        /* Hard border keeps a one-pixel field constant for every sampling footprint. */
        mode = D2D1_BORDER_MODE_HARD;
        hr = ID2D1Effect_SetValue(effect, 9, D2D1_PROPERTY_TYPE_ENUM, (BYTE *)&mode, sizeof(mode));
        ok(hr == S_OK, "Set border failed, hr %#lx.\n", hr);
        for (mode = 0; mode <= 5; ++mode)
        {
            winetest_push_context("large kernel interpolation %u", mode);
            hr = ID2D1Effect_SetValue(effect, 1, D2D1_PROPERTY_TYPE_ENUM, (BYTE *)&mode, sizeof(mode));
            ok(hr == S_OK, "Set interpolation failed, hr %#lx.\n", hr);
            hr = draw_effect(&ctx, effect, NULL, NULL);
            ok(hr == S_OK, "Large kernel draw failed, hr %#lx.\n", hr);
            if (SUCCEEDED(hr)) check_vector(read_float_pixel(&ctx, 0, 0), pixel, .003f);
            winetest_pop_context();
        }
        ID2D1Effect_Release(effect);
    }
    ID2D1Bitmap1_Release(bitmap);
    cleanup_effect_context(&ctx);
}

static void test_convolution_sampling(void)
{
    static const D2D1_VECTOR_4F pixels[] = {{0,0,0,1}, {1,0,0,1}, {0,0,0,1}, {0,0,0,1}};
    static const float expected[] = {1, .75f, .8671875f, .75f, .75f, .8671875f};
    D2D1_VECTOR_2F offset = {-.25f, 0};
    struct effect_test_context ctx;
    ID2D1Bitmap1 *bitmap;
    ID2D1Effect *effect;
    UINT32 dimension = 1, mode;
    float kernel = 1;
    HRESULT hr;

    if (!init_effect_context(&ctx)) return;
    bitmap = create_float_bitmap(&ctx, 4, 1, pixels);
    if (!bitmap) { cleanup_effect_context(&ctx); return; }
    hr = ID2D1DeviceContext_CreateEffect(ctx.context, &CLSID_D2D1ConvolveMatrix, &effect);
    ok(hr == S_OK, "CreateEffect failed, hr %#lx.\n", hr);
    if (SUCCEEDED(hr))
    {
        ID2D1Effect_SetInput(effect, 0, (ID2D1Image *)bitmap, TRUE);
        hr = ID2D1Effect_SetValue(effect, 2, D2D1_PROPERTY_TYPE_UINT32, (BYTE *)&dimension, sizeof(dimension));
        ok(hr == S_OK, "Set width failed, hr %#lx.\n", hr);
        hr = ID2D1Effect_SetValue(effect, 3, D2D1_PROPERTY_TYPE_UINT32, (BYTE *)&dimension, sizeof(dimension));
        ok(hr == S_OK, "Set height failed, hr %#lx.\n", hr);
        hr = ID2D1Effect_SetValue(effect, 4, D2D1_PROPERTY_TYPE_BLOB, (BYTE *)&kernel, sizeof(kernel));
        ok(hr == S_OK, "Set kernel failed, hr %#lx.\n", hr);
        hr = ID2D1Effect_SetValue(effect, 7, D2D1_PROPERTY_TYPE_VECTOR2, (BYTE *)&offset, sizeof(offset));
        ok(hr == S_OK, "Set offset failed, hr %#lx.\n", hr);
        mode = D2D1_BORDER_MODE_HARD;
        hr = ID2D1Effect_SetValue(effect, 9, D2D1_PROPERTY_TYPE_ENUM, (BYTE *)&mode, sizeof(mode));
        ok(hr == S_OK, "Set border failed, hr %#lx.\n", hr);
        for (mode = 0; mode < ARRAY_SIZE(expected); ++mode)
        {
            winetest_push_context("convolution sample %u", mode);
            hr = ID2D1Effect_SetValue(effect, 1, D2D1_PROPERTY_TYPE_ENUM, (BYTE *)&mode, sizeof(mode));
            ok(hr == S_OK, "Set interpolation failed, hr %#lx.\n", hr);
            hr = draw_effect(&ctx, effect, NULL, NULL);
            ok(hr == S_OK, "Draw failed, hr %#lx.\n", hr);
            if (SUCCEEDED(hr)) check_vector(read_float_pixel(&ctx, 1, 0),
                    (D2D1_VECTOR_4F){expected[mode],0,0,1}, .003f);
            winetest_pop_context();
        }
        ID2D1Effect_Release(effect);
    }
    ID2D1Bitmap1_Release(bitmap);
    cleanup_effect_context(&ctx);
}

static void test_camera_plane_crossing(void)
{
    D2D1_VECTOR_4F pixels[16];
    /* X = (x - 1) / (x - 2), Y = y / (x - 2). Only x > 2 is visible. */
    D2D1_MATRIX_4X4_F matrix = {{1,0,0,1, 0,1,0,0, 0,0,1,0, -1,0,0,-2}};
    struct effect_test_context ctx;
    ID2D1Bitmap1 *bitmap;
    ID2D1Effect *effect;
    ID2D1Image *output;
    D2D1_RECT_F bounds;
    D2D1_VECTOR_3F rotation = {0,60,0}, offset = {0,0,-3};
    float depth = 1;
    UINT32 border = D2D1_BORDER_MODE_HARD;
    unsigned int i;
    HRESULT hr;

    if (!init_effect_context(&ctx)) return;
    for (i = 0; i < ARRAY_SIZE(pixels); ++i) pixels[i] = (D2D1_VECTOR_4F){1,0,0,1};
    bitmap = create_float_bitmap(&ctx, 4, 4, pixels);
    if (!bitmap) { cleanup_effect_context(&ctx); return; }
    hr = ID2D1DeviceContext_CreateEffect(ctx.context, &CLSID_D2D13DTransform, &effect);
    ok(hr == S_OK, "CreateEffect failed, hr %#lx.\n", hr);
    if (SUCCEEDED(hr))
    {
        ID2D1Effect_SetInput(effect, 0, (ID2D1Image *)bitmap, TRUE);
        hr = ID2D1Effect_SetValue(effect, 2, D2D1_PROPERTY_TYPE_MATRIX_4X4, (BYTE *)&matrix, sizeof(matrix));
        ok(hr == S_OK, "Set matrix failed, hr %#lx.\n", hr);
        ID2D1Effect_GetOutput(effect, &output);
        hr = ID2D1DeviceContext_GetImageLocalBounds(ctx.context, output, &bounds);
        ok(hr == S_OK, "Crossing bounds failed, hr %#lx.\n", hr);
        hr = draw_effect(&ctx, effect, NULL, NULL);
        ok(hr == S_OK, "Crossing draw failed, hr %#lx.\n", hr);
        if (SUCCEEDED(hr))
        {
            check_vector(read_float_pixel(&ctx, 2, 1), pixels[0], .002f);
            check_vector(read_float_pixel(&ctx, 0, 1), (D2D1_VECTOR_4F){0}, .002f);
        }
        hr = ID2D1Effect_SetValue(effect, 1, D2D1_PROPERTY_TYPE_ENUM, (BYTE *)&border, sizeof(border));
        ok(hr == S_OK, "Set hard border failed, hr %#lx.\n", hr);
        hr = draw_effect(&ctx, effect, NULL, NULL);
        ok(hr == S_OK, "Hard-border crossing failed, hr %#lx.\n", hr);
        if (SUCCEEDED(hr)) check_vector(read_float_pixel(&ctx, 1, 8), (D2D1_VECTOR_4F){0}, .002f);
        /* The entire plane is behind the viewer. */
        matrix._14 = 0; matrix._44 = -1;
        hr = ID2D1Effect_SetValue(effect, 2, D2D1_PROPERTY_TYPE_MATRIX_4X4, (BYTE *)&matrix, sizeof(matrix));
        ok(hr == S_OK, "Set behind-camera matrix failed, hr %#lx.\n", hr);
        hr = ID2D1DeviceContext_GetImageLocalBounds(ctx.context, output, &bounds);
        ok(hr == S_OK, "Hidden bounds failed, hr %#lx.\n", hr);
        if (SUCCEEDED(hr)) ok(bounds.left == 0 && bounds.top == 0 && bounds.right == 0 && bounds.bottom == 0,
                "Hidden plane has nonempty bounds.\n");
        hr = draw_effect(&ctx, effect, NULL, NULL);
        ok(hr == S_OK, "Hidden plane draw failed, hr %#lx.\n", hr);
        if (SUCCEEDED(hr)) check_vector(read_float_pixel(&ctx, 2, 1), (D2D1_VECTOR_4F){0}, .002f);
        ID2D1Image_Release(output);
        ID2D1Effect_Release(effect);
    }
    hr = ID2D1DeviceContext_CreateEffect(ctx.context, &CLSID_D2D13DPerspectiveTransform, &effect);
    ok(hr == S_OK, "Create perspective effect failed, hr %#lx.\n", hr);
    if (SUCCEEDED(hr))
    {
        ID2D1Effect_SetInput(effect, 0, (ID2D1Image *)bitmap, TRUE);
        hr = ID2D1Effect_SetValue(effect, D2D1_3DPERSPECTIVETRANSFORM_PROP_DEPTH,
                D2D1_PROPERTY_TYPE_FLOAT, (BYTE *)&depth, sizeof(depth));
        ok(hr == S_OK, "Set depth failed, hr %#lx.\n", hr);
        hr = ID2D1Effect_SetValue(effect, D2D1_3DPERSPECTIVETRANSFORM_PROP_ROTATION,
                D2D1_PROPERTY_TYPE_VECTOR3, (BYTE *)&rotation, sizeof(rotation));
        ok(hr == S_OK, "Set rotation failed, hr %#lx.\n", hr);
        hr = draw_effect(&ctx, effect, NULL, NULL);
        ok(hr == S_OK, "Perspective crossing draw failed, hr %#lx.\n", hr);
        if (SUCCEEDED(hr)) ok(read_float_pixel(&ctx, 2, 1).x > .5f, "Front of perspective plane is missing.\n");
        hr = ID2D1Effect_SetValue(effect, D2D1_3DPERSPECTIVETRANSFORM_PROP_GLOBAL_OFFSET,
                D2D1_PROPERTY_TYPE_VECTOR3, (BYTE *)&offset, sizeof(offset));
        ok(hr == S_OK, "Set offset failed, hr %#lx.\n", hr);
        hr = draw_effect(&ctx, effect, NULL, NULL);
        ok(hr == S_OK, "Hidden perspective draw failed, hr %#lx.\n", hr);
        if (SUCCEEDED(hr)) check_vector(read_float_pixel(&ctx, 2, 1), (D2D1_VECTOR_4F){0}, .002f);
        ID2D1Effect_Release(effect);
    }
    ID2D1Bitmap1_Release(bitmap);
    cleanup_effect_context(&ctx);
}

static void test_animated_effect_transform(void)
{
    static const float scales[] = {1, .5f, .01f, 0, .01f, .5f, 1};
    static const D2D1_MATRIX_3X2_F identity = {{{1,0,0,1,0,0}}};
    D2D1_VECTOR_4F pixels[16];
    struct effect_test_context ctx;
    ID2D1Bitmap1 *bitmap;
    ID2D1Effect *effect;
    D2D1_MATRIX_3X2_F transform;
    unsigned int i;
    HRESULT hr;

    if (!init_effect_context(&ctx)) return;
    for (i = 0; i < ARRAY_SIZE(pixels); ++i) pixels[i] = (D2D1_VECTOR_4F){1,0,0,1};
    bitmap = create_float_bitmap(&ctx, 4, 4, pixels);
    if (!bitmap) { cleanup_effect_context(&ctx); return; }
    hr = ID2D1DeviceContext_CreateEffect(ctx.context, &CLSID_D2D12DAffineTransform, &effect);
    ok(hr == S_OK, "CreateEffect failed, hr %#lx.\n", hr);
    if (SUCCEEDED(hr))
    {
        ID2D1Effect_SetInput(effect, 0, (ID2D1Image *)bitmap, TRUE);
        for (i = 0; i < ARRAY_SIZE(scales); ++i)
        {
            winetest_push_context("animated effect frame %u, scale %g", i, scales[i]);
            transform = identity;
            transform._11 = transform._22 = scales[i];
            hr = ID2D1Effect_SetValue(effect, D2D1_2DAFFINETRANSFORM_PROP_TRANSFORM_MATRIX,
                    D2D1_PROPERTY_TYPE_MATRIX_3X2, (const BYTE *)&transform, sizeof(transform));
            ok(hr == S_OK, "Set transform failed, hr %#lx.\n", hr);
            hr = draw_effect(&ctx, effect, NULL, NULL);
            ok(hr == S_OK, "Animated draw failed, hr %#lx.\n", hr);
            if (SUCCEEDED(hr) && scales[i] >= .5f)
                check_vector(read_float_pixel(&ctx, 0, 0), pixels[0], .002f);
            if (SUCCEEDED(hr) && !scales[i])
                check_vector(read_float_pixel(&ctx, 0, 0), (D2D1_VECTOR_4F){0}, .002f);
            winetest_pop_context();
        }
        hr = ID2D1Effect_SetValue(effect, D2D1_2DAFFINETRANSFORM_PROP_TRANSFORM_MATRIX,
                D2D1_PROPERTY_TYPE_MATRIX_3X2, (const BYTE *)&identity, sizeof(identity));
        ok(hr == S_OK, "Reset transform failed, hr %#lx.\n", hr);
        transform = identity;
        transform._11 = 0;
        ID2D1DeviceContext_SetTransform(ctx.context, &transform);
        hr = draw_effect(&ctx, effect, NULL, NULL);
        ok(hr == S_OK, "Collapsed destination transform failed, hr %#lx.\n", hr);
        ID2D1DeviceContext_SetTransform(ctx.context, &identity);
        hr = draw_effect(&ctx, effect, NULL, NULL);
        ok(hr == S_OK, "Drawing after collapsed frame failed, hr %#lx.\n", hr);
        if (SUCCEEDED(hr)) check_vector(read_float_pixel(&ctx, 0, 0), pixels[0], .002f);
        ID2D1Effect_Release(effect);
    }
    ID2D1Bitmap1_Release(bitmap);
    cleanup_effect_context(&ctx);
}

static void test_rounded_geometry_effect(void)
{
    struct effect_test_context ctx;
    D2D1_ROUNDED_RECT rect = {{4,4,20,16},4,3};
    D2D1_MATRIX_3X2_F transform = {{{0,1,-1,0,32,0}}};
    D2D1_COLOR_F red = {1,0,0,1};
    ID2D1RoundedRectangleGeometry *geometry;
    ID2D1SolidColorBrush *brush;
    ID2D1CommandList *list;
    ID2D1Effect *effect;
    D2D1_RECT_F bounds;
    float sigma = 1;
    HRESULT hr;

    if (!init_effect_context(&ctx)) return;
    hr = ID2D1Factory1_CreateRoundedRectangleGeometry(ctx.factory, &rect, &geometry);
    ok(hr == S_OK, "Create geometry failed, hr %#lx.\n", hr);
    if (FAILED(hr)) { cleanup_effect_context(&ctx); return; }
    hr = ID2D1RoundedRectangleGeometry_GetBounds(geometry, &transform, &bounds);
    ok(hr == S_OK, "Rounded bounds failed, hr %#lx.\n", hr);
    if (SUCCEEDED(hr)) ok(bounds.left == 16 && bounds.top == 4 && bounds.right == 28 && bounds.bottom == 20,
            "Unexpected rotated bounds {%g,%g,%g,%g}.\n", bounds.left,bounds.top,bounds.right,bounds.bottom);
    hr = ID2D1DeviceContext_CreateSolidColorBrush(ctx.context, &red, NULL, &brush);
    ok(hr == S_OK, "Create brush failed, hr %#lx.\n", hr);
    hr = ID2D1DeviceContext_CreateCommandList(ctx.context, &list);
    ok(hr == S_OK, "Create list failed, hr %#lx.\n", hr);
    ID2D1DeviceContext_SetTarget(ctx.context, (ID2D1Image *)list);
    ID2D1DeviceContext_BeginDraw(ctx.context);
    ID2D1DeviceContext_FillGeometry(ctx.context, (ID2D1Geometry *)geometry, (ID2D1Brush *)brush, NULL);
    ID2D1DeviceContext_DrawGeometry(ctx.context, (ID2D1Geometry *)geometry, (ID2D1Brush *)brush, 1, NULL);
    hr = ID2D1DeviceContext_EndDraw(ctx.context, NULL, NULL);
    ok(hr == S_OK, "Record failed, hr %#lx.\n", hr);
    hr = ID2D1CommandList_Close(list);
    ok(hr == S_OK, "Close failed, hr %#lx.\n", hr);
    ID2D1DeviceContext_SetTarget(ctx.context, (ID2D1Image *)ctx.target);
    hr = ID2D1DeviceContext_CreateEffect(ctx.context, &CLSID_D2D1GaussianBlur, &effect);
    ok(hr == S_OK, "Create blur failed, hr %#lx.\n", hr);
    if (SUCCEEDED(hr))
    {
        ID2D1Effect_SetInput(effect, 0, (ID2D1Image *)list, TRUE);
        hr = ID2D1Effect_SetValue(effect, 0, D2D1_PROPERTY_TYPE_FLOAT, (BYTE *)&sigma, sizeof(sigma));
        ok(hr == S_OK, "Set blur failed, hr %#lx.\n", hr);
        hr = draw_effect(&ctx, effect, NULL, NULL);
        ok(hr == S_OK, "Rounded-list blur failed, hr %#lx.\n", hr);
        if (SUCCEEDED(hr)) check_vector(read_float_pixel(&ctx, 12, 10), (D2D1_VECTOR_4F){1,0,0,1}, .003f);
        ID2D1Effect_Release(effect);
    }
    ID2D1CommandList_Release(list);
    ID2D1SolidColorBrush_Release(brush);
    ID2D1RoundedRectangleGeometry_Release(geometry);
    cleanup_effect_context(&ctx);
}

static void test_animated_blur_timing(void)
{
    struct effect_test_context ctx;
    D2D1_BITMAP_PROPERTIES1 props = {{DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED},96,96,0,NULL};
    ID2D1Bitmap1 *input = NULL, *target = NULL;
    ID2D1Effect *blur;
    DWORD *pixels;
    LARGE_INTEGER start, end, frequency;
    ID3D11Query *query;
    D3D11_QUERY_DESC query_desc = {D3D11_QUERY_EVENT,0};
    unsigned int i, frame;
    HRESULT hr;

    if (!init_effect_context(&ctx)) return;
    pixels = malloc(1024*768*sizeof(*pixels));
    if (!pixels) { cleanup_effect_context(&ctx); return; }
    for (i = 0; i < 1024*768; ++i) pixels[i] = 0xff2080c0;
    hr = ID2D1DeviceContext_CreateBitmap(ctx.context, (D2D1_SIZE_U){1024,768}, pixels, 4096, &props, &input);
    free(pixels);
    ok(hr == S_OK, "Create input failed, hr %#lx.\n", hr);
    props.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET;
    hr = ID2D1DeviceContext_CreateBitmap(ctx.context, (D2D1_SIZE_U){1024,768}, NULL, 0, &props, &target);
    ok(hr == S_OK, "Create target failed, hr %#lx.\n", hr);
    if (!input || !target) goto done;
    ID2D1DeviceContext_SetTarget(ctx.context, (ID2D1Image *)target);
    hr = ID3D11Device_CreateQuery(ctx.d3d, &query_desc, &query);
    ok(hr == S_OK, "Create query failed, hr %#lx.\n", hr);
    if (FAILED(hr)) goto done;
    hr = ID2D1DeviceContext_CreateEffect(ctx.context, &CLSID_D2D1GaussianBlur, &blur);
    ok(hr == S_OK, "Create blur failed, hr %#lx.\n", hr);
    if (SUCCEEDED(hr))
    {
        ID2D1Effect_SetInput(blur, 0, (ID2D1Image *)input, TRUE);
        QueryPerformanceFrequency(&frequency);
        for (frame = 0; frame < 9; ++frame)
        {
            float sigma = 10 + frame * .5f;
            hr = ID2D1Effect_SetValue(blur, 0, D2D1_PROPERTY_TYPE_FLOAT, (BYTE *)&sigma, sizeof(sigma));
            ok(hr == S_OK, "Set sigma failed, hr %#lx.\n", hr);
            if (frame == 1) QueryPerformanceCounter(&start);
            hr = draw_effect(&ctx, blur, NULL, NULL);
            ok(hr == S_OK, "Animated blur frame %u failed, hr %#lx.\n", frame, hr);
            ID3D11DeviceContext_End(ctx.dc, (ID3D11Asynchronous *)query);
            ID3D11DeviceContext_Flush(ctx.dc);
            while ((hr = ID3D11DeviceContext_GetData(ctx.dc, (ID3D11Asynchronous *)query, NULL, 0, 0)) == S_FALSE)
                Sleep(1);
            ok(hr == S_OK, "Frame fence failed, hr %#lx.\n", hr);
        }
        QueryPerformanceCounter(&end);
        trace("Animated 1024x768 blur: %.3f ms/frame (8 warm frames, GPU fence included).\n",
                (end.QuadPart-start.QuadPart)*1000.0/frequency.QuadPart/8);
        ID2D1Effect_Release(blur);
    }
    ID3D11Query_Release(query);
done:
    ID2D1DeviceContext_SetTarget(ctx.context, (ID2D1Image *)ctx.target);
    if (input) ID2D1Bitmap1_Release(input);
    if (target) ID2D1Bitmap1_Release(target);
    cleanup_effect_context(&ctx);
}

static void test_gaussian_animation_pixels(void)
{
    static const float sigmas[] = {0, .001f, .25f, 1, 2.5f, 1};
    D2D1_VECTOR_4F pixels[81] = {{0}};
    struct effect_test_context ctx;
    ID2D1Bitmap1 *bitmap;
    ID2D1Effect *blur;
    unsigned int frame;
    HRESULT hr;

    if (!init_effect_context(&ctx)) return;
    pixels[40] = (D2D1_VECTOR_4F){.5f,0,0,.5f};
    bitmap = create_float_bitmap(&ctx, 9, 9, pixels);
    if (!bitmap) { cleanup_effect_context(&ctx); return; }
    hr = ID2D1DeviceContext_CreateEffect(ctx.context, &CLSID_D2D1GaussianBlur, &blur);
    ok(hr == S_OK, "Create blur failed, hr %#lx.\n", hr);
    if (SUCCEEDED(hr))
    {
        ID2D1Effect_SetInput(blur, 0, (ID2D1Image *)bitmap, TRUE);
        for (frame = 0; frame < ARRAY_SIZE(sigmas); ++frame)
        {
            float sigma = sigmas[frame], sum = 1, center, adjacent;
            int tap, radius = ceilf(3 * sigma);
            winetest_push_context("sigma %g", sigma);
            for (tap = 1; tap <= radius; ++tap) sum += 2 * expf(-.5f * tap * tap / (sigma * sigma));
            center = .5f / (sum * sum);
            adjacent = sigma > 0 ? center * expf(-.5f / (sigma * sigma)) : 0;
            hr = ID2D1Effect_SetValue(blur, 0, D2D1_PROPERTY_TYPE_FLOAT, (BYTE *)&sigma, sizeof(sigma));
            ok(hr == S_OK, "Set sigma failed, hr %#lx.\n", hr);
            hr = draw_effect(&ctx, blur, NULL, NULL);
            ok(hr == S_OK, "Draw failed, hr %#lx.\n", hr);
            if (SUCCEEDED(hr))
            {
                check_vector(read_float_pixel(&ctx, 4, 4), (D2D1_VECTOR_4F){center,0,0,center}, .001f);
                check_vector(read_float_pixel(&ctx, 5, 4), (D2D1_VECTOR_4F){adjacent,0,0,adjacent}, .001f);
            }
            winetest_pop_context();
        }
        ID2D1Effect_Release(blur);
    }
    ID2D1Bitmap1_Release(bitmap);
    cleanup_effect_context(&ctx);
}

static void run_effect_test(const char *filter, const char *name, void (*test)(void))
{
    if (!filter || !strcmp(filter, name))
        test();
}

/* An effect cache uses Flush to decide whether an offscreen image is safe to reuse. */
static void test_flush_reports_deferred_error(void)
{
    struct effect_test_context ctx;
    ID2D1CommandList *unclosed;
    D2D1_TAG tag1, tag2;
    HRESULT hr;

    if (!init_effect_context(&ctx)) return;
    hr = ID2D1DeviceContext_CreateCommandList(ctx.context, &unclosed);
    ok(hr == S_OK, "CreateCommandList failed, hr %#lx.\n", hr);
    if (SUCCEEDED(hr))
    {
        ID2D1DeviceContext_BeginDraw(ctx.context);
        ID2D1DeviceContext_SetTags(ctx.context, 17, 29);
        ID2D1DeviceContext_DrawImage(ctx.context, (ID2D1Image *)unclosed, NULL, NULL,
                D2D1_INTERPOLATION_MODE_LINEAR, D2D1_COMPOSITE_MODE_SOURCE_OVER);
        tag1 = tag2 = 0;
        hr = ID2D1DeviceContext_Flush(ctx.context, &tag1, &tag2);
        ok(hr == D2DERR_WRONG_STATE, "Flush hid the invalid image, hr %#lx.\n", hr);
        ok(tag1 == 17 && tag2 == 29, "Unexpected failure tags %I64u, %I64u.\n", tag1, tag2);
        hr = ID2D1DeviceContext_EndDraw(ctx.context, NULL, NULL);
        ok(hr == D2DERR_WRONG_STATE, "EndDraw lost the error, hr %#lx.\n", hr);
        ID2D1CommandList_Release(unclosed);
    }
    cleanup_effect_context(&ctx);
}

#define RUN_EFFECT_TEST(name) run_effect_test(filter, #name, name)

START_TEST(effects)
{
    const char *filter = getenv("D2D1_EFFECT_TEST");
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    ok(SUCCEEDED(hr), "CoInitializeEx failed, hr %#lx.\n", hr);
    RUN_EFFECT_TEST(test_fixture);
    RUN_EFFECT_TEST(test_flush_reports_deferred_error);
    RUN_EFFECT_TEST(test_pointwise_effects);
    RUN_EFFECT_TEST(test_color_matrix_alpha);
    RUN_EFFECT_TEST(test_alpha_effects);
    RUN_EFFECT_TEST(test_effect_bounds);
    RUN_EFFECT_TEST(test_brightness);
    RUN_EFFECT_TEST(test_flood_crop);
    RUN_EFFECT_TEST(test_shared_graph);
    RUN_EFFECT_TEST(test_two_input_effects);
    RUN_EFFECT_TEST(test_fractional_crop_pixels);
    RUN_EFFECT_TEST(test_transfer_effects);
    RUN_EFFECT_TEST(test_table_transfer);
    RUN_EFFECT_TEST(test_morphology);
    RUN_EFFECT_TEST(test_tile_border);
    RUN_EFFECT_TEST(test_color_filters);
    RUN_EFFECT_TEST(test_displacement);
    RUN_EFFECT_TEST(test_convolution);
    RUN_EFFECT_TEST(test_hue_conversion);
    RUN_EFFECT_TEST(test_atlas_metadata_dpi);
    RUN_EFFECT_TEST(test_blend_modes);
    RUN_EFFECT_TEST(test_projective_transform);
    RUN_EFFECT_TEST(test_perspective_transform);
    RUN_EFFECT_TEST(test_lighting);
    RUN_EFFECT_TEST(test_chroma_white_level);
    RUN_EFFECT_TEST(test_bitmap_source);
    RUN_EFFECT_TEST(test_histogram);
    RUN_EFFECT_TEST(test_contrast);
    RUN_EFFECT_TEST(test_straighten);
    RUN_EFFECT_TEST(test_ycbcr);
    RUN_EFFECT_TEST(test_lookup_table);
    RUN_EFFECT_TEST(test_color_management);
    RUN_EFFECT_TEST(test_color_context_wic);
    RUN_EFFECT_TEST(test_turbulence);
    RUN_EFFECT_TEST(test_edge_filters);
    RUN_EFFECT_TEST(test_empty_and_unbounded_bounds);
    RUN_EFFECT_TEST(test_temperature_tint);
    RUN_EFFECT_TEST(test_vignette);
    RUN_EFFECT_TEST(test_highlights_shadows);
    RUN_EFFECT_TEST(test_hdr_tone_map);
    RUN_EFFECT_TEST(test_icc_color_management);
    RUN_EFFECT_TEST(test_transform_interpolation);
    RUN_EFFECT_TEST(test_unbounded_offset);
    RUN_EFFECT_TEST(test_composite_mask_invert);
    RUN_EFFECT_TEST(test_composite_bounds);
    RUN_EFFECT_TEST(test_bitmap_source_orientations);
    RUN_EFFECT_TEST(test_edge_detection_blur);
    RUN_EFFECT_TEST(test_large_effect_buffers);
    RUN_EFFECT_TEST(test_convolution_sampling);
    RUN_EFFECT_TEST(test_camera_plane_crossing);
    RUN_EFFECT_TEST(test_animated_effect_transform);
    RUN_EFFECT_TEST(test_rounded_geometry_effect);
    RUN_EFFECT_TEST(test_gaussian_animation_pixels);
    if (filter) RUN_EFFECT_TEST(test_animated_blur_timing);
    if (SUCCEEDED(hr)) CoUninitialize();
}
