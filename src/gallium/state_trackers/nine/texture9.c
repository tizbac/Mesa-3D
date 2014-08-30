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

#include "device9.h"
#include "surface9.h"
#include "texture9.h"
#include "nine_helpers.h"
#include "nine_pipe.h"
#include "nine_dump.h"

#include "pipe/p_state.h"
#include "pipe/p_context.h"
#include "pipe/p_screen.h"
#include "util/u_inlines.h"
#include "util/u_resource.h"

#define DBG_CHANNEL DBG_TEXTURE

static HRESULT
NineTexture9_ctor( struct NineTexture9 *This,
                   struct NineUnknownParams *pParams,
                   UINT Width, UINT Height, UINT Levels,
                   DWORD Usage,
                   D3DFORMAT Format,
                   D3DPOOL Pool,
                   HANDLE *pSharedHandle )
{
    struct pipe_screen *screen = pParams->device->screen;
    struct pipe_resource *info = &This->base.base.info;
    unsigned l;
    D3DSURFACE_DESC sfdesc;
    HRESULT hr;
    const boolean shared_create = pSharedHandle && !*pSharedHandle;

    DBG("(%p) Width=%u Height=%u Levels=%u Usage=%s Format=%s Pool=%s "
        "pSharedHandle=%p\n", This, Width, Height, Levels,
        nine_D3DUSAGE_to_str(Usage),
        d3dformat_to_string(Format), nine_D3DPOOL_to_str(Pool), pSharedHandle);

    user_assert(!(Usage & D3DUSAGE_AUTOGENMIPMAP) ||
                (Pool != D3DPOOL_SYSTEMMEM && Levels <= 1), D3DERR_INVALIDCALL);

    /* Won't work, handle is a void * and winsys_handle will be unsigned[3].
     */
    user_assert(!pSharedHandle ||
                Pool == D3DPOOL_SYSTEMMEM, D3DERR_DRIVERINTERNALERROR);

    if (Usage & D3DUSAGE_AUTOGENMIPMAP)
        Levels = 0;

    This->base.format = Format;
    This->base.base.usage = Usage;

    info->screen = screen;
    info->target = PIPE_TEXTURE_2D;
    info->format = d3d9_to_pipe_format(Format);
    info->width0 = Width;
    info->height0 = Height;
    info->depth0 = 1;
    if (Levels)
        info->last_level = Levels - 1;
    else
        info->last_level = util_logbase2(MAX2(Width, Height));
    info->array_size = 1;
    info->nr_samples = 0;
    info->bind = PIPE_BIND_SAMPLER_VIEW;
    info->usage = PIPE_USAGE_DEFAULT;
    info->flags = 0;

    if (Usage & D3DUSAGE_RENDERTARGET)
        info->bind |= PIPE_BIND_RENDER_TARGET;
    if (Usage & D3DUSAGE_DEPTHSTENCIL)
        info->bind |= PIPE_BIND_DEPTH_STENCIL;

    if (Usage & D3DUSAGE_DYNAMIC) {
        info->usage = PIPE_USAGE_DYNAMIC;
        info->bind |=
            PIPE_BIND_TRANSFER_READ |
            PIPE_BIND_TRANSFER_WRITE;
    }
    if (pSharedHandle)
        info->bind |= PIPE_BIND_SHARED;

    if (Pool == D3DPOOL_SYSTEMMEM)
        info->usage = PIPE_USAGE_STAGING;

    if (pSharedHandle && !shared_create) {
        if (Pool == D3DPOOL_SYSTEMMEM) {
            /* Hack for surface creation. */
            This->base.base.resource = (struct pipe_resource *)*pSharedHandle;
        } else {
            struct pipe_resource *res;
            res = screen->resource_from_handle(screen, info,
                                      (struct winsys_handle *)pSharedHandle);
            if (!res)
                return D3DERR_NOTFOUND;
            This->base.base.resource = res;
        }
    }

    This->surfaces = CALLOC(info->last_level + 1, sizeof(*This->surfaces));
    if (!This->surfaces)
        return E_OUTOFMEMORY;

    hr = NineBaseTexture9_ctor(&This->base, pParams, D3DRTYPE_TEXTURE, Pool);
    if (FAILED(hr))
        return hr;
    This->base.pstype = (Height == 1) ? 1 : 0;

    /* Create all the surfaces right away.
     * They manage backing storage, and transfers (LockRect) are deferred
     * to them.
     */
    sfdesc.Format = Format;
    sfdesc.Type = D3DRTYPE_SURFACE;
    sfdesc.Usage = Usage;
    sfdesc.Pool = Pool;
    sfdesc.MultiSampleType = D3DMULTISAMPLE_NONE;
    sfdesc.MultiSampleQuality = 0;
    for (l = 0; l <= info->last_level; ++l) {
        sfdesc.Width = u_minify(Width, l);
        sfdesc.Height = u_minify(Height, l);

        hr = NineSurface9_new(This->base.base.base.device, NineUnknown(This),
                              This->base.base.resource, D3DRTYPE_TEXTURE, l, 0,
                              &sfdesc, &This->surfaces[l]);
        if (FAILED(hr))
            return hr;
    }

    This->dirty_rect.depth = 1; /* widht == 0 means empty, depth stays 1 */

    if (pSharedHandle) {
        if (Pool == D3DPOOL_SYSTEMMEM) {
            This->base.base.resource = NULL;
            if (shared_create)
                *pSharedHandle = This->surfaces[0]->base.data;
        } else
        if (shared_create) {
            boolean ok;
            ok = screen->resource_get_handle(screen, This->base.base.resource,
                                         (struct winsys_handle *)pSharedHandle);
            if (!ok)
                return D3DERR_DRIVERINTERNALERROR;
        }
    }

    return D3D_OK;
}

