
#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/u_math.h"

#include "nouveau_screen.h"
#include "nouveau_context.h"
#include "nouveau_winsys.h"
#include "nouveau_fence.h"
#include "nouveau_buffer.h"
#include "nouveau_mm.h"

struct nouveau_transfer {
   struct pipe_transfer base;

   /* storage for staging transfer: pushbuf if (map && !bo) */
   uint8_t *map;
   struct nouveau_bo *bo;
   uint32_t bo_offset;
   struct nouveau_mm_allocation *mm;
};

#if 0
static INLINE void
nouveau_transfer_info(struct nouveau_transfer *xfer, unsigned count)
{
   struct nv04_resource *buf = nv04_resource(xfer->base.resource);

   debug_printf("TRANSFER[%u] ", count);

   switch (buf->domain) {
   case NOUVEAU_BO_VRAM: debug_printf("VRAM "); break;
   case NOUVEAU_BO_GART: debug_printf("GART "); break;
   default:
      break;
   }

   if (xfer->base.usage & PIPE_TRANSFER_READ)
      debug_printf("rd ");
   if (xfer->base.usage & PIPE_TRANSFER_WRITE)
      debug_printf("wr ");
   if (xfer->base.usage & PIPE_TRANSFER_MAP_DIRECTLY)
      debug_printf("directly ");
   if (xfer->base.usage & PIPE_TRANSFER_DISCARD_RANGE)
      debug_printf("discard ");
   if (xfer->base.usage & PIPE_TRANSFER_DISCARD_WHOLE_RESOURCE)
      debug_printf("drop ");
   if (xfer->base.usage & PIPE_TRANSFER_DONTBLOCK)
      debug_printf("noblock ");
   if (xfer->base.usage & PIPE_TRANSFER_UNSYNCHRONIZED)
      debug_printf("nosync ");
   if (xfer->base.usage & PIPE_TRANSFER_FLUSH_EXPLICIT)
      debug_printf("flush ");

   debug_printf(": buffer(%p,%x)[%x,%x)\n",
                xfer->base.resource, xfer->base.resource->width0,
                xfer->base.box.x,
                xfer->base.box.x + xfer->base.box.width);
}
#endif

static INLINE struct nouveau_transfer *
nouveau_transfer(struct pipe_transfer *transfer)
{
   return (struct nouveau_transfer *)transfer;
}

static boolean
nouveau_buffer_malloc(struct nv04_resource *buf)
{
   if (buf->data)
      return TRUE;
   buf->data = MALLOC(buf->base.width0);
   return buf->data ? TRUE : FALSE;
}

static INLINE boolean
nouveau_buffer_allocate(struct nouveau_screen *screen,
                        struct nv04_resource *buf, unsigned domain)
{
   uint32_t size = buf->base.width0;

   if (buf->base.bind & PIPE_BIND_CONSTANT_BUFFER)
      size = align(size, 0x100);

   if (domain == NOUVEAU_BO_VRAM) {
      buf->mm = nouveau_mm_allocate(screen->mm_VRAM, size,
                                    &buf->bo, &buf->offset);
      if (!buf->bo)
         return nouveau_buffer_allocate(screen, buf, NOUVEAU_BO_GART);
   } else
   if (domain == NOUVEAU_BO_GART) {
      buf->mm = nouveau_mm_allocate(screen->mm_GART, size,
                                    &buf->bo, &buf->offset);
      if (!buf->bo)
         return FALSE;
   } else {
      assert(domain == 0);
      if (!nouveau_buffer_malloc(buf))
         return FALSE;
   }
   buf->domain = domain;
   if (buf->bo)
      buf->address = buf->bo->offset + buf->offset;

   return TRUE;
}

static INLINE void
release_allocation(struct nouveau_mm_allocation **mm,
                   struct nouveau_fence *fence)
{
   nouveau_fence_work(fence, nouveau_mm_free_work, *mm);
   (*mm) = NULL;
}

INLINE void
nouveau_buffer_release_gpu_storage(struct nv04_resource *buf)
{
   nouveau_bo_ref(NULL, &buf->bo);

   if (buf->mm)
      release_allocation(&buf->mm, buf->fence);

   buf->domain = 0;
}

