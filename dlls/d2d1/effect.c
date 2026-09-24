/*
 * Copyright 2018 Nikolay Sivov for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "d2d1_private.h"
#include "wincodec.h"
#include <float.h>

WINE_DEFAULT_DEBUG_CHANNEL(d2d);

static inline struct d2d_transform *impl_from_ID2D1OffsetTransform(ID2D1OffsetTransform *iface)
{
    return CONTAINING_RECORD(iface, struct d2d_transform, ID2D1TransformNode_iface);
}

static inline struct d2d_transform *impl_from_ID2D1BlendTransform(ID2D1BlendTransform *iface)
{
    return CONTAINING_RECORD(iface, struct d2d_transform, ID2D1TransformNode_iface);
}

static inline struct d2d_transform *impl_from_ID2D1BorderTransform(ID2D1BorderTransform *iface)
{
    return CONTAINING_RECORD(iface, struct d2d_transform, ID2D1TransformNode_iface);
}

static inline struct d2d_transform *impl_from_ID2D1BoundsAdjustmentTransform(
        ID2D1BoundsAdjustmentTransform *iface)
{
    return CONTAINING_RECORD(iface, struct d2d_transform, ID2D1TransformNode_iface);
}

static inline struct d2d_vertex_buffer *impl_from_ID2D1VertexBuffer(ID2D1VertexBuffer *iface)
{
    return CONTAINING_RECORD(iface, struct d2d_vertex_buffer, ID2D1VertexBuffer_iface);
}

static HRESULT STDMETHODCALLTYPE d2d_vertex_buffer_QueryInterface(ID2D1VertexBuffer *iface,
        REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_ID2D1VertexBuffer)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        *out = iface;
        ID2D1VertexBuffer_AddRef(iface);
        return S_OK;
    }

    WARN("Unsupported interface %s.\n", debugstr_guid(iid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d2d_vertex_buffer_AddRef(ID2D1VertexBuffer *iface)
{
    struct d2d_vertex_buffer *buffer = impl_from_ID2D1VertexBuffer(iface);
    ULONG refcount = InterlockedIncrement(&buffer->refcount);

    TRACE("%p increasing refcount to %lu.\n", iface, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d2d_vertex_buffer_Release(ID2D1VertexBuffer *iface)
{
    struct d2d_vertex_buffer *buffer = impl_from_ID2D1VertexBuffer(iface);
    ULONG refcount = InterlockedDecrement(&buffer->refcount);

    TRACE("%p decreasing refcount to %lu.\n", iface, refcount);

    if (!refcount)
        free(buffer);

    return refcount;
}

static HRESULT STDMETHODCALLTYPE d2d_vertex_buffer_Map(ID2D1VertexBuffer *iface, BYTE **data, UINT32 size)
{
    FIXME("iface %p, data %p, size %u.\n", iface, data, size);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_vertex_buffer_Unmap(ID2D1VertexBuffer *iface)
{
    FIXME("iface %p.\n", iface);

    return E_NOTIMPL;
}

static const ID2D1VertexBufferVtbl d2d_vertex_buffer_vtbl =
{
    d2d_vertex_buffer_QueryInterface,
    d2d_vertex_buffer_AddRef,
    d2d_vertex_buffer_Release,
    d2d_vertex_buffer_Map,
    d2d_vertex_buffer_Unmap,
};

static HRESULT d2d_vertex_buffer_create(ID2D1VertexBuffer **buffer)
{
    struct d2d_vertex_buffer *object;

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    object->ID2D1VertexBuffer_iface.lpVtbl = &d2d_vertex_buffer_vtbl;
    object->refcount = 1;

    *buffer = &object->ID2D1VertexBuffer_iface;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_offset_transform_QueryInterface(ID2D1OffsetTransform *iface,
        REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_ID2D1OffsetTransform)
            || IsEqualGUID(iid, &IID_ID2D1TransformNode)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        *out = iface;
        ID2D1OffsetTransform_AddRef(iface);
        return S_OK;
    }

    WARN("Unsupported interface %s.\n", debugstr_guid(iid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d2d_offset_transform_AddRef(ID2D1OffsetTransform *iface)
{
    struct d2d_transform *transform = impl_from_ID2D1OffsetTransform(iface);
    ULONG refcount = InterlockedIncrement(&transform->refcount);

    TRACE("%p increasing refcount to %lu.\n", iface, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d2d_offset_transform_Release(ID2D1OffsetTransform *iface)
{
    struct d2d_transform *transform = impl_from_ID2D1OffsetTransform(iface);
    ULONG refcount = InterlockedDecrement(&transform->refcount);

    TRACE("%p decreasing refcount to %lu.\n", iface, refcount);

    if (!refcount)
        free(transform);

    return refcount;
}

static UINT32 STDMETHODCALLTYPE d2d_offset_transform_GetInputCount(ID2D1OffsetTransform *iface)
{
    TRACE("iface %p.\n", iface);

    return 1;
}

static void STDMETHODCALLTYPE d2d_offset_transform_SetOffset(ID2D1OffsetTransform *iface,
        D2D1_POINT_2L offset)
{
    struct d2d_transform *transform = impl_from_ID2D1OffsetTransform(iface);

    TRACE("iface %p, offset %s.\n", iface, debug_d2d_point_2l(&offset));

    transform->offset = offset;
}

static D2D1_POINT_2L * STDMETHODCALLTYPE d2d_offset_transform_GetOffset(ID2D1OffsetTransform *iface,
        D2D1_POINT_2L *offset)
{
    struct d2d_transform *transform = impl_from_ID2D1OffsetTransform(iface);

    TRACE("iface %p.\n", iface);

    *offset = transform->offset;
    return offset;
}

static const ID2D1OffsetTransformVtbl d2d_offset_transform_vtbl =
{
    d2d_offset_transform_QueryInterface,
    d2d_offset_transform_AddRef,
    d2d_offset_transform_Release,
    d2d_offset_transform_GetInputCount,
    d2d_offset_transform_SetOffset,
    d2d_offset_transform_GetOffset,
};

static HRESULT d2d_offset_transform_create(D2D1_POINT_2L offset, ID2D1OffsetTransform **transform)
{
    struct d2d_transform *object;

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    object->ID2D1TransformNode_iface.lpVtbl = (ID2D1TransformNodeVtbl *)&d2d_offset_transform_vtbl;
    object->refcount = 1;
    object->offset = offset;

    *transform = (ID2D1OffsetTransform *)&object->ID2D1TransformNode_iface;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_blend_transform_QueryInterface(ID2D1BlendTransform *iface,
        REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_ID2D1BlendTransform)
            || IsEqualGUID(iid, &IID_ID2D1ConcreteTransform)
            || IsEqualGUID(iid, &IID_ID2D1TransformNode)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        *out = iface;
        ID2D1BlendTransform_AddRef(iface);
        return S_OK;
    }

    WARN("Unsupported interface %s.\n", debugstr_guid(iid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d2d_blend_transform_AddRef(ID2D1BlendTransform *iface)
{
    struct d2d_transform *transform = impl_from_ID2D1BlendTransform(iface);
    ULONG refcount = InterlockedIncrement(&transform->refcount);

    TRACE("%p increasing refcount to %lu.\n", iface, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d2d_blend_transform_Release(ID2D1BlendTransform *iface)
{
    struct d2d_transform *transform = impl_from_ID2D1BlendTransform(iface);
    ULONG refcount = InterlockedDecrement(&transform->refcount);

    TRACE("%p decreasing refcount to %lu.\n", iface, refcount);

    if (!refcount)
        free(transform);

    return refcount;
}

static UINT32 STDMETHODCALLTYPE d2d_blend_transform_GetInputCount(ID2D1BlendTransform *iface)
{
    struct d2d_transform *transform = impl_from_ID2D1BlendTransform(iface);

    TRACE("iface %p.\n", iface);

    return transform->input_count;
}

static HRESULT STDMETHODCALLTYPE d2d_blend_transform_SetOutputBuffer(ID2D1BlendTransform *iface,
        D2D1_BUFFER_PRECISION precision, D2D1_CHANNEL_DEPTH depth)
{
    FIXME("iface %p, precision %u, depth %u stub.\n", iface, precision, depth);

    return E_NOTIMPL;
}

static void STDMETHODCALLTYPE d2d_blend_transform_SetCached(ID2D1BlendTransform *iface,
        BOOL is_cached)
{
    FIXME("iface %p, is_cached %d stub.\n", iface, is_cached);
}

static void STDMETHODCALLTYPE d2d_blend_transform_SetDescription(ID2D1BlendTransform *iface,
        const D2D1_BLEND_DESCRIPTION *description)
{
    struct d2d_transform *transform = impl_from_ID2D1BlendTransform(iface);

    TRACE("iface %p, description %p.\n", iface, description);

    transform->blend_desc = *description;
}

static void STDMETHODCALLTYPE d2d_blend_transform_GetDescription(ID2D1BlendTransform *iface,
        D2D1_BLEND_DESCRIPTION *description)
{
    struct d2d_transform *transform = impl_from_ID2D1BlendTransform(iface);

    TRACE("iface %p, description %p.\n", iface, description);

    *description = transform->blend_desc;
}

static const ID2D1BlendTransformVtbl d2d_blend_transform_vtbl =
{
    d2d_blend_transform_QueryInterface,
    d2d_blend_transform_AddRef,
    d2d_blend_transform_Release,
    d2d_blend_transform_GetInputCount,
    d2d_blend_transform_SetOutputBuffer,
    d2d_blend_transform_SetCached,
    d2d_blend_transform_SetDescription,
    d2d_blend_transform_GetDescription,
};

static HRESULT d2d_blend_transform_create(UINT32 input_count, const D2D1_BLEND_DESCRIPTION *blend_desc,
        ID2D1BlendTransform **transform)
{
    struct d2d_transform *object;

    *transform = NULL;

    if (!input_count)
        return E_INVALIDARG;

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    object->ID2D1TransformNode_iface.lpVtbl = (ID2D1TransformNodeVtbl *)&d2d_blend_transform_vtbl;
    object->refcount = 1;
    object->input_count = input_count;
    object->blend_desc = *blend_desc;

    *transform = (ID2D1BlendTransform *)&object->ID2D1TransformNode_iface;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_border_transform_QueryInterface(ID2D1BorderTransform *iface,
        REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_ID2D1BorderTransform)
            || IsEqualGUID(iid, &IID_ID2D1ConcreteTransform)
            || IsEqualGUID(iid, &IID_ID2D1TransformNode)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        *out = iface;
        ID2D1BorderTransform_AddRef(iface);
        return S_OK;
    }

    WARN("Unsupported interface %s.\n", debugstr_guid(iid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d2d_border_transform_AddRef(ID2D1BorderTransform *iface)
{
    struct d2d_transform *transform = impl_from_ID2D1BorderTransform(iface);
    ULONG refcount = InterlockedIncrement(&transform->refcount);

    TRACE("%p increasing refcount to %lu.\n", iface, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d2d_border_transform_Release(ID2D1BorderTransform *iface)
{
    struct d2d_transform *transform = impl_from_ID2D1BorderTransform(iface);
    ULONG refcount = InterlockedDecrement(&transform->refcount);

    TRACE("%p decreasing refcount to %lu.\n", iface, refcount);

    if (!refcount)
        free(transform);

    return refcount;
}

static UINT32 STDMETHODCALLTYPE d2d_border_transform_GetInputCount(ID2D1BorderTransform *iface)
{
    TRACE("iface %p.\n", iface);

    return 1;
}

static HRESULT STDMETHODCALLTYPE d2d_border_transform_SetOutputBuffer(
        ID2D1BorderTransform *iface, D2D1_BUFFER_PRECISION precision, D2D1_CHANNEL_DEPTH depth)
{
    FIXME("iface %p, precision %u, depth %u stub.\n", iface, precision, depth);

    return E_NOTIMPL;
}

static void STDMETHODCALLTYPE d2d_border_transform_SetCached(
        ID2D1BorderTransform *iface, BOOL is_cached)
{
    FIXME("iface %p, is_cached %d stub.\n", iface, is_cached);
}

static void STDMETHODCALLTYPE d2d_border_transform_SetExtendModeX(
        ID2D1BorderTransform *iface, D2D1_EXTEND_MODE mode)
{
    struct d2d_transform *transform = impl_from_ID2D1BorderTransform(iface);

    TRACE("iface %p.\n", iface);

    switch (mode)
    {
        case D2D1_EXTEND_MODE_CLAMP:
        case D2D1_EXTEND_MODE_WRAP:
        case D2D1_EXTEND_MODE_MIRROR:
            transform->border.mode_x = mode;
            break;
        default:
            ;
    }
}

static void STDMETHODCALLTYPE d2d_border_transform_SetExtendModeY(
        ID2D1BorderTransform *iface, D2D1_EXTEND_MODE mode)
{
    struct d2d_transform *transform = impl_from_ID2D1BorderTransform(iface);

    TRACE("iface %p.\n", iface);

    switch (mode)
    {
        case D2D1_EXTEND_MODE_CLAMP:
        case D2D1_EXTEND_MODE_WRAP:
        case D2D1_EXTEND_MODE_MIRROR:
            transform->border.mode_y = mode;
            break;
        default:
            ;
    }
}

static D2D1_EXTEND_MODE STDMETHODCALLTYPE d2d_border_transform_GetExtendModeX(
        ID2D1BorderTransform *iface)
{
    struct d2d_transform *transform = impl_from_ID2D1BorderTransform(iface);

    TRACE("iface %p.\n", iface);

    return transform->border.mode_x;
}

static D2D1_EXTEND_MODE STDMETHODCALLTYPE d2d_border_transform_GetExtendModeY(
        ID2D1BorderTransform *iface)
{
    struct d2d_transform *transform = impl_from_ID2D1BorderTransform(iface);

    TRACE("iface %p.\n", iface);

    return transform->border.mode_y;
}

static const ID2D1BorderTransformVtbl d2d_border_transform_vtbl =
{
    d2d_border_transform_QueryInterface,
    d2d_border_transform_AddRef,
    d2d_border_transform_Release,
    d2d_border_transform_GetInputCount,
    d2d_border_transform_SetOutputBuffer,
    d2d_border_transform_SetCached,
    d2d_border_transform_SetExtendModeX,
    d2d_border_transform_SetExtendModeY,
    d2d_border_transform_GetExtendModeX,
    d2d_border_transform_GetExtendModeY,
};

static HRESULT d2d_border_transform_create(D2D1_EXTEND_MODE mode_x, D2D1_EXTEND_MODE mode_y,
        ID2D1BorderTransform **transform)
{
    struct d2d_transform *object;

    *transform = NULL;

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    object->ID2D1TransformNode_iface.lpVtbl = (ID2D1TransformNodeVtbl *)&d2d_border_transform_vtbl;
    object->refcount = 1;
    object->border.mode_x = mode_x;
    object->border.mode_y = mode_y;

    *transform = (ID2D1BorderTransform *)&object->ID2D1TransformNode_iface;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_bounds_adjustment_transform_QueryInterface(
        ID2D1BoundsAdjustmentTransform *iface, REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_ID2D1BoundsAdjustmentTransform)
            || IsEqualGUID(iid, &IID_ID2D1TransformNode)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        *out = iface;
        ID2D1BoundsAdjustmentTransform_AddRef(iface);
        return S_OK;
    }

    WARN("Unsupported interface %s.\n", debugstr_guid(iid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d2d_bounds_adjustment_transform_AddRef(
        ID2D1BoundsAdjustmentTransform *iface)
{
    struct d2d_transform *transform = impl_from_ID2D1BoundsAdjustmentTransform(iface);
    ULONG refcount = InterlockedIncrement(&transform->refcount);

    TRACE("%p increasing refcount to %lu.\n", iface, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d2d_bounds_adjustment_transform_Release(
        ID2D1BoundsAdjustmentTransform *iface)
{
    struct d2d_transform *transform = impl_from_ID2D1BoundsAdjustmentTransform(iface);
    ULONG refcount = InterlockedDecrement(&transform->refcount);

    TRACE("%p decreasing refcount to %lu.\n", iface, refcount);

    if (!refcount)
        free(transform);

    return refcount;
}

static UINT32 STDMETHODCALLTYPE d2d_bounds_adjustment_transform_GetInputCount(
        ID2D1BoundsAdjustmentTransform *iface)
{
    TRACE("iface %p.\n", iface);

    return 1;
}

static void STDMETHODCALLTYPE d2d_bounds_adjustment_transform_SetOutputBounds(
        ID2D1BoundsAdjustmentTransform *iface, const D2D1_RECT_L *bounds)
{
    struct d2d_transform *transform = impl_from_ID2D1BoundsAdjustmentTransform(iface);

    TRACE("iface %p.\n", iface);

    transform->bounds = *bounds;
}

static void STDMETHODCALLTYPE d2d_bounds_adjustment_transform_GetOutputBounds(
        ID2D1BoundsAdjustmentTransform *iface, D2D1_RECT_L *bounds)
{
    struct d2d_transform *transform = impl_from_ID2D1BoundsAdjustmentTransform(iface);

    TRACE("iface %p.\n", iface);

    *bounds = transform->bounds;
}

static const ID2D1BoundsAdjustmentTransformVtbl d2d_bounds_adjustment_transform_vtbl =
{
    d2d_bounds_adjustment_transform_QueryInterface,
    d2d_bounds_adjustment_transform_AddRef,
    d2d_bounds_adjustment_transform_Release,
    d2d_bounds_adjustment_transform_GetInputCount,
    d2d_bounds_adjustment_transform_SetOutputBounds,
    d2d_bounds_adjustment_transform_GetOutputBounds,
};

static HRESULT d2d_bounds_adjustment_transform_create(const D2D1_RECT_L *rect,
        ID2D1BoundsAdjustmentTransform **transform)
{
    struct d2d_transform *object;

    *transform = NULL;

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    object->ID2D1TransformNode_iface.lpVtbl = (ID2D1TransformNodeVtbl *)&d2d_bounds_adjustment_transform_vtbl;
    object->refcount = 1;
    object->bounds = *rect;

    *transform = (ID2D1BoundsAdjustmentTransform *)&object->ID2D1TransformNode_iface;

    return S_OK;
}

static struct d2d_transform_node * d2d_transform_graph_get_node(const struct d2d_transform_graph *graph,
        ID2D1TransformNode *object)
{
    struct d2d_transform_node *node;

    LIST_FOR_EACH_ENTRY(node, &graph->nodes, struct d2d_transform_node, entry)
    {
        if (node->object == object)
            return node;
    }

    return NULL;
}

static HRESULT d2d_transform_graph_add_node(struct d2d_transform_graph *graph,
        ID2D1TransformNode *object)
{
    struct d2d_transform_node *node;

    if (!(node = calloc(1, sizeof(*node))))
        return E_OUTOFMEMORY;
    node->input_count = ID2D1TransformNode_GetInputCount(object);
    if (!(node->inputs = calloc(node->input_count, sizeof(*node->inputs))))
    {
        free(node);
        return E_OUTOFMEMORY;
    }

    node->object = object;
    ID2D1TransformNode_AddRef(node->object);
    list_add_tail(&graph->nodes, &node->entry);

    return S_OK;
}

static void d2d_transform_node_disconnect(struct d2d_transform_node *node)
{
    struct d2d_transform_node *output = node->output;
    unsigned int i;

    if (!output)
        return;

    for (i = 0; i < output->input_count; ++i)
    {
        if (output->inputs[i] == node)
        {
            output->inputs[i] = NULL;
            break;
        }
    }
}

static void d2d_transform_graph_delete_node(struct d2d_transform_graph *graph,
        struct d2d_transform_node *node)
{
    unsigned int i;

    list_remove(&node->entry);
    ID2D1TransformNode_Release(node->object);

    for (i = 0; i < graph->input_count; ++i)
    {
        if (graph->inputs[i].node == node)
            memset(&graph->inputs[i].node, 0, sizeof(graph->inputs[i].node));
    }

    if (graph->output == node)
        graph->output = NULL;

    if (node->render_info)
        ID2D1DrawInfo_Release(&node->render_info->ID2D1DrawInfo_iface);

    d2d_transform_node_disconnect(node);

    free(node->inputs);
    free(node);
}

static void d2d_transform_graph_clear(struct d2d_transform_graph *graph)
{
    struct d2d_transform_node *node, *node_next;

    LIST_FOR_EACH_ENTRY_SAFE(node, node_next, &graph->nodes, struct d2d_transform_node, entry)
    {
        d2d_transform_graph_delete_node(graph, node);
    }
    graph->passthrough = false;
}

static inline struct d2d_transform_graph *impl_from_ID2D1TransformGraph(ID2D1TransformGraph *iface)
{
    return CONTAINING_RECORD(iface, struct d2d_transform_graph, ID2D1TransformGraph_iface);
}

static HRESULT STDMETHODCALLTYPE d2d_transform_graph_QueryInterface(ID2D1TransformGraph *iface, REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_ID2D1TransformGraph)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        ID2D1TransformGraph_AddRef(iface);
        *out = iface;
        return S_OK;
    }

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d2d_transform_graph_AddRef(ID2D1TransformGraph *iface)
{
    struct d2d_transform_graph *graph =impl_from_ID2D1TransformGraph(iface);
    ULONG refcount = InterlockedIncrement(&graph->refcount);

    TRACE("%p increasing refcount to %lu.\n", iface, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d2d_transform_graph_Release(ID2D1TransformGraph *iface)
{
    struct d2d_transform_graph *graph = impl_from_ID2D1TransformGraph(iface);
    ULONG refcount = InterlockedDecrement(&graph->refcount);

    TRACE("%p decreasing refcount to %lu.\n", iface, refcount);

    if (!refcount)
    {
        d2d_transform_graph_clear(graph);
        free(graph->inputs);
        free(graph);
    }

    return refcount;
}

static UINT32 STDMETHODCALLTYPE d2d_transform_graph_GetInputCount(ID2D1TransformGraph *iface)
{
    struct d2d_transform_graph *graph = impl_from_ID2D1TransformGraph(iface);

    TRACE("iface %p.\n", iface);

    return graph->input_count;
}

static HRESULT STDMETHODCALLTYPE d2d_transform_graph_SetSingleTransformNode(ID2D1TransformGraph *iface,
        ID2D1TransformNode *object)
{
    struct d2d_transform_graph *graph = impl_from_ID2D1TransformGraph(iface);
    struct d2d_transform_node *node;
    unsigned int i, input_count;
    HRESULT hr;

    TRACE("iface %p, object %p.\n", iface, object);

    d2d_transform_graph_clear(graph);
    if (FAILED(hr = d2d_transform_graph_add_node(graph, object)))
        return hr;

    node = d2d_transform_graph_get_node(graph, object);
    graph->output = node;

    input_count = ID2D1TransformNode_GetInputCount(object);
    if (graph->input_count != input_count)
        return E_INVALIDARG;

    for (i = 0; i < graph->input_count; ++i)
    {
        graph->inputs[i].node = node;
        graph->inputs[i].index = i;
    }

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_transform_graph_AddNode(ID2D1TransformGraph *iface,
        ID2D1TransformNode *object)
{
    struct d2d_transform_graph *graph = impl_from_ID2D1TransformGraph(iface);

    TRACE("iface %p, object %p.\n", iface, object);

    if (d2d_transform_graph_get_node(graph, object))
        return E_INVALIDARG;

    return d2d_transform_graph_add_node(graph, object);
}

static HRESULT STDMETHODCALLTYPE d2d_transform_graph_RemoveNode(ID2D1TransformGraph *iface,
        ID2D1TransformNode *object)
{
    struct d2d_transform_graph *graph = impl_from_ID2D1TransformGraph(iface);
    struct d2d_transform_node *node;

    TRACE("iface %p, object %p.\n", iface, object);

    if (!(node = d2d_transform_graph_get_node(graph, object)))
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);

    d2d_transform_graph_delete_node(graph, node);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_transform_graph_SetOutputNode(ID2D1TransformGraph *iface,
        ID2D1TransformNode *object)
{
    struct d2d_transform_graph *graph = impl_from_ID2D1TransformGraph(iface);
    struct d2d_transform_node *node;

    TRACE("iface %p, object %p.\n", iface, object);

    if (!(node = d2d_transform_graph_get_node(graph, object)))
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);

    graph->output = node;
    graph->passthrough = false;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_transform_graph_ConnectNode(ID2D1TransformGraph *iface,
        ID2D1TransformNode *from_node, ID2D1TransformNode *to_node, UINT32 index)
{
    struct d2d_transform_graph *graph = impl_from_ID2D1TransformGraph(iface);
    struct d2d_transform_node *from, *to;

    TRACE("iface %p, from_node %p, to_node %p, index %u.\n", iface, from_node, to_node, index);

    if (!(from = d2d_transform_graph_get_node(graph, from_node)))
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);

    if (!(to = d2d_transform_graph_get_node(graph, to_node)))
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);

    if (index >= to->input_count)
        return E_INVALIDARG;

    d2d_transform_node_disconnect(from);
    to->inputs[index] = from;
    from->output = to;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_transform_graph_ConnectToEffectInput(ID2D1TransformGraph *iface,
        UINT32 input_index, ID2D1TransformNode *object, UINT32 node_index)
{
    struct d2d_transform_graph *graph = impl_from_ID2D1TransformGraph(iface);
    struct d2d_transform_node *node;
    unsigned int count;

    TRACE("iface %p, input_index %u, object %p, node_index %u.\n", iface, input_index, object, node_index);

    if (!(node = d2d_transform_graph_get_node(graph, object)))
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);

    if (input_index >= graph->input_count)
        return E_INVALIDARG;

    count = ID2D1TransformNode_GetInputCount(object);
    if (node_index >= count)
        return E_INVALIDARG;

    graph->inputs[input_index].node = node;
    graph->inputs[input_index].index = node_index;
    graph->passthrough = false;

    return S_OK;
}

static void STDMETHODCALLTYPE d2d_transform_graph_Clear(ID2D1TransformGraph *iface)
{
    struct d2d_transform_graph *graph = impl_from_ID2D1TransformGraph(iface);

    TRACE("iface %p.\n", iface);

    d2d_transform_graph_clear(graph);
}

static HRESULT STDMETHODCALLTYPE d2d_transform_graph_SetPassthroughGraph(ID2D1TransformGraph *iface, UINT32 index)
{
    struct d2d_transform_graph *graph = impl_from_ID2D1TransformGraph(iface);

    TRACE("iface %p, index %u.\n", iface, index);

    if (index >= graph->input_count)
        return E_INVALIDARG;

    d2d_transform_graph_clear(graph);

    graph->passthrough = true;
    graph->passthrough_input = index;

    return S_OK;
}

static const ID2D1TransformGraphVtbl d2d_transform_graph_vtbl =
{
    d2d_transform_graph_QueryInterface,
    d2d_transform_graph_AddRef,
    d2d_transform_graph_Release,
    d2d_transform_graph_GetInputCount,
    d2d_transform_graph_SetSingleTransformNode,
    d2d_transform_graph_AddNode,
    d2d_transform_graph_RemoveNode,
    d2d_transform_graph_SetOutputNode,
    d2d_transform_graph_ConnectNode,
    d2d_transform_graph_ConnectToEffectInput,
    d2d_transform_graph_Clear,
    d2d_transform_graph_SetPassthroughGraph,
};

static HRESULT d2d_transform_graph_create(UINT32 input_count, struct d2d_transform_graph **graph)
{
    struct d2d_transform_graph *object;

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    object->ID2D1TransformGraph_iface.lpVtbl = &d2d_transform_graph_vtbl;
    object->refcount = 1;
    list_init(&object->nodes);

    if (!(object->inputs = calloc(input_count, sizeof(*object->inputs))))
    {
        free(object);
        return E_OUTOFMEMORY;
    }
    object->input_count = input_count;

    *graph = object;

    return S_OK;
}

struct d2d_effect_impl
{
    ID2D1EffectImpl ID2D1EffectImpl_iface;
    LONG refcount;
    void (*cleanup)(struct d2d_effect_impl *effect);

    /* Followed by properties block, its size and format depends on particular effect. */
};

