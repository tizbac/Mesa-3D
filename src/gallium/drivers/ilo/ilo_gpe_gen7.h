/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 2013 LunarG, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Chia-I Wu <olv@lunarg.com>
 */

#ifndef ILO_GPE_GEN7_H
#define ILO_GPE_GEN7_H

#include "intel_winsys.h"

#include "ilo_common.h"
#include "ilo_cp.h"
#include "ilo_resource.h"
#include "ilo_shader.h"
#include "ilo_gpe_gen6.h"

static inline void
gen7_emit_GPGPU_WALKER(const struct ilo_dev_info *dev,
                       struct ilo_cp *cp)
{
   assert(!"GPGPU_WALKER unsupported");
}

static inline void
gen7_emit_3DSTATE_CLEAR_PARAMS(const struct ilo_dev_info *dev,
                               uint32_t clear_val,
                               struct ilo_cp *cp)
{
   const uint8_t cmd_len = 3;
   const uint32_t dw0 = GEN7_RENDER_CMD(3D, 3DSTATE_CLEAR_PARAMS) |
                        (cmd_len - 2);

   ILO_GPE_VALID_GEN(dev, 7, 7.5);

   ilo_cp_begin(cp, cmd_len);
   ilo_cp_write(cp, dw0);
   ilo_cp_write(cp, clear_val);
   ilo_cp_write(cp, 1);
   ilo_cp_end(cp);
}

static inline void
gen7_emit_3DSTATE_VF(const struct ilo_dev_info *dev,
                     bool enable_cut_index,
                     uint32_t cut_index,
                     struct ilo_cp *cp)
{
   const uint8_t cmd_len = 2;
   uint32_t dw0 = GEN75_RENDER_CMD(3D, 3DSTATE_VF) | (cmd_len - 2);

   ILO_GPE_VALID_GEN(dev, 7.5, 7.5);

   if (enable_cut_index)
      dw0 |= GEN75_VF_DW0_CUT_INDEX_ENABLE;

   ilo_cp_begin(cp, cmd_len);
   ilo_cp_write(cp, dw0);
   ilo_cp_write(cp, cut_index);
   ilo_cp_end(cp);
}

static inline void
gen7_emit_3dstate_pointer(const struct ilo_dev_info *dev,
                          int subop, uint32_t pointer,
                          struct ilo_cp *cp)
{
   const uint8_t cmd_len = 2;
   const uint32_t dw0 = GEN6_RENDER_TYPE_RENDER |
                        GEN6_RENDER_SUBTYPE_3D |
                        subop | (cmd_len - 2);

   ILO_GPE_VALID_GEN(dev, 7, 7.5);

   ilo_cp_begin(cp, cmd_len);
   ilo_cp_write(cp, dw0);
   ilo_cp_write(cp, pointer);
   ilo_cp_end(cp);
}

static inline void
gen7_emit_3DSTATE_CC_STATE_POINTERS(const struct ilo_dev_info *dev,
                                    uint32_t color_calc_state,
                                    struct ilo_cp *cp)
{
   gen7_emit_3dstate_pointer(dev,
         GEN6_RENDER_OPCODE_3DSTATE_CC_STATE_POINTERS, color_calc_state, cp);
}

static inline void
gen7_emit_3DSTATE_GS(const struct ilo_dev_info *dev,
                     const struct ilo_shader_state *gs,
                     int num_samplers,
                     struct ilo_cp *cp)
{
   const uint8_t cmd_len = 7;
   const uint32_t dw0 = GEN6_RENDER_CMD(3D, 3DSTATE_GS) | (cmd_len - 2);
   const struct ilo_shader_cso *cso;
   uint32_t dw2, dw4, dw5;

   ILO_GPE_VALID_GEN(dev, 7, 7.5);

   if (!gs) {
      ilo_cp_begin(cp, cmd_len);
      ilo_cp_write(cp, dw0);
      ilo_cp_write(cp, 0);
      ilo_cp_write(cp, 0);
      ilo_cp_write(cp, 0);
      ilo_cp_write(cp, 0);
      ilo_cp_write(cp, GEN7_GS_DW5_STATISTICS);
      ilo_cp_write(cp, 0);
      ilo_cp_end(cp);
      return;
   }

   cso = ilo_shader_get_kernel_cso(gs);
   dw2 = cso->payload[0];
   dw4 = cso->payload[1];
   dw5 = cso->payload[2];

   dw2 |= ((num_samplers + 3) / 4) << GEN6_THREADDISP_SAMPLER_COUNT__SHIFT;

   ilo_cp_begin(cp, cmd_len);
   ilo_cp_write(cp, dw0);
   ilo_cp_write(cp, ilo_shader_get_kernel_offset(gs));
   ilo_cp_write(cp, dw2);
   ilo_cp_write(cp, 0); /* scratch */
   ilo_cp_write(cp, dw4);
   ilo_cp_write(cp, dw5);
   ilo_cp_write(cp, 0);
   ilo_cp_end(cp);
}

static inline void
gen7_emit_3DSTATE_SF(const struct ilo_dev_info *dev,
                     const struct ilo_rasterizer_state *rasterizer,
                     enum pipe_format zs_format,
                     struct ilo_cp *cp)
{
   const uint8_t cmd_len = 7;
   const uint32_t dw0 = GEN6_RENDER_CMD(3D, 3DSTATE_SF) | (cmd_len - 2);
   const int num_samples = 1;
   uint32_t payload[6];

   ILO_GPE_VALID_GEN(dev, 7, 7.5);

   ilo_gpe_gen6_fill_3dstate_sf_raster(dev,
         rasterizer, num_samples, zs_format,
         payload, Elements(payload));

   ilo_cp_begin(cp, cmd_len);
   ilo_cp_write(cp, dw0);
   ilo_cp_write_multi(cp, payload, 6);
   ilo_cp_end(cp);
}

