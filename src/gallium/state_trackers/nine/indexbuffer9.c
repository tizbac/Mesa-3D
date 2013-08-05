/*
 * Copyright 2011 Joakim Sindholt <opensource@zhasha.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE. */

#include "indexbuffer9.h"
#include "device9.h"
#include "nine_helpers.h"
#include "nine_pipe.h"

#include "pipe/p_screen.h"
#include "pipe/p_context.h"
#include "pipe/p_state.h"
#include "pipe/p_defines.h"
#include "pipe/p_format.h"
#include "util/u_box.h"

#define DBG_CHANNEL DBG_INDEXBUFFER

HRESULT
NineIndexBuffer9_ctor( struct NineIndexBuffer9 *This,
                       struct NineUnknownParams *pParams,
                       D3DINDEXBUFFER_DESC *pDesc )
{
    struct pipe_resource *info = &This->base.info;
    HRESULT hr;

    This->pipe = pParams->device->pipe;

    info->screen = pParams->device->screen;
    info->target = PIPE_BUFFER;
    info->format = PIPE_FORMAT_R8_UNORM;
    info->width0 = pDesc->Size;
    info->flags = 0;

    info->bind = PIPE_BIND_INDEX_BUFFER | PIPE_BIND_TRANSFER_WRITE;
    if (!(pDesc->Usage & D3DUSAGE_WRITEONLY))
        info->bind |= PIPE_BIND_TRANSFER_READ;

    info->usage = PIPE_USAGE_STATIC;
    if (pDesc->Usage & D3DUSAGE_DYNAMIC)
        info->usage = PIPE_USAGE_STREAM;
    if (pDesc->Pool == D3DPOOL_SYSTEMMEM)
        info->usage = PIPE_USAGE_STAGING;

    /* if (This->desc.Usage & D3DUSAGE_DONOTCLIP) { } */
    /* if (This->desc.Usage & D3DUSAGE_NONSECURE) { } */
    /* if (This->desc.Usage & D3DUSAGE_NPATCHES) { } */
    /* if (This->desc.Usage & D3DUSAGE_POINTS) { } */
    /* if (This->desc.Usage & D3DUSAGE_RTPATCHES) { } */
    /* if (This->desc.Usage & D3DUSAGE_SOFTWAREPROCESSING) { } */

    info->height0 = 1;
    info->depth0 = 1;
    info->array_size = 1;
    info->last_level = 0;
    info->nr_samples = 0;

    hr = NineResource9_ctor(&This->base, pParams, TRUE, D3DRTYPE_INDEXBUFFER,
                            pDesc->Pool);
    if (FAILED(hr))
        return hr;

    This->buffer.buffer = This->base.resource;
    This->buffer.offset = 0;
    switch (pDesc->Format) {
    case D3DFMT_INDEX16: This->buffer.index_size = 2; break;
    case D3DFMT_INDEX32: This->buffer.index_size = 4; break;
    default:
        user_assert(!"Invalid index format.", D3DERR_INVALIDCALL);
        break;
    }
    This->buffer.user_buffer = NULL;

    pDesc->Type = D3DRTYPE_INDEXBUFFER;
    This->desc = *pDesc;

    return D3D_OK;
}

void
NineIndexBuffer9_dtor( struct NineIndexBuffer9 *This )
{
    if (This->transfer) { NineIndexBuffer9_Unlock(This); }

    NineResource9_dtor(&This->base);
}

const struct pipe_index_buffer *
NineIndexBuffer9_GetBuffer( struct NineIndexBuffer9 *This )
{
    return &This->buffer;
}

HRESULT WINAPI
NineIndexBuffer9_Lock( struct NineIndexBuffer9 *This,
                       UINT OffsetToLock,
                       UINT SizeToLock,
                       void **ppbData,
                       DWORD Flags )
{
    struct pipe_box box;
    void *data;
    const unsigned usage = d3dlock_buffer_to_pipe_transfer_usage(Flags);

    user_assert(!This->transfer, D3DERR_INVALIDCALL);
    user_assert(ppbData, E_POINTER);

    if (SizeToLock == 0) {
        SizeToLock = This->desc.Size - OffsetToLock;
        user_warn(OffsetToLock != 0);
    }

    u_box_1d(OffsetToLock, SizeToLock, &box);

    data = This->pipe->transfer_map(This->pipe, This->base.resource, 0,
                                    usage, &box, &This->transfer);
    if (!This->transfer) {
        DBG("pipe::transfer_map failed\n"
            " usage = %u\n"
            " box.x = %u\n"
            " box.width = %u\n",
            usage, box.x, box.width);
        /* not sure what to return, msdn suggests this */
        if (Flags & D3DLOCK_DONOTWAIT)
            return D3DERR_WASSTILLDRAWING;
        return D3DERR_INVALIDCALL;
    }
    *ppbData = data;

    return D3D_OK;
}

HRESULT WINAPI
NineIndexBuffer9_Unlock( struct NineIndexBuffer9 *This )
{
    user_assert(This->transfer, D3DERR_INVALIDCALL);
    This->pipe->transfer_unmap(This->pipe, This->transfer);
    This->transfer = NULL;
    return D3D_OK;
}

HRESULT WINAPI
NineIndexBuffer9_GetDesc( struct NineIndexBuffer9 *This,
                          D3DINDEXBUFFER_DESC *pDesc )
{
    user_assert(pDesc, E_POINTER);
    *pDesc = This->desc;
    return D3D_OK;
}

IDirect3DIndexBuffer9Vtbl NineIndexBuffer9_vtable = {
    (void *)NineUnknown_QueryInterface,
    (void *)NineUnknown_AddRef,
    (void *)NineUnknown_Release,
    (void *)NineUnknown_GetDevice, /* actually part of Resource9 iface */
    (void *)NineResource9_SetPrivateData,
    (void *)NineResource9_GetPrivateData,
    (void *)NineResource9_FreePrivateData,
    (void *)NineResource9_SetPriority,
    (void *)NineResource9_GetPriority,
    (void *)NineResource9_PreLoad,
    (void *)NineResource9_GetType,
    (void *)NineIndexBuffer9_Lock,
    (void *)NineIndexBuffer9_Unlock,
    (void *)NineIndexBuffer9_GetDesc
};

static const GUID *NineIndexBuffer9_IIDs[] = {
    &IID_IDirect3DIndexBuffer9,
    &IID_IDirect3DResource9,
    &IID_IUnknown,
    NULL
};

HRESULT
NineIndexBuffer9_new( struct NineDevice9 *pDevice,
                      D3DINDEXBUFFER_DESC *pDesc,
                      struct NineIndexBuffer9 **ppOut )
{
    NINE_DEVICE_CHILD_NEW(IndexBuffer9, ppOut, /* args */ pDevice, pDesc);
}