static inline struct d2d_effect_impl *impl_from_ID2D1EffectImpl(ID2D1EffectImpl *iface)
{
    return CONTAINING_RECORD(iface, struct d2d_effect_impl, ID2D1EffectImpl_iface);
}

static HRESULT STDMETHODCALLTYPE d2d_effect_impl_QueryInterface(ID2D1EffectImpl *iface,
        REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_ID2D1EffectImpl)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        ID2D1EffectImpl_AddRef(iface);
        *out = iface;
        return S_OK;
    }

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d2d_effect_impl_AddRef(ID2D1EffectImpl *iface)
{
    struct d2d_effect_impl *effect = impl_from_ID2D1EffectImpl(iface);
    return InterlockedIncrement(&effect->refcount);
}

static ULONG STDMETHODCALLTYPE d2d_effect_impl_Release(ID2D1EffectImpl *iface)
{
    struct d2d_effect_impl *effect = impl_from_ID2D1EffectImpl(iface);
    LONG refcount = InterlockedDecrement(&effect->refcount);

    if (!refcount)
    {
        if (effect->cleanup) effect->cleanup(effect);
        free(effect);
    }

    return refcount;
}

static HRESULT STDMETHODCALLTYPE d2d_effect_impl_Initialize(ID2D1EffectImpl *iface,
        ID2D1EffectContext *context, ID2D1TransformGraph *graph)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_effect_impl_PrepareForRender(ID2D1EffectImpl *iface, D2D1_CHANGE_TYPE type)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_effect_impl_SetGraph(ID2D1EffectImpl *iface, ID2D1TransformGraph *graph)
{
    return S_OK;
}

static const ID2D1EffectImplVtbl d2d_effect_impl_vtbl =
{
    d2d_effect_impl_QueryInterface,
    d2d_effect_impl_AddRef,
    d2d_effect_impl_Release,
    d2d_effect_impl_Initialize,
    d2d_effect_impl_PrepareForRender,
    d2d_effect_impl_SetGraph,
};

static HRESULT d2d_effect_create_impl(IUnknown **effect_impl, const void *props,
        size_t props_size)
{
    struct d2d_effect_impl *object;

    if (!(object = calloc(1, sizeof(*object) + props_size)))
        return E_OUTOFMEMORY;

    object->ID2D1EffectImpl_iface.lpVtbl = &d2d_effect_impl_vtbl;
    object->refcount = 1;
    if (props_size) memcpy(object + 1, props, props_size);

    *effect_impl = (IUnknown *)&object->ID2D1EffectImpl_iface;

    return S_OK;
}

static UINT32 effect_property_type_size(D2D1_PROPERTY_TYPE prop_type)
{
    static const UINT32 sizes[D2D1_PROPERTY_TYPE_MATRIX_5X4 + 1] =
    {
        [D2D1_PROPERTY_TYPE_BOOL]       = sizeof(BOOL),
        [D2D1_PROPERTY_TYPE_UINT32]     = sizeof(UINT32),
        [D2D1_PROPERTY_TYPE_INT32]      = sizeof(INT32),
        [D2D1_PROPERTY_TYPE_FLOAT]      = sizeof(float),
        [D2D1_PROPERTY_TYPE_VECTOR2]    = sizeof(D2D_VECTOR_2F),
        [D2D1_PROPERTY_TYPE_VECTOR3]    = sizeof(D2D_VECTOR_3F),
        [D2D1_PROPERTY_TYPE_VECTOR4]    = sizeof(D2D_VECTOR_4F),
        [D2D1_PROPERTY_TYPE_ENUM]       = sizeof(UINT32),
        [D2D1_PROPERTY_TYPE_MATRIX_3X2] = sizeof(D2D_MATRIX_3X2_F),
        [D2D1_PROPERTY_TYPE_MATRIX_4X3] = sizeof(D2D_MATRIX_4X3_F),
        [D2D1_PROPERTY_TYPE_MATRIX_4X4] = sizeof(D2D_MATRIX_4X4_F),
        [D2D1_PROPERTY_TYPE_MATRIX_5X4] = sizeof(D2D_MATRIX_5X4_F),
    };

    if (prop_type >= ARRAY_SIZE(sizes))
        return 0;

    return sizes[prop_type];
}

static HRESULT effect_impl_prop_get_helper(const void *prop_data, D2D1_PROPERTY_TYPE prop_type,
        BYTE *data, UINT32 data_size, UINT32 *actual_size)
{
    UINT32 size = effect_property_type_size(prop_type);

    if (actual_size)
        *actual_size = size;

    if (data && data_size)
    {
        if (data_size < size)
            return E_NOT_SUFFICIENT_BUFFER;
        memcpy(data, prop_data, size);
    }

    return S_OK;
}

static HRESULT effect_impl_prop_set_helper(void *prop_data, D2D1_PROPERTY_TYPE prop_type,
        const BYTE *data, UINT32 data_size)
{
    if (data_size != effect_property_type_size(prop_type))
        return E_INVALIDARG;

    memcpy(prop_data, data, data_size);
    return S_OK;
}

