/*
 * Copyright 2012 Nouveau Project
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors: Christoph Bumiller
 */

static uint32_t
nvc0_program_symbol_offset(struct nvc0_program *prog, uint32_t label)
{
   unsigned i;
   for (i = 0; i < prog->num_symbols; ++i)
      if (prog->syms[i].label == label)
         return prog->mem->start + prog->syms[i].offset;
   return ~0;
}

void
nve4_screen_compute_setup(struct nvc0_screen *screen,
                          struct nouveau_pushbuf *push)
{
   struct nouveau_object *chan = screen->base.channel;

   ret = nouveau_object_new(chan, 0xbeef00c0, NVE4_COMPUTE, NULL, 0,
                            &screen->compute);
   if (ret)
      return ret;

   BEGIN_NVC0(push, SUBC_COMPUTE(NV01_SUBCHAN_OBJECT), 1);
   PUSH_DATA (push, screen->compute->oclass);

   BEGIN_NVC0(push, SUBC_COMPUTE(0x02e4), 3);
   PUSH_DATA (push, 0);
   PUSH_DATA (push, 0x438000);
   PUSH_DATA (push, 0xff);
   BEGIN_NVC0(push, SUBC_COMPUTE(0x02f0), 3);
   PUSH_DATA (push, 0);
   PUSH_DATA (push, 0x438000);
   PUSH_DATA (push, 0xff);

   /* Unified address space ? Who needs that ? Certainly not OpenCL. */
   BEGIN_NVC0(push, NVE4_COMPUTE(LOCAL_BASE), 1);
   PUSH_DATA (push, 0);
   BEGIN_NVC0(push, NVE4_COMPUTE(SHARED_BASE), 1);
   PUSH_DATA (push, 0);

   BEGIN_NVC0(push, SUBC_COMPUTE(0x0310), 1);
   PUSH_DATA (push, 0x300);

   BEGIN_NVC0(push, NVE4_COMPUTE(TEX_CB_INDEX), 1);
   PUSH_DATA (push, 0);
}

static void
nve4_compute_upload_input()
{
   BEGIN_NVC0(push, NVE4_COMPUTE(UPLOAD_ADDRESS_HIGH), 2);
   PUSH_DATAh(push, );
   PUSH_DATA (push, );
   BEGIN_NVC0(push, NVE4_COMPUTE(UPLOAD_SIZE), 2);
   PUSH_DATA (push, cp->parm_size);
   PUSH_DATA (push, 0x1);
   BEGIN_1IC0(push, NVE4_COMPUTE(UPLOAD_EXEC), 1 + (cp->parm_size / 4));
   PUSH_DATA (push, 0x41);
   PUSH_DATAp(push, input, cp->parm_size / 4);

   BEGIN_NVC0(push, NVE4_COMPUTE(FLUSH), 1);
   PUSH_DATA (push, NVE4_COMPUTE_FLUSH_CB);
}

static const uint8_t nve4_su_format_map[PIPE_FORMAT_COUNT] =
{
};

static void
nve4_compute_validate_surfaces(struct nvc0_context *nvc0)
{
   struct nouveau_pushbuf *push = nvc0->base.pushbuf;
   unsigned i;
   unsigned th, tm;

   for (i = 0; i < 0; ++i) {
      struct nv50_surface *sf = nv50_surface(nvc0->surfaces[i]);
      if (sf) {
         struct nv04_resource *res = nv04_resource(sf->base.texture);
         struct nv50_miptree *mt = nv50_miptree(res);

         tm = lvl->tile_mode;
         th = NVC0_TILE_HEIGHT(lvl->tile_mode);

         PUSH_DATA (push, (res->address + sf->offset) >> 8);
         PUSH_DATA (push, nve4_su_format_map[sf->base.format]);
         PUSH_DATA (push (1 << 28) | (2 << 22) | (sf->width - 1));
         PUSH_DATA (push, 0x88000000 | (lvl->pitch / 64));
         PUSH_DATA (push, (tm & 0x0f0) << (29 - 4) | (th << 22) |
                    (sf->height - 1));
         PUSH_DATA (push, 0);
         PUSH_DATA (push, (sf->depth - 1));
         PUSH_DATA (push, 0);
      } else {
         PUSH_DATA (push, 0);
         PUSH_DATA (push, 0);
         PUSH_DATA (push, 0);
         PUSH_DATA (push, 0);
         PUSH_DATA (push, 0);
         PUSH_DATA (push, 0);
         PUSH_DATA (push, 0);
         PUSH_DATA (push, 0);
      }
   }
}