static inline void
gen7_emit_3DSTATE_WM(const struct ilo_dev_info *dev,
                     const struct ilo_shader_state *fs,
                     const struct ilo_rasterizer_state *rasterizer,
                     bool cc_may_kill, uint32_t hiz_op,
                     struct ilo_cp *cp)
{
   const uint8_t cmd_len = 3;
   const uint32_t dw0 = GEN6_RENDER_CMD(3D, 3DSTATE_WM) | (cmd_len - 2);
   const int num_samples = 1;
   uint32_t dw1, dw2;

   ILO_GPE_VALID_GEN(dev, 7, 7.5);

   /* see ilo_gpe_init_rasterizer_wm() */
   if (rasterizer) {
      dw1 = rasterizer->wm.payload[0];
      dw2 = rasterizer->wm.payload[1];

      assert(!hiz_op);
      dw1 |= GEN7_WM_DW1_STATISTICS;
   }
   else {
      dw1 = hiz_op;
      dw2 = 0;
   }

   if (fs) {
      const struct ilo_shader_cso *fs_cso = ilo_shader_get_kernel_cso(fs);

      dw1 |= fs_cso->payload[3];
   }

   if (cc_may_kill)
      dw1 |= GEN7_WM_DW1_PS_ENABLE | GEN7_WM_DW1_PS_KILL;

   if (num_samples > 1) {
      dw1 |= rasterizer->wm.dw_msaa_rast;
      dw2 |= rasterizer->wm.dw_msaa_disp;
   }

   ilo_cp_begin(cp, cmd_len);
   ilo_cp_write(cp, dw0);
   ilo_cp_write(cp, dw1);
   ilo_cp_write(cp, dw2);
   ilo_cp_end(cp);
}

static inline void
gen7_emit_3dstate_constant(const struct ilo_dev_info *dev,
                           int subop,
                           const uint32_t *bufs, const int *sizes,
                           int num_bufs,
                           struct ilo_cp *cp)
{
   const uint8_t cmd_len = 7;
   const uint32_t dw0 = GEN6_RENDER_TYPE_RENDER |
                        GEN6_RENDER_SUBTYPE_3D |
                        subop | (cmd_len - 2);
   uint32_t dw[6];
   int total_read_length, i;

   ILO_GPE_VALID_GEN(dev, 7, 7.5);

   /* VS, HS, DS, GS, and PS variants */
   assert(subop >= GEN6_RENDER_OPCODE_3DSTATE_CONSTANT_VS &&
          subop <= GEN7_RENDER_OPCODE_3DSTATE_CONSTANT_DS &&
          subop != GEN6_RENDER_OPCODE_3DSTATE_SAMPLE_MASK);

   assert(num_bufs <= 4);

   dw[0] = 0;
   dw[1] = 0;

   total_read_length = 0;
   for (i = 0; i < 4; i++) {
      int read_len;

      /*
       * From the Ivy Bridge PRM, volume 2 part 1, page 112:
       *
       *     "Constant buffers must be enabled in order from Constant Buffer 0
       *      to Constant Buffer 3 within this command.  For example, it is
       *      not allowed to enable Constant Buffer 1 by programming a
       *      non-zero value in the VS Constant Buffer 1 Read Length without a
       *      non-zero value in VS Constant Buffer 0 Read Length."
       */
      if (i >= num_bufs || !sizes[i]) {
         for (; i < 4; i++) {
            assert(i >= num_bufs || !sizes[i]);
            dw[2 + i] = 0;
         }
         break;
      }

      /* read lengths are in 256-bit units */
      read_len = (sizes[i] + 31) / 32;
      /* the lower 5 bits are used for memory object control state */
      assert(bufs[i] % 32 == 0);

      dw[i / 2] |= read_len << ((i % 2) ? 16 : 0);
      dw[2 + i] = bufs[i];

      total_read_length += read_len;
   }

   /*
    * From the Ivy Bridge PRM, volume 2 part 1, page 113:
    *
    *     "The sum of all four read length fields must be less than or equal
    *      to the size of 64"
    */
   assert(total_read_length <= 64);

   ilo_cp_begin(cp, cmd_len);
   ilo_cp_write(cp, dw0);
   ilo_cp_write_multi(cp, dw, 6);
   ilo_cp_end(cp);
}

static inline void
gen7_emit_3DSTATE_CONSTANT_VS(const struct ilo_dev_info *dev,
                              const uint32_t *bufs, const int *sizes,
                              int num_bufs,
                              struct ilo_cp *cp)
{
   gen7_emit_3dstate_constant(dev, GEN6_RENDER_OPCODE_3DSTATE_CONSTANT_VS,
         bufs, sizes, num_bufs, cp);
}

static inline void
gen7_emit_3DSTATE_CONSTANT_GS(const struct ilo_dev_info *dev,
                              const uint32_t *bufs, const int *sizes,
                              int num_bufs,
                              struct ilo_cp *cp)
{
   gen7_emit_3dstate_constant(dev, GEN6_RENDER_OPCODE_3DSTATE_CONSTANT_GS,
         bufs, sizes, num_bufs, cp);
}

static inline void
gen7_emit_3DSTATE_CONSTANT_PS(const struct ilo_dev_info *dev,
                              const uint32_t *bufs, const int *sizes,
                              int num_bufs,
                              struct ilo_cp *cp)
{
   gen7_emit_3dstate_constant(dev, GEN6_RENDER_OPCODE_3DSTATE_CONSTANT_PS,
         bufs, sizes, num_bufs, cp);
}