static INLINE boolean
nouveau_buffer_reallocate(struct nouveau_screen *screen,
                          struct nv04_resource *buf, unsigned domain)
{
   nouveau_buffer_release_gpu_storage(buf);
   return nouveau_buffer_allocate(screen, buf, domain);
}

static void
nouveau_buffer_destroy(struct pipe_screen *pscreen,
                       struct pipe_resource *presource)
{
   struct nv04_resource *res = nv04_resource(presource);

   nouveau_buffer_release_gpu_storage(res);

   if (res->data && !(res->status & NOUVEAU_BUFFER_STATUS_USER_MEMORY))
      FREE(res->data);

   nouveau_fence_ref(NULL, &res->fence);
   nouveau_fence_ref(NULL, &res->fence_wr);

   FREE(res);
}

static boolean
nouveau_transfer_staging_allocate(struct nouveau_context *nv,
                                  struct nouveau_transfer *xfer,
                                  unsigned size)
{
   xfer->mm = nouveau_mm_allocate(nv->screen->mm_GART, size, &xfer->bo,
                                  &xfer->bo_offset);
   if (!xfer->bo || nouveau_bo_map(xfer->bo, 0, NULL))
      return FALSE;
   xfer->map = (uint8_t *)xfer->bo->map + xfer->bo_offset;
   return TRUE;
}

static INLINE void
nouveau_transfer_staging_free(struct nouveau_context *nv,
                              struct nouveau_transfer *xfer)
{
   if (likely(xfer->bo)) {
      nouveau_bo_ref(NULL, &xfer->bo);
      if (xfer->mm)
         release_allocation(&xfer->mm, nv->screen->fence.current);
   } else {
      FREE(xfer->map);
   }
}

static boolean
nouveau_buffer_download(struct nouveau_context *nv,
                        struct nouveau_transfer *xfer, const boolean whole)
{
   struct nv04_resource *buf = nv04_resource(xfer->base.resource);
   unsigned base, size;

   debug_printf("ugh: %s\n", __FUNCTION__);

   if (whole) {
      if (!nouveau_buffer_malloc(buf))
         return FALSE;
      base = 0;
      size = buf->base.width0;
   } else {
      base = xfer->base.box.x;
      size = xfer->base.box.width;
   }
   if (!xfer->bo) {
      if (!nouveau_transfer_staging_allocate(nv, xfer, size))
         return FALSE;
   }

   nv->copy_data(nv, xfer->bo, xfer->bo_offset, NOUVEAU_BO_GART,
                 buf->bo, buf->offset + base, NOUVEAU_BO_VRAM, size);

   if (nouveau_bo_wait(xfer->bo, NOUVEAU_BO_RD, nv->screen->client))
      return FALSE;
   if (whole)
      buf->status &= ~NOUVEAU_BUFFER_STATUS_GPU_WRITING;

   if (buf->data)
      memcpy(buf->data + base, xfer->map, size);

   return TRUE;
}

static void
nouveau_staging_flush_write(struct nouveau_context *nv,
                            struct nouveau_transfer *xfer,
                            unsigned base, unsigned size)
{
   struct nv04_resource *buf = nv04_resource(xfer->base.resource);
   unsigned start = base + xfer->base.box.x;

   if (buf->data) {
      memcpy(xfer->map + base, buf->data + start, size);
      if (start == 0 && size == buf->base.width0)
         /* cache is up-to-date */
         buf->status &= ~NOUVEAU_BUFFER_STATUS_GPU_WRITING;
   }

   if (xfer->bo) {
      nv->copy_data(nv, buf->bo, buf->offset + start, buf->domain,
                    xfer->bo, xfer->bo_offset + base, NOUVEAU_BO_GART, size);
   } else
   if (buf->base.bind & PIPE_BIND_CONSTANT_BUFFER) {
      nv->push_cb(nv, buf->bo, buf->domain, buf->offset, buf->base.width0,
                  start, size / 4, (const uint32_t *)(xfer->map + base));
   } else {
      nv->push_data(nv, buf->bo, buf->offset + start, buf->domain,
                    size, xfer->map + base);
   }
}