static void
nve4_compute_validate_samplers(struct nvc0_context *nvc0)
{
   boolean need_flush = nve4_validate_tsc(nvc0, 5);
   if (need_flush) {
      BEGIN_NVC0(nvc0->base.push, NVE4_COMPUTE(), 1);
      PUSH_DATA (nvc0->base.push, 0);
   }
}

static void
nve4_compute_validate_textures(struct nvc0_context *nvc0)
{
   struct nouveau_bo *txc = nvc0->screen->txc;
   struct nouveau_pushbuf *push = nvc0->base.pushbuf;
   const unsigned s = 5;
   unsigned i;
   boolean need_flush = FALSE;

   for (i = 0; i < nvc0->num_textures[s]; ++i) {
      struct nv50_tic_entry *tic = nv50_tic_entry(nvc0->textures[s][i]);
      struct nv04_resource *res;
      const boolean dirty = !!(nvc0->textures_dirty[s] & (1 << i));

      if (!tic) {
         nvc0->tex_handles[s][i] |= NVE4_TIC_ENTRY_INVALID;
         continue;
      }
      res = nv04_resource(tic->pipe.texture);

      if (tic->id < 0) {
         tic->id = nvc0_screen_tic_alloc(nvc0->screen, tic);

         PUSH_SPACE(push, 16);
         BEGIN_NVC0(push, NVE4_P2MF(DST_ADDRESS_HIGH), 2);
         PUSH_DATAh(push, txc->offset + (tic->id * 32));
         PUSH_DATA (push, txc->offset + (tic->id * 32));
         BEGIN_NVC0(push, NVE4_P2MF(LINE_LENGTH_IN), 2);
         PUSH_DATA (push, 32);
         PUSH_DATA (push, 1);
         BEGIN_1IC0(push, NVE4_P2MF(EXEC), 9);
         PUSH_DATA (push, 0x1001);
         PUSH_DATAp(push, &tic->tic[0], 8);

         need_flush = TRUE;
      } else
      if (res->status & NOUVEAU_BUFFER_STATUS_GPU_WRITING) {
         BEGIN_NVC0(push, NVC0_3D(TEX_CACHE_CTL), 1);
         PUSH_DATA (push, (tic->id << 4) | 1);
      }
      nvc0->screen->tic.lock[tic->id / 32] |= 1 << (tic->id % 32);

      res->status &= ~NOUVEAU_BUFFER_STATUS_GPU_WRITING;
      res->status |=  NOUVEAU_BUFFER_STATUS_GPU_READING;

      nvc0->tex_handles[s][i] &= ~NVE4_TIC_ENTRY_INVALID;
      nvc0->tex_handles[s][i] |= tic->id;
      if (dirty)
         BCTX_REFN(nvc0->bufctx_cp, TEX(i), res, RD);
   }
   for (; i < nvc0->state.num_textures[s]; ++i)
      nvc0->tex_handles[s][i] |= NVE4_TIC_ENTRY_INVALID;

   nvc0->state.num_textures[s] = nvc0->num_textures[s];
}