static inline void
gen7_emit_3DSTATE_SAMPLE_MASK(const struct ilo_dev_info *dev,
                              unsigned sample_mask,
                              int num_samples,
                              struct ilo_cp *cp)
{
   const uint8_t cmd_len = 2;
   const unsigned valid_mask = ((1 << num_samples) - 1) | 0x1;
   const uint32_t dw0 = GEN6_RENDER_CMD(3D, 3DSTATE_SAMPLE_MASK) |
                        (cmd_len - 2);

   ILO_GPE_VALID_GEN(dev, 7, 7.5);

   /*
    * From the Ivy Bridge PRM, volume 2 part 1, page 294:
    *
    *     "If Number of Multisamples is NUMSAMPLES_1, bits 7:1 of this field
    *      (Sample Mask) must be zero.
    *
    *      If Number of Multisamples is NUMSAMPLES_4, bits 7:4 of this field
    *      must be zero."
    */
   sample_mask &= valid_mask;

   ilo_cp_begin(cp, cmd_len);
   ilo_cp_write(cp, dw0);
   ilo_cp_write(cp, sample_mask);
   ilo_cp_end(cp);
}

static inline void
gen7_emit_3DSTATE_CONSTANT_HS(const struct ilo_dev_info *dev,
                              const uint32_t *bufs, const int *sizes,
                              int num_bufs,
                              struct ilo_cp *cp)
{
   gen7_emit_3dstate_constant(dev, GEN7_RENDER_OPCODE_3DSTATE_CONSTANT_HS,
         bufs, sizes, num_bufs, cp);
}

static inline void
gen7_emit_3DSTATE_CONSTANT_DS(const struct ilo_dev_info *dev,
                              const uint32_t *bufs, const int *sizes,
                              int num_bufs,
                              struct ilo_cp *cp)
{
   gen7_emit_3dstate_constant(dev, GEN7_RENDER_OPCODE_3DSTATE_CONSTANT_DS,
         bufs, sizes, num_bufs, cp);
}

static inline void
gen7_emit_3DSTATE_HS(const struct ilo_dev_info *dev,
                     const struct ilo_shader_state *hs,
                     int num_samplers,
                     struct ilo_cp *cp)
{
   const uint8_t cmd_len = 7;
   const uint32_t dw0 = GEN7_RENDER_CMD(3D, 3DSTATE_HS) | (cmd_len - 2);

   ILO_GPE_VALID_GEN(dev, 7, 7.5);

   assert(!hs);

   ilo_cp_begin(cp, cmd_len);
   ilo_cp_write(cp, dw0);
   ilo_cp_write(cp, 0);
   ilo_cp_write(cp, 0);
   ilo_cp_write(cp, 0);
   ilo_cp_write(cp, 0);
   ilo_cp_write(cp, 0);
   ilo_cp_write(cp, 0);
   ilo_cp_end(cp);
}

static inline void
gen7_emit_3DSTATE_TE(const struct ilo_dev_info *dev,
                     struct ilo_cp *cp)
{
   const uint8_t cmd_len = 4;
   const uint32_t dw0 = GEN7_RENDER_CMD(3D, 3DSTATE_TE) | (cmd_len - 2);

   ILO_GPE_VALID_GEN(dev, 7, 7.5);

   ilo_cp_begin(cp, cmd_len);
   ilo_cp_write(cp, dw0);
   ilo_cp_write(cp, 0);
   ilo_cp_write(cp, 0);
   ilo_cp_write(cp, 0);
   ilo_cp_end(cp);
}

static inline void
gen7_emit_3DSTATE_DS(const struct ilo_dev_info *dev,
                     const struct ilo_shader_state *ds,
                     int num_samplers,
                     struct ilo_cp *cp)
{
   const uint8_t cmd_len = 6;
   const uint32_t dw0 = GEN7_RENDER_CMD(3D, 3DSTATE_DS) | (cmd_len - 2);

   ILO_GPE_VALID_GEN(dev, 7, 7.5);

   assert(!ds);

   ilo_cp_begin(cp, cmd_len);
   ilo_cp_write(cp, dw0);
   ilo_cp_write(cp, 0);
   ilo_cp_write(cp, 0);
   ilo_cp_write(cp, 0);
   ilo_cp_write(cp, 0);
   ilo_cp_write(cp, 0);
   ilo_cp_end(cp);

}

static inline void
gen7_emit_3DSTATE_STREAMOUT(const struct ilo_dev_info *dev,
                            unsigned buffer_mask,
                            int vertex_attrib_count,
                            bool rasterizer_discard,
                            struct ilo_cp *cp)
{
   const uint8_t cmd_len = 3;
   const bool enable = (buffer_mask != 0);
   const uint32_t dw0 = GEN7_RENDER_CMD(3D, 3DSTATE_STREAMOUT) |
                        (cmd_len - 2);
   uint32_t dw1, dw2;
   int read_len;

   ILO_GPE_VALID_GEN(dev, 7, 7.5);

   if (!enable) {
      dw1 = 0 << GEN7_SO_DW1_RENDER_STREAM_SELECT__SHIFT;
      if (rasterizer_discard)
         dw1 |= GEN7_SO_DW1_RENDER_DISABLE;

      dw2 = 0;

      ilo_cp_begin(cp, cmd_len);
      ilo_cp_write(cp, dw0);
      ilo_cp_write(cp, dw1);
      ilo_cp_write(cp, dw2);
      ilo_cp_end(cp);
      return;
   }

   read_len = (vertex_attrib_count + 1) / 2;
   if (!read_len)
      read_len = 1;

   dw1 = GEN7_SO_DW1_SO_ENABLE |
         0 << GEN7_SO_DW1_RENDER_STREAM_SELECT__SHIFT |
         GEN7_SO_DW1_STATISTICS |
         buffer_mask << 8;

   if (rasterizer_discard)
      dw1 |= GEN7_SO_DW1_RENDER_DISABLE;