static void
NineTexture9_dtor( struct NineTexture9 *This )
{
    unsigned l;

    if (This->surfaces) {
        /* The surfaces should have 0 references and be unbound now. */
        for (l = 0; l <= This->base.base.info.last_level; ++l)
            NineUnknown_Destroy(&This->surfaces[l]->base.base);
        FREE(This->surfaces);
    }

    NineBaseTexture9_dtor(&This->base);
}

HRESULT WINAPI
NineTexture9_GetLevelDesc( struct NineTexture9 *This,
                           UINT Level,
                           D3DSURFACE_DESC *pDesc )
{
    user_assert(Level <= This->base.base.info.last_level, D3DERR_INVALIDCALL);
    user_assert(Level == 0 || !(This->base.base.usage & D3DUSAGE_AUTOGENMIPMAP),
                D3DERR_INVALIDCALL);

    *pDesc = This->surfaces[Level]->desc;

    return D3D_OK;
}

HRESULT WINAPI
NineTexture9_GetSurfaceLevel( struct NineTexture9 *This,
                              UINT Level,
                              IDirect3DSurface9 **ppSurfaceLevel )
{
    user_assert(Level <= This->base.base.info.last_level, D3DERR_INVALIDCALL);
    user_assert(Level == 0 || !(This->base.base.usage & D3DUSAGE_AUTOGENMIPMAP),
                D3DERR_INVALIDCALL);

    NineUnknown_AddRef(NineUnknown(This->surfaces[Level]));
    *ppSurfaceLevel = (IDirect3DSurface9 *)This->surfaces[Level];

    return D3D_OK;
}

HRESULT WINAPI
NineTexture9_LockRect( struct NineTexture9 *This,
                       UINT Level,
                       D3DLOCKED_RECT *pLockedRect,
                       const RECT *pRect,
                       DWORD Flags )
{
    user_assert(Level <= This->base.base.info.last_level, D3DERR_INVALIDCALL);
    user_assert(Level == 0 || !(This->base.base.usage & D3DUSAGE_AUTOGENMIPMAP),
                D3DERR_INVALIDCALL);

    return NineSurface9_LockRect(This->surfaces[Level], pLockedRect,
                                 pRect, Flags);
}