static INLINE boolean
nouveau_buffer_busy(struct nv04_resource *buf, unsigned rw)
{
   if (rw == PIPE_TRANSFER_READ)
      return (buf->fence_wr && !nouveau_fence_signalled(buf->fence_wr));
   else
      return (buf->fence && !nouveau_fence_signalled(buf->fence));
}

static INLINE int
nouveau_resource_busy(struct nouveau_context *nv, struct nv04_resource *res)
{
   if (res->mm)
      return nouveau_buffer_busy(res, PIPE_TRANSFER_WRITE);
   return nouveau_bo_wait(res->bo, NOUVEAU_BO_WR, nv->screen->client);
}

static INLINE boolean
nouveau_buffer_sync(struct nv04_resource *buf, unsigned rw)
{
   if (rw == PIPE_TRANSFER_READ) {
      if (!buf->fence_wr)
         return TRUE;
      debug_printf("syncing(rd): %i\n", nouveau_buffer_busy(buf, rw));
      if (!nouveau_fence_wait(buf->fence_wr))
         return FALSE;
   } else {
      if (!buf->fence)
         return TRUE;
      debug_printf("syncing(wr): %i\n", nouveau_buffer_busy(buf, rw));
      if (!nouveau_fence_wait(buf->fence))
         return FALSE;

      nouveau_fence_ref(NULL, &buf->fence);
   }
   nouveau_fence_ref(NULL, &buf->fence_wr);

   return TRUE;
}

static struct pipe_transfer *
nouveau_buffer_transfer_get(struct pipe_context *pipe,
                            struct pipe_resource *resource,
                            unsigned level, unsigned usage,
                            const struct pipe_box *box)
{
   struct nouveau_context *nv = nouveau_context(pipe);
   struct nv04_resource *buf = nv04_resource(resource);
   struct nouveau_transfer *xfer = CALLOC_STRUCT(nouveau_transfer);
   if (!xfer)
      return NULL;

   xfer->base.resource = resource;
   xfer->base.box.x = box->x;
   xfer->base.box.width = box->width;
   xfer->base.usage = usage;

   if ((usage & PIPE_TRANSFER_DISCARD_WHOLE_RESOURCE) &&
       !(buf->base.bind & PIPE_BIND_SHARED) &&
       nouveau_resource_busy(nv, buf)) {
      nouveau_buffer_reallocate(nv->screen, buf, buf->domain);
      nouveau_fence_ref(NULL, &buf->fence);
      nouveau_fence_ref(NULL, &buf->fence_wr);

      /* invalidate if there are any references besides the transfer */
      if (resource->reference.count > 1)
         nv->invalidate_resource_storage(nv, resource,
                                         resource->reference.count - 1);
   }

   return &xfer->base;
}

static void
nouveau_buffer_transfer_destroy(struct pipe_context *pipe,
                                struct pipe_transfer *transfer)
{
   struct nv04_resource *buf = nv04_resource(transfer->resource);
   struct nouveau_transfer *xfer = nouveau_transfer(transfer);
   struct nouveau_context *nv = nouveau_context(pipe);

   if (transfer->usage & PIPE_TRANSFER_WRITE) {
      if (xfer->map && !(transfer->usage & PIPE_TRANSFER_FLUSH_EXPLICIT))
         nouveau_staging_flush_write(nv, xfer, 0, transfer->box.width);

      if (likely(buf->domain != 0)) {
         if (buf->base.bind &
             (PIPE_BIND_VERTEX_BUFFER | PIPE_BIND_INDEX_BUFFER))
            nv->vbo_dirty = TRUE;
         /*
         if (buf->base.bind & PIPE_BIND_SAMPLER_VIEW)
            nv->tex_dirty = TRUE;
         */
      }

      buf->status |= NOUVEAU_BUFFER_STATUS_INITIALIZED;
   }

   if (xfer->map)
      nouveau_transfer_staging_free(nv, xfer);

   FREE(xfer);
}

static uint8_t *
nouveau_transfer_staging(struct nouveau_context *nv,
                         struct nouveau_transfer *xfer, boolean discard)
{
   struct nv04_resource *buf = nv04_resource(xfer->base.resource);
   uint8_t *data;
   const unsigned base = xfer->base.box.x;
   const unsigned size = xfer->base.box.width;
   const boolean push = size < 192;

   /* check if we already have storage */
   if (xfer->map)
      return buf->data ? (buf->data + base) : xfer->map;