   /* API_OPENGL */
   if (true)
      dw1 |= GEN7_SO_DW1_REORDER_TRAILING;

   dw2 = 0 << GEN7_SO_DW2_STREAM3_READ_OFFSET__SHIFT |
         0 << GEN7_SO_DW2_STREAM3_READ_LEN__SHIFT |
         0 << GEN7_SO_DW2_STREAM2_READ_OFFSET__SHIFT |
         0 << GEN7_SO_DW2_STREAM2_READ_LEN__SHIFT |
         0 << GEN7_SO_DW2_STREAM1_READ_OFFSET__SHIFT |
         0 << GEN7_SO_DW2_STREAM1_READ_LEN__SHIFT |
         0 << GEN7_SO_DW2_STREAM0_READ_OFFSET__SHIFT |
         (read_len - 1) << GEN7_SO_DW2_STREAM0_READ_LEN__SHIFT;

   ilo_cp_begin(cp, cmd_len);
   ilo_cp_write(cp, dw0);
   ilo_cp_write(cp, dw1);
   ilo_cp_write(cp, dw2);
   ilo_cp_end(cp);
}

static inline void
gen7_emit_3DSTATE_SBE(const struct ilo_dev_info *dev,
                      const struct ilo_rasterizer_state *rasterizer,
                      const struct ilo_shader_state *fs,
                      struct ilo_cp *cp)
{
   const uint8_t cmd_len = 14;
   const uint32_t dw0 = GEN7_RENDER_CMD(3D, 3DSTATE_SBE) | (cmd_len - 2);
   uint32_t dw[13];

   ILO_GPE_VALID_GEN(dev, 7, 7.5);

   ilo_gpe_gen6_fill_3dstate_sf_sbe(dev, rasterizer, fs, dw, Elements(dw));

   ilo_cp_begin(cp, cmd_len);
   ilo_cp_write(cp, dw0);
   ilo_cp_write_multi(cp, dw, 13);
   ilo_cp_end(cp);
}

static inline void
gen7_emit_3DSTATE_PS(const struct ilo_dev_info *dev,
                     const struct ilo_shader_state *fs,
                     int num_samplers, bool dual_blend,
                     struct ilo_cp *cp)
{
   const uint8_t cmd_len = 8;
   const uint32_t dw0 = GEN7_RENDER_CMD(3D, 3DSTATE_PS) | (cmd_len - 2);
   const struct ilo_shader_cso *cso;
   uint32_t dw2, dw4, dw5;

   ILO_GPE_VALID_GEN(dev, 7, 7.5);

   if (!fs) {
      int max_threads;

      /* GPU hangs if none of the dispatch enable bits is set */
      dw4 = GEN7_PS_DW4_8_PIXEL_DISPATCH;

      /* see brwCreateContext() */
      switch (dev->gen) {
      case ILO_GEN(7.5):
         max_threads = (dev->gt == 3) ? 408 : (dev->gt == 2) ? 204 : 102;
         dw4 |= (max_threads - 1) << GEN75_PS_DW4_MAX_THREADS__SHIFT;
         break;
      case ILO_GEN(7):
      default:
         max_threads = (dev->gt == 2) ? 172 : 48;
         dw4 |= (max_threads - 1) << GEN7_PS_DW4_MAX_THREADS__SHIFT;
         break;
      }

      ilo_cp_begin(cp, cmd_len);
      ilo_cp_write(cp, dw0);
      ilo_cp_write(cp, 0);
      ilo_cp_write(cp, 0);
      ilo_cp_write(cp, 0);
      ilo_cp_write(cp, dw4);
      ilo_cp_write(cp, 0);
      ilo_cp_write(cp, 0);
      ilo_cp_write(cp, 0);
      ilo_cp_end(cp);

      return;
   }

   cso = ilo_shader_get_kernel_cso(fs);
   dw2 = cso->payload[0];
   dw4 = cso->payload[1];
   dw5 = cso->payload[2];

   dw2 |= (num_samplers + 3) / 4 << GEN6_THREADDISP_SAMPLER_COUNT__SHIFT;

   if (dual_blend)
      dw4 |= GEN7_PS_DW4_DUAL_SOURCE_BLEND;

   ilo_cp_begin(cp, cmd_len);
   ilo_cp_write(cp, dw0);
   ilo_cp_write(cp, ilo_shader_get_kernel_offset(fs));
   ilo_cp_write(cp, dw2);
   ilo_cp_write(cp, 0); /* scratch */
   ilo_cp_write(cp, dw4);
   ilo_cp_write(cp, dw5);
   ilo_cp_write(cp, 0); /* kernel 1 */
   ilo_cp_write(cp, 0); /* kernel 2 */
   ilo_cp_end(cp);
}

static inline void
gen7_emit_3DSTATE_VIEWPORT_STATE_POINTERS_SF_CLIP(const struct ilo_dev_info *dev,
                                                  uint32_t sf_clip_viewport,
                                                  struct ilo_cp *cp)
{
   gen7_emit_3dstate_pointer(dev,
         GEN7_RENDER_OPCODE_3DSTATE_VIEWPORT_STATE_POINTERS_SF_CLIP,
         sf_clip_viewport, cp);
}

static inline void
gen7_emit_3DSTATE_VIEWPORT_STATE_POINTERS_CC(const struct ilo_dev_info *dev,
                                             uint32_t cc_viewport,
                                             struct ilo_cp *cp)
{
   gen7_emit_3dstate_pointer(dev,
         GEN7_RENDER_OPCODE_3DSTATE_VIEWPORT_STATE_POINTERS_CC,
         cc_viewport, cp);
}

static inline void
gen7_emit_3DSTATE_BLEND_STATE_POINTERS(const struct ilo_dev_info *dev,
                                       uint32_t blend_state,
                                       struct ilo_cp *cp)
{
   gen7_emit_3dstate_pointer(dev,
         GEN7_RENDER_OPCODE_3DSTATE_BLEND_STATE_POINTERS,
         blend_state, cp);
}