#define EFFECT_PROPERTY_SET(name, prop, type) \
    static HRESULT __stdcall name##_##prop##_set(IUnknown *iface, const BYTE *data, UINT32 data_size) \
    { \
        struct d2d_effect_impl *effect = impl_from_ID2D1EffectImpl((ID2D1EffectImpl *)iface); \
        struct name##_properties *props = (struct name##_properties *)(effect + 1); \
        return effect_impl_prop_set_helper(&props->prop, D2D1_PROPERTY_TYPE_##type, data, data_size); \
    } \

#define EFFECT_PROPERTY_GET(name, prop, type) \
    static HRESULT __stdcall name##_##prop##_get(const IUnknown *iface, BYTE *data, \
            UINT32 data_size, UINT32 *actual_size) \
    { \
        struct d2d_effect_impl *effect = impl_from_ID2D1EffectImpl((ID2D1EffectImpl *)iface); \
        struct name##_properties *props = (struct name##_properties *)(effect + 1); \
        return effect_impl_prop_get_helper(&props->prop, D2D1_PROPERTY_TYPE_##type, data, data_size, actual_size); \
    } \

#define EFFECT_PROPERTY_RW(name, prop, type) \
    EFFECT_PROPERTY_SET(name, prop, type) \
    EFFECT_PROPERTY_GET(name, prop, type)

#define BINDING_RW(name, prop) name##_##prop##_set, name##_##prop##_get

static const WCHAR _2d_affine_transform_description[] =
L"<?xml version='1.0'?>                                                      \
  <Effect>                                                                   \
    <Property name='DisplayName' type='string' value='2D Affine Transform'/> \
    <Property name='Author'      type='string' value='The Wine Project'/>    \
    <Property name='Category'    type='string' value='Stub'/>                \
    <Property name='Description' type='string' value='2D Affine Transform'/> \
    <Inputs>                                                                 \
      <Input name='Source'/>                                                 \
    </Inputs>                                                                \
    <Property name='InterpolationMode' type='enum' />                        \
    <Property name='BorderMode' type='enum' />                               \
    <Property name='TransformMatrix' type='matrix3x2' />                     \
    <Property name='Sharpness' type='float' />                               \
  </Effect>";

struct _2d_affine_transform_properties
{
    D2D1_2DAFFINETRANSFORM_INTERPOLATION_MODE interpolation_mode;
    D2D1_BORDER_MODE border_mode;
    D2D1_MATRIX_3X2_F transform_matrix;
    float sharpness;
};

EFFECT_PROPERTY_RW(_2d_affine_transform, interpolation_mode, ENUM)
EFFECT_PROPERTY_RW(_2d_affine_transform, border_mode, ENUM)
EFFECT_PROPERTY_RW(_2d_affine_transform, transform_matrix, MATRIX_3X2)
EFFECT_PROPERTY_RW(_2d_affine_transform, sharpness, FLOAT)

static const D2D1_PROPERTY_BINDING _2d_affine_transform_bindings[] =
{
    { L"InterpolationMode", BINDING_RW(_2d_affine_transform, interpolation_mode) },
    { L"BorderMode", BINDING_RW(_2d_affine_transform, border_mode) },
    { L"TransformMatrix", BINDING_RW(_2d_affine_transform, transform_matrix) },
    { L"Sharpness", BINDING_RW(_2d_affine_transform, sharpness) },
};

static HRESULT __stdcall _2d_affine_transform_factory(IUnknown **effect)
{
    static const struct _2d_affine_transform_properties properties =
    {
        .interpolation_mode = D2D1_2DAFFINETRANSFORM_INTERPOLATION_MODE_LINEAR,
        .border_mode = D2D1_BORDER_MODE_SOFT,
        .transform_matrix = { ._11 = 1.0f, ._22 = 1.0f },
    };
    return d2d_effect_create_impl(effect, &properties, sizeof(properties));
}

static const WCHAR _3d_perspective_transform_description[] =
L"<?xml version='1.0'?>                                                           \
  <Effect>                                                                        \
    <Property name='DisplayName' type='string' value='3D Perspective Transform'/> \
    <Property name='Author'      type='string' value='The Wine Project'/>         \
    <Property name='Category'    type='string' value='Stub'/>                     \
    <Property name='Description' type='string' value='3D Perspective Transform'/> \
    <Inputs>                                                                      \
      <Input name='Source'/>                                                      \
    </Inputs>                                                                     \
    <Property name='InterpolationMode' type='enum' />                             \
    <Property name='BorderMode' type='enum' />                                    \
    <Property name='Depth' type='float' />                                        \
    <Property name='PerspectiveOrigin' type='vector2' />                          \
    <Property name='LocalOffset' type='vector3' />                                \
    <Property name='GlobalOffset' type='vector3' />                               \
    <Property name='RotationOrigin' type='vector3' />                             \
    <Property name='Rotation' type='vector3' />                                   \
  </Effect>";

struct _3d_perspective_transform_properties
{
    D2D1_3DPERSPECTIVETRANSFORM_INTERPOLATION_MODE interpolation_mode;
    D2D1_BORDER_MODE border_mode;
    float depth;
    D2D1_VECTOR_2F perspective_origin;
    D2D1_VECTOR_3F local_offset;
    D2D1_VECTOR_3F global_offset;
    D2D1_VECTOR_3F rotation_origin;
    D2D1_VECTOR_3F rotation;
};

EFFECT_PROPERTY_RW(_3d_perspective_transform, interpolation_mode, ENUM)
EFFECT_PROPERTY_RW(_3d_perspective_transform, border_mode, ENUM)
EFFECT_PROPERTY_RW(_3d_perspective_transform, depth, FLOAT)
EFFECT_PROPERTY_RW(_3d_perspective_transform, perspective_origin, VECTOR2)
EFFECT_PROPERTY_RW(_3d_perspective_transform, local_offset, VECTOR3)
EFFECT_PROPERTY_RW(_3d_perspective_transform, global_offset, VECTOR3)
EFFECT_PROPERTY_RW(_3d_perspective_transform, rotation_origin, VECTOR3)
EFFECT_PROPERTY_RW(_3d_perspective_transform, rotation, VECTOR3)

static const D2D1_PROPERTY_BINDING _3d_perspective_transform_bindings[] =
{
    { L"InterpolationMode", BINDING_RW(_3d_perspective_transform, interpolation_mode) },
    { L"BorderMode", BINDING_RW(_3d_perspective_transform, border_mode) },
    { L"Depth", BINDING_RW(_3d_perspective_transform, depth) },
    { L"PerspectiveOrigin", BINDING_RW(_3d_perspective_transform, perspective_origin) },
    { L"LocalOffset", BINDING_RW(_3d_perspective_transform, local_offset) },
    { L"GlobalOffset", BINDING_RW(_3d_perspective_transform, global_offset) },
    { L"RotationOrigin", BINDING_RW(_3d_perspective_transform, rotation_origin) },
    { L"Rotation", BINDING_RW(_3d_perspective_transform, rotation) },
};

static HRESULT __stdcall _3d_perspective_transform_factory(IUnknown **effect)
{
    static const struct _3d_perspective_transform_properties properties =
    {
        .interpolation_mode = D2D1_3DPERSPECTIVETRANSFORM_INTERPOLATION_MODE_LINEAR,
        .border_mode = D2D1_BORDER_MODE_SOFT,
        .depth = 1000.0f,
    };
    return d2d_effect_create_impl(effect, &properties, sizeof(properties));
}

static const WCHAR composite_description[] =
L"<?xml version='1.0'?>                                                   \
  <Effect>                                                                \
    <Property name='DisplayName' type='string' value='Composite'/>        \
    <Property name='Author'      type='string' value='The Wine Project'/> \
    <Property name='Category'    type='string' value='Stub'/>             \
    <Property name='Description' type='string' value='Composite'/>        \
    <Inputs minimum='1' maximum='0xffffffff' >                            \
      <Input name='Source1'/>                                             \
      <Input name='Source2'/>                                             \
    </Inputs>                                                             \
    <Property name='Mode' type='enum' />                                  \
  </Effect>";

struct composite_properties
{
    D2D1_COMPOSITE_MODE mode;
};

EFFECT_PROPERTY_RW(composite, mode, ENUM)

static const D2D1_PROPERTY_BINDING composite_bindings[] =
{
    { L"Mode", BINDING_RW(composite, mode) },
};

static HRESULT __stdcall composite_factory(IUnknown **effect)
{
    static const struct composite_properties properties =
    {
        .mode = D2D1_COMPOSITE_MODE_SOURCE_OVER,
    };
    return d2d_effect_create_impl(effect, &properties, sizeof(properties));
}

static const WCHAR crop_description[] =
L"<?xml version='1.0'?>                                                   \
  <Effect>                                                                \
    <Property name='DisplayName' type='string' value='Crop'/>             \
    <Property name='Author'      type='string' value='The Wine Project'/> \
    <Property name='Category'    type='string' value='Stub'/>             \
    <Property name='Description' type='string' value='Crop'/>             \
    <Inputs >                                                             \
      <Input name='Source'/>                                              \
    </Inputs>                                                             \
    <Property name='Rect' type='vector4' />                               \
    <Property name='BorderMode' type='enum' />                            \
  </Effect>";

struct crop_properties
{
    D2D1_VECTOR_4F rect;
    D2D1_BORDER_MODE border_mode;
};

EFFECT_PROPERTY_RW(crop, rect, VECTOR4)
EFFECT_PROPERTY_RW(crop, border_mode, ENUM)

static const D2D1_PROPERTY_BINDING crop_bindings[] =
{
    { L"Rect", BINDING_RW(crop, rect) },
    { L"BorderMode", BINDING_RW(crop, border_mode) },
};

static HRESULT __stdcall crop_factory(IUnknown **effect)
{
    static const struct crop_properties properties =
    {
        .rect = { -INFINITY, -INFINITY, INFINITY, INFINITY },
        .border_mode = D2D1_BORDER_MODE_SOFT,
    };
    return d2d_effect_create_impl(effect, &properties, sizeof(properties));
}

static const WCHAR shadow_description[] =
L"<?xml version='1.0'?>                                                   \
  <Effect>                                                                \
    <Property name='DisplayName' type='string' value='Shadow'/>           \
    <Property name='Author'      type='string' value='The Wine Project'/> \
    <Property name='Category'    type='string' value='Stub'/>             \
    <Property name='Description' type='string' value='Shadow'/>           \
    <Inputs >                                                             \
      <Input name='Source'/>                                              \
    </Inputs>                                                             \
    <Property name='BlurStandardDeviation' type='float' />                \
    <Property name='Color' type='vector4' />                              \
    <Property name='Optimization' type='enum' />                          \
  </Effect>";

struct shadow_properties
{
    float blur_standard_deviation;
    D2D_VECTOR_4F color;
    D2D1_SHADOW_OPTIMIZATION optimization;
};

EFFECT_PROPERTY_RW(shadow, blur_standard_deviation, FLOAT)
EFFECT_PROPERTY_RW(shadow, color, VECTOR4)
EFFECT_PROPERTY_RW(shadow, optimization, ENUM)

static const D2D1_PROPERTY_BINDING shadow_bindings[] =
{
    { L"BlurStandardDeviation", BINDING_RW(shadow, blur_standard_deviation) },
    { L"Color", BINDING_RW(shadow, color) },
    { L"Optimization", BINDING_RW(shadow, optimization) },
};

static HRESULT __stdcall shadow_factory(IUnknown **effect)
{
    static const struct shadow_properties properties =
    {
        .blur_standard_deviation = 3.0f,
        .color = { 0.0f, 0.0f, 0.0f, 1.0f },
        .optimization = D2D1_SHADOW_OPTIMIZATION_BALANCED,
    };
    return d2d_effect_create_impl(effect, &properties, sizeof(properties));
}

static const WCHAR grayscale_description[] =
L"<?xml version='1.0'?>                                                   \
  <Effect>                                                                \
    <Property name='DisplayName' type='string' value='Grayscale'/>        \
    <Property name='Author'      type='string' value='The Wine Project'/> \
    <Property name='Category'    type='string' value='Stub'/>             \
    <Property name='Description' type='string' value='Grayscale'/>        \
    <Inputs >                                                             \
      <Input name='Source'/>                                              \
    </Inputs>                                                             \
  </Effect>";

static HRESULT __stdcall grayscale_factory(IUnknown **effect)
{
    return d2d_effect_create_impl(effect, NULL, 0);
}

static const WCHAR color_matrix_description[] =
L"<?xml version='1.0'?>                                                   \
  <Effect>                                                                \
    <Property name='DisplayName' type='string' value='Color Matrix'/>     \
    <Property name='Author'      type='string' value='The Wine Project'/> \
    <Property name='Category'    type='string' value='Stub'/>             \
    <Property name='Description' type='string' value='Color Matrix'/>     \
    <Inputs >                                                             \
      <Input name='Source'/>                                              \
    </Inputs>                                                             \
    <Property name='ColorMatrix' type='matrix5x4' />                      \
    <Property name='AlphaMode' type='enum' />                             \
    <Property name='ClampOutput' type='bool' />                           \
  </Effect>";

struct color_matrix_properties
{
    D2D1_MATRIX_5X4_F color_matrix;
    D2D1_COLORMATRIX_ALPHA_MODE alpha_mode;
    BOOL clamp_output;
};

EFFECT_PROPERTY_RW(color_matrix, color_matrix, MATRIX_5X4)
EFFECT_PROPERTY_RW(color_matrix, alpha_mode, ENUM)
EFFECT_PROPERTY_RW(color_matrix, clamp_output, BOOL)

static const D2D1_PROPERTY_BINDING color_matrix_bindings[] =
{
    { L"ColorMatrix", BINDING_RW(color_matrix, color_matrix) },
    { L"AlphaMode", BINDING_RW(color_matrix, alpha_mode) },
    { L"ClampOutput", BINDING_RW(color_matrix, clamp_output) },
};

static HRESULT __stdcall color_matrix_factory(IUnknown **effect)
{
    static const struct color_matrix_properties properties =
    {
        .color_matrix = { ._11 = 1.0f, ._22 = 1.0f, ._33 = 1.0f, ._44 = 1.0f },
        .alpha_mode = D2D1_COLORMATRIX_ALPHA_MODE_PREMULTIPLIED,
    };
    return d2d_effect_create_impl(effect, &properties, sizeof(properties));
}

static const WCHAR flood_description[] =
L"<?xml version='1.0'?>                                                   \
  <Effect>                                                                \
    <Property name='DisplayName' type='string' value='Flood'/>            \
    <Property name='Author'      type='string' value='The Wine Project'/> \
    <Property name='Category'    type='string' value='Stub'/>             \
    <Property name='Description' type='string' value='Flood'/>            \
    <Inputs minimum='0' maximum='0' >                                     \
    </Inputs>                                                             \
    <Property name='Color' type='vector4' />                              \
  </Effect>";

struct flood_properties
{
    D2D_VECTOR_4F color;
};

EFFECT_PROPERTY_RW(flood, color, VECTOR4)

static const D2D1_PROPERTY_BINDING flood_bindings[] =
{
    { L"Color", BINDING_RW(flood, color) },
};

static HRESULT __stdcall flood_factory(IUnknown **effect)
{
    static const struct flood_properties properties =
    {
        .color = {0.0f, 0.0f, 0.0f, 1.0f},
    };
    return d2d_effect_create_impl(effect, &properties, sizeof(properties));
}

static const WCHAR gaussian_blur_description[] =
L"<?xml version='1.0'?>                                                   \
  <Effect>                                                                \
    <Property name='DisplayName' type='string' value='Gaussian Blur'/>    \
    <Property name='Author'      type='string' value='The Wine Project'/> \
    <Property name='Category'    type='string' value='Stub'/>             \
    <Property name='Description' type='string' value='Gaussian Blur'/>    \
    <Inputs>                                                              \
      <Input name='Source'/>                                              \
    </Inputs>                                                             \
    <Property name='StandardDeviation' type='float' />                    \
    <Property name='Optimization' type='enum' />                          \
    <Property name='BorderMode' type='enum' />                            \
  </Effect>";

struct gaussian_blur_properties
{
    float standard_deviation;
    D2D1_GAUSSIANBLUR_OPTIMIZATION optimization;
    D2D1_BORDER_MODE border_mode;
};

EFFECT_PROPERTY_RW(gaussian_blur, standard_deviation, FLOAT)
EFFECT_PROPERTY_RW(gaussian_blur, optimization, ENUM)
EFFECT_PROPERTY_RW(gaussian_blur, border_mode, ENUM)

static const D2D1_PROPERTY_BINDING gaussian_blur_bindings[] =
{
    { L"StandardDeviation", BINDING_RW(gaussian_blur, standard_deviation) },
    { L"Optimization", BINDING_RW(gaussian_blur, optimization) },
    { L"BorderMode", BINDING_RW(gaussian_blur, border_mode) },
};

static HRESULT __stdcall gaussian_blur_factory(IUnknown **effect)
{
    static const struct gaussian_blur_properties properties =
    {
        .standard_deviation = 3.0f,
        .optimization = D2D1_GAUSSIANBLUR_OPTIMIZATION_BALANCED,
        .border_mode = D2D1_BORDER_MODE_SOFT,
    };
    return d2d_effect_create_impl(effect, &properties, sizeof(properties));
}

static const WCHAR point_specular_description[] =
L"<?xml version='1.0'?>                                                   \
  <Effect>                                                                \
    <Property name='DisplayName' type='string' value='Point Specular'/>   \
    <Property name='Author'      type='string' value='The Wine Project'/> \
    <Property name='Category'    type='string' value='Stub'/>             \
    <Property name='Description' type='string' value='Point Specular'/>   \
    <Inputs>                                                              \
      <Input name='Source'/>                                              \
    </Inputs>                                                             \
    <Property name='LightPosition' type='vector3' />                      \
    <Property name='SpecularExponent' type='float' />                     \
    <Property name='SpecularConstant' type='float' />                     \
    <Property name='SurfaceScale' type='float' />                         \
    <Property name='Color' type='vector3' />                              \
    <Property name='KernelUnitLength' type='vector2' />                   \
    <Property name='ScaleMode' type='enum' />                             \
  </Effect>";

struct point_specular_properties
{
    D2D_VECTOR_3F light_position;
    float specular_exponent;
    float specular_constant;
    float surface_scale;
    D2D_VECTOR_3F color;
    D2D_VECTOR_2F kernel_unit_length;
    D2D1_POINTSPECULAR_SCALE_MODE scale_mode;
};

EFFECT_PROPERTY_RW(point_specular, light_position, VECTOR3)
EFFECT_PROPERTY_RW(point_specular, specular_exponent, FLOAT)
EFFECT_PROPERTY_RW(point_specular, specular_constant, FLOAT)
EFFECT_PROPERTY_RW(point_specular, surface_scale, FLOAT)
EFFECT_PROPERTY_RW(point_specular, color, VECTOR3)
EFFECT_PROPERTY_RW(point_specular, kernel_unit_length, VECTOR2)
EFFECT_PROPERTY_RW(point_specular, scale_mode, ENUM)

static const D2D1_PROPERTY_BINDING point_specular_bindings[] =
{
    { L"LightPosition", BINDING_RW(point_specular, light_position) },
    { L"SpecularExponent", BINDING_RW(point_specular, specular_exponent) },
    { L"SpecularConstant", BINDING_RW(point_specular, specular_constant) },
    { L"SurfaceScale", BINDING_RW(point_specular, surface_scale) },
    { L"Color", BINDING_RW(point_specular, color) },
    { L"KernelUnitLength", BINDING_RW(point_specular, kernel_unit_length) },
    { L"ScaleMode", BINDING_RW(point_specular, scale_mode) },
};

static HRESULT __stdcall point_specular_factory(IUnknown **effect)
{
    static const struct point_specular_properties properties =
    {
        .specular_exponent = 1.0f,
        .specular_constant = 1.0f,
        .surface_scale = 1.0f,
        .color = { 1.0f, 1.0f, 1.0f },
        .kernel_unit_length = { 1.0f, 1.0f },
        .scale_mode = D2D1_POINTSPECULAR_SCALE_MODE_LINEAR,
    };
    return d2d_effect_create_impl(effect, &properties, sizeof(properties));
}

static const WCHAR arithmetic_composite_description[] =
L"<?xml version='1.0'?>                                                   \
  <Effect>                                                                \
    <Property name='DisplayName' type='string' value='Arithmetic Composite'/> \
    <Property name='Author'      type='string' value='The Wine Project'/> \
    <Property name='Category'    type='string' value='Stub'/>             \
    <Property name='Description' type='string' value='Arithmetic Composite'/> \
    <Inputs minimum='2' maximum='2' >                                     \
      <Input name='Source1'/>                                             \
      <Input name='Source2'/>                                             \
    </Inputs>                                                             \
    <Property name='Coefficients' type='vector4' />                       \
    <Property name='ClampOutput' type='bool' />                           \
  </Effect>";

struct arithmetic_composite_properties
{
    D2D_VECTOR_4F coefficients;
    BOOL clamp_output;
};

EFFECT_PROPERTY_RW(arithmetic_composite, coefficients, VECTOR4)
EFFECT_PROPERTY_RW(arithmetic_composite, clamp_output, BOOL)

static const D2D1_PROPERTY_BINDING arithmetic_composite_bindings[] =
{
    { L"Coefficients", BINDING_RW(arithmetic_composite, coefficients) },
    { L"ClampOutput", BINDING_RW(arithmetic_composite, clamp_output) },
};

static HRESULT __stdcall arithmetic_composite_factory(IUnknown **effect)
{
    static const struct arithmetic_composite_properties properties =
    {
        .coefficients = { 1.0f, 0.0f, 0.0f, 0.0f },
    };
    return d2d_effect_create_impl(effect, &properties, sizeof(properties));
}

static const WCHAR blend_description[] =
L"<?xml version='1.0'?>                                                   \
  <Effect>                                                                \
    <Property name='DisplayName' type='string' value='Blend'/>            \
    <Property name='Author'      type='string' value='The Wine Project'/> \
    <Property name='Category'    type='string' value='Stub'/>             \
    <Property name='Description' type='string' value='Blend'/>            \
    <Inputs minimum='2' maximum='2' >                                     \
      <Input name='Source1'/>                                             \
      <Input name='Source2'/>                                             \
    </Inputs>                                                             \
    <Property name='Mode' type='enum' />                                  \
  </Effect>";

struct blend_properties
{
    D2D1_BLEND_MODE mode;
};

EFFECT_PROPERTY_RW(blend, mode, ENUM)

static const D2D1_PROPERTY_BINDING blend_bindings[] =
{
    { L"Mode", BINDING_RW(blend, mode) },
};

static HRESULT __stdcall blend_factory(IUnknown **effect)
{
    static const struct blend_properties properties = {};
    return d2d_effect_create_impl(effect, &properties, sizeof(properties));
}

static const WCHAR brightness_description[] =
L"<?xml version='1.0'?>                                                   \
  <Effect>                                                                \
    <Property name='DisplayName' type='string' value='Brightness'/>       \
    <Property name='Author'      type='string' value='The Wine Project'/> \
    <Property name='Category'    type='string' value='Stub'/>             \
    <Property name='Description' type='string' value='Brightness'/>       \
    <Inputs>                                                              \
      <Input name='Source'/>                                              \
    </Inputs>                                                             \
    <Property name='WhitePoint' type='vector2' />                         \
    <Property name='BlackPoint' type='vector2' />                         \
  </Effect>";

struct brightness_properties
{
    D2D_VECTOR_2F white_point;
    D2D_VECTOR_2F black_point;
};

EFFECT_PROPERTY_RW(brightness, white_point, VECTOR2)
EFFECT_PROPERTY_RW(brightness, black_point, VECTOR2)

static const D2D1_PROPERTY_BINDING brightness_bindings[] =
{
    { L"WhitePoint", BINDING_RW(brightness, white_point) },
    { L"BlackPoint", BINDING_RW(brightness, black_point) },
};

static HRESULT __stdcall brightness_factory(IUnknown **effect)
{
    static const struct brightness_properties properties =
    {
        .white_point = { 1.0f, 1.0f },
    };
    return d2d_effect_create_impl(effect, &properties, sizeof(properties));
}

static const WCHAR directional_blur_description[] =
L"<?xml version='1.0'?>                                                   \
  <Effect>                                                                \
    <Property name='DisplayName' type='string' value='Directional Blur'/> \
    <Property name='Author'      type='string' value='The Wine Project'/> \
    <Property name='Category'    type='string' value='Stub'/>             \
    <Property name='Description' type='string' value='Directional Blur'/> \
    <Inputs>                                                              \
      <Input name='Source'/>                                              \
    </Inputs>                                                             \
    <Property name='StandardDeviation' type='float' />                    \
    <Property name='Angle' type='float' />                                \
    <Property name='Optimization' type='enum' />                          \
    <Property name='BorderMode' type='enum' />                            \
  </Effect>";

struct directional_blur_properties
{
    float standard_deviation;
    float angle;
    D2D1_DIRECTIONALBLUR_OPTIMIZATION optimization;
    D2D1_BORDER_MODE border_mode;
};

EFFECT_PROPERTY_RW(directional_blur, standard_deviation, FLOAT)
EFFECT_PROPERTY_RW(directional_blur, angle, FLOAT)
EFFECT_PROPERTY_RW(directional_blur, optimization, ENUM)
EFFECT_PROPERTY_RW(directional_blur, border_mode, ENUM)

static const D2D1_PROPERTY_BINDING directional_blur_bindings[] =
{
    { L"StandardDeviation", BINDING_RW(directional_blur, standard_deviation) },
    { L"Angle", BINDING_RW(directional_blur, angle) },
    { L"Optimization", BINDING_RW(directional_blur, optimization) },
    { L"BorderMode", BINDING_RW(directional_blur, border_mode) },
};

static HRESULT __stdcall directional_blur_factory(IUnknown **effect)
{
    static const struct directional_blur_properties properties =
    {
        .standard_deviation = 3.0f,
        .optimization = D2D1_DIRECTIONALBLUR_OPTIMIZATION_BALANCED,
    };
    return d2d_effect_create_impl(effect, &properties, sizeof(properties));
}

static const WCHAR hue_rotation_description[] =
L"<?xml version='1.0'?>                                                   \
  <Effect>                                                                \
    <Property name='DisplayName' type='string' value='Hue Rotation'/>     \
    <Property name='Author'      type='string' value='The Wine Project'/> \
    <Property name='Category'    type='string' value='Stub'/>             \
    <Property name='Description' type='string' value='Hue Rotation'/>     \
    <Inputs>                                                              \
      <Input name='Source'/>                                              \
    </Inputs>                                                             \
    <Property name='Angle' type='float' />                                \
  </Effect>";

struct hue_rotation_properties
{
    float angle;
};

EFFECT_PROPERTY_RW(hue_rotation, angle, FLOAT)

static const D2D1_PROPERTY_BINDING hue_rotation_bindings[] =
{
    { L"Angle", BINDING_RW(hue_rotation, angle) },
};

static HRESULT __stdcall hue_rotation_factory(IUnknown **effect)
{
    static const struct hue_rotation_properties properties = {};
    return d2d_effect_create_impl(effect, &properties, sizeof(properties));
}

static const WCHAR saturation_description[] =
L"<?xml version='1.0'?>                                                   \
  <Effect>                                                                \
    <Property name='DisplayName' type='string' value='Saturation'/>       \
    <Property name='Author'      type='string' value='The Wine Project'/> \
    <Property name='Category'    type='string' value='Stub'/>             \
    <Property name='Description' type='string' value='Saturation'/>       \
    <Inputs>                                                              \
      <Input name='Source'/>                                              \
    </Inputs>                                                             \
    <Property name='Saturation' type='float' />                           \
  </Effect>";

struct saturation_properties
{
    float saturation;
};

EFFECT_PROPERTY_RW(saturation, saturation, FLOAT)

static const D2D1_PROPERTY_BINDING saturation_bindings[] =
{
    { L"Saturation", BINDING_RW(saturation, saturation) },
};

static HRESULT __stdcall saturation_factory(IUnknown **effect)
{
    static const struct saturation_properties properties =
    {
        .saturation = 0.5f,
    };
    return d2d_effect_create_impl(effect, &properties, sizeof(properties));
}

static const WCHAR scale_description[] =
L"<?xml version='1.0'?>                                                   \
  <Effect>                                                                \
    <Property name='DisplayName' type='string' value='Scale'/>            \
    <Property name='Author'      type='string' value='The Wine Project'/> \
    <Property name='Category'    type='string' value='Stub'/>             \
    <Property name='Description' type='string' value='Scale'/>            \
    <Inputs>                                                              \
      <Input name='Source'/>                                              \
    </Inputs>                                                             \
    <Property name='Scale' type='vector2' />                              \
    <Property name='CenterPoint' type='vector2' />                        \
    <Property name='InterpolationMode' type='enum' />                     \
    <Property name='BorderMode' type='enum' />                            \
    <Property name='Sharpness' type='float' />                            \
  </Effect>";

struct scale_properties
{
    D2D_VECTOR_2F scale;
    D2D_VECTOR_2F center_point;
    D2D1_BORDER_MODE border_mode;
    float sharpness;
    D2D1_SCALE_INTERPOLATION_MODE interpolation_mode;
};

EFFECT_PROPERTY_RW(scale, scale, VECTOR2)
EFFECT_PROPERTY_RW(scale, center_point, VECTOR2)
EFFECT_PROPERTY_RW(scale, border_mode, ENUM)
EFFECT_PROPERTY_RW(scale, sharpness, FLOAT)
EFFECT_PROPERTY_RW(scale, interpolation_mode, ENUM)

static const D2D1_PROPERTY_BINDING scale_bindings[] =
{
    { L"Scale", BINDING_RW(scale, scale) },
    { L"CenterPoint", BINDING_RW(scale, center_point) },
    { L"BorderMode", BINDING_RW(scale, border_mode) },
    { L"Sharpness", BINDING_RW(scale, sharpness) },
    { L"InterpolationMode", BINDING_RW(scale, interpolation_mode) },
};

static HRESULT __stdcall scale_factory(IUnknown **effect)
{
    static const struct scale_properties properties =
    {
        .scale = { 1.0f, 1.0f },
        .border_mode = D2D1_BORDER_MODE_SOFT,
        .interpolation_mode = D2D1_SCALE_INTERPOLATION_MODE_LINEAR,
    };
    return d2d_effect_create_impl(effect, &properties, sizeof(properties));
}

struct opacity_properties { float opacity; };
struct exposure_properties { float exposure; };
EFFECT_PROPERTY_GET(opacity, opacity, FLOAT)
EFFECT_PROPERTY_GET(exposure, exposure, FLOAT)

static HRESULT __stdcall opacity_opacity_set(IUnknown *iface, const BYTE *data, UINT32 size)
{
    struct d2d_effect_impl *effect = impl_from_ID2D1EffectImpl((ID2D1EffectImpl *)iface);
    struct opacity_properties *props = (void *)(effect + 1);
    float value;
    if (!data || size != sizeof(value)) return E_INVALIDARG;
    memcpy(&value, data, sizeof(value));
    if (!isfinite(value) || value < 0 || value > 1) return E_INVALIDARG;
    props->opacity = value;
    return S_OK;
}

static HRESULT __stdcall exposure_exposure_set(IUnknown *iface, const BYTE *data, UINT32 size)
{
    struct d2d_effect_impl *effect = impl_from_ID2D1EffectImpl((ID2D1EffectImpl *)iface);
    struct exposure_properties *props = (void *)(effect + 1);
    float value;
    if (!data || size != sizeof(value)) return E_INVALIDARG;
    memcpy(&value, data, sizeof(value));
    if (!isfinite(value) || value < -2 || value > 2) return E_INVALIDARG;
    props->exposure = value;
    return S_OK;
}

static const WCHAR opacity_description[] =
    L"<?xml version='1.0'?><Effect>"
    L"<Property name='DisplayName' type='string' value='Opacity'/>"
    L"<Property name='Author' type='string' value='The Wine Project'/>"
    L"<Property name='Category' type='string' value='Color'/>"
    L"<Property name='Description' type='string' value='Opacity'/>"
    L"<Inputs><Input name='Source'/></Inputs>"
    L"<Property name='Opacity' type='float'/></Effect>";
static const WCHAR exposure_description[] =
    L"<?xml version='1.0'?><Effect>"
    L"<Property name='DisplayName' type='string' value='Exposure'/>"
    L"<Property name='Author' type='string' value='The Wine Project'/>"
    L"<Property name='Category' type='string' value='Color'/>"
    L"<Property name='Description' type='string' value='Exposure'/>"
    L"<Inputs><Input name='Source'/></Inputs>"
    L"<Property name='ExposureValue' type='float'/></Effect>";
static const D2D1_PROPERTY_BINDING opacity_bindings[] =
{
    {L"Opacity", BINDING_RW(opacity, opacity)},
};
static const D2D1_PROPERTY_BINDING exposure_bindings[] =
{
    {L"ExposureValue", BINDING_RW(exposure, exposure)},
};
static HRESULT __stdcall opacity_factory(IUnknown **effect)
{
    const struct opacity_properties properties = {1};
    return d2d_effect_create_impl(effect, &properties, sizeof(properties));
}
static HRESULT __stdcall exposure_factory(IUnknown **effect)
{
    const struct exposure_properties properties = {0};
    return d2d_effect_create_impl(effect, &properties, sizeof(properties));
}

static const WCHAR crossfade_description[] =
    L"<?xml version='1.0'?><Effect>"
    L"<Property name='DisplayName' type='string' value='Cross Fade'/>"
    L"<Property name='Author' type='string' value='The Wine Project'/>"
    L"<Property name='Category' type='string' value='Composite'/>"
    L"<Property name='Description' type='string' value='Cross Fade'/>"
    L"<Inputs><Input name='Destination'/><Input name='Source'/></Inputs>"
    L"<Property name='Weight' type='float'/></Effect>";
static const WCHAR alpha_mask_description[] =
    L"<?xml version='1.0'?><Effect>"
    L"<Property name='DisplayName' type='string' value='Alpha Mask'/>"
    L"<Property name='Author' type='string' value='The Wine Project'/>"
    L"<Property name='Category' type='string' value='Composite'/>"
    L"<Property name='Description' type='string' value='Alpha Mask'/>"
    L"<Inputs><Input name='Source'/><Input name='AlphaMask'/></Inputs></Effect>";
static const D2D1_PROPERTY_BINDING crossfade_bindings[] =
{
    {L"Weight", BINDING_RW(opacity, opacity)},
};
static HRESULT __stdcall crossfade_factory(IUnknown **effect)
{
    const struct opacity_properties properties = {.5f};
    return d2d_effect_create_impl(effect, &properties, sizeof(properties));
}

struct transfer_properties
{
    float red_offset, red_amplitude, red_exponent;
    BOOL red_disable;
    float green_offset, green_amplitude, green_exponent;
    BOOL green_disable;
    float blue_offset, blue_amplitude, blue_exponent;
    BOOL blue_disable;
    float alpha_offset, alpha_amplitude, alpha_exponent;
    BOOL alpha_disable, clamp;
};

#define TRANSFER_CHANNEL(c) \
    EFFECT_PROPERTY_RW(transfer, c##_offset, FLOAT) \
    EFFECT_PROPERTY_RW(transfer, c##_amplitude, FLOAT) \
    EFFECT_PROPERTY_RW(transfer, c##_exponent, FLOAT) \
    EFFECT_PROPERTY_RW(transfer, c##_disable, BOOL)
TRANSFER_CHANNEL(red)
TRANSFER_CHANNEL(green)
TRANSFER_CHANNEL(blue)
TRANSFER_CHANNEL(alpha)
EFFECT_PROPERTY_RW(transfer, clamp, BOOL)
#undef TRANSFER_CHANNEL

#define EFFECT_XML_BEGIN(name) L"<?xml version='1.0'?><Effect>" \
    L"<Property name='DisplayName' type='string' value='" name L"'/>" \
    L"<Property name='Author' type='string' value='The Wine Project'/>" \
    L"<Property name='Category' type='string' value='Color'/>" \
    L"<Property name='Description' type='string' value='" name L"'/>" \
    L"<Inputs><Input name='Source'/></Inputs>"
#define LINEAR_XML(c) L"<Property name='" c L"YIntercept' type='float'/>" \
    L"<Property name='" c L"Slope' type='float'/><Property name='" c L"Disable' type='bool'/>"
#define GAMMA_XML(c) L"<Property name='" c L"Amplitude' type='float'/>" \
    L"<Property name='" c L"Exponent' type='float'/><Property name='" c L"Offset' type='float'/>" \
    L"<Property name='" c L"Disable' type='bool'/>"
static const WCHAR linear_transfer_description[] = EFFECT_XML_BEGIN(L"Linear Transfer")
    LINEAR_XML(L"Red") LINEAR_XML(L"Green") LINEAR_XML(L"Blue") LINEAR_XML(L"Alpha")
    L"<Property name='ClampOutput' type='bool'/></Effect>";
static const WCHAR gamma_transfer_description[] = EFFECT_XML_BEGIN(L"Gamma Transfer")
    GAMMA_XML(L"Red") GAMMA_XML(L"Green") GAMMA_XML(L"Blue") GAMMA_XML(L"Alpha")
    L"<Property name='ClampOutput' type='bool'/></Effect>";
#undef LINEAR_XML
#undef GAMMA_XML

#define LINEAR_BIND(c, n) {n L"YIntercept", BINDING_RW(transfer, c##_offset)}, \
    {n L"Slope", BINDING_RW(transfer, c##_amplitude)}, {n L"Disable", BINDING_RW(transfer, c##_disable)}
#define GAMMA_BIND(c, n) {n L"Amplitude", BINDING_RW(transfer, c##_amplitude)}, \
    {n L"Exponent", BINDING_RW(transfer, c##_exponent)}, {n L"Offset", BINDING_RW(transfer, c##_offset)}, \
    {n L"Disable", BINDING_RW(transfer, c##_disable)}
static const D2D1_PROPERTY_BINDING linear_transfer_bindings[] =
{
    LINEAR_BIND(red, L"Red"), LINEAR_BIND(green, L"Green"), LINEAR_BIND(blue, L"Blue"), LINEAR_BIND(alpha, L"Alpha"),
    {L"ClampOutput", BINDING_RW(transfer, clamp)},
};
static const D2D1_PROPERTY_BINDING gamma_transfer_bindings[] =
{
    GAMMA_BIND(red, L"Red"), GAMMA_BIND(green, L"Green"), GAMMA_BIND(blue, L"Blue"), GAMMA_BIND(alpha, L"Alpha"),
    {L"ClampOutput", BINDING_RW(transfer, clamp)},
};
#undef LINEAR_BIND
#undef GAMMA_BIND
static HRESULT __stdcall linear_transfer_factory(IUnknown **effect)
{
    const struct transfer_properties props = {0,1,1,FALSE, 0,1,1,FALSE, 0,1,1,FALSE, 0,1,1,FALSE, FALSE};
    return d2d_effect_create_impl(effect, &props, sizeof(props));
}
static HRESULT __stdcall gamma_transfer_factory(IUnknown **effect)
{
    return linear_transfer_factory(effect);
}

struct table_transfer_properties
{
    float *tables[4];
    UINT32 sizes[4];
    BOOL disabled[4], clamp;
};

static HRESULT table_transfer_set(IUnknown *iface, unsigned int channel, const BYTE *data, UINT32 size)
{
    struct d2d_effect_impl *effect = impl_from_ID2D1EffectImpl((ID2D1EffectImpl *)iface);
    struct table_transfer_properties *p = (void *)(effect + 1);
    float *copy;
    UINT32 i;
    if (!data || !size || size % sizeof(float)) return E_INVALIDARG;
    for (i = 0; i < size / sizeof(float); ++i)
    {
        float value;
        memcpy(&value, data + i * sizeof(value), sizeof(value));
        if (!isfinite(value)) return E_INVALIDARG;
    }
    if (!(copy = malloc(size))) return E_OUTOFMEMORY;
    memcpy(copy, data, size);
    free(p->tables[channel]);
    p->tables[channel] = copy;
    p->sizes[channel] = size;
    return S_OK;
}

static HRESULT table_transfer_get(const IUnknown *iface, unsigned int channel, BYTE *data, UINT32 size, UINT32 *actual)
{
    struct d2d_effect_impl *effect = impl_from_ID2D1EffectImpl((ID2D1EffectImpl *)iface);
    struct table_transfer_properties *p = (void *)(effect + 1);
    static const float identity_table[] = {0,1};
    UINT32 required = p->sizes[channel] ? p->sizes[channel] : sizeof(identity_table);
    if (actual) *actual = required;
    if (!data) return S_OK;
    if (size < required) return E_NOT_SUFFICIENT_BUFFER;
    memcpy(data, p->tables[channel] ? p->tables[channel] : identity_table, required);
    return S_OK;
}

#define TABLE_CALLBACKS(name, channel) \
static HRESULT __stdcall table_##name##_set(IUnknown *iface, const BYTE *data, UINT32 size) \
{ return table_transfer_set(iface, channel, data, size); } \
static HRESULT __stdcall table_##name##_get(const IUnknown *iface, BYTE *data, UINT32 size, UINT32 *actual) \
{ return table_transfer_get(iface, channel, data, size, actual); } \
static HRESULT __stdcall table_##name##_disable_set(IUnknown *iface, const BYTE *data, UINT32 size) \
{ struct d2d_effect_impl *e = impl_from_ID2D1EffectImpl((ID2D1EffectImpl *)iface); \
  struct table_transfer_properties *p = (void *)(e + 1); \
  if (!data || size != sizeof(BOOL)) return E_INVALIDARG; memcpy(&p->disabled[channel], data, size); return S_OK; } \
static HRESULT __stdcall table_##name##_disable_get(const IUnknown *iface, BYTE *data, UINT32 size, UINT32 *actual) \
{ struct d2d_effect_impl *e = impl_from_ID2D1EffectImpl((ID2D1EffectImpl *)iface); \
  struct table_transfer_properties *p = (void *)(e + 1); \
  return effect_impl_prop_get_helper(&p->disabled[channel], D2D1_PROPERTY_TYPE_BOOL, data, size, actual); }
TABLE_CALLBACKS(red, 0)
TABLE_CALLBACKS(green, 1)
TABLE_CALLBACKS(blue, 2)
TABLE_CALLBACKS(alpha, 3)
#undef TABLE_CALLBACKS
EFFECT_PROPERTY_RW(table_transfer, clamp, BOOL)

#define TABLE_XML(c) L"<Property name='" c L"Table' type='blob'/><Property name='" c L"Disable' type='bool'/>"
static const WCHAR table_transfer_description[] = EFFECT_XML_BEGIN(L"Table Transfer")
    TABLE_XML(L"Red") TABLE_XML(L"Green") TABLE_XML(L"Blue") TABLE_XML(L"Alpha")
    L"<Property name='ClampOutput' type='bool'/></Effect>";
#undef TABLE_XML
#define TABLE_BIND(c, n) {n L"Table", table_##c##_set, table_##c##_get}, \
    {n L"Disable", table_##c##_disable_set, table_##c##_disable_get}
static const D2D1_PROPERTY_BINDING table_transfer_bindings[] =
{
    TABLE_BIND(red, L"Red"), TABLE_BIND(green, L"Green"), TABLE_BIND(blue, L"Blue"), TABLE_BIND(alpha, L"Alpha"),
    {L"ClampOutput", BINDING_RW(table_transfer, clamp)},
};
#undef TABLE_BIND
static void table_transfer_cleanup(struct d2d_effect_impl *effect)
{
    struct table_transfer_properties *p = (void *)(effect + 1);
    unsigned int i;
    for (i = 0; i < 4; ++i) free(p->tables[i]);
}
static HRESULT __stdcall table_transfer_factory(IUnknown **effect)
{
    const struct table_transfer_properties p = {0};
    HRESULT hr = d2d_effect_create_impl(effect, &p, sizeof(p));
    if (SUCCEEDED(hr)) impl_from_ID2D1EffectImpl((ID2D1EffectImpl *)*effect)->cleanup = table_transfer_cleanup;
    return hr;
}

struct morphology_properties { UINT32 mode, width, height; };
EFFECT_PROPERTY_RW(morphology, mode, ENUM)
EFFECT_PROPERTY_RW(morphology, width, UINT32)
EFFECT_PROPERTY_RW(morphology, height, UINT32)
static const WCHAR morphology_description[] = EFFECT_XML_BEGIN(L"Morphology")
    L"<Property name='Mode' type='enum'/><Property name='Width' type='uint32'/>"
    L"<Property name='Height' type='uint32'/></Effect>";
static const D2D1_PROPERTY_BINDING morphology_bindings[] =
{
    {L"Mode", BINDING_RW(morphology, mode)},
    {L"Width", BINDING_RW(morphology, width)},
    {L"Height", BINDING_RW(morphology, height)},
};
static HRESULT __stdcall morphology_factory(IUnknown **effect)
{
    const struct morphology_properties p = {0, 1, 1};
    return d2d_effect_create_impl(effect, &p, sizeof(p));
}

struct tile_properties { D2D1_RECT_F rect; };
struct border_properties { UINT32 x, y; };
EFFECT_PROPERTY_RW(tile, rect, VECTOR4)
EFFECT_PROPERTY_RW(border, x, ENUM)
EFFECT_PROPERTY_RW(border, y, ENUM)
static const WCHAR tile_description[] = EFFECT_XML_BEGIN(L"Tile")
    L"<Property name='Rect' type='vector4'/></Effect>";
static const WCHAR border_description[] = EFFECT_XML_BEGIN(L"Border")
    L"<Property name='EdgeModeX' type='enum'/><Property name='EdgeModeY' type='enum'/></Effect>";
static const D2D1_PROPERTY_BINDING tile_bindings[] = {{L"Rect", BINDING_RW(tile, rect)}};
static const D2D1_PROPERTY_BINDING border_bindings[] =
{
    {L"EdgeModeX", BINDING_RW(border, x)}, {L"EdgeModeY", BINDING_RW(border, y)},
};
static HRESULT __stdcall tile_factory(IUnknown **effect)
{
    const struct tile_properties p = {{0,0,100,100}};
    return d2d_effect_create_impl(effect, &p, sizeof(p));
}
static HRESULT __stdcall border_factory(IUnknown **effect)
{
    const struct border_properties p = {0,0};
    return d2d_effect_create_impl(effect, &p, sizeof(p));
}

struct sepia_properties { float intensity; UINT32 alpha; };
struct tint_properties { D2D1_VECTOR_4F colour; BOOL clamp; };
struct posterize_properties { UINT32 red, green, blue; };
EFFECT_PROPERTY_RW(sepia, intensity, FLOAT)
EFFECT_PROPERTY_RW(sepia, alpha, ENUM)
EFFECT_PROPERTY_RW(tint, colour, VECTOR4)
EFFECT_PROPERTY_RW(tint, clamp, BOOL)
EFFECT_PROPERTY_RW(posterize, red, UINT32)
EFFECT_PROPERTY_RW(posterize, green, UINT32)
EFFECT_PROPERTY_RW(posterize, blue, UINT32)
static const WCHAR sepia_description[] = EFFECT_XML_BEGIN(L"Sepia")
    L"<Property name='Intensity' type='float'/><Property name='AlphaMode' type='enum'/></Effect>";
static const WCHAR tint_description[] = EFFECT_XML_BEGIN(L"Tint")
    L"<Property name='Color' type='vector4'/><Property name='ClampOutput' type='bool'/></Effect>";
static const WCHAR posterize_description[] = EFFECT_XML_BEGIN(L"Posterize")
    L"<Property name='RedValueCount' type='uint32'/><Property name='GreenValueCount' type='uint32'/>"
    L"<Property name='BlueValueCount' type='uint32'/></Effect>";
static const D2D1_PROPERTY_BINDING sepia_bindings[] =
{
    {L"Intensity", BINDING_RW(sepia, intensity)}, {L"AlphaMode", BINDING_RW(sepia, alpha)},
};
static const D2D1_PROPERTY_BINDING tint_bindings[] =
{
    {L"Color", BINDING_RW(tint, colour)}, {L"ClampOutput", BINDING_RW(tint, clamp)},
};
static const D2D1_PROPERTY_BINDING posterize_bindings[] =
{
    {L"RedValueCount", BINDING_RW(posterize, red)}, {L"GreenValueCount", BINDING_RW(posterize, green)},
    {L"BlueValueCount", BINDING_RW(posterize, blue)},
};
static HRESULT __stdcall sepia_factory(IUnknown **effect)
{
    const struct sepia_properties p = {.5f, D2D1_ALPHA_MODE_PREMULTIPLIED};
    return d2d_effect_create_impl(effect, &p, sizeof(p));
}
static HRESULT __stdcall tint_factory(IUnknown **effect)
{
    const struct tint_properties p = {{1,1,1,1}, FALSE};
    return d2d_effect_create_impl(effect, &p, sizeof(p));
}
static HRESULT __stdcall posterize_factory(IUnknown **effect)
{
    const struct posterize_properties p = {4,4,4};
    return d2d_effect_create_impl(effect, &p, sizeof(p));
}

struct displacement_properties { float scale; UINT32 x, y; };
EFFECT_PROPERTY_RW(displacement, scale, FLOAT)
EFFECT_PROPERTY_RW(displacement, x, ENUM)
EFFECT_PROPERTY_RW(displacement, y, ENUM)
static const WCHAR displacement_description[] =
    L"<?xml version='1.0'?><Effect>"
    L"<Property name='DisplayName' type='string' value='Displacement Map'/>"
    L"<Property name='Author' type='string' value='The Wine Project'/>"
    L"<Property name='Category' type='string' value='Distort'/>"
    L"<Property name='Description' type='string' value='Displacement Map'/>"
    L"<Inputs><Input name='Source'/><Input name='Displacement'/></Inputs>"
    L"<Property name='Scale' type='float'/><Property name='XChannelSelect' type='enum'/>"
    L"<Property name='YChannelSelect' type='enum'/></Effect>";
static const D2D1_PROPERTY_BINDING displacement_bindings[] =
{
    {L"Scale", BINDING_RW(displacement, scale)}, {L"XChannelSelect", BINDING_RW(displacement, x)},
    {L"YChannelSelect", BINDING_RW(displacement, y)},
};
static HRESULT __stdcall displacement_factory(IUnknown **effect)
{
    const struct displacement_properties p = {0,3,3};
    return d2d_effect_create_impl(effect, &p, sizeof(p));
}

struct convolution_properties
{
    D2D1_VECTOR_2F unit;
    UINT32 scale_mode, width, height;
    float *kernel;
    UINT32 kernel_size;
    float divisor, bias;
    D2D1_VECTOR_2F offset;
    BOOL preserve;
    UINT32 border;
    BOOL clamp;
};
EFFECT_PROPERTY_RW(convolution, unit, VECTOR2)
EFFECT_PROPERTY_RW(convolution, scale_mode, ENUM)
EFFECT_PROPERTY_RW(convolution, width, UINT32)
EFFECT_PROPERTY_RW(convolution, height, UINT32)
EFFECT_PROPERTY_RW(convolution, divisor, FLOAT)
EFFECT_PROPERTY_RW(convolution, bias, FLOAT)
EFFECT_PROPERTY_RW(convolution, offset, VECTOR2)
EFFECT_PROPERTY_RW(convolution, preserve, BOOL)
EFFECT_PROPERTY_RW(convolution, border, ENUM)
EFFECT_PROPERTY_RW(convolution, clamp, BOOL)
static HRESULT __stdcall convolution_kernel_set(IUnknown *iface, const BYTE *data, UINT32 size)
{
    struct d2d_effect_impl *e = impl_from_ID2D1EffectImpl((ID2D1EffectImpl *)iface);
    struct convolution_properties *p = (void *)(e + 1);
    float *copy;
    if (!data || !size || size % sizeof(float)) return E_INVALIDARG;
    if (!(copy = malloc(size))) return E_OUTOFMEMORY;
    memcpy(copy, data, size);
    free(p->kernel); p->kernel = copy; p->kernel_size = size;
    return S_OK;
}
static HRESULT __stdcall convolution_kernel_get(const IUnknown *iface, BYTE *data, UINT32 size, UINT32 *actual)
{
    static const float identity_kernel[9] = {0,0,0,0,1,0,0,0,0};
    struct d2d_effect_impl *e = impl_from_ID2D1EffectImpl((ID2D1EffectImpl *)iface);
    struct convolution_properties *p = (void *)(e + 1);
    UINT32 required = p->kernel_size ? p->kernel_size : sizeof(identity_kernel);
    if (actual) *actual = required;
    if (!data) return S_OK;
    if (size < required) return E_NOT_SUFFICIENT_BUFFER;
    memcpy(data, p->kernel ? p->kernel : identity_kernel, required);
    return S_OK;
}
static const WCHAR convolution_description[] = EFFECT_XML_BEGIN(L"Convolve Matrix")
    L"<Property name='KernelUnitLength' type='vector2'/><Property name='ScaleMode' type='enum'/>"
    L"<Property name='KernelSizeX' type='uint32'/><Property name='KernelSizeY' type='uint32'/>"
    L"<Property name='KernelMatrix' type='blob'/><Property name='Divisor' type='float'/>"
    L"<Property name='Bias' type='float'/><Property name='KernelOffset' type='vector2'/>"
    L"<Property name='PreserveAlpha' type='bool'/><Property name='BorderMode' type='enum'/>"
    L"<Property name='ClampOutput' type='bool'/></Effect>";
static const D2D1_PROPERTY_BINDING convolution_bindings[] =
{
    {L"KernelUnitLength", BINDING_RW(convolution, unit)}, {L"ScaleMode", BINDING_RW(convolution, scale_mode)},
    {L"KernelSizeX", BINDING_RW(convolution, width)}, {L"KernelSizeY", BINDING_RW(convolution, height)},
    {L"KernelMatrix", convolution_kernel_set, convolution_kernel_get}, {L"Divisor", BINDING_RW(convolution, divisor)},
    {L"Bias", BINDING_RW(convolution, bias)}, {L"KernelOffset", BINDING_RW(convolution, offset)},
    {L"PreserveAlpha", BINDING_RW(convolution, preserve)}, {L"BorderMode", BINDING_RW(convolution, border)},
    {L"ClampOutput", BINDING_RW(convolution, clamp)},
};
static void convolution_cleanup(struct d2d_effect_impl *e)
{
    struct convolution_properties *p = (void *)(e + 1);
    free(p->kernel);
}
static HRESULT __stdcall convolution_factory(IUnknown **effect)
{
    const struct convolution_properties p = {{1,1}, 1, 3, 3, NULL, 0, 1, 0, {0,0}, FALSE, 0, FALSE};
    HRESULT hr = d2d_effect_create_impl(effect, &p, sizeof(p));
    if (SUCCEEDED(hr)) impl_from_ID2D1EffectImpl((ID2D1EffectImpl *)*effect)->cleanup = convolution_cleanup;
    return hr;
}

struct hue_conversion_properties { UINT32 space; };
EFFECT_PROPERTY_RW(hue_conversion, space, ENUM)
static const WCHAR rgb_to_hue_description[] = EFFECT_XML_BEGIN(L"RGB to Hue")
    L"<Property name='OutputColorSpace' type='enum'/></Effect>";
static const WCHAR hue_to_rgb_description[] = EFFECT_XML_BEGIN(L"Hue to RGB")
    L"<Property name='InputColorSpace' type='enum'/></Effect>";
static const D2D1_PROPERTY_BINDING rgb_to_hue_bindings[] = {{L"OutputColorSpace", BINDING_RW(hue_conversion, space)}};
static const D2D1_PROPERTY_BINDING hue_to_rgb_bindings[] = {{L"InputColorSpace", BINDING_RW(hue_conversion, space)}};
static HRESULT __stdcall hue_conversion_factory(IUnknown **effect)
{
    const struct hue_conversion_properties p = {0};
    return d2d_effect_create_impl(effect, &p, sizeof(p));
}

struct atlas_properties { D2D1_RECT_F rect, padding; };
struct dpi_properties { UINT32 interpolation, border; D2D1_VECTOR_2F dpi; };
EFFECT_PROPERTY_RW(atlas, rect, VECTOR4)
EFFECT_PROPERTY_RW(atlas, padding, VECTOR4)
EFFECT_PROPERTY_RW(dpi, interpolation, ENUM)
EFFECT_PROPERTY_RW(dpi, border, ENUM)
EFFECT_PROPERTY_RW(dpi, dpi, VECTOR2)
static const WCHAR atlas_description[] = EFFECT_XML_BEGIN(L"Atlas")
    L"<Property name='InputRect' type='vector4'/><Property name='InputPaddingRect' type='vector4'/></Effect>";
static const WCHAR metadata_description[] = EFFECT_XML_BEGIN(L"Opacity Metadata")
    L"<Property name='InputOpaqueRect' type='vector4'/></Effect>";
static const WCHAR dpi_description[] = EFFECT_XML_BEGIN(L"DPI Compensation")
    L"<Property name='InterpolationMode' type='enum'/><Property name='BorderMode' type='enum'/>"
    L"<Property name='InputDpi' type='vector2'/></Effect>";
static const D2D1_PROPERTY_BINDING atlas_bindings[] =
{
    {L"InputRect", BINDING_RW(atlas, rect)}, {L"InputPaddingRect", BINDING_RW(atlas, padding)},
};
static const D2D1_PROPERTY_BINDING metadata_bindings[] = {{L"InputOpaqueRect", BINDING_RW(atlas, rect)}};
static const D2D1_PROPERTY_BINDING dpi_bindings[] =
{
    {L"InterpolationMode", BINDING_RW(dpi, interpolation)}, {L"BorderMode", BINDING_RW(dpi, border)},
    {L"InputDpi", BINDING_RW(dpi, dpi)},
};
static HRESULT __stdcall atlas_factory(IUnknown **effect)
{
    const struct atlas_properties p = {{-FLT_MAX,-FLT_MAX,FLT_MAX,FLT_MAX}, {-FLT_MAX,-FLT_MAX,FLT_MAX,FLT_MAX}};
    return d2d_effect_create_impl(effect, &p, sizeof(p));
}
static HRESULT __stdcall dpi_factory(IUnknown **effect)
{
    const struct dpi_properties p = {1,0,{96,96}};
    return d2d_effect_create_impl(effect, &p, sizeof(p));
}

struct transform3d_properties { UINT32 interpolation, border; D2D1_MATRIX_4X4_F matrix; };
EFFECT_PROPERTY_RW(transform3d, interpolation, ENUM)
EFFECT_PROPERTY_RW(transform3d, border, ENUM)
EFFECT_PROPERTY_RW(transform3d, matrix, MATRIX_4X4)
static const WCHAR transform3d_description[] = EFFECT_XML_BEGIN(L"3D Transform")
    L"<Property name='InterpolationMode' type='enum'/><Property name='BorderMode' type='enum'/>"
    L"<Property name='TransformMatrix' type='matrix4x4'/></Effect>";
static const D2D1_PROPERTY_BINDING transform3d_bindings[] =
{
    {L"InterpolationMode", BINDING_RW(transform3d, interpolation)}, {L"BorderMode", BINDING_RW(transform3d, border)},
    {L"TransformMatrix", BINDING_RW(transform3d, matrix)},
};
static HRESULT __stdcall transform3d_factory(IUnknown **effect)
{
    const struct transform3d_properties p = {1,0,{{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}}};
    return d2d_effect_create_impl(effect, &p, sizeof(p));
}

struct lighting_properties
{
    D2D1_VECTOR_3F position, at;
    float azimuth, elevation, focus, cone, exponent, constant, surface;
    D2D1_VECTOR_3F colour;
    D2D1_VECTOR_2F unit;
    UINT32 scale;
};
EFFECT_PROPERTY_RW(lighting, position, VECTOR3)
EFFECT_PROPERTY_RW(lighting, at, VECTOR3)
EFFECT_PROPERTY_RW(lighting, azimuth, FLOAT)
EFFECT_PROPERTY_RW(lighting, elevation, FLOAT)
EFFECT_PROPERTY_RW(lighting, focus, FLOAT)
EFFECT_PROPERTY_RW(lighting, cone, FLOAT)
EFFECT_PROPERTY_RW(lighting, exponent, FLOAT)
EFFECT_PROPERTY_RW(lighting, constant, FLOAT)
EFFECT_PROPERTY_RW(lighting, surface, FLOAT)
EFFECT_PROPERTY_RW(lighting, colour, VECTOR3)
EFFECT_PROPERTY_RW(lighting, unit, VECTOR2)
EFFECT_PROPERTY_RW(lighting, scale, ENUM)
#define LIGHT_TAIL L"<Property name='SurfaceScale' type='float'/><Property name='Color' type='vector3'/>" \
    L"<Property name='KernelUnitLength' type='vector2'/><Property name='ScaleMode' type='enum'/></Effect>"
#define DISTANT_HEAD L"<Property name='Azimuth' type='float'/><Property name='Elevation' type='float'/>"
#define POINT_HEAD L"<Property name='LightPosition' type='vector3'/>"
#define SPOT_HEAD POINT_HEAD L"<Property name='PointsAt' type='vector3'/><Property name='Focus' type='float'/>" \
    L"<Property name='LimitingConeAngle' type='float'/>"
#define DIFFUSE_XML L"<Property name='DiffuseConstant' type='float'/>"
#define SPECULAR_XML L"<Property name='SpecularExponent' type='float'/><Property name='SpecularConstant' type='float'/>"
static const WCHAR distant_diffuse_description[] = EFFECT_XML_BEGIN(L"Distant Diffuse") DISTANT_HEAD DIFFUSE_XML LIGHT_TAIL;
static const WCHAR distant_specular_description[] = EFFECT_XML_BEGIN(L"Distant Specular") DISTANT_HEAD SPECULAR_XML LIGHT_TAIL;
static const WCHAR point_diffuse_description[] = EFFECT_XML_BEGIN(L"Point Diffuse") POINT_HEAD DIFFUSE_XML LIGHT_TAIL;
static const WCHAR spot_diffuse_description[] = EFFECT_XML_BEGIN(L"Spot Diffuse") SPOT_HEAD DIFFUSE_XML LIGHT_TAIL;
static const WCHAR spot_specular_description[] = EFFECT_XML_BEGIN(L"Spot Specular") SPOT_HEAD SPECULAR_XML LIGHT_TAIL;
#define LIGHT_BIND {L"SurfaceScale", BINDING_RW(lighting, surface)}, {L"Color", BINDING_RW(lighting, colour)}, \
    {L"KernelUnitLength", BINDING_RW(lighting, unit)}, {L"ScaleMode", BINDING_RW(lighting, scale)}
#define DISTANT_BIND {L"Azimuth", BINDING_RW(lighting, azimuth)}, {L"Elevation", BINDING_RW(lighting, elevation)}
#define POINT_BIND {L"LightPosition", BINDING_RW(lighting, position)}
#define SPOT_BIND POINT_BIND, {L"PointsAt", BINDING_RW(lighting, at)}, {L"Focus", BINDING_RW(lighting, focus)}, \
    {L"LimitingConeAngle", BINDING_RW(lighting, cone)}
#define DIFFUSE_BIND {L"DiffuseConstant", BINDING_RW(lighting, constant)}
#define SPECULAR_BIND {L"SpecularExponent", BINDING_RW(lighting, exponent)}, {L"SpecularConstant", BINDING_RW(lighting, constant)}
static const D2D1_PROPERTY_BINDING distant_diffuse_bindings[] = {DISTANT_BIND, DIFFUSE_BIND, LIGHT_BIND};
static const D2D1_PROPERTY_BINDING distant_specular_bindings[] = {DISTANT_BIND, SPECULAR_BIND, LIGHT_BIND};
static const D2D1_PROPERTY_BINDING point_diffuse_bindings[] = {POINT_BIND, DIFFUSE_BIND, LIGHT_BIND};
static const D2D1_PROPERTY_BINDING spot_diffuse_bindings[] = {SPOT_BIND, DIFFUSE_BIND, LIGHT_BIND};
static const D2D1_PROPERTY_BINDING spot_specular_bindings[] = {SPOT_BIND, SPECULAR_BIND, LIGHT_BIND};
static HRESULT __stdcall lighting_factory(IUnknown **effect)
{
    const struct lighting_properties p = {{0,0,0},{0,0,0},0,0,1,90,1,1,1,{1,1,1},{1,1},1};
    return d2d_effect_create_impl(effect, &p, sizeof(p));
}

struct chroma_properties { D2D1_VECTOR_3F colour; float tolerance; BOOL invert, feather; };
struct white_level_properties { float input, output; };
EFFECT_PROPERTY_RW(chroma, colour, VECTOR3)
EFFECT_PROPERTY_RW(chroma, tolerance, FLOAT)
EFFECT_PROPERTY_RW(chroma, invert, BOOL)
EFFECT_PROPERTY_RW(chroma, feather, BOOL)
EFFECT_PROPERTY_RW(white_level, input, FLOAT)
EFFECT_PROPERTY_RW(white_level, output, FLOAT)
static const WCHAR chroma_description[] = EFFECT_XML_BEGIN(L"Chroma Key")
    L"<Property name='Color' type='vector3'/><Property name='Tolerance' type='float'/>"
    L"<Property name='InvertAlpha' type='bool'/><Property name='Feather' type='bool'/></Effect>";
static const WCHAR white_level_description[] = EFFECT_XML_BEGIN(L"White Level Adjustment")
    L"<Property name='InputWhiteLevel' type='float'/><Property name='OutputWhiteLevel' type='float'/></Effect>";
static const D2D1_PROPERTY_BINDING chroma_bindings[] =
{
    {L"Color", BINDING_RW(chroma, colour)}, {L"Tolerance", BINDING_RW(chroma, tolerance)},
    {L"InvertAlpha", BINDING_RW(chroma, invert)}, {L"Feather", BINDING_RW(chroma, feather)},
};
static const D2D1_PROPERTY_BINDING white_level_bindings[] =
{
    {L"InputWhiteLevel", BINDING_RW(white_level, input)}, {L"OutputWhiteLevel", BINDING_RW(white_level, output)},
};
static HRESULT __stdcall chroma_factory(IUnknown **effect)
{
    const struct chroma_properties p = {{0,1,0}, .1f, FALSE, FALSE};
    return d2d_effect_create_impl(effect, &p, sizeof(p));
}
static HRESULT __stdcall white_level_factory(IUnknown **effect)
{
    const struct white_level_properties p = {80,80};
    return d2d_effect_create_impl(effect, &p, sizeof(p));
}

struct bitmap_source_properties
{
    IWICBitmapSource *source;
    D2D1_VECTOR_2F scale;
    UINT32 interpolation;
    BOOL dpi_correction;
    UINT32 alpha, orientation;
};
EFFECT_PROPERTY_RW(bitmap_source, scale, VECTOR2)
EFFECT_PROPERTY_RW(bitmap_source, interpolation, ENUM)
EFFECT_PROPERTY_RW(bitmap_source, dpi_correction, BOOL)
EFFECT_PROPERTY_RW(bitmap_source, alpha, ENUM)
EFFECT_PROPERTY_RW(bitmap_source, orientation, ENUM)
static HRESULT __stdcall bitmap_source_source_set(IUnknown *iface, const BYTE *data, UINT32 size)
{
    struct d2d_effect_impl *e = impl_from_ID2D1EffectImpl((ID2D1EffectImpl *)iface);
    struct bitmap_source_properties *p = (void *)(e + 1);
    IUnknown *unknown;
    IWICBitmapSource *source = NULL;
    HRESULT hr;
    if (!data || size != sizeof(unknown)) return E_INVALIDARG;
    memcpy(&unknown, data, sizeof(unknown));
    if (unknown && FAILED(hr = IUnknown_QueryInterface(unknown, &IID_IWICBitmapSource, (void **)&source))) return hr;
    if (p->source) IWICBitmapSource_Release(p->source);
    p->source = source;
    return S_OK;
}
static HRESULT __stdcall bitmap_source_source_get(const IUnknown *iface, BYTE *data, UINT32 size, UINT32 *actual)
{
    struct d2d_effect_impl *e = impl_from_ID2D1EffectImpl((ID2D1EffectImpl *)iface);
    struct bitmap_source_properties *p = (void *)(e + 1);
    if (actual) *actual = sizeof(p->source);
    if (!data) return S_OK;
    if (size != sizeof(p->source)) return E_INVALIDARG;
    memcpy(data, &p->source, sizeof(p->source));
    if (p->source) IWICBitmapSource_AddRef(p->source);
    return S_OK;
}
static const WCHAR bitmap_source_description[] =
    L"<?xml version='1.0'?><Effect>"
    L"<Property name='DisplayName' type='string' value='Bitmap Source'/>"
    L"<Property name='Author' type='string' value='The Wine Project'/>"
    L"<Property name='Category' type='string' value='Source'/>"
    L"<Property name='Description' type='string' value='Bitmap Source'/>"
    L"<Inputs minimum='0' maximum='0'></Inputs>"
    L"<Property name='WicBitmapSource' type='iunknown'/><Property name='Scale' type='vector2'/>"
    L"<Property name='InterpolationMode' type='enum'/><Property name='EnableDPICorrection' type='bool'/>"
    L"<Property name='AlphaMode' type='enum'/><Property name='Orientation' type='enum'/></Effect>";
static const D2D1_PROPERTY_BINDING bitmap_source_bindings[] =
{
    {L"WicBitmapSource", bitmap_source_source_set, bitmap_source_source_get},
    {L"Scale", BINDING_RW(bitmap_source, scale)}, {L"InterpolationMode", BINDING_RW(bitmap_source, interpolation)},
    {L"EnableDPICorrection", BINDING_RW(bitmap_source, dpi_correction)}, {L"AlphaMode", BINDING_RW(bitmap_source, alpha)},
    {L"Orientation", BINDING_RW(bitmap_source, orientation)},
};
static void bitmap_source_cleanup(struct d2d_effect_impl *e)
{
    struct bitmap_source_properties *p = (void *)(e + 1);
    if (p->source) IWICBitmapSource_Release(p->source);
}
static HRESULT __stdcall bitmap_source_factory(IUnknown **effect)
{
    const struct bitmap_source_properties p = {NULL,{1,1},1,FALSE,1,1};
    HRESULT hr = d2d_effect_create_impl(effect, &p, sizeof(p));
    if (SUCCEEDED(hr)) impl_from_ID2D1EffectImpl((ID2D1EffectImpl *)*effect)->cleanup = bitmap_source_cleanup;
    return hr;
}

struct histogram_properties { UINT32 bins, channel; float *values; };
EFFECT_PROPERTY_GET(histogram, bins, UINT32)
EFFECT_PROPERTY_GET(histogram, channel, ENUM)
static HRESULT __stdcall histogram_bins_set(IUnknown *iface, const BYTE *data, UINT32 size)
{
    struct d2d_effect_impl *e = impl_from_ID2D1EffectImpl((ID2D1EffectImpl *)iface);
    struct histogram_properties *p = (void *)(e + 1);
    UINT32 bins;
    if (!data || size != sizeof(bins)) return E_INVALIDARG;
    memcpy(&bins, data, size);
    if (bins < 2 || bins > 1024) return E_INVALIDARG;
    free(p->values); p->values = NULL; p->bins = bins;
    return S_OK;
}
static HRESULT __stdcall histogram_channel_set(IUnknown *iface, const BYTE *data, UINT32 size)
{
    struct d2d_effect_impl *e = impl_from_ID2D1EffectImpl((ID2D1EffectImpl *)iface);
    struct histogram_properties *p = (void *)(e + 1);
    UINT32 channel;
    if (!data || size != sizeof(channel)) return E_INVALIDARG;
    memcpy(&channel, data, size);
    if (channel > 3) return E_INVALIDARG;
    free(p->values); p->values = NULL; p->channel = channel;
    return S_OK;
}
static HRESULT __stdcall histogram_output_get(const IUnknown *iface, BYTE *data, UINT32 size, UINT32 *actual)
{
    struct d2d_effect_impl *e = impl_from_ID2D1EffectImpl((ID2D1EffectImpl *)iface);
    struct histogram_properties *p = (void *)(e + 1);
    if (actual) *actual = p->bins * sizeof(float);
    if (!data) return S_OK;
    if (size != p->bins * sizeof(float)) return E_INVALIDARG;
    if (!p->values) return D2DERR_WRONG_STATE;
    memcpy(data, p->values, size);
    return S_OK;
}
HRESULT d2d_effect_set_histogram(struct d2d_effect *effect, const float *values, UINT32 count)
{
    struct d2d_effect_impl *e = impl_from_ID2D1EffectImpl(effect->impl);
    struct histogram_properties *p = (void *)(e + 1);
    float *copy;
    if (count != p->bins) return E_INVALIDARG;
    if (!(copy = malloc(count * sizeof(float)))) return E_OUTOFMEMORY;
    memcpy(copy, values, count * sizeof(float));
    free(p->values); p->values = copy;
    return S_OK;
}
static const WCHAR histogram_description[] = EFFECT_XML_BEGIN(L"Histogram")
    L"<Property name='NumBins' type='uint32'/><Property name='ChannelSelect' type='enum'/>"
    L"<Property name='HistogramOutput' type='blob'/></Effect>";
static const D2D1_PROPERTY_BINDING histogram_bindings[] =
{
    {L"NumBins", histogram_bins_set, histogram_bins_get}, {L"ChannelSelect", histogram_channel_set, histogram_channel_get},
    {L"HistogramOutput", NULL, histogram_output_get},
};
static void histogram_cleanup(struct d2d_effect_impl *e)
{
    struct histogram_properties *p = (void *)(e + 1);
    free(p->values);
}
static HRESULT __stdcall histogram_factory(IUnknown **effect)
{
    const struct histogram_properties p = {256,0,NULL};
    HRESULT hr = d2d_effect_create_impl(effect, &p, sizeof(p));
    if (SUCCEEDED(hr)) impl_from_ID2D1EffectImpl((ID2D1EffectImpl *)*effect)->cleanup = histogram_cleanup;
    return hr;
}

struct contrast_properties { float contrast; BOOL clamp; };
EFFECT_PROPERTY_RW(contrast, contrast, FLOAT)
EFFECT_PROPERTY_RW(contrast, clamp, BOOL)
static const WCHAR contrast_description[] = EFFECT_XML_BEGIN(L"Contrast")
    L"<Property name='Contrast' type='float'/><Property name='ClampInput' type='bool'/></Effect>";
static const D2D1_PROPERTY_BINDING contrast_bindings[] =
{
    {L"Contrast", BINDING_RW(contrast, contrast)}, {L"ClampInput", BINDING_RW(contrast, clamp)},
};
static HRESULT __stdcall contrast_factory(IUnknown **effect)
{
    const struct contrast_properties p = {0,FALSE};
    return d2d_effect_create_impl(effect, &p, sizeof(p));
}

struct straighten_properties { float angle; BOOL maintain; UINT32 interpolation; };
EFFECT_PROPERTY_RW(straighten, angle, FLOAT)
EFFECT_PROPERTY_RW(straighten, maintain, BOOL)
EFFECT_PROPERTY_RW(straighten, interpolation, ENUM)
static const WCHAR straighten_description[] = EFFECT_XML_BEGIN(L"Straighten")
    L"<Property name='Angle' type='float'/><Property name='MaintainSize' type='bool'/>"
    L"<Property name='ScaleMode' type='enum'/></Effect>";
static const D2D1_PROPERTY_BINDING straighten_bindings[] =
{
    {L"Angle", BINDING_RW(straighten, angle)}, {L"MaintainSize", BINDING_RW(straighten, maintain)},
    {L"ScaleMode", BINDING_RW(straighten, interpolation)},
};
static HRESULT __stdcall straighten_factory(IUnknown **effect)
{
    const struct straighten_properties p = {0,TRUE,1};
    return d2d_effect_create_impl(effect, &p, sizeof(p));
}

struct ycbcr_properties { UINT32 subsampling; D2D1_MATRIX_3X2_F transform; UINT32 interpolation; };
EFFECT_PROPERTY_RW(ycbcr, subsampling, ENUM)
EFFECT_PROPERTY_RW(ycbcr, transform, MATRIX_3X2)
EFFECT_PROPERTY_RW(ycbcr, interpolation, ENUM)
static const WCHAR ycbcr_description[] =
    L"<?xml version='1.0'?><Effect>"
    L"<Property name='DisplayName' type='string' value='YCbCr'/>"
    L"<Property name='Author' type='string' value='The Wine Project'/>"
    L"<Property name='Category' type='string' value='Color'/>"
    L"<Property name='Description' type='string' value='YCbCr'/>"
    L"<Inputs><Input name='Y'/><Input name='CbCr'/></Inputs>"
    L"<Property name='ChromaSubsampling' type='enum'/><Property name='TransformMatrix' type='matrix3x2'/>"
    L"<Property name='InterpolationMode' type='enum'/></Effect>";
static const D2D1_PROPERTY_BINDING ycbcr_bindings[] =
{
    {L"ChromaSubsampling", BINDING_RW(ycbcr, subsampling)}, {L"TransformMatrix", BINDING_RW(ycbcr, transform)},
    {L"InterpolationMode", BINDING_RW(ycbcr, interpolation)},
};
static HRESULT __stdcall ycbcr_factory(IUnknown **effect)
{
    const struct ycbcr_properties p = {0,{{1,0,0,1,0,0}},1};
    return d2d_effect_create_impl(effect, &p, sizeof(p));
}

struct lookup_effect_properties { ID2D1LookupTable3D *lut; UINT32 alpha; };
EFFECT_PROPERTY_RW(lookup_effect, alpha, ENUM)
static HRESULT __stdcall lookup_effect_lut_set(IUnknown *iface, const BYTE *data, UINT32 size)
{
    struct d2d_effect_impl *e = impl_from_ID2D1EffectImpl((ID2D1EffectImpl *)iface);
    struct lookup_effect_properties *p = (void *)(e+1);
    IUnknown *unknown;
    ID2D1LookupTable3D *lut = NULL;
    HRESULT hr;
    if (!data || size != sizeof(unknown)) return E_INVALIDARG;
    memcpy(&unknown,data,size);
    if (unknown && FAILED(hr=IUnknown_QueryInterface(unknown,&IID_ID2D1LookupTable3D,(void **)&lut))) return hr;
    if (p->lut) ID2D1LookupTable3D_Release(p->lut);
    p->lut=lut; return S_OK;
}
static HRESULT __stdcall lookup_effect_lut_get(const IUnknown *iface, BYTE *data, UINT32 size, UINT32 *actual)
{
    struct d2d_effect_impl *e = impl_from_ID2D1EffectImpl((ID2D1EffectImpl *)iface);
    struct lookup_effect_properties *p = (void *)(e+1);
    if (actual) *actual=sizeof(p->lut);
    if (!data) return S_OK;
    if (size!=sizeof(p->lut)) return E_INVALIDARG;
    memcpy(data,&p->lut,size);
    if (p->lut) ID2D1LookupTable3D_AddRef(p->lut);
    return S_OK;
}
static const WCHAR lookup_effect_description[] = EFFECT_XML_BEGIN(L"3D Lookup Table")
    L"<Property name='Lut' type='iunknown'/><Property name='AlphaMode' type='enum'/></Effect>";
static const D2D1_PROPERTY_BINDING lookup_effect_bindings[] =
{
    {L"Lut",lookup_effect_lut_set,lookup_effect_lut_get}, {L"AlphaMode",BINDING_RW(lookup_effect,alpha)},
};
static void lookup_effect_cleanup(struct d2d_effect_impl *e)
{
    struct lookup_effect_properties *p=(void *)(e+1);
    if (p->lut) ID2D1LookupTable3D_Release(p->lut);
}
static HRESULT __stdcall lookup_effect_factory(IUnknown **effect)
{
    const struct lookup_effect_properties p={NULL,1};
    HRESULT hr=d2d_effect_create_impl(effect,&p,sizeof(p));
    if (SUCCEEDED(hr)) impl_from_ID2D1EffectImpl((ID2D1EffectImpl *)*effect)->cleanup=lookup_effect_cleanup;
    return hr;
}

struct management_properties { ID2D1ColorContext *source, *destination; UINT32 source_intent, destination_intent, alpha, quality; };
EFFECT_PROPERTY_RW(management, source_intent, ENUM)
EFFECT_PROPERTY_RW(management, destination_intent, ENUM)
EFFECT_PROPERTY_RW(management, alpha, ENUM)
EFFECT_PROPERTY_RW(management, quality, ENUM)
#define COLOR_CONTEXT_BINDING(field) \
static HRESULT __stdcall management_##field##_set(IUnknown *iface,const BYTE *data,UINT32 size) \
{ struct d2d_effect_impl *e=impl_from_ID2D1EffectImpl((ID2D1EffectImpl *)iface); \
  struct management_properties *p=(void *)(e+1); ID2D1ColorContext *value=NULL; IUnknown *u; HRESULT hr; \
  if (!data || size!=sizeof(u)) return E_INVALIDARG; memcpy(&u,data,size); \
  if(u && FAILED(hr=IUnknown_QueryInterface(u,&IID_ID2D1ColorContext,(void **)&value))) return hr; \
  if(p->field) ID2D1ColorContext_Release(p->field); p->field=value; return S_OK; } \
static HRESULT __stdcall management_##field##_get(const IUnknown *iface,BYTE *data,UINT32 size,UINT32 *actual) \
{ struct d2d_effect_impl *e=impl_from_ID2D1EffectImpl((ID2D1EffectImpl *)iface); \
  struct management_properties *p=(void *)(e+1); if(actual)*actual=sizeof(p->field); \
  if(!data)return S_OK; if(size!=sizeof(p->field))return E_INVALIDARG; \
  memcpy(data,&p->field,size); if(p->field)ID2D1ColorContext_AddRef(p->field); return S_OK; }
COLOR_CONTEXT_BINDING(source)
COLOR_CONTEXT_BINDING(destination)
#undef COLOR_CONTEXT_BINDING
static const WCHAR management_description[] = EFFECT_XML_BEGIN(L"Color Management")
    L"<Property name='SourceContext' type='colorcontext'/><Property name='SourceIntent' type='enum'/>"
    L"<Property name='DestinationContext' type='colorcontext'/><Property name='DestinationIntent' type='enum'/>"
    L"<Property name='AlphaMode' type='enum'/><Property name='Quality' type='enum'/></Effect>";
static const D2D1_PROPERTY_BINDING management_bindings[] =
{
    {L"SourceContext",management_source_set,management_source_get}, {L"SourceIntent",BINDING_RW(management,source_intent)},
    {L"DestinationContext",management_destination_set,management_destination_get}, {L"DestinationIntent",BINDING_RW(management,destination_intent)},
    {L"AlphaMode",BINDING_RW(management,alpha)}, {L"Quality",BINDING_RW(management,quality)},
};
static void management_cleanup(struct d2d_effect_impl *e)
{
    struct management_properties *p=(void *)(e+1);
    if(p->source)ID2D1ColorContext_Release(p->source);
    if(p->destination)ID2D1ColorContext_Release(p->destination);
}
static HRESULT __stdcall management_factory(IUnknown **effect)
{
    const struct management_properties p={NULL,NULL,0,0,1,1};
    HRESULT hr=d2d_effect_create_impl(effect,&p,sizeof(p));
    if(SUCCEEDED(hr))impl_from_ID2D1EffectImpl((ID2D1EffectImpl *)*effect)->cleanup=management_cleanup;
    return hr;
}

struct turbulence_properties
{
    D2D1_VECTOR_2F offset, size, frequency;
    UINT32 octaves, seed, noise;
    BOOL stitch;
};
EFFECT_PROPERTY_RW(turbulence, offset, VECTOR2)
EFFECT_PROPERTY_RW(turbulence, size, VECTOR2)
EFFECT_PROPERTY_RW(turbulence, frequency, VECTOR2)
EFFECT_PROPERTY_RW(turbulence, octaves, UINT32)
EFFECT_PROPERTY_RW(turbulence, seed, UINT32)
EFFECT_PROPERTY_RW(turbulence, noise, ENUM)
EFFECT_PROPERTY_RW(turbulence, stitch, BOOL)
static const WCHAR turbulence_description[] =
    L"<?xml version='1.0'?><Effect>"
    L"<Property name='DisplayName' type='string' value='Turbulence'/>"
    L"<Property name='Author' type='string' value='The Wine Project'/>"
    L"<Property name='Category' type='string' value='Source'/>"
    L"<Property name='Description' type='string' value='Turbulence'/>"
    L"<Inputs minimum='0' maximum='0'></Inputs>"
    L"<Property name='Offset' type='vector2'/><Property name='Size' type='vector2'/>"
    L"<Property name='BaseFrequency' type='vector2'/><Property name='NumOctaves' type='uint32'/>"
    L"<Property name='Seed' type='uint32'/><Property name='Noise' type='enum'/><Property name='Stitchable' type='bool'/></Effect>";
static const D2D1_PROPERTY_BINDING turbulence_bindings[] =
{
    {L"Offset",BINDING_RW(turbulence,offset)}, {L"Size",BINDING_RW(turbulence,size)},
    {L"BaseFrequency",BINDING_RW(turbulence,frequency)}, {L"NumOctaves",BINDING_RW(turbulence,octaves)},
    {L"Seed",BINDING_RW(turbulence,seed)}, {L"Noise",BINDING_RW(turbulence,noise)}, {L"Stitchable",BINDING_RW(turbulence,stitch)},
};
static HRESULT __stdcall turbulence_factory(IUnknown **effect)
{
    const struct turbulence_properties p={{0,0},{0,0},{.01f,.01f},1,0,0,FALSE};
    return d2d_effect_create_impl(effect,&p,sizeof(p));
}

struct sharpen_properties { float sharpness, threshold; };
struct emboss_properties { float height, direction; };
struct edge_properties { float strength, radius; UINT32 mode; BOOL overlay; UINT32 alpha; };
EFFECT_PROPERTY_RW(sharpen,sharpness,FLOAT)
EFFECT_PROPERTY_RW(sharpen,threshold,FLOAT)
EFFECT_PROPERTY_RW(emboss,height,FLOAT)
EFFECT_PROPERTY_RW(emboss,direction,FLOAT)
EFFECT_PROPERTY_RW(edge,strength,FLOAT)
EFFECT_PROPERTY_RW(edge,radius,FLOAT)
EFFECT_PROPERTY_RW(edge,mode,ENUM)
EFFECT_PROPERTY_RW(edge,overlay,BOOL)
EFFECT_PROPERTY_RW(edge,alpha,ENUM)
static const WCHAR sharpen_description[] = EFFECT_XML_BEGIN(L"Sharpen")
    L"<Property name='Sharpness' type='float'/><Property name='Threshold' type='float'/></Effect>";
static const WCHAR emboss_description[] = EFFECT_XML_BEGIN(L"Emboss")
    L"<Property name='Height' type='float'/><Property name='Direction' type='float'/></Effect>";
static const WCHAR edge_description[] = EFFECT_XML_BEGIN(L"Edge Detection")
    L"<Property name='Strength' type='float'/><Property name='BlurRadius' type='float'/>"
    L"<Property name='Mode' type='enum'/><Property name='OverlayEdges' type='bool'/><Property name='AlphaMode' type='enum'/></Effect>";
static const D2D1_PROPERTY_BINDING sharpen_bindings[] =
{
    {L"Sharpness",BINDING_RW(sharpen,sharpness)}, {L"Threshold",BINDING_RW(sharpen,threshold)},
};
static const D2D1_PROPERTY_BINDING emboss_bindings[] =
{
    {L"Height",BINDING_RW(emboss,height)}, {L"Direction",BINDING_RW(emboss,direction)},
};
static const D2D1_PROPERTY_BINDING edge_bindings[] =
{
    {L"Strength",BINDING_RW(edge,strength)}, {L"BlurRadius",BINDING_RW(edge,radius)},
    {L"Mode",BINDING_RW(edge,mode)}, {L"OverlayEdges",BINDING_RW(edge,overlay)}, {L"AlphaMode",BINDING_RW(edge,alpha)},
};
static HRESULT __stdcall sharpen_factory(IUnknown **effect)
{
    const struct sharpen_properties p={0,0};
    return d2d_effect_create_impl(effect,&p,sizeof(p));
}
static HRESULT __stdcall emboss_factory(IUnknown **effect)
{
    const struct emboss_properties p={1,0};
    return d2d_effect_create_impl(effect,&p,sizeof(p));
}
static HRESULT __stdcall edge_factory(IUnknown **effect)
{
    const struct edge_properties p={.5f,0,0,FALSE,1};
    return d2d_effect_create_impl(effect,&p,sizeof(p));
}

struct temperature_properties { float temperature, tint; };
EFFECT_PROPERTY_GET(temperature,temperature,FLOAT)
EFFECT_PROPERTY_GET(temperature,tint,FLOAT)
static HRESULT __stdcall temperature_temperature_set(IUnknown *iface,const BYTE *data,UINT32 size)
{
    struct d2d_effect_impl *e=impl_from_ID2D1EffectImpl((ID2D1EffectImpl *)iface);
    struct temperature_properties *p=(void *)(e+1);
    float value;
    if(!data||size!=sizeof(value))return E_INVALIDARG;
    memcpy(&value,data,size);p->temperature=fminf(1,fmaxf(-1,value));return S_OK;
}
static HRESULT __stdcall temperature_tint_set(IUnknown *iface,const BYTE *data,UINT32 size)
{
    struct d2d_effect_impl *e=impl_from_ID2D1EffectImpl((ID2D1EffectImpl *)iface);
    struct temperature_properties *p=(void *)(e+1);
    float value;
    if(!data||size!=sizeof(value))return E_INVALIDARG;
    memcpy(&value,data,size);p->tint=fminf(1,fmaxf(-1,value));return S_OK;
}
static const WCHAR temperature_description[] = EFFECT_XML_BEGIN(L"Temperature and Tint")
    L"<Property name='Temperature' type='float'/><Property name='Tint' type='float'/></Effect>";
static const D2D1_PROPERTY_BINDING temperature_bindings[] =
{
    {L"Temperature",BINDING_RW(temperature,temperature)}, {L"Tint",BINDING_RW(temperature,tint)},
};
static HRESULT __stdcall temperature_factory(IUnknown **effect)
{
    const struct temperature_properties p={0,0};
    return d2d_effect_create_impl(effect,&p,sizeof(p));
}

struct vignette_properties { D2D1_VECTOR_4F colour; float transition, strength; };
EFFECT_PROPERTY_RW(vignette,colour,VECTOR4)
EFFECT_PROPERTY_RW(vignette,transition,FLOAT)
EFFECT_PROPERTY_RW(vignette,strength,FLOAT)
static const WCHAR vignette_description[] = EFFECT_XML_BEGIN(L"Vignette")
    L"<Property name='Color' type='vector4'/><Property name='TransitionSize' type='float'/>"
    L"<Property name='Strength' type='float'/></Effect>";
static const D2D1_PROPERTY_BINDING vignette_bindings[] =
{
    {L"Color",BINDING_RW(vignette,colour)}, {L"TransitionSize",BINDING_RW(vignette,transition)},
    {L"Strength",BINDING_RW(vignette,strength)},
};
static HRESULT __stdcall vignette_factory(IUnknown **effect)
{
    const struct vignette_properties p={{0,0,0,1},.1f,.5f};
    return d2d_effect_create_impl(effect,&p,sizeof(p));
}

struct highlights_properties { float highlights, shadows, clarity; UINT32 gamma; float radius; };
EFFECT_PROPERTY_RW(highlights,highlights,FLOAT)
EFFECT_PROPERTY_RW(highlights,shadows,FLOAT)
EFFECT_PROPERTY_RW(highlights,clarity,FLOAT)
EFFECT_PROPERTY_RW(highlights,gamma,ENUM)
EFFECT_PROPERTY_RW(highlights,radius,FLOAT)
static const WCHAR highlights_description[] = EFFECT_XML_BEGIN(L"Highlights and Shadows")
    L"<Property name='Highlights' type='float'/><Property name='Shadows' type='float'/>"
    L"<Property name='Clarity' type='float'/><Property name='InputGamma' type='enum'/>"
    L"<Property name='MaskBlurRadius' type='float'/></Effect>";
static const D2D1_PROPERTY_BINDING highlights_bindings[] =
{
    {L"Highlights",BINDING_RW(highlights,highlights)}, {L"Shadows",BINDING_RW(highlights,shadows)},
    {L"Clarity",BINDING_RW(highlights,clarity)}, {L"InputGamma",BINDING_RW(highlights,gamma)},
    {L"MaskBlurRadius",BINDING_RW(highlights,radius)},
};
static HRESULT __stdcall highlights_factory(IUnknown **effect)
{
    const struct highlights_properties p={0,0,0,1,1.25f};
    return d2d_effect_create_impl(effect,&p,sizeof(p));
}

struct hdr_properties { float input, output; UINT32 display; };
EFFECT_PROPERTY_RW(hdr,input,FLOAT)
EFFECT_PROPERTY_RW(hdr,output,FLOAT)
EFFECT_PROPERTY_RW(hdr,display,ENUM)
static const WCHAR hdr_description[] = EFFECT_XML_BEGIN(L"HDR Tone Map")
    L"<Property name='InputMaxLuminance' type='float'/><Property name='OutputMaxLuminance' type='float'/>"
    L"<Property name='DisplayMode' type='enum'/></Effect>";
static const D2D1_PROPERTY_BINDING hdr_bindings[] =
{
    {L"InputMaxLuminance",BINDING_RW(hdr,input)}, {L"OutputMaxLuminance",BINDING_RW(hdr,output)},
    {L"DisplayMode",BINDING_RW(hdr,display)},
};
static HRESULT __stdcall hdr_factory(IUnknown **effect)
{
    const struct hdr_properties p={1000,1000,0};
    return d2d_effect_create_impl(effect,&p,sizeof(p));
}
#undef LIGHT_TAIL
#undef DISTANT_HEAD
#undef POINT_HEAD
#undef SPOT_HEAD
#undef DIFFUSE_XML
#undef SPECULAR_XML
#undef LIGHT_BIND
#undef DISTANT_BIND
#undef POINT_BIND
#undef SPOT_BIND
#undef DIFFUSE_BIND
#undef SPECULAR_BIND

void d2d_effects_init_builtins(struct d2d_factory *factory)
{
    static const WCHAR alpha_description[] =
            L"<?xml version='1.0'?><Effect><Property name='DisplayName' type='string' value='Alpha conversion'/>"
            L"<Property name='Author' type='string' value='The Wine Project'/>"
            L"<Property name='Category' type='string' value='Color'/>"
            L"<Property name='Description' type='string' value='Alpha conversion'/>"
            L"<Inputs><Input name='Source'/></Inputs></Effect>";
    static const struct builtin_description
    {
        const CLSID *clsid;
        const WCHAR *description;
        PD2D1_EFFECT_FACTORY factory;
        const D2D1_PROPERTY_BINDING *bindings;
        UINT32 binding_count;
    }
    builtin_effects[] =
    {
#define X(name) name##_description, name##_factory
#define X2(name) name##_description, name##_factory, name##_bindings, ARRAY_SIZE(name##_bindings)
        { &CLSID_D2D12DAffineTransform, X2(_2d_affine_transform) },
        { &CLSID_D2D13DPerspectiveTransform, X2(_3d_perspective_transform) },
        { &CLSID_D2D1Composite, X2(composite) },
        { &CLSID_D2D1Crop, X2(crop) },
        { &CLSID_D2D1Shadow, X2(shadow) },
        { &CLSID_D2D1Grayscale, X(grayscale) },
        { &CLSID_D2D1ColorMatrix, X2(color_matrix) },
        { &CLSID_D2D1Flood, X2(flood) },
        { &CLSID_D2D1GaussianBlur, X2(gaussian_blur) },
        { &CLSID_D2D1PointSpecular, X2(point_specular) },
        { &CLSID_D2D1ArithmeticComposite, X2(arithmetic_composite) },
        { &CLSID_D2D1Blend, X2(blend) },
        { &CLSID_D2D1Brightness, X2(brightness) },
        { &CLSID_D2D1DirectionalBlur, X2(directional_blur) },
        { &CLSID_D2D1HueRotation, X2(hue_rotation) },
        { &CLSID_D2D1Saturation, X2(saturation) },
        { &CLSID_D2D1Scale, X2(scale) },
        { &CLSID_D2D1Premultiply, alpha_description, grayscale_factory },
        { &CLSID_D2D1UnPremultiply, alpha_description, grayscale_factory },
        { &CLSID_D2D1LuminanceToAlpha, alpha_description, grayscale_factory },
        { &CLSID_D2D1Invert, alpha_description, grayscale_factory },
        { &CLSID_D2D1Opacity, X2(opacity) },
        { &CLSID_D2D1Exposure, X2(exposure) },
        { &CLSID_D2D1CrossFade, X2(crossfade) },
        { &CLSID_D2D1AlphaMask, alpha_mask_description, grayscale_factory },
        { &CLSID_D2D1LinearTransfer, X2(linear_transfer) },
        { &CLSID_D2D1GammaTransfer, X2(gamma_transfer) },
        { &CLSID_D2D1TableTransfer, X2(table_transfer) },
        { &CLSID_D2D1DiscreteTransfer, X2(table_transfer) },
        { &CLSID_D2D1Morphology, X2(morphology) },
        { &CLSID_D2D1Tile, X2(tile) },
        { &CLSID_D2D1Border, X2(border) },
        { &CLSID_D2D1Sepia, X2(sepia) },
        { &CLSID_D2D1Tint, X2(tint) },
        { &CLSID_D2D1Posterize, X2(posterize) },
        { &CLSID_D2D1DisplacementMap, X2(displacement) },
        { &CLSID_D2D1ConvolveMatrix, X2(convolution) },
        { &CLSID_D2D1RgbToHue, rgb_to_hue_description, hue_conversion_factory, rgb_to_hue_bindings, ARRAY_SIZE(rgb_to_hue_bindings) },
        { &CLSID_D2D1HueToRgb, hue_to_rgb_description, hue_conversion_factory, hue_to_rgb_bindings, ARRAY_SIZE(hue_to_rgb_bindings) },
        { &CLSID_D2D1Atlas, X2(atlas) },
        { &CLSID_D2D1OpacityMetadata, metadata_description, atlas_factory, metadata_bindings, ARRAY_SIZE(metadata_bindings) },
        { &CLSID_D2D1DpiCompensation, X2(dpi) },
        { &CLSID_D2D13DTransform, X2(transform3d) },
        { &CLSID_D2D1DistantDiffuse, distant_diffuse_description, lighting_factory, distant_diffuse_bindings, ARRAY_SIZE(distant_diffuse_bindings) },
        { &CLSID_D2D1DistantSpecular, distant_specular_description, lighting_factory, distant_specular_bindings, ARRAY_SIZE(distant_specular_bindings) },
        { &CLSID_D2D1PointDiffuse, point_diffuse_description, lighting_factory, point_diffuse_bindings, ARRAY_SIZE(point_diffuse_bindings) },
        { &CLSID_D2D1SpotDiffuse, spot_diffuse_description, lighting_factory, spot_diffuse_bindings, ARRAY_SIZE(spot_diffuse_bindings) },
        { &CLSID_D2D1SpotSpecular, spot_specular_description, lighting_factory, spot_specular_bindings, ARRAY_SIZE(spot_specular_bindings) },
        { &CLSID_D2D1ChromaKey, X2(chroma) },
        { &CLSID_D2D1WhiteLevelAdjustment, X2(white_level) },
        { &CLSID_D2D1BitmapSource, X2(bitmap_source) },
        { &CLSID_D2D1Histogram, X2(histogram) },
        { &CLSID_D2D1Contrast, X2(contrast) },
        { &CLSID_D2D1Straighten, X2(straighten) },
        { &CLSID_D2D1YCbCr, X2(ycbcr) },
        { &CLSID_D2D1LookupTable3D, X2(lookup_effect) },
        { &CLSID_D2D1ColorManagement, X2(management) },
        { &CLSID_D2D1Turbulence, X2(turbulence) },
        { &CLSID_D2D1Sharpen, X2(sharpen) },
        { &CLSID_D2D1Emboss, X2(emboss) },
        { &CLSID_D2D1EdgeDetection, X2(edge) },
        { &CLSID_D2D1TemperatureTint, X2(temperature) },
        { &CLSID_D2D1Vignette, X2(vignette) },
        { &CLSID_D2D1HighlightsShadows, X2(highlights) },
        { &CLSID_D2D1HdrToneMap, X2(hdr) },
#undef X2
#undef X
    };
    unsigned int i;
    HRESULT hr;

    for (i = 0; i < ARRAY_SIZE(builtin_effects); ++i)
    {
        const struct builtin_description *desc = &builtin_effects[i];

        if (FAILED(hr = d2d_factory_register_builtin_effect(factory, desc->clsid, desc->description,
                desc->bindings, desc->binding_count, desc->factory)))
        {
            WARN("Failed to register the effect %s, hr %#lx.\n", wine_dbgstr_guid(desc->clsid), hr);
        }
    }
}

/* Same syntax is used for value and default values. */
static HRESULT d2d_effect_parse_float_array(D2D1_PROPERTY_TYPE type, const WCHAR *value,
        float *vec)
{
    unsigned int i, num_components;
    WCHAR *end_ptr;

    /* Type values are sequential. */
    switch (type)
    {
        case D2D1_PROPERTY_TYPE_VECTOR2:
        case D2D1_PROPERTY_TYPE_VECTOR3:
        case D2D1_PROPERTY_TYPE_VECTOR4:
            num_components = (type - D2D1_PROPERTY_TYPE_VECTOR2) + 2;
            break;
        case D2D1_PROPERTY_TYPE_MATRIX_3X2:
            num_components = 6;
            break;
        case D2D1_PROPERTY_TYPE_MATRIX_4X3:
        case D2D1_PROPERTY_TYPE_MATRIX_4X4:
        case D2D1_PROPERTY_TYPE_MATRIX_5X4:
            num_components = (type - D2D1_PROPERTY_TYPE_MATRIX_4X3) * 4 + 12;
            break;
        default:
            return E_UNEXPECTED;
    }

    if (*(value++) != '(') return E_INVALIDARG;

    for (i = 0; i < num_components; ++i)
    {
        vec[i] = wcstof(value, &end_ptr);
        if (value == end_ptr) return E_INVALIDARG;
        value = end_ptr;

        /* Trailing characters after last component are ignored. */
        if (i == num_components - 1) continue;
        if (*(value++) != ',') return E_INVALIDARG;
    }

    return S_OK;
}

static HRESULT d2d_effect_properties_internal_add(struct d2d_effect_properties *props,
        const WCHAR *name, UINT32 index, BOOL subprop, D2D1_PROPERTY_TYPE type, const WCHAR *value)
{
    static const UINT32 sizes[] =
    {
        [D2D1_PROPERTY_TYPE_UNKNOWN]       = 0,
        [D2D1_PROPERTY_TYPE_STRING]        = 0,
        [D2D1_PROPERTY_TYPE_BOOL]          = sizeof(BOOL),
        [D2D1_PROPERTY_TYPE_UINT32]        = sizeof(UINT32),
        [D2D1_PROPERTY_TYPE_INT32]         = sizeof(INT32),
        [D2D1_PROPERTY_TYPE_FLOAT]         = sizeof(float),
        [D2D1_PROPERTY_TYPE_VECTOR2]       = sizeof(D2D_VECTOR_2F),
        [D2D1_PROPERTY_TYPE_VECTOR3]       = sizeof(D2D_VECTOR_3F),
        [D2D1_PROPERTY_TYPE_VECTOR4]       = sizeof(D2D_VECTOR_4F),
        [D2D1_PROPERTY_TYPE_BLOB]          = 0,
        [D2D1_PROPERTY_TYPE_IUNKNOWN]      = sizeof(IUnknown *),
        [D2D1_PROPERTY_TYPE_ENUM]          = sizeof(UINT32),
        [D2D1_PROPERTY_TYPE_ARRAY]         = sizeof(UINT32),
        [D2D1_PROPERTY_TYPE_CLSID]         = sizeof(CLSID),
        [D2D1_PROPERTY_TYPE_MATRIX_3X2]    = sizeof(D2D_MATRIX_3X2_F),
        [D2D1_PROPERTY_TYPE_MATRIX_4X3]    = sizeof(D2D_MATRIX_4X3_F),
        [D2D1_PROPERTY_TYPE_MATRIX_4X4]    = sizeof(D2D_MATRIX_4X4_F),
        [D2D1_PROPERTY_TYPE_MATRIX_5X4]    = sizeof(D2D_MATRIX_5X4_F),
        [D2D1_PROPERTY_TYPE_COLOR_CONTEXT] = sizeof(ID2D1ColorContext *),
    };
    struct d2d_effect_property *p;
    HRESULT hr;

    assert(type >= D2D1_PROPERTY_TYPE_STRING && type <= D2D1_PROPERTY_TYPE_COLOR_CONTEXT);

    if (!d2d_array_reserve((void **)&props->properties, &props->size, props->count + 1,
            sizeof(*props->properties)))
    {
        return E_OUTOFMEMORY;
    }

    /* TODO: we could save some space for properties that have both getter and setter. */
    if (!d2d_array_reserve((void **)&props->data.ptr, &props->data.size,
            props->data.count + sizes[type], sizeof(*props->data.ptr)))
    {
        return E_OUTOFMEMORY;
    }
    props->data.count += sizes[type];

    p = &props->properties[props->count++];
    memset(p, 0, sizeof(*p));
    p->index = index;
    if (p->index < 0x80000000)
    {
        props->custom_count++;
        /* FIXME: this should probably be controlled by subproperty */
        p->readonly = FALSE;
    }
    else if (subprop)
        p->readonly = TRUE;
    else
        p->readonly = index != D2D1_PROPERTY_CACHED && index != D2D1_PROPERTY_PRECISION;
    p->name = wcsdup(name);
    p->type = type;
    if (p->type == D2D1_PROPERTY_TYPE_STRING)
    {
        p->data.ptr = wcsdup(value);
        p->size = value ? (wcslen(value) + 1) * sizeof(WCHAR) : sizeof(WCHAR);
    }
    else if (p->type == D2D1_PROPERTY_TYPE_BLOB)
    {
        p->data.ptr = NULL;
        p->size = 0;
    }
    else
    {
        void *src = NULL;
        WCHAR *end_ptr;
        UINT32 _uint32;
        float _vec[20];
        CLSID _clsid;
        BOOL _bool;

        p->data.offset = props->offset;
        p->size = sizes[type];
        props->offset += p->size;

        if (value)
        {
            switch (p->type)
            {
                case D2D1_PROPERTY_TYPE_UINT32:
                case D2D1_PROPERTY_TYPE_INT32:
                    _uint32 = wcstoul(value, NULL, 0);
                    src = &_uint32;
                    break;
                case D2D1_PROPERTY_TYPE_FLOAT:
                    _vec[0] = wcstof(value, &end_ptr);
                    src = &_vec[0];
                    break;
                case D2D1_PROPERTY_TYPE_ENUM:
                    _uint32 = wcstoul(value, NULL, 10);
                    src = &_uint32;
                    break;
                case D2D1_PROPERTY_TYPE_BOOL:
                    if (!wcscmp(value, L"true")) _bool = TRUE;
                    else if (!wcscmp(value, L"false")) _bool = FALSE;
                    else return E_INVALIDARG;
                    src = &_bool;
                    break;
                case D2D1_PROPERTY_TYPE_CLSID:
                    CLSIDFromString(value, &_clsid);
                    src = &_clsid;
                    break;
                case D2D1_PROPERTY_TYPE_VECTOR2:
                case D2D1_PROPERTY_TYPE_VECTOR3:
                case D2D1_PROPERTY_TYPE_VECTOR4:
                case D2D1_PROPERTY_TYPE_MATRIX_3X2:
                case D2D1_PROPERTY_TYPE_MATRIX_4X3:
                case D2D1_PROPERTY_TYPE_MATRIX_4X4:
                case D2D1_PROPERTY_TYPE_MATRIX_5X4:
                    if (FAILED(hr = d2d_effect_parse_float_array(p->type, value, _vec)))
                    {
                        WARN("Failed to parse float array %s for type %u.\n",
                                wine_dbgstr_w(value), p->type);
                        return hr;
                    }
                    src = _vec;
                    break;
                case D2D1_PROPERTY_TYPE_IUNKNOWN:
                case D2D1_PROPERTY_TYPE_COLOR_CONTEXT:
                    break;
                default:
                    FIXME("Initial value for property type %u is not handled.\n", p->type);
            }

            if (src && p->size) memcpy(props->data.ptr + p->data.offset, src, p->size);
        }
        else if (p->size)
            memset(props->data.ptr + p->data.offset, 0, p->size);
    }

    return S_OK;
}

HRESULT d2d_effect_properties_add(struct d2d_effect_properties *props, const WCHAR *name,
        UINT32 index, D2D1_PROPERTY_TYPE type, const WCHAR *value)
{
    return d2d_effect_properties_internal_add(props, name, index, FALSE, type, value);
}

HRESULT d2d_effect_subproperties_add(struct d2d_effect_properties *props, const WCHAR *name,
        UINT32 index, D2D1_PROPERTY_TYPE type, const WCHAR *value)
{
    return d2d_effect_properties_internal_add(props, name, index, TRUE, type, value);
}

static HRESULT d2d_effect_duplicate_properties(struct d2d_effect *effect,
        struct d2d_effect_properties *dst, const struct d2d_effect_properties *src)
{
    HRESULT hr;
    size_t i;

    *dst = *src;
    dst->effect = effect;

    if (!(dst->data.ptr = malloc(dst->data.size)))
        return E_OUTOFMEMORY;
    memcpy(dst->data.ptr, src->data.ptr, dst->data.size);

    if (!(dst->properties = calloc(dst->size, sizeof(*dst->properties))))
        return E_OUTOFMEMORY;

    for (i = 0; i < dst->count; ++i)
    {
        struct d2d_effect_property *d = &dst->properties[i];
        const struct d2d_effect_property *s = &src->properties[i];

        *d = *s;
        d->name = wcsdup(s->name);
        if (d->type == D2D1_PROPERTY_TYPE_STRING)
            d->data.ptr = wcsdup((WCHAR *)s->data.ptr);

        if (s->subproperties)
        {
            if (!(d->subproperties = calloc(1, sizeof(*d->subproperties))))
                return E_OUTOFMEMORY;
            if (FAILED(hr = d2d_effect_duplicate_properties(effect, d->subproperties, s->subproperties)))
                return hr;
        }
    }

    return S_OK;
}

static struct d2d_effect_property * d2d_effect_properties_get_property_by_index(
        const struct d2d_effect_properties *properties, UINT32 index)
{
    unsigned int i;

    for (i = 0; i < properties->count; ++i)
    {
        if (properties->properties[i].index == index)
            return &properties->properties[i];
    }

    return NULL;
}

struct d2d_effect_property * d2d_effect_properties_get_property_by_name(
        const struct d2d_effect_properties *properties, const WCHAR *name)
{
    unsigned int i;

    for (i = 0; i < properties->count; ++i)
    {
        if (!wcscmp(properties->properties[i].name, name))
            return &properties->properties[i];
    }

    return NULL;
}

static UINT32 d2d_effect_properties_get_value_size(const struct d2d_effect_properties *properties,
        UINT32 index)
{
    struct d2d_effect *effect = properties->effect;
    struct d2d_effect_property *prop;
    UINT32 size;

    if (!(prop = d2d_effect_properties_get_property_by_index(properties, index)))
        return 0;

    if (prop->get_function)
    {
        if (FAILED(prop->get_function((IUnknown *)effect->impl, NULL, 0, &size))) return 0;
        return size;
    }

    return prop->size;
}

static HRESULT d2d_effect_return_string(const WCHAR *str, WCHAR *buffer, UINT32 buffer_size)
{
    UINT32 size = str ? wcslen(str) : 0;
    if (size >= buffer_size) return D2DERR_INSUFFICIENT_BUFFER;
    if (str) memcpy(buffer, str, (size + 1) * sizeof(*buffer));
    else *buffer = 0;
    return S_OK;
}

static HRESULT d2d_effect_property_get_value(const struct d2d_effect_properties *properties,
        const struct d2d_effect_property *prop, D2D1_PROPERTY_TYPE type, BYTE *value, UINT32 size)
{
    struct d2d_effect *effect = properties->effect;
    PD2D1_PROPERTY_GET_FUNCTION get_function = effect ? prop->get_function : NULL;
    UINT32 actual_size;

    memset(value, 0, size);

    if (type != D2D1_PROPERTY_TYPE_UNKNOWN && prop->type != type) return E_INVALIDARG;
    /* Do not check sizes for variable-length properties. */
    if (prop->type != D2D1_PROPERTY_TYPE_STRING
            && prop->type != D2D1_PROPERTY_TYPE_BLOB
            && prop->size != size)
    {
        return E_INVALIDARG;
    }

    if (get_function)
        return get_function((IUnknown *)effect->impl, value, size, &actual_size);

    switch (prop->type)
    {
        case D2D1_PROPERTY_TYPE_BLOB:
            memset(value, 0, size);
            break;
        case D2D1_PROPERTY_TYPE_STRING:
            return d2d_effect_return_string(prop->data.ptr, (WCHAR *)value, size / sizeof(WCHAR));
        default:
            memcpy(value, properties->data.ptr + prop->data.offset, size);
            break;
    }

    return S_OK;
}

HRESULT d2d_effect_property_get_uint32_value(const struct d2d_effect_properties *properties,
        const struct d2d_effect_property *prop, UINT32 *value)
{
    return d2d_effect_property_get_value(properties, prop, D2D1_PROPERTY_TYPE_UINT32,
            (BYTE *)value, sizeof(*value));
}

static HRESULT d2d_effect_property_set_value(struct d2d_effect_properties *properties,
        struct d2d_effect_property *prop, D2D1_PROPERTY_TYPE type, const BYTE *value, UINT32 size)
{
    struct d2d_effect *effect = properties->effect;
    if (prop->readonly || !effect) return E_INVALIDARG;
    if (type != D2D1_PROPERTY_TYPE_UNKNOWN && prop->type != type) return E_INVALIDARG;
    if (prop->get_function && !prop->set_function) return E_INVALIDARG;
    if (prop->index < 0x80000000 && !prop->set_function) return E_INVALIDARG;

    if (prop->set_function)
        return prop->set_function((IUnknown *)effect->impl, value, size);

    if (prop->size != size) return E_INVALIDARG;

    switch (prop->type)
    {
        case D2D1_PROPERTY_TYPE_BOOL:
        case D2D1_PROPERTY_TYPE_UINT32:
        case D2D1_PROPERTY_TYPE_ENUM:
            memcpy(properties->data.ptr + prop->data.offset, value, size);
            break;
        default:
            FIXME("Unhandled type %u.\n", prop->type);
    }

    return S_OK;
}

void d2d_effect_properties_cleanup(struct d2d_effect_properties *props)
{
    struct d2d_effect_property *p;
    size_t i;

    for (i = 0; i < props->count; ++i)
    {
        p = &props->properties[i];
        free(p->name);
        if (p->type == D2D1_PROPERTY_TYPE_STRING)
            free(p->data.ptr);
        if (p->subproperties)
        {
            d2d_effect_properties_cleanup(p->subproperties);
            free(p->subproperties);
        }
    }
    free(props->properties);
    free(props->data.ptr);
}

static inline struct d2d_effect_context *impl_from_ID2D1EffectContext(ID2D1EffectContext *iface)
{
    return CONTAINING_RECORD(iface, struct d2d_effect_context, ID2D1EffectContext_iface);
}

static HRESULT STDMETHODCALLTYPE d2d_effect_context_QueryInterface(ID2D1EffectContext *iface, REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_ID2D1EffectContext)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        ID2D1EffectContext_AddRef(iface);
        *out = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d2d_effect_context_AddRef(ID2D1EffectContext *iface)
{
    struct d2d_effect_context *effect_context = impl_from_ID2D1EffectContext(iface);
    ULONG refcount = InterlockedIncrement(&effect_context->refcount);

    TRACE("%p increasing refcount to %lu.\n", iface, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d2d_effect_context_Release(ID2D1EffectContext *iface)
{
    struct d2d_effect_context *effect_context = impl_from_ID2D1EffectContext(iface);
    ULONG refcount = InterlockedDecrement(&effect_context->refcount);

    TRACE("%p decreasing refcount to %lu.\n", iface, refcount);

    if (!refcount)
    {
        ID2D1DeviceContext6_Release(&effect_context->device_context->ID2D1DeviceContext6_iface);
        free(effect_context);
    }

    return refcount;
}

static void STDMETHODCALLTYPE d2d_effect_context_GetDpi(ID2D1EffectContext *iface, float *dpi_x, float *dpi_y)
{
    struct d2d_effect_context *effect_context = impl_from_ID2D1EffectContext(iface);

    TRACE("iface %p, dpi_x %p, dpi_y %p.\n", iface, dpi_x, dpi_y);

    ID2D1DeviceContext6_GetDpi(&effect_context->device_context->ID2D1DeviceContext6_iface, dpi_x, dpi_y);
}

static HRESULT STDMETHODCALLTYPE d2d_effect_context_CreateEffect(ID2D1EffectContext *iface,
        REFCLSID clsid, ID2D1Effect **effect)
{
    struct d2d_effect_context *effect_context = impl_from_ID2D1EffectContext(iface);

    TRACE("iface %p, clsid %s, effect %p.\n", iface, debugstr_guid(clsid), effect);

    return ID2D1DeviceContext6_CreateEffect(&effect_context->device_context->ID2D1DeviceContext6_iface,
            clsid, effect);
}

static HRESULT STDMETHODCALLTYPE d2d_effect_context_GetMaximumSupportedFeatureLevel(ID2D1EffectContext *iface,
        const D3D_FEATURE_LEVEL *levels, UINT32 level_count, D3D_FEATURE_LEVEL *max_level)
{
    FIXME("iface %p, levels %p, level_count %u, max_level %p stub!\n", iface, levels, level_count, max_level);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_effect_context_CreateTransformNodeFromEffect(ID2D1EffectContext *iface,
        ID2D1Effect *effect, ID2D1TransformNode **node)
{
    FIXME("iface %p, effect %p, node %p stub!\n", iface, effect, node);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_effect_context_CreateBlendTransform(ID2D1EffectContext *iface,
        UINT32 num_inputs, const D2D1_BLEND_DESCRIPTION *description, ID2D1BlendTransform **transform)
{
    TRACE("iface %p, num_inputs %u, description %p, transform %p,\n", iface, num_inputs, description, transform);

    return d2d_blend_transform_create(num_inputs, description, transform);
}

static HRESULT STDMETHODCALLTYPE d2d_effect_context_CreateBorderTransform(ID2D1EffectContext *iface,
        D2D1_EXTEND_MODE mode_x, D2D1_EXTEND_MODE mode_y, ID2D1BorderTransform **transform)
{
    TRACE("iface %p, mode_x %#x, mode_y %#x, transform %p.\n", iface, mode_x, mode_y, transform);

    return d2d_border_transform_create(mode_x, mode_y, transform);
}

static HRESULT STDMETHODCALLTYPE d2d_effect_context_CreateOffsetTransform(ID2D1EffectContext *iface,
        D2D1_POINT_2L offset, ID2D1OffsetTransform **transform)
{
    TRACE("iface %p, offset %s, transform %p.\n", iface, debug_d2d_point_2l(&offset), transform);

    return d2d_offset_transform_create(offset, transform);
}

static HRESULT STDMETHODCALLTYPE d2d_effect_context_CreateBoundsAdjustmentTransform(ID2D1EffectContext *iface,
        const D2D1_RECT_L *output_rect, ID2D1BoundsAdjustmentTransform **transform)
{
    TRACE("iface %p, output_rect %s, transform %p.\n", iface, debug_d2d_rect_l(output_rect), transform);

    return d2d_bounds_adjustment_transform_create(output_rect, transform);
}

static HRESULT STDMETHODCALLTYPE d2d_effect_context_LoadPixelShader(ID2D1EffectContext *iface,
        REFGUID shader_id, const BYTE *buffer, UINT32 buffer_size)
{
    struct d2d_effect_context *effect_context = impl_from_ID2D1EffectContext(iface);
    struct d2d_device *device = effect_context->device_context->device;
    ID3D11PixelShader *shader;
    HRESULT hr;

    TRACE("iface %p, shader_id %s, buffer %p, buffer_size %u.\n",
            iface, debugstr_guid(shader_id), buffer, buffer_size);

    if (d2d_device_get_indexed_object(&device->shaders, shader_id, NULL))
        return S_OK;

    if (FAILED(hr = ID3D11Device1_CreatePixelShader(effect_context->device_context->d3d_device,
            buffer, buffer_size, NULL, &shader)))
    {
        WARN("Failed to create a pixel shader, hr %#lx.\n", hr);
        return hr;
    }

    hr = d2d_device_add_indexed_object(&device->shaders, shader_id, (IUnknown *)shader);
    ID3D11PixelShader_Release(shader);

    return hr;
}

static HRESULT STDMETHODCALLTYPE d2d_effect_context_LoadVertexShader(ID2D1EffectContext *iface,
        REFGUID shader_id, const BYTE *buffer, UINT32 buffer_size)
{
    struct d2d_effect_context *effect_context = impl_from_ID2D1EffectContext(iface);
    struct d2d_device *device = effect_context->device_context->device;
    ID3D11VertexShader *shader;
    HRESULT hr;

    TRACE("iface %p, shader_id %s, buffer %p, buffer_size %u.\n",
            iface, debugstr_guid(shader_id), buffer, buffer_size);

    if (d2d_device_get_indexed_object(&device->shaders, shader_id, NULL))
        return S_OK;

    if (FAILED(hr = ID3D11Device1_CreateVertexShader(effect_context->device_context->d3d_device,
            buffer, buffer_size, NULL, &shader)))
    {
        WARN("Failed to create vertex shader, hr %#lx.\n", hr);
        return hr;
    }

    hr = d2d_device_add_indexed_object(&device->shaders, shader_id, (IUnknown *)shader);
    ID3D11VertexShader_Release(shader);

    return hr;
}

static HRESULT STDMETHODCALLTYPE d2d_effect_context_LoadComputeShader(ID2D1EffectContext *iface,
        REFGUID shader_id, const BYTE *buffer, UINT32 buffer_size)
{
    struct d2d_effect_context *effect_context = impl_from_ID2D1EffectContext(iface);
    struct d2d_device *device = effect_context->device_context->device;
    ID3D11ComputeShader *shader;
    HRESULT hr;

    TRACE("iface %p, shader_id %s, buffer %p, buffer_size %u.\n",
            iface, debugstr_guid(shader_id), buffer, buffer_size);

    if (d2d_device_get_indexed_object(&device->shaders, shader_id, NULL))
        return S_OK;

    if (FAILED(hr = ID3D11Device1_CreateComputeShader(effect_context->device_context->d3d_device,
            buffer, buffer_size, NULL, &shader)))
    {
        WARN("Failed to create a compute shader, hr %#lx.\n", hr);
        return hr;
    }

    hr = d2d_device_add_indexed_object(&device->shaders, shader_id, (IUnknown *)shader);
    ID3D11ComputeShader_Release(shader);

    return hr;
}

static BOOL STDMETHODCALLTYPE d2d_effect_context_IsShaderLoaded(ID2D1EffectContext *iface, REFGUID shader_id)
{
    struct d2d_effect_context *effect_context = impl_from_ID2D1EffectContext(iface);
    struct d2d_device *device = effect_context->device_context->device;

    TRACE("iface %p, shader_id %s.\n", iface, debugstr_guid(shader_id));

    return d2d_device_get_indexed_object(&device->shaders, shader_id, NULL);
}

static HRESULT STDMETHODCALLTYPE d2d_effect_context_CreateResourceTexture(ID2D1EffectContext *iface,
        const GUID *id,  const D2D1_RESOURCE_TEXTURE_PROPERTIES *texture_properties,
        const BYTE *data, const UINT32 *strides, UINT32 data_size, ID2D1ResourceTexture **texture)
{
    FIXME("iface %p, id %s, texture_properties %p, data %p, strides %p, data_size %u, texture %p stub!\n",
            iface, debugstr_guid(id), texture_properties, data, strides, data_size, texture);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_effect_context_FindResourceTexture(ID2D1EffectContext *iface,
        const GUID *id, ID2D1ResourceTexture **texture)
{
    FIXME("iface %p, id %s, texture %p stub!\n", iface, debugstr_guid(id), texture);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_effect_context_CreateVertexBuffer(ID2D1EffectContext *iface,
        const D2D1_VERTEX_BUFFER_PROPERTIES *buffer_properties, const GUID *id,
        const D2D1_CUSTOM_VERTEX_BUFFER_PROPERTIES *custom_buffer_properties,
        ID2D1VertexBuffer **buffer)
{
    struct d2d_effect_context *effect_context = impl_from_ID2D1EffectContext(iface);
    struct d2d_device_context *context = effect_context->device_context;
    HRESULT hr;

    FIXME("iface %p, buffer_properties %p, id %s, custom_buffer_properties %p, buffer %p stub!\n",
            iface, buffer_properties, debugstr_guid(id), custom_buffer_properties, buffer);

    if (id && d2d_device_get_indexed_object(&context->vertex_buffers, id, (IUnknown **)buffer))
        return S_OK;

    if (SUCCEEDED(hr = d2d_vertex_buffer_create(buffer)))
    {
        if (id)
            hr = d2d_device_add_indexed_object(&context->vertex_buffers, id, (IUnknown *)*buffer);
    }

    if (FAILED(hr))
        *buffer = NULL;

    return hr;
}

static HRESULT STDMETHODCALLTYPE d2d_effect_context_FindVertexBuffer(ID2D1EffectContext *iface,
        const GUID *id, ID2D1VertexBuffer **buffer)
{
    struct d2d_effect_context *effect_context = impl_from_ID2D1EffectContext(iface);
    struct d2d_device_context *context = effect_context->device_context;

    TRACE("iface %p, id %s, buffer %p.\n", iface, debugstr_guid(id), buffer);

    if (!d2d_device_get_indexed_object(&context->vertex_buffers, id, (IUnknown **)buffer))
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_effect_context_CreateColorContext(ID2D1EffectContext *iface,
        D2D1_COLOR_SPACE space, const BYTE *profile, UINT32 profile_size, ID2D1ColorContext **color_context)
{
    struct d2d_effect_context *effect_context = impl_from_ID2D1EffectContext(iface);

    TRACE("iface %p, space %#x, profile %p, profile_size %u, color_context %p.\n",
            iface, space, profile, profile_size, color_context);

    return ID2D1DeviceContext6_CreateColorContext(&effect_context->device_context->ID2D1DeviceContext6_iface,
            space, profile, profile_size, color_context);
}

static HRESULT STDMETHODCALLTYPE d2d_effect_context_CreateColorContextFromFilename(ID2D1EffectContext *iface,
        const WCHAR *filename, ID2D1ColorContext **color_context)
{
    struct d2d_effect_context *effect_context = impl_from_ID2D1EffectContext(iface);

    TRACE("iface %p, filename %s, color_context %p.\n", iface, debugstr_w(filename), color_context);

    return ID2D1DeviceContext6_CreateColorContextFromFilename(&effect_context->device_context->ID2D1DeviceContext6_iface,
            filename, color_context);
}

static HRESULT STDMETHODCALLTYPE d2d_effect_context_CreateColorContextFromWicColorContext(ID2D1EffectContext *iface,
        IWICColorContext *wic_color_context, ID2D1ColorContext **color_context)
{
    struct d2d_effect_context *effect_context = impl_from_ID2D1EffectContext(iface);

    TRACE("iface %p, wic_color_context %p, color_context %p.\n", iface, wic_color_context, color_context);

    return ID2D1DeviceContext6_CreateColorContextFromWicColorContext(&effect_context->device_context->ID2D1DeviceContext6_iface,
            wic_color_context, color_context);
}

static HRESULT STDMETHODCALLTYPE d2d_effect_context_CheckFeatureSupport(ID2D1EffectContext *iface,
        D2D1_FEATURE feature, void *data, UINT32 data_size)
{
    struct d2d_effect_context *effect_context = impl_from_ID2D1EffectContext(iface);
    D3D11_FEATURE d3d11_feature;

    TRACE("iface %p, feature %#x, data %p, data_size %u.\n", iface, feature, data, data_size);

    /* Data structures are compatible. */
    switch (feature)
    {
        case D2D1_FEATURE_DOUBLES: d3d11_feature = D3D11_FEATURE_DOUBLES; break;
        case D2D1_FEATURE_D3D10_X_HARDWARE_OPTIONS: d3d11_feature = D3D11_FEATURE_D3D10_X_HARDWARE_OPTIONS; break;
        default:
            WARN("Unexpected feature index %d.\n", feature);
            return E_INVALIDARG;
    }

    return ID3D11Device1_CheckFeatureSupport(effect_context->device_context->d3d_device,
            d3d11_feature, data, data_size);
}

static BOOL STDMETHODCALLTYPE d2d_effect_context_IsBufferPrecisionSupported(ID2D1EffectContext *iface,
        D2D1_BUFFER_PRECISION precision)
{
    struct d2d_effect_context *effect_context = impl_from_ID2D1EffectContext(iface);

    TRACE("iface %p, precision %u.\n", iface, precision);

    return ID2D1DeviceContext6_IsBufferPrecisionSupported(&effect_context->device_context->ID2D1DeviceContext6_iface,
            precision);
}

static const ID2D1EffectContextVtbl d2d_effect_context_vtbl =
{
    d2d_effect_context_QueryInterface,
    d2d_effect_context_AddRef,
    d2d_effect_context_Release,
    d2d_effect_context_GetDpi,
    d2d_effect_context_CreateEffect,
    d2d_effect_context_GetMaximumSupportedFeatureLevel,
    d2d_effect_context_CreateTransformNodeFromEffect,
    d2d_effect_context_CreateBlendTransform,
    d2d_effect_context_CreateBorderTransform,
    d2d_effect_context_CreateOffsetTransform,
    d2d_effect_context_CreateBoundsAdjustmentTransform,
    d2d_effect_context_LoadPixelShader,
    d2d_effect_context_LoadVertexShader,
    d2d_effect_context_LoadComputeShader,
    d2d_effect_context_IsShaderLoaded,
    d2d_effect_context_CreateResourceTexture,
    d2d_effect_context_FindResourceTexture,
    d2d_effect_context_CreateVertexBuffer,
    d2d_effect_context_FindVertexBuffer,
    d2d_effect_context_CreateColorContext,
    d2d_effect_context_CreateColorContextFromFilename,
    d2d_effect_context_CreateColorContextFromWicColorContext,
    d2d_effect_context_CheckFeatureSupport,
    d2d_effect_context_IsBufferPrecisionSupported,
};

void d2d_effect_context_init(struct d2d_effect_context *effect_context, struct d2d_device_context *device_context)
{
    effect_context->ID2D1EffectContext_iface.lpVtbl = &d2d_effect_context_vtbl;
    effect_context->refcount = 1;
    effect_context->device_context = device_context;
    ID2D1DeviceContext6_AddRef(&device_context->ID2D1DeviceContext6_iface);
}

static inline struct d2d_effect *impl_from_ID2D1Effect(ID2D1Effect *iface)
{
    return CONTAINING_RECORD(iface, struct d2d_effect, ID2D1Effect_iface);
}

static void d2d_effect_cleanup(struct d2d_effect *effect)
{
    unsigned int i;

    for (i = 0; i < effect->input_count; ++i)
    {
        if (effect->inputs[i])
            ID2D1Image_Release(effect->inputs[i]);
    }
    free(effect->inputs);
    ID2D1EffectContext_Release(&effect->effect_context->ID2D1EffectContext_iface);
    if (effect->graph)
        ID2D1TransformGraph_Release(&effect->graph->ID2D1TransformGraph_iface);
    d2d_effect_properties_cleanup(&effect->properties);
    if (effect->impl)
        ID2D1EffectImpl_Release(effect->impl);
}

static HRESULT STDMETHODCALLTYPE d2d_effect_QueryInterface(ID2D1Effect *iface, REFIID iid, void **out)
{
    struct d2d_effect *effect = impl_from_ID2D1Effect(iface);
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_ID2D1Effect)
            || IsEqualGUID(iid, &IID_ID2D1Properties)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        ID2D1Effect_AddRef(iface);
        *out = iface;
        return S_OK;
    }

    if (IsEqualGUID(iid, &IID_ID2D1Image)
            || IsEqualGUID(iid, &IID_ID2D1Resource))
    {
        ID2D1Image_AddRef(&effect->ID2D1Image_iface);
        *out = &effect->ID2D1Image_iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d2d_effect_AddRef(ID2D1Effect *iface)
{
    struct d2d_effect *effect = impl_from_ID2D1Effect(iface);
    ULONG refcount = InterlockedIncrement(&effect->refcount);

    TRACE("%p increasing refcount to %lu.\n", iface, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d2d_effect_Release(ID2D1Effect *iface)
{
    struct d2d_effect *effect = impl_from_ID2D1Effect(iface);
    ULONG refcount = InterlockedDecrement(&effect->refcount);

    TRACE("%p decreasing refcount to %lu.\n", iface, refcount);

    if (!refcount)
    {
        d2d_effect_cleanup(effect);
        free(effect);
    }

    return refcount;
}

static UINT32 STDMETHODCALLTYPE d2d_effect_GetPropertyCount(ID2D1Effect *iface)
{
    struct d2d_effect *effect = impl_from_ID2D1Effect(iface);

    TRACE("iface %p.\n", iface);

    return ID2D1Properties_GetPropertyCount(&effect->properties.ID2D1Properties_iface);
}

static HRESULT STDMETHODCALLTYPE d2d_effect_GetPropertyName(ID2D1Effect *iface, UINT32 index,
        WCHAR *name, UINT32 name_count)
{
    struct d2d_effect *effect = impl_from_ID2D1Effect(iface);

    TRACE("iface %p, index %u, name %p, name_count %u.\n", iface, index, name, name_count);

    return ID2D1Properties_GetPropertyName(&effect->properties.ID2D1Properties_iface,
            index, name, name_count);
}

static UINT32 STDMETHODCALLTYPE d2d_effect_GetPropertyNameLength(ID2D1Effect *iface, UINT32 index)
{
    struct d2d_effect *effect = impl_from_ID2D1Effect(iface);

    TRACE("iface %p, index %u.\n", iface, index);

    return ID2D1Properties_GetPropertyNameLength(&effect->properties.ID2D1Properties_iface, index);
}

static D2D1_PROPERTY_TYPE STDMETHODCALLTYPE d2d_effect_GetType(ID2D1Effect *iface, UINT32 index)
{
    struct d2d_effect *effect = impl_from_ID2D1Effect(iface);

    TRACE("iface %p, index %#x.\n", iface, index);

    return ID2D1Properties_GetType(&effect->properties.ID2D1Properties_iface, index);
}

static UINT32 STDMETHODCALLTYPE d2d_effect_GetPropertyIndex(ID2D1Effect *iface, const WCHAR *name)
{
    struct d2d_effect *effect = impl_from_ID2D1Effect(iface);

    TRACE("iface %p, name %s.\n", iface, debugstr_w(name));

    return ID2D1Properties_GetPropertyIndex(&effect->properties.ID2D1Properties_iface, name);
}

static HRESULT STDMETHODCALLTYPE d2d_effect_SetValueByName(ID2D1Effect *iface, const WCHAR *name,
        D2D1_PROPERTY_TYPE type, const BYTE *value, UINT32 value_size)
{
    struct d2d_effect *effect = impl_from_ID2D1Effect(iface);

    TRACE("iface %p, name %s, type %u, value %p, value_size %u.\n", iface, debugstr_w(name),
            type, value, value_size);

    return ID2D1Properties_SetValueByName(&effect->properties.ID2D1Properties_iface, name,
            type, value, value_size);
}

static HRESULT STDMETHODCALLTYPE d2d_effect_SetValue(ID2D1Effect *iface, UINT32 index, D2D1_PROPERTY_TYPE type,
        const BYTE *value, UINT32 value_size)
{
    struct d2d_effect *effect = impl_from_ID2D1Effect(iface);

    TRACE("iface %p, index %#x, type %u, value %p, value_size %u.\n", iface, index, type, value, value_size);

    return ID2D1Properties_SetValue(&effect->properties.ID2D1Properties_iface, index, type,
            value, value_size);
}

static HRESULT STDMETHODCALLTYPE d2d_effect_GetValueByName(ID2D1Effect *iface, const WCHAR *name,
        D2D1_PROPERTY_TYPE type, BYTE *value, UINT32 value_size)
{
    struct d2d_effect *effect = impl_from_ID2D1Effect(iface);

    TRACE("iface %p, name %s, type %#x, value %p, value_size %u.\n", iface, debugstr_w(name), type,
            value, value_size);

    return ID2D1Properties_GetValueByName(&effect->properties.ID2D1Properties_iface, name, type,
            value, value_size);
}

static HRESULT d2d_effect_get_value(struct d2d_effect *effect, UINT32 index, D2D1_PROPERTY_TYPE type,
        BYTE *value, UINT32 value_size)
{
    return ID2D1Properties_GetValue(&effect->properties.ID2D1Properties_iface, index, type, value, value_size);
}

static HRESULT STDMETHODCALLTYPE d2d_effect_GetValue(ID2D1Effect *iface, UINT32 index, D2D1_PROPERTY_TYPE type,
        BYTE *value, UINT32 value_size)
{
    struct d2d_effect *effect = impl_from_ID2D1Effect(iface);

    TRACE("iface %p, index %#x, type %u, value %p, value_size %u.\n", iface, index, type, value, value_size);

    return d2d_effect_get_value(effect, index, type, value, value_size);
}

static UINT32 STDMETHODCALLTYPE d2d_effect_GetValueSize(ID2D1Effect *iface, UINT32 index)
{
    struct d2d_effect *effect = impl_from_ID2D1Effect(iface);

    TRACE("iface %p, index %#x.\n", iface, index);

    return ID2D1Properties_GetValueSize(&effect->properties.ID2D1Properties_iface, index);
}

static HRESULT STDMETHODCALLTYPE d2d_effect_GetSubProperties(ID2D1Effect *iface, UINT32 index,
        ID2D1Properties **props)
{
    struct d2d_effect *effect = impl_from_ID2D1Effect(iface);

    TRACE("iface %p, index %u, props %p.\n", iface, index, props);

    return ID2D1Properties_GetSubProperties(&effect->properties.ID2D1Properties_iface, index, props);
}

static void STDMETHODCALLTYPE d2d_effect_SetInput(ID2D1Effect *iface, UINT32 index, ID2D1Image *input, BOOL invalidate)
{
    struct d2d_effect *effect = impl_from_ID2D1Effect(iface);

    TRACE("iface %p, index %u, input %p, invalidate %#x.\n", iface, index, input, invalidate);

    if (index >= effect->input_count)
        return;

    if (input)
        ID2D1Image_AddRef(input);
    if (effect->inputs[index])
        ID2D1Image_Release(effect->inputs[index]);
    effect->inputs[index] = input;
}

static HRESULT d2d_effect_set_input_count(struct d2d_effect *effect, UINT32 count)
{
    bool initialized = effect->inputs != NULL;
    HRESULT hr = S_OK;
    unsigned int i;

    if (count == effect->input_count)
        return S_OK;

    if (count < effect->input_count)
    {
        for (i = count; i < effect->input_count; ++i)
        {
            if (effect->inputs[i])
                ID2D1Image_Release(effect->inputs[i]);
        }
    }
    else
    {
        if (!d2d_array_reserve((void **)&effect->inputs, &effect->inputs_size,
                count, sizeof(*effect->inputs)))
        {
            ERR("Failed to resize inputs array.\n");
            return E_OUTOFMEMORY;
        }

        memset(&effect->inputs[effect->input_count], 0, sizeof(*effect->inputs) * (count - effect->input_count));
    }
    effect->input_count = count;

    if (initialized)
    {
        ID2D1TransformGraph_Release(&effect->graph->ID2D1TransformGraph_iface);
        effect->graph = NULL;

        if (SUCCEEDED(hr = d2d_transform_graph_create(count, &effect->graph)))
        {
            if (FAILED(hr = ID2D1EffectImpl_SetGraph(effect->impl, &effect->graph->ID2D1TransformGraph_iface)))
                WARN("Failed to set a new transform graph, hr %#lx.\n", hr);
        }
    }

    return hr;
}

static HRESULT STDMETHODCALLTYPE d2d_effect_SetInputCount(ID2D1Effect *iface, UINT32 count)
{
    struct d2d_effect *effect = impl_from_ID2D1Effect(iface);
    unsigned int min_inputs, max_inputs;

    TRACE("iface %p, count %u.\n", iface, count);

    d2d_effect_get_value(effect, D2D1_PROPERTY_MIN_INPUTS, D2D1_PROPERTY_TYPE_UINT32,
            (BYTE *)&min_inputs, sizeof(min_inputs));
    d2d_effect_get_value(effect, D2D1_PROPERTY_MAX_INPUTS, D2D1_PROPERTY_TYPE_UINT32,
            (BYTE *)&max_inputs, sizeof(max_inputs));

    if (count < min_inputs || count > max_inputs)
        return E_INVALIDARG;

    return d2d_effect_set_input_count(effect, count);
}

static void STDMETHODCALLTYPE d2d_effect_GetInput(ID2D1Effect *iface, UINT32 index, ID2D1Image **input)
{
    struct d2d_effect *effect = impl_from_ID2D1Effect(iface);

    TRACE("iface %p, index %u, input %p.\n", iface, index, input);

    if (index < effect->input_count && effect->inputs[index])
        ID2D1Image_AddRef(*input = effect->inputs[index]);
    else
        *input = NULL;
}

static UINT32 STDMETHODCALLTYPE d2d_effect_GetInputCount(ID2D1Effect *iface)
{
    struct d2d_effect *effect = impl_from_ID2D1Effect(iface);

    TRACE("iface %p.\n", iface);

    return effect->input_count;
}

static void STDMETHODCALLTYPE d2d_effect_GetOutput(ID2D1Effect *iface, ID2D1Image **output)
{
    struct d2d_effect *effect = impl_from_ID2D1Effect(iface);

    TRACE("iface %p, output %p.\n", iface, output);

    ID2D1Image_AddRef(*output = &effect->ID2D1Image_iface);
}

static const ID2D1EffectVtbl d2d_effect_vtbl =
{
    d2d_effect_QueryInterface,
    d2d_effect_AddRef,
    d2d_effect_Release,
    d2d_effect_GetPropertyCount,
    d2d_effect_GetPropertyName,
    d2d_effect_GetPropertyNameLength,
    d2d_effect_GetType,
    d2d_effect_GetPropertyIndex,
    d2d_effect_SetValueByName,
    d2d_effect_SetValue,
    d2d_effect_GetValueByName,
    d2d_effect_GetValue,
    d2d_effect_GetValueSize,
    d2d_effect_GetSubProperties,
    d2d_effect_SetInput,
    d2d_effect_SetInputCount,
    d2d_effect_GetInput,
    d2d_effect_GetInputCount,
    d2d_effect_GetOutput,
};

static inline struct d2d_effect *impl_from_ID2D1Image(ID2D1Image *iface)
{
    return CONTAINING_RECORD(iface, struct d2d_effect, ID2D1Image_iface);
}

static HRESULT STDMETHODCALLTYPE d2d_effect_image_QueryInterface(ID2D1Image *iface, REFIID iid, void **out)
{
    struct d2d_effect *effect = impl_from_ID2D1Image(iface);

    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    return d2d_effect_QueryInterface(&effect->ID2D1Effect_iface, iid, out);
}

static ULONG STDMETHODCALLTYPE d2d_effect_image_AddRef(ID2D1Image *iface)
{
    struct d2d_effect *effect = impl_from_ID2D1Image(iface);

    TRACE("iface %p.\n", iface);

    return d2d_effect_AddRef(&effect->ID2D1Effect_iface);
}

static ULONG STDMETHODCALLTYPE d2d_effect_image_Release(ID2D1Image *iface)
{
    struct d2d_effect *effect = impl_from_ID2D1Image(iface);

    TRACE("iface %p.\n", iface);

    return d2d_effect_Release(&effect->ID2D1Effect_iface);
}

static void STDMETHODCALLTYPE d2d_effect_image_GetFactory(ID2D1Image *iface, ID2D1Factory **factory)
{
    struct d2d_effect *effect = impl_from_ID2D1Image(iface);

    TRACE("iface %p, factory %p.\n", iface, factory);

    ID2D1Factory_AddRef(*factory = effect->effect_context->device_context->factory);
}

static const ID2D1ImageVtbl d2d_effect_image_vtbl =
{
    d2d_effect_image_QueryInterface,
    d2d_effect_image_AddRef,
    d2d_effect_image_Release,
    d2d_effect_image_GetFactory,
};

struct d2d_effect *d2d_effect_from_image(ID2D1Image *image)
{
    if (!image || image->lpVtbl != &d2d_effect_image_vtbl)
        return NULL;
    return impl_from_ID2D1Image(image);
}

static inline struct d2d_effect_properties *impl_from_ID2D1Properties(ID2D1Properties *iface)
{
    return CONTAINING_RECORD(iface, struct d2d_effect_properties, ID2D1Properties_iface);
}

static HRESULT STDMETHODCALLTYPE d2d_effect_properties_QueryInterface(ID2D1Properties *iface,
        REFIID riid, void **obj)
{
    if (IsEqualGUID(riid, &IID_ID2D1Properties) ||
            IsEqualGUID(riid, &IID_IUnknown))
    {
        *obj = iface;
        ID2D1Properties_AddRef(iface);
        return S_OK;
    }

    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d2d_effect_properties_AddRef(ID2D1Properties *iface)
{
    struct d2d_effect_properties *properties = impl_from_ID2D1Properties(iface);

    if (properties->effect)
        return ID2D1Effect_AddRef(&properties->effect->ID2D1Effect_iface);

    return InterlockedIncrement(&properties->refcount);
}

static ULONG STDMETHODCALLTYPE d2d_effect_properties_Release(ID2D1Properties *iface)
{
    struct d2d_effect_properties *properties = impl_from_ID2D1Properties(iface);
    ULONG refcount;

    if (properties->effect)
        return ID2D1Effect_Release(&properties->effect->ID2D1Effect_iface);

    refcount = InterlockedDecrement(&properties->refcount);

    if (!refcount)
    {
        d2d_effect_properties_cleanup(properties);
        free(properties);
    }

    return refcount;
}

static UINT32 STDMETHODCALLTYPE d2d_effect_properties_GetPropertyCount(ID2D1Properties *iface)
{
    struct d2d_effect_properties *properties = impl_from_ID2D1Properties(iface);

    TRACE("iface %p.\n", iface);

    return properties->custom_count;
}

static HRESULT STDMETHODCALLTYPE d2d_effect_properties_GetPropertyName(ID2D1Properties *iface,
        UINT32 index, WCHAR *name, UINT32 name_count)
{
    struct d2d_effect_properties *properties = impl_from_ID2D1Properties(iface);
    struct d2d_effect_property *prop;

    TRACE("iface %p, index %u, name %p, name_count %u.\n", iface, index, name, name_count);

    if (!(prop = d2d_effect_properties_get_property_by_index(properties, index)))
        return D2DERR_INVALID_PROPERTY;

    return d2d_effect_return_string(prop->name, name, name_count);
}

static UINT32 STDMETHODCALLTYPE d2d_effect_properties_GetPropertyNameLength(ID2D1Properties *iface,
        UINT32 index)
{
    struct d2d_effect_properties *properties = impl_from_ID2D1Properties(iface);
    struct d2d_effect_property *prop;

    TRACE("iface %p, index %u.\n", iface, index);

    if (!(prop = d2d_effect_properties_get_property_by_index(properties, index)))
        return D2DERR_INVALID_PROPERTY;

    return wcslen(prop->name);
}

static D2D1_PROPERTY_TYPE STDMETHODCALLTYPE d2d_effect_properties_GetType(ID2D1Properties *iface,
        UINT32 index)
{
    struct d2d_effect_properties *properties = impl_from_ID2D1Properties(iface);
    struct d2d_effect_property *prop;

    TRACE("iface %p, index %#x.\n", iface, index);

    if (!(prop = d2d_effect_properties_get_property_by_index(properties, index)))
        return D2D1_PROPERTY_TYPE_UNKNOWN;

    return prop->type;
}

static UINT32 STDMETHODCALLTYPE d2d_effect_properties_GetPropertyIndex(ID2D1Properties *iface,
        const WCHAR *name)
{
    struct d2d_effect_properties *properties = impl_from_ID2D1Properties(iface);
    struct d2d_effect_property *prop;

    TRACE("iface %p, name %s.\n", iface, debugstr_w(name));

    if (!(prop = d2d_effect_properties_get_property_by_name(properties, name)))
        return D2D1_INVALID_PROPERTY_INDEX;

    return prop->index;
}

static HRESULT STDMETHODCALLTYPE d2d_effect_properties_SetValueByName(ID2D1Properties *iface,
        const WCHAR *name, D2D1_PROPERTY_TYPE type, const BYTE *value, UINT32 value_size)
{
    struct d2d_effect_properties *properties = impl_from_ID2D1Properties(iface);
    struct d2d_effect_property *prop;

    TRACE("iface %p, name %s, type %u, value %p, value_size %u.\n", iface, debugstr_w(name),
            type, value, value_size);

    if (!(prop = d2d_effect_properties_get_property_by_name(properties, name)))
        return D2DERR_INVALID_PROPERTY;

    return d2d_effect_property_set_value(properties, prop, type, value, value_size);
}

static HRESULT STDMETHODCALLTYPE d2d_effect_properties_SetValue(ID2D1Properties *iface,
        UINT32 index, D2D1_PROPERTY_TYPE type, const BYTE *value, UINT32 value_size)
{
    struct d2d_effect_properties *properties = impl_from_ID2D1Properties(iface);
    struct d2d_effect_property *prop;

    TRACE("iface %p, index %#x, type %u, value %p, value_size %u.\n", iface, index, type, value, value_size);

    if (!(prop = d2d_effect_properties_get_property_by_index(properties, index)))
        return D2DERR_INVALID_PROPERTY;

    return d2d_effect_property_set_value(properties, prop, type, value, value_size);
}

static HRESULT STDMETHODCALLTYPE d2d_effect_properties_GetValueByName(ID2D1Properties *iface,
        const WCHAR *name, D2D1_PROPERTY_TYPE type, BYTE *value, UINT32 value_size)
{
    struct d2d_effect_properties *properties = impl_from_ID2D1Properties(iface);
    struct d2d_effect_property *prop;

    TRACE("iface %p, name %s, type %#x, value %p, value_size %u.\n", iface, debugstr_w(name), type,
            value, value_size);

    if (!(prop = d2d_effect_properties_get_property_by_name(properties, name)))
        return D2DERR_INVALID_PROPERTY;

    return d2d_effect_property_get_value(properties, prop, type, value, value_size);
}

static HRESULT STDMETHODCALLTYPE d2d_effect_properties_GetValue(ID2D1Properties *iface,
        UINT32 index, D2D1_PROPERTY_TYPE type, BYTE *value, UINT32 value_size)
{
    struct d2d_effect_properties *properties = impl_from_ID2D1Properties(iface);
    struct d2d_effect_property *prop;

    TRACE("iface %p, index %#x, type %u, value %p, value_size %u.\n", iface, index, type, value, value_size);

    if (!(prop = d2d_effect_properties_get_property_by_index(properties, index)))
        return D2DERR_INVALID_PROPERTY;

    return d2d_effect_property_get_value(properties, prop, type, value, value_size);
}

static UINT32 STDMETHODCALLTYPE d2d_effect_properties_GetValueSize(ID2D1Properties *iface,
        UINT32 index)
{
    struct d2d_effect_properties *properties = impl_from_ID2D1Properties(iface);

    TRACE("iface %p, index %#x.\n", iface, index);

    return d2d_effect_properties_get_value_size(properties, index);
}

static HRESULT STDMETHODCALLTYPE d2d_effect_properties_GetSubProperties(ID2D1Properties *iface,
        UINT32 index, ID2D1Properties **props)
{
    struct d2d_effect_properties *properties = impl_from_ID2D1Properties(iface);
    struct d2d_effect_property *prop;

    TRACE("iface %p, index %u, props %p.\n", iface, index, props);

    if (!(prop = d2d_effect_properties_get_property_by_index(properties, index)))
        return D2DERR_INVALID_PROPERTY;

    if (!prop->subproperties) return D2DERR_NO_SUBPROPERTIES;

    *props = &prop->subproperties->ID2D1Properties_iface;
    ID2D1Properties_AddRef(*props);
    return S_OK;
}

static const ID2D1PropertiesVtbl d2d_effect_properties_vtbl =
{
    d2d_effect_properties_QueryInterface,
    d2d_effect_properties_AddRef,
    d2d_effect_properties_Release,
    d2d_effect_properties_GetPropertyCount,
    d2d_effect_properties_GetPropertyName,
    d2d_effect_properties_GetPropertyNameLength,
    d2d_effect_properties_GetType,
    d2d_effect_properties_GetPropertyIndex,
    d2d_effect_properties_SetValueByName,
    d2d_effect_properties_SetValue,
    d2d_effect_properties_GetValueByName,
    d2d_effect_properties_GetValue,
    d2d_effect_properties_GetValueSize,
    d2d_effect_properties_GetSubProperties,
};

void d2d_effect_init_properties(struct d2d_effect *effect,
        struct d2d_effect_properties *properties)
{
    properties->ID2D1Properties_iface.lpVtbl = &d2d_effect_properties_vtbl;
    properties->effect = effect;
    properties->refcount = 1;
}

static struct d2d_render_info *impl_from_ID2D1DrawInfo(ID2D1DrawInfo *iface)
{
    return CONTAINING_RECORD(iface, struct d2d_render_info, ID2D1DrawInfo_iface);
}

static HRESULT STDMETHODCALLTYPE d2d_draw_info_QueryInterface(ID2D1DrawInfo *iface, REFIID iid,
        void **obj)
{
    TRACE("iface %p, iid %s, obj %p.\n", iface, debugstr_guid(iid), obj);

    if (IsEqualGUID(iid, &IID_ID2D1DrawInfo)
            || IsEqualGUID(iid, &IID_ID2D1RenderInfo)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        *obj = iface;
        ID2D1DrawInfo_AddRef(iface);
        return S_OK;
    }

    WARN("Unsupported interface %s.\n", debugstr_guid(iid));

    *obj = NULL;

    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d2d_draw_info_AddRef(ID2D1DrawInfo *iface)
{
    struct d2d_render_info *render_info = impl_from_ID2D1DrawInfo(iface);
    ULONG refcount = InterlockedIncrement(&render_info->refcount);

    TRACE("iface %p refcount %lu.\n", iface, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d2d_draw_info_Release(ID2D1DrawInfo *iface)
{
    struct d2d_render_info *render_info = impl_from_ID2D1DrawInfo(iface);
    ULONG refcount = InterlockedDecrement(&render_info->refcount);

    TRACE("iface %p refcount %lu.\n", iface, refcount);

    if (!refcount)
        free(render_info);

    return refcount;
}

static HRESULT STDMETHODCALLTYPE d2d_draw_info_SetInputDescription(ID2D1DrawInfo *iface,
        UINT32 index, D2D1_INPUT_DESCRIPTION description)
{
    FIXME("iface %p, index %u stub.\n", iface, index);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_draw_info_SetOutputBuffer(ID2D1DrawInfo *iface,
        D2D1_BUFFER_PRECISION precision, D2D1_CHANNEL_DEPTH depth)
{
    FIXME("iface %p, precision %u, depth %u stub.\n", iface, precision, depth);

    return E_NOTIMPL;
}

static void STDMETHODCALLTYPE d2d_draw_info_SetCached(ID2D1DrawInfo *iface, BOOL is_cached)
{
    FIXME("iface %p, is_cached %d stub.\n", iface, is_cached);
}

static void STDMETHODCALLTYPE d2d_draw_info_SetInstructionCountHint(ID2D1DrawInfo *iface,
        UINT32 count)
{
    FIXME("iface %p, count %u stub.\n", iface, count);
}

static HRESULT STDMETHODCALLTYPE d2d_draw_info_SetPixelShaderConstantBuffer(ID2D1DrawInfo *iface,
        const BYTE *buffer, UINT32 size)
{
    FIXME("iface %p, buffer %p, size %u stub.\n", iface, buffer, size);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_draw_info_SetResourceTexture(ID2D1DrawInfo *iface,
        UINT32 index, ID2D1ResourceTexture *texture)
{
    FIXME("iface %p, index %u, texture %p stub.\n", iface, index, texture);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_draw_info_SetVertexShaderConstantBuffer(ID2D1DrawInfo *iface,
        const BYTE *buffer, UINT32 size)
{
    FIXME("iface %p, buffer %p, size %u stub.\n", iface, buffer, size);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_draw_info_SetPixelShader(ID2D1DrawInfo *iface,
        REFGUID id, D2D1_PIXEL_OPTIONS options)
{
    struct d2d_render_info *render_info = impl_from_ID2D1DrawInfo(iface);

    TRACE("iface %p, id %s, options %u.\n", iface, debugstr_guid(id), options);

    render_info->mask |= D2D_RENDER_INFO_PIXEL_SHADER;
    render_info->pixel_shader = *id;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_draw_info_SetVertexProcessing(ID2D1DrawInfo *iface,
        ID2D1VertexBuffer *buffer, D2D1_VERTEX_OPTIONS options,
        const D2D1_BLEND_DESCRIPTION *description, const D2D1_VERTEX_RANGE *range,
        const GUID *shader)
{
    FIXME("iface %p, buffer %p, options %#x, description %p, range %p, shader %s stub.\n",
            iface, buffer, options, description, range, debugstr_guid(shader));

    return E_NOTIMPL;
}

static const ID2D1DrawInfoVtbl d2d_draw_info_vtbl =
{
    d2d_draw_info_QueryInterface,
    d2d_draw_info_AddRef,
    d2d_draw_info_Release,
    d2d_draw_info_SetInputDescription,
    d2d_draw_info_SetOutputBuffer,
    d2d_draw_info_SetCached,
    d2d_draw_info_SetInstructionCountHint,
    d2d_draw_info_SetPixelShaderConstantBuffer,
    d2d_draw_info_SetResourceTexture,
    d2d_draw_info_SetVertexShaderConstantBuffer,
    d2d_draw_info_SetPixelShader,
    d2d_draw_info_SetVertexProcessing,
};

static HRESULT d2d_effect_render_info_create(struct d2d_render_info **obj)
{
    struct d2d_render_info *object;

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    object->ID2D1DrawInfo_iface.lpVtbl = &d2d_draw_info_vtbl;
    object->refcount = 1;

    *obj = object;

    return S_OK;
}

static bool d2d_transform_node_needs_render_info(const struct d2d_transform_node *node)
{
    static const GUID *iids[] =
    {
        &IID_ID2D1SourceTransform,
        &IID_ID2D1ComputeTransform,
        &IID_ID2D1DrawTransform,
    };
    unsigned int i;
    IUnknown *obj;

    for (i = 0; i < ARRAY_SIZE(iids); ++i)
    {
        if (SUCCEEDED(ID2D1TransformNode_QueryInterface(node->object, iids[i], (void **)&obj)))
        {
            IUnknown_Release(obj);
            return true;
        }
    }

    return false;
}

static HRESULT d2d_effect_transform_graph_initialize_nodes(struct d2d_transform_graph *graph)
{
    ID2D1DrawTransform *draw_transform;
    struct d2d_transform_node *node;
    HRESULT hr;

    LIST_FOR_EACH_ENTRY(node, &graph->nodes, struct d2d_transform_node, entry)
    {
        if (d2d_transform_node_needs_render_info(node))
        {
            if (FAILED(hr = d2d_effect_render_info_create(&node->render_info)))
                return hr;
        }

        if (SUCCEEDED(ID2D1TransformNode_QueryInterface(node->object, &IID_ID2D1DrawTransform,
                (void **)&draw_transform)))
        {
            hr = ID2D1DrawTransform_SetDrawInfo(draw_transform, &node->render_info->ID2D1DrawInfo_iface);
            ID2D1DrawTransform_Release(draw_transform);
            if (FAILED(hr))
            {
                WARN("Failed to set draw info, hr %#lx.\n", hr);
                return hr;
            }
        }
        else
        {
            FIXME("Unsupported node %p.\n", node);
            return E_NOTIMPL;
        }
    }

    return S_OK;
}

HRESULT d2d_effect_create(struct d2d_device_context *context, const CLSID *effect_id,
        ID2D1Effect **effect)
{
    struct d2d_effect_context *effect_context;
    const struct d2d_effect_registration *reg;
    struct d2d_effect *object;
    UINT32 input_count;
    WCHAR clsidW[39];
    HRESULT hr;

    if (!(reg = d2d_factory_get_registered_effect(context->factory, effect_id)))
    {
        WARN("Effect id %s not found.\n", wine_dbgstr_guid(effect_id));
        return D2DERR_EFFECT_IS_NOT_REGISTERED;
    }

    if (!(effect_context = calloc(1, sizeof(*effect_context))))
        return E_OUTOFMEMORY;
    d2d_effect_context_init(effect_context, context);

    if (!(object = calloc(1, sizeof(*object))))
    {
        ID2D1EffectContext_Release(&effect_context->ID2D1EffectContext_iface);
        return E_OUTOFMEMORY;
    }

    object->ID2D1Effect_iface.lpVtbl = &d2d_effect_vtbl;
    object->ID2D1Image_iface.lpVtbl = &d2d_effect_image_vtbl;
    object->refcount = 1;
    object->effect_context = effect_context;

    /* Create properties */
    d2d_effect_duplicate_properties(object, &object->properties, reg->properties);

    StringFromGUID2(effect_id, clsidW, ARRAY_SIZE(clsidW));
    d2d_effect_properties_add(&object->properties, L"CLSID", D2D1_PROPERTY_CLSID, D2D1_PROPERTY_TYPE_CLSID, clsidW);
    d2d_effect_properties_add(&object->properties, L"Cached", D2D1_PROPERTY_CACHED, D2D1_PROPERTY_TYPE_BOOL, L"false");
    d2d_effect_properties_add(&object->properties, L"Precision", D2D1_PROPERTY_PRECISION, D2D1_PROPERTY_TYPE_ENUM, L"0");

    /* Sync instance input count with default input count from the description. */
    d2d_effect_get_value(object, D2D1_PROPERTY_INPUTS, D2D1_PROPERTY_TYPE_ARRAY, (BYTE *)&input_count, sizeof(input_count));
    d2d_effect_set_input_count(object, input_count);

    if (FAILED(hr = d2d_transform_graph_create(input_count, &object->graph)))
    {
        ID2D1EffectContext_Release(&effect_context->ID2D1EffectContext_iface);
        return hr;
    }

    if (FAILED(hr = reg->factory((IUnknown **)&object->impl)))
    {
        WARN("Failed to create implementation object, hr %#lx.\n", hr);
        ID2D1Effect_Release(&object->ID2D1Effect_iface);
        return hr;
    }

    if (FAILED(hr = ID2D1EffectImpl_Initialize(object->impl, &effect_context->ID2D1EffectContext_iface,
            &object->graph->ID2D1TransformGraph_iface)))
    {
        WARN("Failed to initialize effect, hr %#lx.\n", hr);
        ID2D1Effect_Release(&object->ID2D1Effect_iface);
        return hr;
    }

    if (FAILED(hr = d2d_effect_transform_graph_initialize_nodes(object->graph)))
    {
        WARN("Failed to initialize graph nodes, hr %#lx.\n", hr);
        ID2D1Effect_Release(&object->ID2D1Effect_iface);
        return hr;
    }

    *effect = &object->ID2D1Effect_iface;

    TRACE("Created effect %p.\n", *effect);

    return S_OK;
}