   if (!discard && buf->domain == NOUVEAU_BO_VRAM) {
      /* if GPU is writing, caching everything will likely be useless */
      const boolean whole = !(buf->status & NOUVEAU_BUFFER_STATUS_GPU_WRITING);
      if ((buf->status & NOUVEAU_BUFFER_STATUS_INITIALIZED) &&
          (!whole || !buf->data)) {
         if (!nouveau_buffer_download(nv, xfer, whole))
            return NULL;
         return whole ? (buf->data + base) : xfer->map;
      }
   }

   if (!push) {
      if (!nouveau_transfer_staging_allocate(nv, xfer, size))
         return NULL;
      data = buf->data ? (buf->data + base) : xfer->map;
   } else
   if (buf->data) {
      data = buf->data + base;
   } else {
      xfer->map = data = MALLOC(size);
      if (!data)
         return NULL;
   }
   if (unlikely(buf->domain == NOUVEAU_BO_GART && !discard)) {
      if (nouveau_bo_map(buf->bo, 0, NULL))
         return NULL;
      memcpy(data, (const uint8_t *)buf->bo->map + base, size);
   }
   return data;
}

static void *
nouveau_buffer_transfer_map(struct pipe_context *pipe,
                            struct pipe_transfer *transfer)
{
   struct nouveau_context *nv = nouveau_context(pipe);
   struct nouveau_transfer *xfer = nouveau_transfer(transfer);
   struct nv04_resource *buf = nv04_resource(transfer->resource);
   struct nouveau_bo *bo = buf->bo;
   uint32_t flags = 0;
   const unsigned usage = xfer->base.usage;
   const boolean discard = usage &
      (PIPE_TRANSFER_DISCARD_RANGE | PIPE_TRANSFER_DISCARD_WHOLE_RESOURCE);

   if (buf->domain == NOUVEAU_BO_VRAM)
      return nouveau_transfer_staging(nv, xfer, discard);

   /* MALLOC buffer */
   if (unlikely(buf->domain == 0))
      return buf->data + xfer->base.box.x;

   /* GART */
   if (unlikely(!buf->mm)) {
      /* not sub-allocated, use kernel fences */
      flags = nouveau_screen_transfer_flags(usage);
   } else
   if (!(usage & PIPE_TRANSFER_UNSYNCHRONIZED)) {
      /* use staging transfer if we won't have to wait */
      const unsigned rw = usage & PIPE_TRANSFER_READ_WRITE;
      if (nouveau_buffer_busy(buf, rw)) {
         if (usage & PIPE_TRANSFER_DONTBLOCK)
            return NULL;
         if (discard)
            return nouveau_transfer_staging(nv, xfer, TRUE);
         if (usage & PIPE_TRANSFER_WRITE)
            if (!nouveau_buffer_busy(buf, PIPE_TRANSFER_READ))
               return nouveau_transfer_staging(nv, xfer, FALSE);
      }
   }
   if (unlikely(nouveau_bo_map(buf->bo, flags, nv->screen->client)))
      return NULL;
   return (uint8_t *)bo->map + (buf->offset + xfer->base.box.x);
}

static void
nouveau_buffer_transfer_flush_region(struct pipe_context *pipe,
                                     struct pipe_transfer *transfer,
                                     const struct pipe_box *box)
{
   struct nouveau_transfer *xfer = nouveau_transfer(transfer);
   if (xfer->map)
      nouveau_staging_flush_write(nouveau_context(pipe), xfer,
                                  box->x, box->width);
}

static void
nouveau_buffer_transfer_unmap(struct pipe_context *pipe,
                              struct pipe_transfer *transfer)
{
}


static void
nouveau_transfer_inline_write(struct pipe_context *pipe,
                              struct pipe_resource *res,
                              unsigned level,
                              unsigned usage, /* discard | write implied */
                              const struct pipe_box *box,
                              const void *data,
                              unsigned stride,
                              unsigned layer_stride)
{
   
}