static inline void
gen7_emit_3DSTATE_DEPTH_STENCIL_STATE_POINTERS(const struct ilo_dev_info *dev,
                                               uint32_t depth_stencil_state,
                                               struct ilo_cp *cp)
{
   gen7_emit_3dstate_pointer(dev,
         GEN7_RENDER_OPCODE_3DSTATE_DEPTH_STENCIL_STATE_POINTERS,
         depth_stencil_state, cp);
}

static inline void
gen7_emit_3DSTATE_BINDING_TABLE_POINTERS_VS(const struct ilo_dev_info *dev,
                                            uint32_t binding_table,
                                            struct ilo_cp *cp)
{
   gen7_emit_3dstate_pointer(dev,
         GEN7_RENDER_OPCODE_3DSTATE_BINDING_TABLE_POINTERS_VS,
         binding_table, cp);
}

static inline void
gen7_emit_3DSTATE_BINDING_TABLE_POINTERS_HS(const struct ilo_dev_info *dev,
                                            uint32_t binding_table,
                                            struct ilo_cp *cp)
{
   gen7_emit_3dstate_pointer(dev,
         GEN7_RENDER_OPCODE_3DSTATE_BINDING_TABLE_POINTERS_HS,
         binding_table, cp);
}

static inline void
gen7_emit_3DSTATE_BINDING_TABLE_POINTERS_DS(const struct ilo_dev_info *dev,
                                            uint32_t binding_table,
                                            struct ilo_cp *cp)
{
   gen7_emit_3dstate_pointer(dev,
         GEN7_RENDER_OPCODE_3DSTATE_BINDING_TABLE_POINTERS_DS,
         binding_table, cp);
}

static inline void
gen7_emit_3DSTATE_BINDING_TABLE_POINTERS_GS(const struct ilo_dev_info *dev,
                                            uint32_t binding_table,
                                            struct ilo_cp *cp)
{
   gen7_emit_3dstate_pointer(dev,
         GEN7_RENDER_OPCODE_3DSTATE_BINDING_TABLE_POINTERS_GS,
         binding_table, cp);
}

static inline void
gen7_emit_3DSTATE_BINDING_TABLE_POINTERS_PS(const struct ilo_dev_info *dev,
                                            uint32_t binding_table,
                                            struct ilo_cp *cp)
{
   gen7_emit_3dstate_pointer(dev,
         GEN7_RENDER_OPCODE_3DSTATE_BINDING_TABLE_POINTERS_PS,
         binding_table, cp);
}

static inline void
gen7_emit_3DSTATE_SAMPLER_STATE_POINTERS_VS(const struct ilo_dev_info *dev,
                                            uint32_t sampler_state,
                                            struct ilo_cp *cp)
{
   gen7_emit_3dstate_pointer(dev,
         GEN7_RENDER_OPCODE_3DSTATE_SAMPLER_STATE_POINTERS_VS,
         sampler_state, cp);
}

static inline void
gen7_emit_3DSTATE_SAMPLER_STATE_POINTERS_HS(const struct ilo_dev_info *dev,
                                            uint32_t sampler_state,
                                            struct ilo_cp *cp)
{
   gen7_emit_3dstate_pointer(dev,
         GEN7_RENDER_OPCODE_3DSTATE_SAMPLER_STATE_POINTERS_HS,
         sampler_state, cp);
}

static inline void
gen7_emit_3DSTATE_SAMPLER_STATE_POINTERS_DS(const struct ilo_dev_info *dev,
                                            uint32_t sampler_state,
                                            struct ilo_cp *cp)
{
   gen7_emit_3dstate_pointer(dev,
         GEN7_RENDER_OPCODE_3DSTATE_SAMPLER_STATE_POINTERS_DS,
         sampler_state, cp);
}

static inline void
gen7_emit_3DSTATE_SAMPLER_STATE_POINTERS_GS(const struct ilo_dev_info *dev,
                                            uint32_t sampler_state,
                                            struct ilo_cp *cp)
{
   gen7_emit_3dstate_pointer(dev,
         GEN7_RENDER_OPCODE_3DSTATE_SAMPLER_STATE_POINTERS_GS,
         sampler_state, cp);
}

static inline void
gen7_emit_3DSTATE_SAMPLER_STATE_POINTERS_PS(const struct ilo_dev_info *dev,
                                            uint32_t sampler_state,
                                            struct ilo_cp *cp)
{
   gen7_emit_3dstate_pointer(dev,
         GEN7_RENDER_OPCODE_3DSTATE_SAMPLER_STATE_POINTERS_PS,
         sampler_state, cp);
}

static inline void
gen7_emit_3dstate_urb(const struct ilo_dev_info *dev,
                      int subop, int offset, int size,
                      int entry_size,
                      struct ilo_cp *cp)
{
   const uint8_t cmd_len = 2;
   const uint32_t dw0 = GEN6_RENDER_TYPE_RENDER |
                        GEN6_RENDER_SUBTYPE_3D |
                        subop | (cmd_len - 2);
   const int row_size = 64; /* 512 bits */
   int alloc_size, num_entries, min_entries, max_entries;

   ILO_GPE_VALID_GEN(dev, 7, 7.5);

   /* VS, HS, DS, and GS variants */
   assert(subop >= GEN7_RENDER_OPCODE_3DSTATE_URB_VS &&
          subop <= GEN7_RENDER_OPCODE_3DSTATE_URB_GS);

   /* in multiples of 8KB */
   assert(offset % 8192 == 0);
   offset /= 8192;

   /* in multiple of 512-bit rows */
   alloc_size = (entry_size + row_size - 1) / row_size;
   if (!alloc_size)
      alloc_size = 1;