HRESULT WINAPI
NineTexture9_UnlockRect( struct NineTexture9 *This,
                         UINT Level )
{
    user_assert(Level <= This->base.base.info.last_level, D3DERR_INVALIDCALL);

    return NineSurface9_UnlockRect(This->surfaces[Level]);
}

HRESULT WINAPI
NineTexture9_AddDirtyRect( struct NineTexture9 *This,
                           const RECT *pDirtyRect )
{
    DBG("This=%p pDirtyRect=%p[(%u,%u)-(%u,%u)]\n", This, pDirtyRect,
        pDirtyRect ? pDirtyRect->left : 0, pDirtyRect ? pDirtyRect->top : 0,
        pDirtyRect ? pDirtyRect->right : 0, pDirtyRect ? pDirtyRect->bottom : 0);

    /* Tracking dirty regions on DEFAULT or SYSTEMMEM resources is pointless,
     * because we always write to the final storage. Just marked it dirty in
     * case we need to generate mip maps.
     */
    if (This->base.base.pool != D3DPOOL_MANAGED) {
        if (This->base.base.usage & D3DUSAGE_AUTOGENMIPMAP)
            This->base.dirty_mip = TRUE;
        return D3D_OK;
    }
    This->base.dirty = TRUE;

    BASETEX_REGISTER_UPDATE(&This->base);

    if (!pDirtyRect) {
        u_box_origin_2d(This->base.base.info.width0,
                        This->base.base.info.height0, &This->dirty_rect);
    } else {
        struct pipe_box box;
        rect_to_pipe_box_clamp(&box, pDirtyRect);
        u_box_cover_2d(&This->dirty_rect, &This->dirty_rect, &box);
    }
    return D3D_OK;
}

IDirect3DTexture9Vtbl NineTexture9_vtable = {
    (void *)NineUnknown_QueryInterface,
    (void *)NineUnknown_AddRef,
    (void *)NineUnknown_Release,
    (void *)NineUnknown_GetDevice, /* actually part of Resource9 iface */
    (void *)NineResource9_SetPrivateData,
    (void *)NineResource9_GetPrivateData,
    (void *)NineResource9_FreePrivateData,
    (void *)NineResource9_SetPriority,
    (void *)NineResource9_GetPriority,
    (void *)NineBaseTexture9_PreLoad,
    (void *)NineResource9_GetType,
    (void *)NineBaseTexture9_SetLOD,
    (void *)NineBaseTexture9_GetLOD,
    (void *)NineBaseTexture9_GetLevelCount,
    (void *)NineBaseTexture9_SetAutoGenFilterType,
    (void *)NineBaseTexture9_GetAutoGenFilterType,
    (void *)NineBaseTexture9_GenerateMipSubLevels,
    (void *)NineTexture9_GetLevelDesc,
    (void *)NineTexture9_GetSurfaceLevel,
    (void *)NineTexture9_LockRect,
    (void *)NineTexture9_UnlockRect,
    (void *)NineTexture9_AddDirtyRect
};

static const GUID *NineTexture9_IIDs[] = {
    &IID_IDirect3DTexture9,
    &IID_IDirect3DBaseTexture9,
    &IID_IDirect3DResource9,
    &IID_IUnknown,
    NULL
};

HRESULT
NineTexture9_new( struct NineDevice9 *pDevice,
                  UINT Width, UINT Height, UINT Levels,
                  DWORD Usage,
                  D3DFORMAT Format,
                  D3DPOOL Pool,
                  struct NineTexture9 **ppOut,
                  HANDLE *pSharedHandle )
{
    NINE_DEVICE_CHILD_NEW(Texture9, ppOut, pDevice,
                          Width, Height, Levels,
                          Usage, Format, Pool, pSharedHandle);
}