void *
nouveau_resource_map_offset(struct nouveau_context *nv,
                            struct nv04_resource *res, uint32_t offset,
                            uint32_t flags)
{
   if (res->domain == NOUVEAU_BO_VRAM) {
      if (!res->data || (res->status & NOUVEAU_BUFFER_STATUS_GPU_WRITING)) {
         struct nouveau_transfer xfer;
         xfer.base.resource = &res->base;
         nouveau_buffer_download(nv, &xfer, TRUE);
         if (xfer.bo)
            nouveau_transfer_staging_free(nv, &xfer);
      }
      return res->data + offset;
   }

   if (res->domain == 0 || (res->status & NOUVEAU_BUFFER_STATUS_USER_MEMORY))
      return res->data + offset;

   if (res->mm) {
      unsigned rw;
      rw = (flags & NOUVEAU_BO_WR) ? PIPE_TRANSFER_WRITE : PIPE_TRANSFER_READ;
      nouveau_buffer_sync(res, rw);
      if (nouveau_bo_map(res->bo, 0, NULL))
         return NULL;
   } else {
      if (nouveau_bo_map(res->bo, flags, nv->screen->client))
         return NULL;
   }
   return (uint8_t *)res->bo->map + (res->offset + offset);
}


const struct u_resource_vtbl nouveau_buffer_vtbl =
{
   u_default_resource_get_handle,     /* get_handle */
   nouveau_buffer_destroy,               /* resource_destroy */
   nouveau_buffer_transfer_get,          /* get_transfer */
   nouveau_buffer_transfer_destroy,      /* transfer_destroy */
   nouveau_buffer_transfer_map,          /* transfer_map */
   nouveau_buffer_transfer_flush_region, /* transfer_flush_region */
   nouveau_buffer_transfer_unmap,        /* transfer_unmap */
   u_default_transfer_inline_write    /* transfer_inline_write */
};

struct pipe_resource *
nouveau_buffer_create(struct pipe_screen *pscreen,
                      const struct pipe_resource *templ)
{
   struct nouveau_screen *screen = nouveau_screen(pscreen);
   struct nv04_resource *buffer;
   boolean ret;

   buffer = CALLOC_STRUCT(nv04_resource);
   if (!buffer)
      return NULL;

   buffer->base = *templ;
   buffer->vtbl = &nouveau_buffer_vtbl;
   pipe_reference_init(&buffer->base.reference, 1);
   buffer->base.screen = pscreen;

   if (buffer->base.bind &
       (screen->vidmem_bindings & screen->sysmem_bindings)) {
      switch (buffer->base.usage) {
      case PIPE_USAGE_DEFAULT:
      case PIPE_USAGE_DYNAMIC:
      case PIPE_USAGE_IMMUTABLE:
      case PIPE_USAGE_STATIC:
         buffer->domain = NOUVEAU_BO_VRAM;
         break;
      case PIPE_USAGE_STAGING:
      case PIPE_USAGE_STREAM:
         buffer->domain = NOUVEAU_BO_GART;
         break;
      default:
         assert(0);
         break;
      }
   } else {
      if (buffer->base.bind & screen->vidmem_bindings)
         buffer->domain = NOUVEAU_BO_VRAM;
      else
      if (buffer->base.bind & screen->sysmem_bindings)
         buffer->domain = NOUVEAU_BO_GART;
   }
   ret = nouveau_buffer_allocate(screen, buffer, buffer->domain);

   if (ret == FALSE)
      goto fail;

   return &buffer->base;

fail:
   FREE(buffer);
   return NULL;
}


/* User buffers. */

struct pipe_resource *
nouveau_user_buffer_create(struct pipe_screen *pscreen, void *ptr,
                           unsigned bytes, unsigned bind)
{
   struct nv04_resource *buffer;

   buffer = CALLOC_STRUCT(nv04_resource);
   if (!buffer)
      return NULL;

   pipe_reference_init(&buffer->base.reference, 1);
   buffer->vtbl = &nouveau_buffer_vtbl;
   buffer->base.screen = pscreen;
   buffer->base.format = PIPE_FORMAT_R8_UNORM;
   buffer->base.usage = PIPE_USAGE_IMMUTABLE;
   buffer->base.bind = bind;
   buffer->base.width0 = bytes;
   buffer->base.height0 = 1;
   buffer->base.depth0 = 1;

   buffer->data = ptr;
   buffer->status = NOUVEAU_BUFFER_STATUS_USER_MEMORY;

   return &buffer->base;
}