   /*
    * From the Ivy Bridge PRM, volume 2 part 1, page 34:
    *
    *     "VS URB Entry Allocation Size equal to 4(5 512-bit URB rows) may
    *      cause performance to decrease due to banking in the URB. Element
    *      sizes of 16 to 20 should be programmed with six 512-bit URB rows."
    */
   if (subop == GEN7_RENDER_OPCODE_3DSTATE_URB_VS && alloc_size == 5)
      alloc_size = 6;

   /* in multiples of 8 */
   num_entries = (size / row_size / alloc_size) & ~7;

   switch (subop) {
   case GEN7_RENDER_OPCODE_3DSTATE_URB_VS:
      switch (dev->gen) {
      case ILO_GEN(7.5):
         max_entries = (dev->gt >= 2) ? 1664 : 640;
         min_entries = (dev->gt >= 2) ? 64 : 32;
         break;
      case ILO_GEN(7):
      default:
         max_entries = (dev->gt == 2) ? 704 : 512;
         min_entries = 32;
         break;
      }

      assert(num_entries >= min_entries);
      if (num_entries > max_entries)
         num_entries = max_entries;
      break;
   case GEN7_RENDER_OPCODE_3DSTATE_URB_HS:
      max_entries = (dev->gt == 2) ? 64 : 32;
      if (num_entries > max_entries)
         num_entries = max_entries;
      break;
   case GEN7_RENDER_OPCODE_3DSTATE_URB_DS:
      if (num_entries)
         assert(num_entries >= 138);
      break;
   case GEN7_RENDER_OPCODE_3DSTATE_URB_GS:
      switch (dev->gen) {
      case ILO_GEN(7.5):
         max_entries = (dev->gt >= 2) ? 640 : 256;
         break;
      case ILO_GEN(7):
      default:
         max_entries = (dev->gt == 2) ? 320 : 192;
         break;
      }

      if (num_entries > max_entries)
         num_entries = max_entries;
      break;
   default:
      break;
   }

   ilo_cp_begin(cp, cmd_len);
   ilo_cp_write(cp, dw0);
   ilo_cp_write(cp, offset << GEN7_URB_ANY_DW1_OFFSET__SHIFT |
                    (alloc_size - 1) << GEN7_URB_ANY_DW1_ENTRY_SIZE__SHIFT |
                    num_entries);
   ilo_cp_end(cp);
}

static inline void
gen7_emit_3DSTATE_URB_VS(const struct ilo_dev_info *dev,
                         int offset, int size, int entry_size,
                         struct ilo_cp *cp)
{
   gen7_emit_3dstate_urb(dev, GEN7_RENDER_OPCODE_3DSTATE_URB_VS,
         offset, size, entry_size, cp);
}

static inline void
gen7_emit_3DSTATE_URB_HS(const struct ilo_dev_info *dev,
                         int offset, int size, int entry_size,
                         struct ilo_cp *cp)
{
   gen7_emit_3dstate_urb(dev, GEN7_RENDER_OPCODE_3DSTATE_URB_HS,
         offset, size, entry_size, cp);
}

static inline void
gen7_emit_3DSTATE_URB_DS(const struct ilo_dev_info *dev,
                         int offset, int size, int entry_size,
                         struct ilo_cp *cp)
{
   gen7_emit_3dstate_urb(dev, GEN7_RENDER_OPCODE_3DSTATE_URB_DS,
         offset, size, entry_size, cp);
}

static inline void
gen7_emit_3DSTATE_URB_GS(const struct ilo_dev_info *dev,
                         int offset, int size, int entry_size,
                         struct ilo_cp *cp)
{
   gen7_emit_3dstate_urb(dev, GEN7_RENDER_OPCODE_3DSTATE_URB_GS,
         offset, size, entry_size, cp);
}

static inline void
gen7_emit_3dstate_push_constant_alloc(const struct ilo_dev_info *dev,
                                      int subop, int offset, int size,
                                      struct ilo_cp *cp)
{
   const uint8_t cmd_len = 2;
   const uint32_t dw0 = GEN6_RENDER_TYPE_RENDER |
                        GEN6_RENDER_SUBTYPE_3D |
                        subop | (cmd_len - 2);
   int end;

   ILO_GPE_VALID_GEN(dev, 7, 7.5);

   /* VS, HS, DS, GS, and PS variants */
   assert(subop >= GEN7_RENDER_OPCODE_3DSTATE_PUSH_CONSTANT_ALLOC_VS &&
          subop <= GEN7_RENDER_OPCODE_3DSTATE_PUSH_CONSTANT_ALLOC_PS);

   /*
    * From the Ivy Bridge PRM, volume 2 part 1, page 68:
    *
    *     "(A table that says the maximum size of each constant buffer is
    *      16KB")
    *
    * From the Ivy Bridge PRM, volume 2 part 1, page 115:
    *
    *     "The sum of the Constant Buffer Offset and the Constant Buffer Size
    *      may not exceed the maximum value of the Constant Buffer Size."
    *
    * Thus, the valid range of buffer end is [0KB, 16KB].
    */
   end = (offset + size) / 1024;
   if (end > 16) {
      assert(!"invalid constant buffer end");
      end = 16;
   }

   /* the valid range of buffer offset is [0KB, 15KB] */
   offset = (offset + 1023) / 1024;
   if (offset > 15) {
      assert(!"invalid constant buffer offset");
      offset = 15;
   }

   if (offset > end) {
      assert(!size);
      offset = end;
   }

   /* the valid range of buffer size is [0KB, 15KB] */
   size = end - offset;
   if (size > 15) {
      assert(!"invalid constant buffer size");
      size = 15;
   }

