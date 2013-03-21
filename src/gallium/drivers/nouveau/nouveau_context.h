#ifndef __NOUVEAU_CONTEXT_H__
#define __NOUVEAU_CONTEXT_H__

#include "pipe/p_context.h"
#include <libdrm/nouveau.h>

#define NOUVEAU_MAX_SCRATCH_BUFS 4

struct nouveau_context {
   struct pipe_context pipe;
   struct nouveau_screen *screen;

   struct nouveau_object *channel;
   struct nouveau_client *client;
   struct nouveau_pushbuf *pushbuf;

   boolean vbo_dirty;
   boolean cb_dirty;

   void (*copy_data)(struct nouveau_context *,
                     struct nouveau_bo *dst, unsigned, unsigned,
                     struct nouveau_bo *src, unsigned, unsigned, unsigned);
   void (*push_data)(struct nouveau_context *,
                     struct nouveau_bo *dst, unsigned, unsigned,
                     unsigned, const void *);
   /* base, size refer to the whole constant buffer */
   void (*push_cb)(struct nouveau_context *,
                   struct nouveau_bo *, unsigned domain,
                   unsigned base, unsigned size,
                   unsigned offset, unsigned words, const uint32_t *);

   /* @return: @ref reduced by nr of references found in context */
   int (*invalidate_resource_storage)(struct nouveau_context *,
                                      struct pipe_resource *,
                                      int ref);

   struct {
      uint8_t *map;
      unsigned id;
      unsigned wrap;
      unsigned offset;
      unsigned end;
      struct nouveau_bo *bo[NOUVEAU_MAX_SCRATCH_BUFS];
      struct nouveau_bo *current;
      struct nouveau_bo **runout;
      unsigned nr_runout;
      unsigned bo_size;
   } scratch;

   struct {
      uint32_t buf_cache_count;
      uint32_t buf_cache_frame;
   } stats;
};

static INLINE struct nouveau_context *
nouveau_context(struct pipe_context *pipe)
{
   return (struct nouveau_context *)pipe;
}

#define NOUVEAU_CONTEXT_EXCLUSIVE_CHANNEL (1 << 0)
#define NOUVEAU_CONTEXT_DEFERRED          (1 << 1)

static INLINE int
nouveau_context_init(struct nouveau_context *nv, uint32_t flags)
{
   const int immediate = !(flags & NOUVEAU_CONTEXT_DEFERRED);
   int ret;

   if (flags & NOUVEAU_CONTEXT_EXCLUSIVE_CHANNEL) {
#ifdef NVE0_FIFO_ENGINE_GR
      if (nv->chipset >= 0xe0) {
         struct nve0_fifo data = { .engine = NVE0_FIFO_ENGINE_GR };
         ret = nouveau_object_new(&nv->screen->device->object, 0,
                                  NOUVEAU_FIFO_CHANNEL_CLASS,
                                  &data, sizeof(data), &nv->channel);
      } else
#endif
      if (nv->chipset >= 0xc0) {
         struct nvc0_fifo data = { };
         ret = nouveau_object_new(&nv->screen->device->object, 0,
                                  NOUVEAU_FIFO_CHANNEL_CLASS,
                                  &data, sizeof(data), &nv->channel);
      } else
      if (nv->chipset < 0xe0) {
         struct nv04_fifo data = { .vram = 0xbeef0202, .gart = 0xbeef0202 };
         ret = nouveau_object_new(&nv->screen->device->object, 0,
                                  NOUVEAU_FIFO_CHANNEL_CLASS,
                                  &data, sizeof(data), &nv->channel);
      }
      if (ret)
         return ret;
   } else {
      nv->channel = nv->screen->channel;
   }

   ret = nouveau_client_new(nv->screen->device, &nv->client);
   if (ret)
      return ret;

   ret = nouveau_pushbuf_new(nv->client, nv->channel, 4, 512 * 1024, immediate,
                             &nv->pushbuf);
   if (ret)
      return ret;
}

static INLINE void
nouveau_context_fini(struct nouveau_context *nv)
{
   if (nv->pushbuf)
      nouveau_pushbuf_del(&nv->pushbuf);
   if (nv->client)
      nouveau_object_del(&nv->client);
   if (nv->channel && nv->channel != nv->screen->channel)
      nouveau_object_del(&nv->channel);
}

void
nouveau_context_init_vdec(struct nouveau_context *);

void
nouveau_scratch_runout_release(struct nouveau_context *);

/* This is needed because we don't hold references outside of context::scratch,
 * because we don't want to un-bo_ref each allocation every time. This is less
 * work, and we need the wrap index anyway for extreme situations.
 */
static INLINE void
nouveau_scratch_done(struct nouveau_context *nv)
{
   nv->scratch.wrap = nv->scratch.id;
   if (unlikely(nv->scratch.nr_runout))
      nouveau_scratch_runout_release(nv);
}

/* Get pointer to scratch buffer.
 * The returned nouveau_bo is only referenced by the context, don't un-ref it !
 */
void *
nouveau_scratch_get(struct nouveau_context *, unsigned size, uint64_t *gpu_addr,
                    struct nouveau_bo **);

static INLINE void
nouveau_context_destroy(struct nouveau_context *ctx)
{
   int i;

   for (i = 0; i < NOUVEAU_MAX_SCRATCH_BUFS; ++i)
      if (ctx->scratch.bo[i])
         nouveau_bo_ref(NULL, &ctx->scratch.bo[i]);

   FREE(ctx);
}

static INLINE  void
nouveau_context_update_frame_stats(struct nouveau_context *nv)
{
   nv->stats.buf_cache_frame <<= 1;
   if (nv->stats.buf_cache_count) {
      nv->stats.buf_cache_count = 0;
      nv->stats.buf_cache_frame |= 1;
      if ((nv->stats.buf_cache_frame & 0xf) == 0xf)
         nv->screen->hint_buf_keep_sysmem_copy = TRUE;
   }
}

#endif