static void
nve4_compute_state_validate(struct nvc0_context *nvc0)
{
   nve4_compute_validate_textures(nvc0);
   nve4_compute_validate_samplers(nvc0);
   nve4_compute_validate_surfaces(nvc0);

   nvc0_bufctx_fence(nvc0, nvc0->bufctx_cp, FALSE);

   nouveau_pushbuf_bufctx(nvc0->base.pushbuf, nvc0->bufctx_cp);
   if (unlikely(nouveau_pushbuf_validate(nvc0->base.pushbuf)))
      return FALSE;
   if (unlikely(nvc0->state.flushed))
      nvc0_bufctx_fence(nvc0, nvc0->bufctx_cp, TRUE);

   return TRUE;
}

void
nve4_launch_grid(struct pipe_context *pipe,
                 const uint *block_layout, const uint *grid_layout,
                 uint32_t label,
                 const void *input)
{
   struct nvc0_context *nvc0 = nvc0_context(pipe);
   struct nouveau_pushbuf *push = nvc0->base.pushbuf;
   struct nvc0_program *cp = nvc0->compprog;
   uint32_t *desc;
   struct nouveau_bo *desc_bo;
   uint64_t addr, desc_gpuaddr;
   uint32_t l1cfg;
   int ret;

   desc = nouveau_scratch_get(&nvc0->base, 512, &addr, &desc_bo);
   desc_gpuaddr = align(addr, 256);
   desc += (desc_gpuaddr - addr) / 4;

   BCTX_REFN_bo(nvc0->bufctx_cp, CP_DESC, desc_bo);

   ret = nve4_compute_state_validate(nvc0);
   if (ret)
      goto out;

   memset(desc, 0, 256);

   desc[7]  = 0xbc000000;

   desc[8]  = nvc0_program_symbol_offset(cp, label);

   desc[10] = 0x44014000;

   desc[11] = grid_layout[0];
   desc[12] = (grid_layout[2] << 16) | grid_layout[1];

   desc[17] = (block_layout[0] << 16) | 6;
   desc[18] = (block_layout[2] << 16) | block_layout[1];

   desc[16] = cp.cp.smem_size;

   if (cp->cp.smem_size >= (48 << 10))
      l1cfg = NVE4_COMPUTE_LAUNCH_DESC_20_CACHE_SPLIT_48K_SHARED_16K_L1;
   else
   if (cp->cp.smem_size >= (32 << 10))
      l1cfg = NVE4_COMPUTE_LAUNCH_DESC_20_CACHE_SPLIT_32K_SHARED_32K_L1;
   else
      l1cfg = NVE4_COMPUTE_LAUNCH_DESC_20_CACHE_SPLIT_16K_SHARED_48K_L1;

   desc[19] = (l1cfg << 25) | 0x8b;
   desc[46] = (cp->max_gpr << 24) | cp->cp.lmem_size;

   desc[47] = 0x30002000;

   for (i = 0; i < 7; ++i) {
      struct nv04_resource *res = nv04_resource(nvc0->constbuf[5][i]);
      if (res) {
         const uint64_t address = res->address + nvc0->constbuf[5][i].offset;
         desc[28 + i * 2] = address;
         desc[29 + i * 2] = (nvc0->constbuf[5][i].size << 15) | (address >> 32);
      }
   }

   BEGIN_NVC0(push, NVE4_COMPUTE(LAUNCH_DESC_ADDRESS), 1);
   PUSH_DATA (push, desc_gpuaddr >> 8);
   BEGIN_NVC0(push, NVE4_COMPUTE(LAUNCH), 1);
   PUSH_DATA (push, 0x3);
   BEGIN_NVC0(push, SUBC_COMPUTE(NV50_GRAPH_SERIALIZE), 1);
   PUSH_DATA (push, 0);

out:
   if (ret)
      NOUVEAU_ERR("Failed to launch grid !\n");
   nouveau_scratch_done(&nvc0->base);
   nouveau_bufctx_reset(nvc0->bufctx_cp, NVC0_BIND_CP_DESC);
}