   ilo_cp_begin(cp, cmd_len);
   ilo_cp_write(cp, dw0);
   ilo_cp_write(cp, offset << GEN7_PCB_ALLOC_ANY_DW1_OFFSET__SHIFT |
                    size);
   ilo_cp_end(cp);
}

static inline void
gen7_emit_3DSTATE_PUSH_CONSTANT_ALLOC_VS(const struct ilo_dev_info *dev,
                                         int offset, int size,
                                         struct ilo_cp *cp)
{
   gen7_emit_3dstate_push_constant_alloc(dev,
         GEN7_RENDER_OPCODE_3DSTATE_PUSH_CONSTANT_ALLOC_VS, offset, size, cp);
}

static inline void
gen7_emit_3DSTATE_PUSH_CONSTANT_ALLOC_HS(const struct ilo_dev_info *dev,
                                         int offset, int size,
                                         struct ilo_cp *cp)
{
   gen7_emit_3dstate_push_constant_alloc(dev,
         GEN7_RENDER_OPCODE_3DSTATE_PUSH_CONSTANT_ALLOC_HS, offset, size, cp);
}

static inline void
gen7_emit_3DSTATE_PUSH_CONSTANT_ALLOC_DS(const struct ilo_dev_info *dev,
                                         int offset, int size,
                                         struct ilo_cp *cp)
{
   gen7_emit_3dstate_push_constant_alloc(dev,
         GEN7_RENDER_OPCODE_3DSTATE_PUSH_CONSTANT_ALLOC_DS, offset, size, cp);
}

static inline void
gen7_emit_3DSTATE_PUSH_CONSTANT_ALLOC_GS(const struct ilo_dev_info *dev,
                                         int offset, int size,
                                         struct ilo_cp *cp)
{
   gen7_emit_3dstate_push_constant_alloc(dev,
         GEN7_RENDER_OPCODE_3DSTATE_PUSH_CONSTANT_ALLOC_GS, offset, size, cp);
}

static inline void
gen7_emit_3DSTATE_PUSH_CONSTANT_ALLOC_PS(const struct ilo_dev_info *dev,
                                         int offset, int size,
                                         struct ilo_cp *cp)
{
   gen7_emit_3dstate_push_constant_alloc(dev,
         GEN7_RENDER_OPCODE_3DSTATE_PUSH_CONSTANT_ALLOC_PS, offset, size, cp);
}

static inline void
gen7_emit_3DSTATE_SO_DECL_LIST(const struct ilo_dev_info *dev,
                               const struct pipe_stream_output_info *so_info,
                               struct ilo_cp *cp)
{
   uint16_t cmd_len;
   uint32_t dw0;
   int buffer_selects, num_entries, i;
   uint16_t so_decls[128];

   ILO_GPE_VALID_GEN(dev, 7, 7.5);

   buffer_selects = 0;
   num_entries = 0;

   if (so_info) {
      int buffer_offsets[PIPE_MAX_SO_BUFFERS];

      memset(buffer_offsets, 0, sizeof(buffer_offsets));

      for (i = 0; i < so_info->num_outputs; i++) {
         unsigned decl, buf, reg, mask;

         buf = so_info->output[i].output_buffer;

         /* pad with holes */
         assert(buffer_offsets[buf] <= so_info->output[i].dst_offset);
         while (buffer_offsets[buf] < so_info->output[i].dst_offset) {
            int num_dwords;

            num_dwords = so_info->output[i].dst_offset - buffer_offsets[buf];
            if (num_dwords > 4)
               num_dwords = 4;

            decl = buf << GEN7_SO_DECL_OUTPUT_SLOT__SHIFT |
                   GEN7_SO_DECL_HOLE_FLAG |
                   ((1 << num_dwords) - 1) << GEN7_SO_DECL_COMPONENT_MASK__SHIFT;

            so_decls[num_entries++] = decl;
            buffer_offsets[buf] += num_dwords;
         }

         reg = so_info->output[i].register_index;
         mask = ((1 << so_info->output[i].num_components) - 1) <<
            so_info->output[i].start_component;

         decl = buf << GEN7_SO_DECL_OUTPUT_SLOT__SHIFT |
                reg << GEN7_SO_DECL_REG_INDEX__SHIFT |
                mask << GEN7_SO_DECL_COMPONENT_MASK__SHIFT;

         so_decls[num_entries++] = decl;
         buffer_selects |= 1 << buf;
         buffer_offsets[buf] += so_info->output[i].num_components;
      }
   }

   /*
    * From the Ivy Bridge PRM, volume 2 part 1, page 201:
    *
    *     "Errata: All 128 decls for all four streams must be included
    *      whenever this command is issued. The "Num Entries [n]" fields still
    *      contain the actual numbers of valid decls."
    *
    * Also note that "DWord Length" has 9 bits for this command, and the type
    * of cmd_len is thus uint16_t.
    */
   cmd_len = 2 * 128 + 3;
   dw0 = GEN7_RENDER_CMD(3D, 3DSTATE_SO_DECL_LIST) | (cmd_len - 2);

   ilo_cp_begin(cp, cmd_len);
   ilo_cp_write(cp, dw0);
   ilo_cp_write(cp, 0 << GEN7_SO_DECL_DW1_STREAM3_BUFFER_SELECTS__SHIFT |
                    0 << GEN7_SO_DECL_DW1_STREAM2_BUFFER_SELECTS__SHIFT |
                    0 << GEN7_SO_DECL_DW1_STREAM1_BUFFER_SELECTS__SHIFT |
                    buffer_selects << GEN7_SO_DECL_DW1_STREAM0_BUFFER_SELECTS__SHIFT);
   ilo_cp_write(cp, 0 << GEN7_SO_DECL_DW2_STREAM3_ENTRY_COUNT__SHIFT |
                    0 << GEN7_SO_DECL_DW2_STREAM2_ENTRY_COUNT__SHIFT |
                    0 << GEN7_SO_DECL_DW2_STREAM1_ENTRY_COUNT__SHIFT |
                    num_entries << GEN7_SO_DECL_DW2_STREAM0_ENTRY_COUNT__SHIFT);