/* Like download, but for GART buffers. Merge ? */
static INLINE boolean
nouveau_buffer_data_fetch(struct nouveau_context *nv, struct nv04_resource *buf,
                          struct nouveau_bo *bo, unsigned offset, unsigned size)
{
   if (!buf->data) {
      buf->data = MALLOC(size);
      if (!buf->data)
         return FALSE;
   }
   if (nouveau_bo_map(bo, NOUVEAU_BO_RD, nv->screen->client))
      return FALSE;
   memcpy(buf->data, (uint8_t *)bo->map + offset, size);

   return TRUE;
}

/* Migrate a linear buffer (vertex, index, constants) USER -> GART -> VRAM. */
boolean
nouveau_buffer_migrate(struct nouveau_context *nv,
                       struct nv04_resource *buf, const unsigned new_domain)
{
#if 0
   struct nouveau_screen *screen = nv->screen;
   struct nouveau_bo *bo;
   const unsigned old_domain = buf->domain;
   unsigned size = buf->base.width0;
   unsigned offset;
   int ret;

   assert(new_domain != old_domain);

   if (new_domain == NOUVEAU_BO_GART && old_domain == 0) {
      if (!nouveau_buffer_allocate(screen, buf, new_domain))
         return FALSE;
      ret = nouveau_bo_map(buf->bo, 0, nv->screen->client);
      if (ret)
         return ret;
      memcpy((uint8_t *)buf->bo->map + buf->offset, buf->data, size);
      FREE(buf->data);
   } else
   if (old_domain != 0 && new_domain != 0) {
      struct nouveau_mm_allocation *mm = buf->mm;

      if (new_domain == NOUVEAU_BO_VRAM) {
         /* keep a system memory copy of our data in case we hit a fallback */
         if (!nouveau_buffer_data_fetch(nv, buf, buf->bo, buf->offset, size))
            return FALSE;
         if (nouveau_mesa_debug)
            debug_printf("migrating %u KiB to VRAM\n", size / 1024);
      }

      offset = buf->offset;
      bo = buf->bo;
      buf->bo = NULL;
      buf->mm = NULL;
      nouveau_buffer_allocate(screen, buf, new_domain);

      nv->copy_data(nv, buf->bo, buf->offset, new_domain,
                    bo, offset, old_domain, buf->base.width0);

      nouveau_bo_ref(NULL, &bo);
      if (mm)
         release_allocation(&mm, screen->fence.current);
   } else
   if (new_domain == NOUVEAU_BO_VRAM && old_domain == 0) {
      if (!nouveau_buffer_allocate(screen, buf, NOUVEAU_BO_VRAM))
         return FALSE;
      if (!nouveau_buffer_upload(nv, buf, 0, buf->base.width0))
         return FALSE;
   } else
      return FALSE;

   assert(buf->domain == new_domain);
   return TRUE;
#endif
}

/* Migrate data from glVertexAttribPointer(non-VBO) user buffers to GART.
 * We'd like to only allocate @size bytes here, but then we'd have to rebase
 * the vertex indices ...
 */
boolean
nouveau_user_buffer_upload(struct nouveau_context *nv,
                           struct nv04_resource *buf,
                           unsigned base, unsigned size)
{
   struct nouveau_screen *screen = nouveau_screen(buf->base.screen);
   int ret;

   assert(buf->status & NOUVEAU_BUFFER_STATUS_USER_MEMORY);

   buf->base.width0 = base + size;
   if (!nouveau_buffer_reallocate(screen, buf, NOUVEAU_BO_GART))
      return FALSE;

   ret = nouveau_bo_map(buf->bo, 0, nv->screen->client);
   if (ret)
      return FALSE;
   memcpy((uint8_t *)buf->bo->map + buf->offset + base, buf->data + base, size);

   return TRUE;
}


/* Scratch data allocation. */

static INLINE int
nouveau_scratch_bo_alloc(struct nouveau_context *nv, struct nouveau_bo **pbo,
                         unsigned size)
{
   return nouveau_bo_new(nv->screen->device, NOUVEAU_BO_GART | NOUVEAU_BO_MAP,
                         4096, size, NULL, pbo);
}