   for (i = 0; i < num_entries; i++) {
      ilo_cp_write(cp, so_decls[i]);
      ilo_cp_write(cp, 0);
   }
   for (; i < 128; i++) {
      ilo_cp_write(cp, 0);
      ilo_cp_write(cp, 0);
   }

   ilo_cp_end(cp);
}

static inline void
gen7_emit_3DSTATE_SO_BUFFER(const struct ilo_dev_info *dev,
                            int index, int base, int stride,
                            const struct pipe_stream_output_target *so_target,
                            struct ilo_cp *cp)
{
   const uint8_t cmd_len = 4;
   const uint32_t dw0 = GEN7_RENDER_CMD(3D, 3DSTATE_SO_BUFFER) |
                        (cmd_len - 2);
   struct ilo_buffer *buf;
   int end;

   ILO_GPE_VALID_GEN(dev, 7, 7.5);

   if (!so_target || !so_target->buffer) {
      ilo_cp_begin(cp, cmd_len);
      ilo_cp_write(cp, dw0);
      ilo_cp_write(cp, index << GEN7_SO_BUF_DW1_INDEX__SHIFT);
      ilo_cp_write(cp, 0);
      ilo_cp_write(cp, 0);
      ilo_cp_end(cp);
      return;
   }

   buf = ilo_buffer(so_target->buffer);

   /* DWord-aligned */
   assert(stride % 4 == 0 && base % 4 == 0);
   assert(so_target->buffer_offset % 4 == 0);

   stride &= ~3;
   base = (base + so_target->buffer_offset) & ~3;
   end = (base + so_target->buffer_size) & ~3;

   ilo_cp_begin(cp, cmd_len);
   ilo_cp_write(cp, dw0);
   ilo_cp_write(cp, index << GEN7_SO_BUF_DW1_INDEX__SHIFT |
                    stride);
   ilo_cp_write_bo(cp, base, buf->bo, INTEL_RELOC_WRITE);
   ilo_cp_write_bo(cp, end, buf->bo, INTEL_RELOC_WRITE);
   ilo_cp_end(cp);
}

static inline void
gen7_emit_3DPRIMITIVE(const struct ilo_dev_info *dev,
                      const struct pipe_draw_info *info,
                      const struct ilo_ib_state *ib,
                      bool rectlist,
                      struct ilo_cp *cp)
{
   const uint8_t cmd_len = 7;
   const uint32_t dw0 = GEN6_RENDER_CMD(3D, 3DPRIMITIVE) | (cmd_len - 2);
   const int prim = (rectlist) ?
      GEN6_3DPRIM_RECTLIST : ilo_gpe_gen6_translate_pipe_prim(info->mode);
   const int vb_access = (info->indexed) ?
      GEN7_3DPRIM_DW1_ACCESS_RANDOM :
      GEN7_3DPRIM_DW1_ACCESS_SEQUENTIAL;
   const uint32_t vb_start = info->start +
      ((info->indexed) ? ib->draw_start_offset : 0);

   ILO_GPE_VALID_GEN(dev, 7, 7.5);

   ilo_cp_begin(cp, cmd_len);
   ilo_cp_write(cp, dw0);
   ilo_cp_write(cp, vb_access | prim);
   ilo_cp_write(cp, info->count);
   ilo_cp_write(cp, vb_start);
   ilo_cp_write(cp, info->instance_count);
   ilo_cp_write(cp, info->start_instance);
   ilo_cp_write(cp, info->index_bias);
   ilo_cp_end(cp);
}

static inline uint32_t
gen7_emit_SF_CLIP_VIEWPORT(const struct ilo_dev_info *dev,
                           const struct ilo_viewport_cso *viewports,
                           unsigned num_viewports,
                           struct ilo_cp *cp)
{
   const int state_align = 64 / 4;
   const int state_len = 16 * num_viewports;
   uint32_t state_offset, *dw;
   unsigned i;

   ILO_GPE_VALID_GEN(dev, 7, 7.5);

   /*
    * From the Ivy Bridge PRM, volume 2 part 1, page 270:
    *
    *     "The viewport-specific state used by both the SF and CL units
    *      (SF_CLIP_VIEWPORT) is stored as an array of up to 16 elements, each
    *      of which contains the DWords described below. The start of each
    *      element is spaced 16 DWords apart. The location of first element of
    *      the array, as specified by both Pointer to SF_VIEWPORT and Pointer
    *      to CLIP_VIEWPORT, is aligned to a 64-byte boundary."
    */
   assert(num_viewports && num_viewports <= 16);

   dw = ilo_cp_steal_ptr(cp, "SF_CLIP_VIEWPORT",
         state_len, state_align, &state_offset);

   for (i = 0; i < num_viewports; i++) {
      const struct ilo_viewport_cso *vp = &viewports[i];

      dw[0] = fui(vp->m00);
      dw[1] = fui(vp->m11);
      dw[2] = fui(vp->m22);
      dw[3] = fui(vp->m30);
      dw[4] = fui(vp->m31);
      dw[5] = fui(vp->m32);
      dw[6] = 0;
      dw[7] = 0;
      dw[8] = fui(vp->min_gbx);
      dw[9] = fui(vp->max_gbx);
      dw[10] = fui(vp->min_gby);
      dw[11] = fui(vp->max_gby);
      dw[12] = 0;
      dw[13] = 0;
      dw[14] = 0;
      dw[15] = 0;

      dw += 16;
   }

   return state_offset;
}

#endif /* ILO_GPE_GEN7_H */