void
nouveau_scratch_runout_release(struct nouveau_context *nv)
{
   if (!nv->scratch.nr_runout)
      return;
   do {
      --nv->scratch.nr_runout;
      nouveau_bo_ref(NULL, &nv->scratch.runout[nv->scratch.nr_runout]);
   } while (nv->scratch.nr_runout);

   FREE(nv->scratch.runout);
   nv->scratch.end = 0;
   nv->scratch.runout = NULL;
}

/* Allocate an extra bo if we can't fit everything we need simultaneously.
 * (Could happen for very large user arrays.)
 */
static INLINE boolean
nouveau_scratch_runout(struct nouveau_context *nv, unsigned size)
{
   int ret;
   const unsigned n = nv->scratch.nr_runout++;

   nv->scratch.runout = REALLOC(nv->scratch.runout,
                                (n + 0) * sizeof(*nv->scratch.runout),
                                (n + 1) * sizeof(*nv->scratch.runout));
   nv->scratch.runout[n] = NULL;

   ret = nouveau_scratch_bo_alloc(nv, &nv->scratch.runout[n], size);
   if (!ret) {
      ret = nouveau_bo_map(nv->scratch.runout[n], 0, NULL);
      if (ret)
         nouveau_bo_ref(NULL, &nv->scratch.runout[--nv->scratch.nr_runout]);
   }
   if (!ret) {
      nv->scratch.current = nv->scratch.runout[n];
      nv->scratch.offset = 0;
      nv->scratch.end = size;
      nv->scratch.map = nv->scratch.current->map;
   }
   return !ret;
}

/* Continue to next scratch buffer, if available (no wrapping, large enough).
 * Allocate it if it has not yet been created.
 */
static INLINE boolean
nouveau_scratch_next(struct nouveau_context *nv, unsigned size)
{
   struct nouveau_bo *bo;
   int ret;
   const unsigned i = (nv->scratch.id + 1) % NOUVEAU_MAX_SCRATCH_BUFS;

   if ((size > nv->scratch.bo_size) || (i == nv->scratch.wrap))
      return FALSE;
   nv->scratch.id = i;

   bo = nv->scratch.bo[i];
   if (!bo) {
      ret = nouveau_scratch_bo_alloc(nv, &bo, nv->scratch.bo_size);
      if (ret)
         return FALSE;
      nv->scratch.bo[i] = bo;
   }
   nv->scratch.current = bo;
   nv->scratch.offset = 0;
   nv->scratch.end = nv->scratch.bo_size;

   ret = nouveau_bo_map(bo, NOUVEAU_BO_WR, nv->screen->client);
   if (!ret)
      nv->scratch.map = bo->map;
   return !ret;
}

static boolean
nouveau_scratch_more(struct nouveau_context *nv, unsigned min_size)
{
   boolean ret;

   ret = nouveau_scratch_next(nv, min_size);
   if (!ret)
      ret = nouveau_scratch_runout(nv, min_size);
   return ret;
}


/* Copy data to a scratch buffer and return address & bo the data resides in. */
uint64_t
nouveau_scratch_data(struct nouveau_context *nv,
                     const void *data, unsigned base, unsigned size,
                     struct nouveau_bo **bo)
{
   unsigned bgn = MAX2(base, nv->scratch.offset);
   unsigned end = bgn + size;

   if (end >= nv->scratch.end) {
      end = base + size;
      if (!nouveau_scratch_more(nv, end))
         return 0;
      bgn = base;
   }
   nv->scratch.offset = align(end, 4);

   memcpy(nv->scratch.map + bgn, (const uint8_t *)data + base, size);

   *bo = nv->scratch.current;
   return (*bo)->offset + (bgn - base);
}

void *
nouveau_scratch_get(struct nouveau_context *nv,
                    unsigned size, uint64_t *gpu_addr, struct nouveau_bo **pbo)
{
   unsigned bgn = nv->scratch.offset;
   unsigned end = nv->scratch.offset + size;

   if (end >= nv->scratch.end) {
      end = size;
      if (!nouveau_scratch_more(nv, end))
         return NULL;
      bgn = 0;
   }
   nv->scratch.offset = align(end, 4);

   *pbo = nv->scratch.current;
   *gpu_addr = nv->scratch.current->offset + bgn;
   return nv->scratch.map + bgn;
}
