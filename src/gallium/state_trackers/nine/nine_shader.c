/*
 * Copyright 2011 Joakim Sindholt <opensource@zhasha.com>
 * Copyright 2013 Christoph Bumiller
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

#include "nine_shader.h"

#include "device9.h"
#include "nine_debug.h"
#include "nine_state.h"

#include "util/u_memory.h"
#include "util/u_inlines.h"
#include "pipe/p_shader_tokens.h"
#include "tgsi/tgsi_ureg.h"
#include "tgsi/tgsi_dump.h"

#define DBG_CHANNEL DBG_SHADER

#if 1
#define NINE_TGSI_LAZY_DEVS /* don't use TGSI_OPCODE_BREAKC */
#endif
#define NINE_TGSI_LAZY_R600 /* don't use TGSI_OPCODE_DP2A */

#define DUMP(args...) _nine_debug_printf(DBG_CHANNEL, NULL, args)


struct shader_translator;

typedef HRESULT (*translate_instruction_func)(struct shader_translator *);

static INLINE const char *d3dsio_to_string(unsigned opcode);


#define NINED3D_SM1_VS 0xfffe
#define NINED3D_SM1_PS 0xffff

#define NINE_MAX_COND_DEPTH 64
#define NINE_MAX_LOOP_DEPTH 64

#define NINED3DSP_END 0x0000ffff

#define NINED3DSPTYPE_FLOAT4  0
#define NINED3DSPTYPE_INT4    1
#define NINED3DSPTYPE_BOOL    2

#define NINED3DSPR_IMMEDIATE (D3DSPR_PREDICATE + 1)

#define NINED3DSP_WRITEMASK_MASK  D3DSP_WRITEMASK_ALL
#define NINED3DSP_WRITEMASK_SHIFT 16

#define NINED3DSHADER_INST_PREDICATED (1 << 28)

#define NINED3DSHADER_REL_OP_GT 1
#define NINED3DSHADER_REL_OP_EQ 2
#define NINED3DSHADER_REL_OP_GE 3
#define NINED3DSHADER_REL_OP_LT 4
#define NINED3DSHADER_REL_OP_NE 5
#define NINED3DSHADER_REL_OP_LE 6

#define NINED3DSIO_OPCODE_FLAGS_SHIFT 16
#define NINED3DSIO_OPCODE_FLAGS_MASK  (0xff << NINED3DSIO_OPCODE_FLAGS_SHIFT)

#define NINED3DSI_TEXLD_PROJECT 0x1
#define NINED3DSI_TEXLD_BIAS    0x2

#define NINED3DSP_WRITEMASK_0   0x1
#define NINED3DSP_WRITEMASK_1   0x2
#define NINED3DSP_WRITEMASK_2   0x4
#define NINED3DSP_WRITEMASK_3   0x8
#define NINED3DSP_WRITEMASK_ALL 0xf

#define NINED3DSP_NOSWIZZLE ((0 << 0) | (1 << 2) | (2 << 4) | (3 << 6))

#define NINE_SWIZZLE4(x,y,z,w) \
   TGSI_SWIZZLE_##x, TGSI_SWIZZLE_##y, TGSI_SWIZZLE_##z, TGSI_SWIZZLE_##w

#define NINED3DSPDM_SATURATE (D3DSPDM_SATURATE >> D3DSP_DSTMOD_SHIFT)
#define NINED3DSPDM_PARTIALP (D3DSPDM_PARTIALPRECISION >> D3DSP_DSTMOD_SHIFT)
#define NINED3DSPDM_CENTROID (D3DSPDM_MSAMPCENTROID >> D3DSP_DSTMOD_SHIFT)

/*
 * NEG     all, not ps: m3x2, m3x3, m3x4, m4x3, m4x4
 * BIAS    <= PS 1.4 (x-0.5)
 * BIASNEG <= PS 1.4 (-(x-0.5))
 * SIGN    <= PS 1.4 (2(x-0.5))
 * SIGNNEG <= PS 1.4 (-2(x-0.5))
 * COMP    <= PS 1.4 (1-x)
 * X2       = PS 1.4 (2x)
 * X2NEG    = PS 1.4 (-2x)
 * DZ      <= PS 1.4, tex{ld,crd} (.xy/.z), z=0 => .11
 * DW      <= PS 1.4, tex{ld,crd} (.xy/.w), w=0 => .11
 * ABS     >= SM 3.0 (abs(x))
 * ABSNEG  >= SM 3.0 (-abs(x))
 * NOT     >= SM 2.0 pedication only
 */
#define NINED3DSPSM_NONE    (D3DSPSM_NONE    >> D3DSP_SRCMOD_SHIFT)
#define NINED3DSPSM_NEG     (D3DSPSM_NEG     >> D3DSP_SRCMOD_SHIFT)
#define NINED3DSPSM_BIAS    (D3DSPSM_BIAS    >> D3DSP_SRCMOD_SHIFT)
#define NINED3DSPSM_BIASNEG (D3DSPSM_BIASNEG >> D3DSP_SRCMOD_SHIFT)
#define NINED3DSPSM_SIGN    (D3DSPSM_SIGN    >> D3DSP_SRCMOD_SHIFT)
#define NINED3DSPSM_SIGNNEG (D3DSPSM_SIGNNEG >> D3DSP_SRCMOD_SHIFT)
#define NINED3DSPSM_COMP    (D3DSPSM_COMP    >> D3DSP_SRCMOD_SHIFT)
#define NINED3DSPSM_X2      (D3DSPSM_X2      >> D3DSP_SRCMOD_SHIFT)
#define NINED3DSPSM_X2NEG   (D3DSPSM_X2NEG   >> D3DSP_SRCMOD_SHIFT)
#define NINED3DSPSM_DZ      (D3DSPSM_DZ      >> D3DSP_SRCMOD_SHIFT)
#define NINED3DSPSM_DW      (D3DSPSM_DW      >> D3DSP_SRCMOD_SHIFT)
#define NINED3DSPSM_ABS     (D3DSPSM_ABS     >> D3DSP_SRCMOD_SHIFT)
#define NINED3DSPSM_ABSNEG  (D3DSPSM_ABSNEG  >> D3DSP_SRCMOD_SHIFT)
#define NINED3DSPSM_NOT     (D3DSPSM_NOT     >> D3DSP_SRCMOD_SHIFT)

static const char *sm1_mod_str[] =
{
    [NINED3DSPSM_NONE] = "",
    [NINED3DSPSM_NEG] = "-",
    [NINED3DSPSM_BIAS] = "bias",
    [NINED3DSPSM_BIASNEG] = "biasneg",
    [NINED3DSPSM_SIGN] = "sign",
    [NINED3DSPSM_SIGNNEG] = "signneg",
    [NINED3DSPSM_COMP] = "comp",
    [NINED3DSPSM_X2] = "x2",
    [NINED3DSPSM_X2NEG] = "x2neg",
    [NINED3DSPSM_DZ] = "dz",
    [NINED3DSPSM_DW] = "dw",
    [NINED3DSPSM_ABS] = "abs",
    [NINED3DSPSM_ABSNEG] = "-abs",
    [NINED3DSPSM_NOT] = "not"
};

static void
sm1_dump_writemask(BYTE mask)
{
    if (mask & 1) DUMP("x"); else DUMP("_");
    if (mask & 2) DUMP("y"); else DUMP("_");
    if (mask & 4) DUMP("z"); else DUMP("_");
    if (mask & 8) DUMP("w"); else DUMP("_");
}

static void
sm1_dump_swizzle(BYTE s)
{
    char c[4] = { 'x', 'y', 'z', 'w' };
    DUMP("%c%c%c%c",
         c[(s >> 0) & 3], c[(s >> 2) & 3], c[(s >> 4) & 3], c[(s >> 6) & 3]);
}

static const char sm1_file_char[] =
{
    [D3DSPR_TEMP] = 'r',
    [D3DSPR_INPUT] = 'v',
    [D3DSPR_CONST] = 'c',
    [D3DSPR_ADDR] = 'A',
    [D3DSPR_RASTOUT] = 'R',
    [D3DSPR_ATTROUT] = 'D',
    [D3DSPR_OUTPUT] = 'o',
    [D3DSPR_CONSTINT] = 'I',
    [D3DSPR_COLOROUT] = 'C',
    [D3DSPR_DEPTHOUT] = 'D',
    [D3DSPR_SAMPLER] = 's',
    [D3DSPR_CONST2] = 'c',
    [D3DSPR_CONST3] = 'c',
    [D3DSPR_CONST4] = 'c',
    [D3DSPR_CONSTBOOL] = 'B',
    [D3DSPR_LOOP] = 'L',
    [D3DSPR_TEMPFLOAT16] = 'h',
    [D3DSPR_MISCTYPE] = 'M',
    [D3DSPR_LABEL] = 'X',
    [D3DSPR_PREDICATE] = 'p'
};

static void
sm1_dump_reg(BYTE file, INT index)
{
    switch (file) {
    case D3DSPR_LOOP:
        DUMP("aL");
        break;
    case D3DSPR_COLOROUT:
        DUMP("oC%i", index);
        break;
    case D3DSPR_DEPTHOUT:
        DUMP("oDepth");
        break;
    case D3DSPR_RASTOUT:
        DUMP("oRast%i", index);
        break;
    case D3DSPR_CONSTINT:
        DUMP("iconst[%i]", index);
        break;
    case D3DSPR_CONSTBOOL:
        DUMP("bconst[%i]", index);
        break;
    default:
        DUMP("%c%i", sm1_file_char[file], index);
        break;
    }
}

struct sm1_src_param
{
    INT idx;
    struct sm1_src_param *rel;
    BYTE file;
    BYTE swizzle;
    BYTE mod;
    BYTE type;
    union {
        DWORD d[4];
        float f[4];
        int i[4];
        BOOL b;
    } imm;
};
static void
sm1_parse_immediate(struct shader_translator *, struct sm1_src_param *);

struct sm1_dst_param
{
    INT idx;
    struct sm1_src_param *rel;
    BYTE file;
    BYTE mask;
    BYTE mod;
    BYTE shift; /* sint4 */
    BYTE type;
};

static INLINE void
assert_replicate_swizzle(const struct ureg_src *reg)
{
    assert(reg->SwizzleY == reg->SwizzleX &&
           reg->SwizzleZ == reg->SwizzleX &&
           reg->SwizzleW == reg->SwizzleX);
}

static void
sm1_dump_immediate(const struct sm1_src_param *param)
{
    switch (param->type) {
    case NINED3DSPTYPE_FLOAT4:
        DUMP("{ %f %f %f %f }",
             param->imm.f[0], param->imm.f[1],
             param->imm.f[2], param->imm.f[3]);
        break;
    case NINED3DSPTYPE_INT4:
        DUMP("{ %i %i %i %i }",
             param->imm.i[0], param->imm.i[1],
             param->imm.i[2], param->imm.i[3]);
        break;
    case NINED3DSPTYPE_BOOL:
        DUMP("%s", param->imm.b ? "TRUE" : "FALSE");
        break;
    default:
        assert(0);
        break;
    }
}

static void
sm1_dump_src_param(const struct sm1_src_param *param)
{
    if (param->file == NINED3DSPR_IMMEDIATE) {
        assert(!param->mod &&
               !param->rel &&
               param->swizzle == NINED3DSP_NOSWIZZLE);
        sm1_dump_immediate(param);
        return;
    }

    if (param->mod)
        DUMP("%s(", sm1_mod_str[param->mod]);
    if (param->rel) {
        DUMP("%c[", sm1_file_char[param->file]);
        sm1_dump_src_param(param->rel);
        DUMP("+%i]", param->idx);
    } else {
        sm1_dump_reg(param->file, param->idx);
    }
    if (param->mod)
       DUMP(")");
    if (param->swizzle != NINED3DSP_NOSWIZZLE) {
       DUMP(".");
       sm1_dump_swizzle(param->swizzle);
    }
}

static void
sm1_dump_dst_param(const struct sm1_dst_param *param)
{
   if (param->mod & NINED3DSPDM_SATURATE)
      DUMP("sat ");
   if (param->mod & NINED3DSPDM_PARTIALP)
      DUMP("pp ");
   if (param->mod & NINED3DSPDM_CENTROID)
      DUMP("centroid ");
   if (param->shift < 0)
      DUMP("/%u ", 1 << -param->shift);
   if (param->shift > 0)
      DUMP("*%u ", 1 << param->shift);

   if (param->rel) {
      DUMP("%c[", sm1_file_char[param->file]);
      sm1_dump_src_param(param->rel);
      DUMP("+%i]", param->idx);
   } else {
      sm1_dump_reg(param->file, param->idx);
   }
   if (param->mask != NINED3DSP_WRITEMASK_ALL) {
      DUMP(".");
      sm1_dump_writemask(param->mask);
   }
}

struct sm1_semantic
{
   struct sm1_dst_param reg;
   BYTE sampler_type;
   D3DDECLUSAGE usage;
   BYTE usage_idx;
};

struct sm1_op_info
{
    /* NOTE: 0 is a valid TGSI opcode, but if handler is set, this parameter
     * should be ignored completely */
    unsigned sio;
    unsigned opcode; /* TGSI_OPCODE_x */

    /* versions are still set even handler is set */
    struct {
        unsigned min;
        unsigned max;
    } vert_version, frag_version;

    /* number of regs parsed outside of special handler */
    unsigned ndst;
    unsigned nsrc;

    /* some instructions don't map perfectly, so use a special handler */
    translate_instruction_func handler;
};

struct sm1_instruction
{
    D3DSHADER_INSTRUCTION_OPCODE_TYPE opcode;
    BYTE flags;
    BOOL coissue;
    BOOL predicated;
    BYTE ndst;
    BYTE nsrc;
    struct sm1_src_param src[4];
    struct sm1_src_param src_rel[4];
    struct sm1_src_param pred;
    struct sm1_src_param dst_rel[1];
    struct sm1_dst_param dst[1];

    struct sm1_op_info *info;
};

static void
sm1_dump_instruction(struct sm1_instruction *insn, unsigned indent)
{
    unsigned i;

    /* no info stored for these: */
    if (insn->opcode == D3DSIO_DCL)
        return;
    for (i = 0; i < indent; ++i)
        DUMP("  ");

    if (insn->predicated) {
        DUMP("@");
        sm1_dump_src_param(&insn->pred);
        DUMP(" ");
    }
    DUMP("%s", d3dsio_to_string(insn->opcode));
    if (insn->flags) {
        switch (insn->opcode) {
        case D3DSIO_TEX:
            DUMP(insn->flags == NINED3DSI_TEXLD_PROJECT ? "p" : "b");
            break;
        default:
            DUMP("_%x", insn->flags);
            break;
        }
    }
    if (insn->coissue)
        DUMP("_co");
    DUMP(" ");

    for (i = 0; i < insn->ndst && i < Elements(insn->dst); ++i) {
        sm1_dump_dst_param(&insn->dst[i]);
        DUMP(" ");
    }

    for (i = 0; i < insn->nsrc && i < Elements(insn->src); ++i) {
        sm1_dump_src_param(&insn->src[i]);
        DUMP(" ");
    }
    if (insn->opcode == D3DSIO_DEF ||
        insn->opcode == D3DSIO_DEFI ||
        insn->opcode == D3DSIO_DEFB)
        sm1_dump_immediate(&insn->src[0]);

    DUMP("\n");
}

struct sm1_local_const
{
    INT idx;
    struct ureg_src reg;
    union {
        boolean b;
        float f[4];
        int32_t i[4];
    } imm;
};

struct shader_translator
{
    const DWORD *byte_code;
    const DWORD *parse;
    const DWORD *parse_next;

    struct ureg_program *ureg;

    /* shader version */
    struct {
        BYTE major;
        BYTE minor;
    } version;
    unsigned processor; /* TGSI_PROCESSOR_VERTEX/FRAMGENT */

    boolean native_integers;
    boolean inline_subroutines;
    boolean lower_preds;
    boolean want_texcoord;
    boolean shift_wpos;
    unsigned texcoord_sn;

    struct sm1_instruction insn; /* current instruction */

    struct {
        struct ureg_dst *r;
        struct ureg_dst oPos;
        struct ureg_dst oFog;
        struct ureg_dst oPts;
        struct ureg_dst oCol[4];
        struct ureg_dst o[PIPE_MAX_SHADER_OUTPUTS];
        struct ureg_dst oDepth;
        struct ureg_src v[PIPE_MAX_SHADER_INPUTS];
        struct ureg_src vPos;
        struct ureg_src vFace;
        struct ureg_src s;
        struct ureg_dst p;
        struct ureg_dst a;
        struct ureg_dst tS[8]; /* texture stage registers */
        struct ureg_dst tdst; /* scratch dst if we need extra modifiers */
        struct ureg_dst t[5]; /* scratch TEMPs */
        struct ureg_src vC[2]; /* PS color in */
        struct ureg_src vT[8]; /* PS texcoord in */
        struct ureg_dst rL[NINE_MAX_LOOP_DEPTH]; /* loop ctr */
        struct ureg_dst aL[NINE_MAX_LOOP_DEPTH]; /* loop ctr ADDR register */
    } regs;
    unsigned num_temp; /* Elements(regs.r) */
    unsigned num_scratch;
    unsigned loop_depth;
    unsigned loop_depth_max;
    unsigned cond_depth;
    unsigned loop_labels[NINE_MAX_LOOP_DEPTH];
    unsigned cond_labels[NINE_MAX_COND_DEPTH];

    unsigned *inst_labels; /* LABEL op */
    unsigned num_inst_labels;

    unsigned sampler_targets[NINE_MAX_SAMPLERS]; /* TGSI_TEXTURE_x */

    struct sm1_local_const *lconstf;
    unsigned num_lconstf;
    struct sm1_local_const lconsti[NINE_MAX_CONST_I];
    struct sm1_local_const lconstb[NINE_MAX_CONST_B];

    boolean indirect_const_access;

    struct nine_shader_info *info;

    int16_t op_info_map[D3DSIO_BREAKP + 1];
};

#define IS_VS (tx->processor == TGSI_PROCESSOR_VERTEX)
#define IS_PS (tx->processor == TGSI_PROCESSOR_FRAGMENT)

static void
sm1_read_semantic(struct shader_translator *, struct sm1_semantic *);

static void
sm1_instruction_check(const struct sm1_instruction *insn)
{
    if (insn->opcode == D3DSIO_CRS)
    {
        if (insn->dst[0].mask & NINED3DSP_WRITEMASK_3)
        {
            DBG("CRS.mask.w\n");
        }
    }
}

static boolean
tx_lconstf(struct shader_translator *tx, struct ureg_src *src, INT index)
{
   INT i;
   assert(index >= 0 && index < (NINE_MAX_CONST_F * 2));
   for (i = 0; i < tx->num_lconstf; ++i) {
      if (tx->lconstf[i].idx == index) {
         *src = tx->lconstf[i].reg;
         return TRUE;
      }
   }
   return FALSE;
}
static boolean
tx_lconsti(struct shader_translator *tx, struct ureg_src *src, INT index)
{
   assert(index >= 0 && index < NINE_MAX_CONST_I);
   if (tx->lconsti[index].idx == index)
      *src = tx->lconsti[index].reg;
   return tx->lconsti[index].idx == index;
}
static boolean
tx_lconstb(struct shader_translator *tx, struct ureg_src *src, INT index)
{
   assert(index >= 0 && index < NINE_MAX_CONST_B);
   if (tx->lconstb[index].idx == index)
      *src = tx->lconstb[index].reg;
   return tx->lconstb[index].idx == index;
}

static void
tx_set_lconstf(struct shader_translator *tx, INT index, float f[4])
{
    unsigned n;

    /* Anno1404 sets out of range constants. */
    assert(index >= 0 && index < (NINE_MAX_CONST_F * 2));
    if (index >= NINE_MAX_CONST_F)
        WARN("lconstf index %i too high, indirect access won't work\n", index);

    for (n = 0; n < tx->num_lconstf; ++n)
        if (tx->lconstf[n].idx == index)
            break;
    if (n == tx->num_lconstf) {
       if ((n % 8) == 0) {
          tx->lconstf = REALLOC(tx->lconstf,
                                (n + 0) * sizeof(tx->lconstf[0]),
                                (n + 8) * sizeof(tx->lconstf[0]));
          assert(tx->lconstf);
       }
       tx->num_lconstf++;
    }
    tx->lconstf[n].idx = index;
    tx->lconstf[n].reg = ureg_imm4f(tx->ureg, f[0], f[1], f[2], f[3]);

    memcpy(tx->lconstf[n].imm.f, f, sizeof(tx->lconstf[n].imm.f));
}
static void
tx_set_lconsti(struct shader_translator *tx, INT index, int i[4])
{
    assert(index >= 0 && index < NINE_MAX_CONST_I);
    tx->lconsti[index].idx = index;
    tx->lconsti[index].reg = tx->native_integers ?
       ureg_imm4i(tx->ureg, i[0], i[1], i[2], i[3]) :
       ureg_imm4f(tx->ureg, i[0], i[1], i[2], i[3]);
}
static void
tx_set_lconstb(struct shader_translator *tx, INT index, BOOL b)
{
    assert(index >= 0 && index < NINE_MAX_CONST_B);
    tx->lconstb[index].idx = index;
    tx->lconstb[index].reg = tx->native_integers ?
       ureg_imm1u(tx->ureg, b ? 0xffffffff : 0) :
       ureg_imm1f(tx->ureg, b ? 1.0f : 0.0f);
}

static INLINE struct ureg_dst
tx_scratch(struct shader_translator *tx)
{
    assert(tx->num_scratch < Elements(tx->regs.t));
    if (ureg_dst_is_undef(tx->regs.t[tx->num_scratch]))
        tx->regs.t[tx->num_scratch] = ureg_DECL_local_temporary(tx->ureg);
    return tx->regs.t[tx->num_scratch++];
}

static INLINE struct ureg_dst
tx_scratch_scalar(struct shader_translator *tx)
{
    return ureg_writemask(tx_scratch(tx), TGSI_WRITEMASK_X);
}

static INLINE struct ureg_src
tx_src_scalar(struct ureg_dst dst)
{
    struct ureg_src src = ureg_src(dst);
    int c = ffs(dst.WriteMask) - 1;
    if (dst.WriteMask == (1 << c))
        src = ureg_scalar(src, c);
    return src;
}

/* Need to declare all constants if indirect addressing is used,
 * otherwise we could scan the shader to determine the maximum.
 * TODO: It doesn't really matter for nv50 so I won't do the scan,
 * but radeon drivers might care, if they don't infer it from TGSI.
 */
static void
tx_decl_constants(struct shader_translator *tx)
{
    unsigned i, n = 0;

    for (i = 0; i < NINE_MAX_CONST_F; ++i)
        ureg_DECL_constant(tx->ureg, n++);
    for (i = 0; i < NINE_MAX_CONST_I; ++i)
        ureg_DECL_constant(tx->ureg, n++);
    for (i = 0; i < (NINE_MAX_CONST_B / 4); ++i)
        ureg_DECL_constant(tx->ureg, n++);
}

static INLINE void
tx_temp_alloc(struct shader_translator *tx, INT idx)
{
    assert(idx >= 0);
    if (idx >= tx->num_temp) {
       unsigned k = tx->num_temp;
       unsigned n = idx + 1;
       tx->regs.r = REALLOC(tx->regs.r,
                            k * sizeof(tx->regs.r[0]),
                            n * sizeof(tx->regs.r[0]));
       for (; k < n; ++k)
          tx->regs.r[k] = ureg_dst_undef();
       tx->num_temp = n;
    }
    if (ureg_dst_is_undef(tx->regs.r[idx]))
        tx->regs.r[idx] = ureg_DECL_temporary(tx->ureg);
}

static INLINE void
tx_addr_alloc(struct shader_translator *tx, INT idx)
{
    assert(idx == 0);
    if (ureg_dst_is_undef(tx->regs.a))
        tx->regs.a = ureg_DECL_address(tx->ureg);
}

static INLINE void
tx_pred_alloc(struct shader_translator *tx, INT idx)
{
    assert(idx == 0);
    if (ureg_dst_is_undef(tx->regs.p))
        tx->regs.p = ureg_DECL_predicate(tx->ureg);
}

static INLINE void
tx_texcoord_alloc(struct shader_translator *tx, INT idx)
{
    assert(IS_PS);
    assert(idx >= 0 && idx < Elements(tx->regs.vT));
    if (ureg_src_is_undef(tx->regs.vT[idx]))
       tx->regs.vT[idx] = ureg_DECL_fs_input(tx->ureg, tx->texcoord_sn, idx,
                                             TGSI_INTERPOLATE_PERSPECTIVE);
}

static INLINE unsigned *
tx_bgnloop(struct shader_translator *tx)
{
    tx->loop_depth++;
    if (tx->loop_depth_max < tx->loop_depth)
        tx->loop_depth_max = tx->loop_depth;
    assert(tx->loop_depth < NINE_MAX_LOOP_DEPTH);
    return &tx->loop_labels[tx->loop_depth - 1];
}

static INLINE unsigned *
tx_endloop(struct shader_translator *tx)
{
    assert(tx->loop_depth);
    tx->loop_depth--;
    ureg_fixup_label(tx->ureg, tx->loop_labels[tx->loop_depth],
                     ureg_get_instruction_number(tx->ureg));
    return &tx->loop_labels[tx->loop_depth];
}

static struct ureg_dst
tx_get_loopctr(struct shader_translator *tx)
{
    const unsigned l = tx->loop_depth - 1;

    if (!tx->loop_depth)
    {
        DBG("loop counter requested outside of loop\n");
        return ureg_dst_undef();
    }

    if (ureg_dst_is_undef(tx->regs.aL[l]))
    {
        struct ureg_dst rreg = ureg_DECL_local_temporary(tx->ureg);
        struct ureg_dst areg = ureg_DECL_address(tx->ureg);
        unsigned c;

        assert(l % 4 == 0);
        for (c = l; c < (l + 4) && c < Elements(tx->regs.aL); ++c) {
            tx->regs.rL[c] = ureg_writemask(rreg, 1 << (c & 3));
            tx->regs.aL[c] = ureg_writemask(areg, 1 << (c & 3));
        }
    }
    return tx->regs.rL[l];
}
static struct ureg_dst
tx_get_aL(struct shader_translator *tx)
{
    if (!ureg_dst_is_undef(tx_get_loopctr(tx)))
        return tx->regs.aL[tx->loop_depth - 1];
    return ureg_dst_undef();
}

static INLINE unsigned *
tx_cond(struct shader_translator *tx)
{
   assert(tx->cond_depth <= NINE_MAX_COND_DEPTH);
   tx->cond_depth++;
   return &tx->cond_labels[tx->cond_depth - 1];
}

static INLINE unsigned *
tx_elsecond(struct shader_translator *tx)
{
   assert(tx->cond_depth);
   return &tx->cond_labels[tx->cond_depth - 1];
}

static INLINE void
tx_endcond(struct shader_translator *tx)
{
   assert(tx->cond_depth);
   tx->cond_depth--;
   ureg_fixup_label(tx->ureg, tx->cond_labels[tx->cond_depth],
                    ureg_get_instruction_number(tx->ureg));
}

static INLINE struct ureg_dst
nine_ureg_dst_register(unsigned file, int index)
{
    return ureg_dst(ureg_src_register(file, index));
}

static struct ureg_src
tx_src_param(struct shader_translator *tx, const struct sm1_src_param *param)
{
    struct ureg_program *ureg = tx->ureg;
    struct ureg_src src;
    struct ureg_dst tmp;

    switch (param->file)
    {
    case D3DSPR_TEMP:
        assert(!param->rel);
        tx_temp_alloc(tx, param->idx);
        src = ureg_src(tx->regs.r[param->idx]);
        break;
 /* case D3DSPR_TEXTURE: == D3DSPR_ADDR */
    case D3DSPR_ADDR:
        assert(!param->rel);
        if (IS_VS) {
            tx_addr_alloc(tx, param->idx);
            src = ureg_src(tx->regs.a);
        } else {
            if (tx->version.major < 2 && tx->version.minor < 4) {
                /* no subroutines, so should be defined */
                src = ureg_src(tx->regs.tS[param->idx]);
            } else {
                tx_texcoord_alloc(tx, param->idx);
                src = tx->regs.vT[param->idx];
            }
        }
        break;
    case D3DSPR_INPUT:
        if (IS_VS) {
            src = ureg_src_register(TGSI_FILE_INPUT, param->idx);
        } else {
            if (tx->version.major < 3) {
                assert(!param->rel);
                src = ureg_DECL_fs_input(tx->ureg, TGSI_SEMANTIC_COLOR,
                                         param->idx,
                                         TGSI_INTERPOLATE_PERSPECTIVE);
            } else {
                assert(!param->rel); /* TODO */
                assert(param->idx < Elements(tx->regs.v));
                src = tx->regs.v[param->idx];
            }
        }
        break;
    case D3DSPR_PREDICATE:
        assert(!param->rel);
        tx_pred_alloc(tx, param->idx);
        src = ureg_src(tx->regs.p);
        break;
    case D3DSPR_SAMPLER:
        assert(param->mod == NINED3DSPSM_NONE);
        assert(param->swizzle == NINED3DSP_NOSWIZZLE);
        assert(!param->rel);
        src = ureg_src_register(TGSI_FILE_SAMPLER, param->idx);
        break;
    case D3DSPR_CONST:
        if (param->rel)
            tx->indirect_const_access = TRUE;
        if (param->rel || !tx_lconstf(tx, &src, param->idx)) {
            if (!param->rel)
                nine_info_mark_const_f_used(tx->info, param->idx);
            src = ureg_src_register(TGSI_FILE_CONSTANT, param->idx);
        }
        break;
    case D3DSPR_CONST2:
    case D3DSPR_CONST3:
    case D3DSPR_CONST4:
        DBG("CONST2/3/4 should have been collapsed into D3DSPR_CONST !\n");
        assert(!"CONST2/3/4");
        src = ureg_imm1f(ureg, 0.0f);
        break;
    case D3DSPR_CONSTINT:
        if (param->rel || !tx_lconsti(tx, &src, param->idx)) {
            if (!param->rel)
                nine_info_mark_const_i_used(tx->info, param->idx);
            src = ureg_src_register(TGSI_FILE_CONSTANT,
                                    tx->info->const_i_base + param->idx);
        }
        break;
    case D3DSPR_CONSTBOOL:
        if (param->rel || !tx_lconstb(tx, &src, param->idx)) {
           char r = param->idx / 4;
           char s = param->idx & 3;
           if (!param->rel)
               nine_info_mark_const_b_used(tx->info, param->idx);
           src = ureg_src_register(TGSI_FILE_CONSTANT,
                                   tx->info->const_b_base + r);
           src = ureg_swizzle(src, s, s, s, s);
        }
        break;
    case D3DSPR_LOOP:
        src = tx_src_scalar(tx_get_aL(tx));
        break;
    case D3DSPR_MISCTYPE:
        switch (param->idx) {
        case D3DSMO_POSITION:
           if (ureg_src_is_undef(tx->regs.vPos))
               tx->regs.vPos = ureg_DECL_fs_input(ureg,
                                                  TGSI_SEMANTIC_POSITION, 0,
                                                  TGSI_INTERPOLATE_LINEAR);
           if (tx->shift_wpos) {
               /* TODO: do this only once */
               struct ureg_dst wpos = tx_scratch(tx);
               ureg_SUB(ureg, wpos, tx->regs.vPos,
                        ureg_imm4f(ureg, 0.5f, 0.5f, 0.0f, 0.0f));
               src = ureg_src(wpos);
           } else {
               src = tx->regs.vPos;
           }
           break;
        case D3DSMO_FACE:
           if (ureg_src_is_undef(tx->regs.vFace)) {
               tx->regs.vFace = ureg_DECL_fs_input(ureg,
                                                   TGSI_SEMANTIC_FACE, 0,
                                                   TGSI_INTERPOLATE_CONSTANT);
               tx->regs.vFace = ureg_scalar(tx->regs.vFace, TGSI_SWIZZLE_X);
           }
           src = tx->regs.vFace;
           break;
        default:
            assert(!"invalid src D3DSMO");
            break;
        }
        assert(!param->rel);
        break;
    case D3DSPR_TEMPFLOAT16:
        break;
    default:
        assert(!"invalid src D3DSPR");
    }
    if (param->rel)
        src = ureg_src_indirect(src, tx_src_param(tx, param->rel));

    if (param->swizzle != NINED3DSP_NOSWIZZLE)
        src = ureg_swizzle(src,
                           (param->swizzle >> 0) & 0x3,
                           (param->swizzle >> 2) & 0x3,
                           (param->swizzle >> 4) & 0x3,
                           (param->swizzle >> 6) & 0x3);

    switch (param->mod) {
    case NINED3DSPSM_ABS:
        src = ureg_abs(src);
        break;
    case NINED3DSPSM_ABSNEG:
        src = ureg_negate(ureg_abs(src));
        break;
    case NINED3DSPSM_NEG:
        src = ureg_negate(src);
        break;
    case NINED3DSPSM_BIAS:
        tmp = tx_scratch(tx);
        ureg_SUB(ureg, tmp, src, ureg_imm1f(ureg, 0.5f));
        src = ureg_src(tmp);
        break;
    case NINED3DSPSM_BIASNEG:
        tmp = tx_scratch(tx);
        ureg_SUB(ureg, tmp, ureg_imm1f(ureg, 0.5f), src);
        src = ureg_src(tmp);
        break;
    case NINED3DSPSM_NOT:
        if (tx->native_integers) {
            tmp = tx_scratch(tx);
            ureg_NOT(ureg, tmp, src);
            src = ureg_src(tmp);
            break;
        }
        /* fall through */
    case NINED3DSPSM_COMP:
        tmp = tx_scratch(tx);
        ureg_SUB(ureg, tmp, ureg_imm1f(ureg, 1.0f), src);
        src = ureg_src(tmp);
        break;
    case NINED3DSPSM_DZ:
    case NINED3DSPSM_DW:
        /* handled in instruction */
        break;
    case NINED3DSPSM_SIGN:
        tmp = tx_scratch(tx);
        ureg_MAD(ureg, tmp, src, ureg_imm1f(ureg, 2.0f), ureg_imm1f(ureg, -1.0f));
        src = ureg_src(tmp);
        break;
    case NINED3DSPSM_SIGNNEG:
        tmp = tx_scratch(tx);
        ureg_MAD(ureg, tmp, src, ureg_imm1f(ureg, -2.0f), ureg_imm1f(ureg, 1.0f));
        src = ureg_src(tmp);
        break;
    case NINED3DSPSM_X2:
        tmp = tx_scratch(tx);
        ureg_ADD(ureg, tmp, src, src);
        src = ureg_src(tmp);
        break;
    case NINED3DSPSM_X2NEG:
        tmp = tx_scratch(tx);
        ureg_ADD(ureg, tmp, src, src);
        src = ureg_negate(ureg_src(tmp));
        break;
    default:
        assert(param->mod == NINED3DSPSM_NONE);
        break;
    }

    return src;
}

static struct ureg_dst
_tx_dst_param(struct shader_translator *tx, const struct sm1_dst_param *param)
{
    struct ureg_dst dst;

    switch (param->file)
    {
    case D3DSPR_TEMP:
        assert(!param->rel);
        tx_temp_alloc(tx, param->idx);
        dst = tx->regs.r[param->idx];
        break;
 /* case D3DSPR_TEXTURE: == D3DSPR_ADDR */
    case D3DSPR_ADDR:
        assert(!param->rel);
        if (tx->version.major < 2 && !IS_VS) {
            if (ureg_dst_is_undef(tx->regs.tS[param->idx]))
                tx->regs.tS[param->idx] = ureg_DECL_temporary(tx->ureg);
            dst = tx->regs.tS[param->idx];
        } else
        if (!IS_VS && tx->insn.opcode == D3DSIO_TEXKILL) { /* maybe others, too */
            tx_texcoord_alloc(tx, param->idx);
            dst = ureg_dst(tx->regs.vT[param->idx]);
        } else {
            tx_addr_alloc(tx, param->idx);
            dst = tx->regs.a;
        }
        break;
    case D3DSPR_RASTOUT:
        assert(!param->rel);
        switch (param->idx) {
        case 0:
            if (ureg_dst_is_undef(tx->regs.oPos))
                tx->regs.oPos =
                    ureg_DECL_output(tx->ureg, TGSI_SEMANTIC_POSITION, 0);
            dst = tx->regs.oPos;
            break;
        case 1:
            if (ureg_dst_is_undef(tx->regs.oFog))
                tx->regs.oFog =
                    ureg_DECL_output(tx->ureg, TGSI_SEMANTIC_FOG, 0);
            dst = tx->regs.oFog;
            break;
        case 2:
            if (ureg_dst_is_undef(tx->regs.oPts))
                tx->regs.oPts =
                    ureg_DECL_output(tx->ureg, TGSI_SEMANTIC_PSIZE, 0);
            dst = tx->regs.oPts;
            break;
        default:
            assert(0);
            break;
        }
        break;
 /* case D3DSPR_TEXCRDOUT: == D3DSPR_OUTPUT */
    case D3DSPR_OUTPUT:
        if (tx->version.major < 3) {
            assert(!param->rel);
            dst = ureg_DECL_output(tx->ureg, tx->texcoord_sn, param->idx);
        } else {
            assert(!param->rel); /* TODO */
            assert(param->idx < Elements(tx->regs.o));
            dst = tx->regs.o[param->idx];
        }
        break;
    case D3DSPR_ATTROUT: /* VS */
    case D3DSPR_COLOROUT: /* PS */
        assert(param->idx >= 0 && param->idx < 4);
        assert(!param->rel);
        tx->info->rt_mask |= 1 << param->idx;
        if (ureg_dst_is_undef(tx->regs.oCol[param->idx]))
            tx->regs.oCol[param->idx] =
               ureg_DECL_output(tx->ureg, TGSI_SEMANTIC_COLOR, param->idx);
        dst = tx->regs.oCol[param->idx];
        if (IS_VS && tx->version.major < 3)
            dst = ureg_saturate(dst);
        break;
    case D3DSPR_DEPTHOUT:
        assert(!param->rel);
        if (ureg_dst_is_undef(tx->regs.oDepth))
           tx->regs.oDepth =
              ureg_DECL_output_masked(tx->ureg, TGSI_SEMANTIC_POSITION, 0,
                                      TGSI_WRITEMASK_Z);
        dst = tx->regs.oDepth; /* XXX: must write .z component */
        break;
    case D3DSPR_PREDICATE:
        assert(!param->rel);
        tx_pred_alloc(tx, param->idx);
        dst = tx->regs.p;
        break;
    case D3DSPR_TEMPFLOAT16:
        DBG("unhandled D3DSPR: %u\n", param->file);
        break;
    default:
        assert(!"invalid dst D3DSPR");
        break;
    }
    if (param->rel)
        dst = ureg_dst_indirect(dst, tx_src_param(tx, param->rel));

    if (param->mask != NINED3DSP_WRITEMASK_ALL)
        dst = ureg_writemask(dst, param->mask);
    if (param->mod & NINED3DSPDM_SATURATE)
        dst = ureg_saturate(dst);

    return dst;
}

static struct ureg_dst
tx_dst_param(struct shader_translator *tx, const struct sm1_dst_param *param)
{
    if (param->shift) {
        tx->regs.tdst = ureg_writemask(tx_scratch(tx), param->mask);
        return tx->regs.tdst;
    }
    return _tx_dst_param(tx, param);
}

static void
tx_apply_dst0_modifiers(struct shader_translator *tx)
{
    struct ureg_dst rdst;
    float f;

    if (!tx->insn.ndst || !tx->insn.dst[0].shift || tx->insn.opcode == D3DSIO_TEXKILL)
        return;
    rdst = _tx_dst_param(tx, &tx->insn.dst[0]);

    assert(rdst.File != TGSI_FILE_ADDRESS); /* this probably isn't possible */

    if (tx->insn.dst[0].shift < 0)
        f = 1.0f / (1 << -tx->insn.dst[0].shift);
    else
        f = 1 << tx->insn.dst[0].shift;

    ureg_MUL(tx->ureg, rdst, ureg_src(tx->regs.tdst), ureg_imm1f(tx->ureg, f));
}

static struct ureg_src
tx_dst_param_as_src(struct shader_translator *tx, const struct sm1_dst_param *param)
{
    struct ureg_src src;

    assert(!param->shift);
    assert(!(param->mod & NINED3DSPDM_SATURATE));

    switch (param->file) {
    case D3DSPR_INPUT:
        if (IS_VS) {
            src = ureg_src_register(TGSI_FILE_INPUT, param->idx);
        } else {
            assert(!param->rel);
            assert(param->idx < Elements(tx->regs.v));
            src = tx->regs.v[param->idx];
        }
        break;
    default:
        src = ureg_src(tx_dst_param(tx, param));
        break;
    }
    if (param->rel)
        src = ureg_src_indirect(src, tx_src_param(tx, param->rel));

    if (!param->mask)
        WARN("mask is 0, using identity swizzle\n");

    if (param->mask && param->mask != NINED3DSP_WRITEMASK_ALL) {
        char s[4];
        int n;
        int c;
        for (n = 0, c = 0; c < 4; ++c)
            if (param->mask & (1 << c))
                s[n++] = c;
        assert(n);
        for (c = n; c < 4; ++c)
            s[c] = s[n - 1];
        src = ureg_swizzle(src, s[0], s[1], s[2], s[3]);
    }
    return src;
}

static HRESULT
NineTranslateInstruction_Mkxn(struct shader_translator *tx, const unsigned k, const unsigned n)
{
    struct ureg_program *ureg = tx->ureg;
    struct ureg_dst dst;
    struct ureg_src src[2];
    unsigned i;

    dst = tx_dst_param(tx, &tx->insn.dst[0]);
    src[0] = tx_src_param(tx, &tx->insn.src[0]);
    src[1] = tx_src_param(tx, &tx->insn.src[1]);

    for (i = 0; i < n; i++, src[1].Index++)
    {
        const unsigned m = (1 << i);

        if (!(dst.WriteMask & m))
            continue;

        /* XXX: src == dst case ? */

        switch (k) {
        case 3:
            ureg_DP3(ureg, ureg_writemask(dst, m), src[0], src[1]);
            break;
        case 4:
            ureg_DP4(ureg, ureg_writemask(dst, m), src[0], src[1]);
            break;
        default:
            DBG("invalid operation: M%ux%u\n", m, n);
            break;
        }
    }

    return D3D_OK;
}

#define VNOTSUPPORTED   0, 0
#define V(maj, min)     (((maj) << 8) | (min))

static INLINE const char *
d3dsio_to_string( unsigned opcode )
{
    static const char *names[] = {
        "NOP",
        "MOV",
        "ADD",
        "SUB",
        "MAD",
        "MUL",
        "RCP",
        "RSQ",
        "DP3",
        "DP4",
        "MIN",
        "MAX",
        "SLT",
        "SGE",
        "EXP",
        "LOG",
        "LIT",
        "DST",
        "LRP",
        "FRC",
        "M4x4",
        "M4x3",
        "M3x4",
        "M3x3",
        "M3x2",
        "CALL",
        "CALLNZ",
        "LOOP",
        "RET",
        "ENDLOOP",
        "LABEL",
        "DCL",
        "POW",
        "CRS",
        "SGN",
        "ABS",
        "NRM",
        "SINCOS",
        "REP",
        "ENDREP",
        "IF",
        "IFC",
        "ELSE",
        "ENDIF",
        "BREAK",
        "BREAKC",
        "MOVA",
        "DEFB",
        "DEFI",
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        "TEXCOORD",
        "TEXKILL",
        "TEX",
        "TEXBEM",
        "TEXBEML",
        "TEXREG2AR",
        "TEXREG2GB",
        "TEXM3x2PAD",
        "TEXM3x2TEX",
        "TEXM3x3PAD",
        "TEXM3x3TEX",
        NULL,
        "TEXM3x3SPEC",
        "TEXM3x3VSPEC",
        "EXPP",
        "LOGP",
        "CND",
        "DEF",
        "TEXREG2RGB",
        "TEXDP3TEX",
        "TEXM3x2DEPTH",
        "TEXDP3",
        "TEXM3x3",
        "TEXDEPTH",
        "CMP",
        "BEM",
        "DP2ADD",
        "DSX",
        "DSY",
        "TEXLDD",
        "SETP",
        "TEXLDL",
        "BREAKP"
    };

    if (opcode < Elements(names)) return names[opcode];

    switch (opcode) {
    case D3DSIO_PHASE: return "PHASE";
    case D3DSIO_COMMENT: return "COMMENT";
    case D3DSIO_END: return "END";
    default:
        return NULL;
    }
}

#define NULL_INSTRUCTION            { 0, { 0, 0 }, { 0, 0 }, 0, 0, NULL }
#define IS_VALID_INSTRUCTION(inst)  ((inst).vert_version.min | \
                                     (inst).vert_version.max | \
                                     (inst).frag_version.min | \
                                     (inst).frag_version.max)

#define SPECIAL(name) \
    NineTranslateInstruction_##name

#define DECL_SPECIAL(name) \
    static HRESULT \
    NineTranslateInstruction_##name( struct shader_translator *tx )

static HRESULT
NineTranslateInstruction_Generic(struct shader_translator *);

DECL_SPECIAL(M4x4)
{
    return NineTranslateInstruction_Mkxn(tx, 4, 3);
}

DECL_SPECIAL(M4x3)
{
    return NineTranslateInstruction_Mkxn(tx, 4, 3);
}

DECL_SPECIAL(M3x4)
{
    return NineTranslateInstruction_Mkxn(tx, 3, 4);
}

DECL_SPECIAL(M3x3)
{
    return NineTranslateInstruction_Mkxn(tx, 3, 3);
}

DECL_SPECIAL(M3x2)
{
    return NineTranslateInstruction_Mkxn(tx, 3, 2);
}

DECL_SPECIAL(CMP)
{
    ureg_CMP(tx->ureg, tx_dst_param(tx, &tx->insn.dst[0]),
             tx_src_param(tx, &tx->insn.src[0]),
             tx_src_param(tx, &tx->insn.src[2]),
             tx_src_param(tx, &tx->insn.src[1]));
    return D3D_OK;
}

DECL_SPECIAL(CND)
{
    struct ureg_dst dst = tx_dst_param(tx, &tx->insn.dst[0]);
    struct ureg_dst cgt;
    struct ureg_src cnd;

    if (tx->insn.coissue && tx->version.major == 1 && tx->version.minor < 4) {
        ureg_MOV(tx->ureg,
                 dst, tx_src_param(tx, &tx->insn.src[1]));
        return D3D_OK;
    }

    cnd = tx_src_param(tx, &tx->insn.src[0]);
#ifdef NINE_TGSI_LAZY_R600
    cgt = tx_scratch(tx);

    if (tx->version.major == 1 && tx->version.minor < 4) {
        cgt.WriteMask = TGSI_WRITEMASK_W;
        ureg_SGT(tx->ureg, cgt, cnd, ureg_imm1f(tx->ureg, 0.5f));
        cnd = ureg_scalar(cnd, TGSI_SWIZZLE_W);
    } else {
        ureg_SGT(tx->ureg, cgt, cnd, ureg_imm1f(tx->ureg, 0.5f));
    }
    ureg_CMP(tx->ureg, dst,
             tx_src_param(tx, &tx->insn.src[1]),
             tx_src_param(tx, &tx->insn.src[2]), ureg_negate(cnd));
#else
    if (tx->version.major == 1 && tx->version.minor < 4)
        cnd = ureg_scalar(cnd, TGSI_SWIZZLE_W);
    ureg_CND(tx->ureg, dst,
             tx_src_param(tx, &tx->insn.src[1]),
             tx_src_param(tx, &tx->insn.src[2]), cnd);
#endif
    return D3D_OK;
}

DECL_SPECIAL(CALL)
{
    assert(tx->insn.src[0].idx < tx->num_inst_labels);
    ureg_CAL(tx->ureg, &tx->inst_labels[tx->insn.src[0].idx]);
    return D3D_OK;
}

DECL_SPECIAL(CALLNZ)
{
    struct ureg_program *ureg = tx->ureg;
    struct ureg_dst tmp = tx_scratch_scalar(tx);
    struct ureg_src src = tx_src_param(tx, &tx->insn.src[1]);

    /* NOTE: source should be const bool, so we can use NOT/SUB instead of [U]SNE 0 */
    if (!tx->insn.flags) {
        if (tx->native_integers)
            ureg_NOT(ureg, tmp, src);
        else
            ureg_SUB(ureg, tmp, ureg_imm1f(ureg, 1.0f), src);
    }
    ureg_IF(ureg, tx->insn.flags ? src : tx_src_scalar(tmp), tx_cond(tx));
    ureg_CAL(ureg, &tx->inst_labels[tx->insn.src[0].idx]);
    tx_endcond(tx);
    ureg_ENDIF(ureg);
    return D3D_OK;
}

DECL_SPECIAL(MOV_vs1x)
{
    if (tx->insn.dst[0].file == D3DSPR_ADDR) {
        ureg_ARL(tx->ureg,
                 tx_dst_param(tx, &tx->insn.dst[0]),
                 tx_src_param(tx, &tx->insn.src[0]));
        return D3D_OK;
    }
    return NineTranslateInstruction_Generic(tx);
}

DECL_SPECIAL(LOOP)
{
    struct ureg_program *ureg = tx->ureg;
    unsigned *label;
    struct ureg_src src = tx_src_param(tx, &tx->insn.src[1]);
    struct ureg_src iter = ureg_scalar(src, TGSI_SWIZZLE_X);
    struct ureg_src init = ureg_scalar(src, TGSI_SWIZZLE_Y);
    struct ureg_src step = ureg_scalar(src, TGSI_SWIZZLE_Z);
    struct ureg_dst ctr;
    struct ureg_dst tmp = tx_scratch_scalar(tx);

    label = tx_bgnloop(tx);
    ctr = tx_get_loopctr(tx);

    ureg_MOV(tx->ureg, ctr, init);
    ureg_BGNLOOP(tx->ureg, label);
    if (tx->native_integers) {
        /* we'll let the backend pull up that MAD ... */
        ureg_UMAD(ureg, tmp, iter, step, init);
        ureg_USEQ(ureg, tmp, ureg_src(ctr), tx_src_scalar(tmp));
#ifdef NINE_TGSI_LAZY_DEVS
        ureg_UIF(ureg, tx_src_scalar(tmp), tx_cond(tx));
#endif
    } else {
        /* can't simply use SGE for precision because step might be negative */
        ureg_MAD(ureg, tmp, iter, step, init);
        ureg_SEQ(ureg, tmp, ureg_src(ctr), tx_src_scalar(tmp));
#ifdef NINE_TGSI_LAZY_DEVS
        ureg_IF(ureg, tx_src_scalar(tmp), tx_cond(tx));
#endif
    }
#ifdef NINE_TGSI_LAZY_DEVS
    ureg_BRK(ureg);
    tx_endcond(tx);
    ureg_ENDIF(ureg);
#else
    ureg_BREAKC(ureg, tx_src_scalar(tmp));
#endif
    if (tx->native_integers) {
        ureg_UARL(ureg, tx_get_aL(tx), tx_src_scalar(ctr));
        ureg_UADD(ureg, ctr, tx_src_scalar(ctr), step);
    } else {
        ureg_ARL(ureg, tx_get_aL(tx), tx_src_scalar(ctr));
        ureg_ADD(ureg, ctr, tx_src_scalar(ctr), step);
    }
    return D3D_OK;
}

DECL_SPECIAL(RET)
{
    ureg_RET(tx->ureg);
    return D3D_OK;
}

DECL_SPECIAL(ENDLOOP)
{
    ureg_ENDLOOP(tx->ureg, tx_endloop(tx));
    return D3D_OK;
}

DECL_SPECIAL(LABEL)
{
    unsigned k = tx->num_inst_labels;
    unsigned n = tx->insn.src[0].idx;
    assert(n < 2048);
    if (n >= k)
       tx->inst_labels = REALLOC(tx->inst_labels,
                                 k * sizeof(tx->inst_labels[0]),
                                 n * sizeof(tx->inst_labels[0]));

    tx->inst_labels[n] = ureg_get_instruction_number(tx->ureg);
    return D3D_OK;
}

DECL_SPECIAL(SINCOS)
{
    struct ureg_dst dst = tx_dst_param(tx, &tx->insn.dst[0]);
    struct ureg_src src = tx_src_param(tx, &tx->insn.src[0]);

    assert(!(dst.WriteMask & 0xc));

    dst.WriteMask &= TGSI_WRITEMASK_XY; /* z undefined, w untouched */
    ureg_SCS(tx->ureg, dst, src);
    return D3D_OK;
}

DECL_SPECIAL(SGN)
{
    ureg_SSG(tx->ureg,
             tx_dst_param(tx, &tx->insn.dst[0]),
             tx_src_param(tx, &tx->insn.src[0]));
    return D3D_OK;
}

DECL_SPECIAL(REP)
{
    struct ureg_program *ureg = tx->ureg;
    unsigned *label;
    struct ureg_src rep = tx_src_param(tx, &tx->insn.src[0]);
    struct ureg_dst ctr;
    struct ureg_dst tmp = tx_scratch_scalar(tx);
    struct ureg_src imm =
        tx->native_integers ? ureg_imm1u(ureg, 0) : ureg_imm1f(ureg, 0.0f);

    label = tx_bgnloop(tx);
    ctr = tx_get_loopctr(tx);

    /* NOTE: rep must be constant, so we don't have to save the count */
    assert(rep.File == TGSI_FILE_CONSTANT || rep.File == TGSI_FILE_IMMEDIATE);

    ureg_MOV(ureg, ctr, imm);
    ureg_BGNLOOP(ureg, label);
    if (tx->native_integers)
    {
        ureg_USGE(ureg, tmp, tx_src_scalar(ctr), rep);
#ifdef NINE_TGSI_LAZY_DEVS
        ureg_UIF(ureg, tx_src_scalar(tmp), tx_cond(tx));
#endif
    }
    else
    {
        ureg_SGE(ureg, tmp, tx_src_scalar(ctr), rep);
#ifdef NINE_TGSI_LAZY_DEVS
        ureg_IF(ureg, tx_src_scalar(tmp), tx_cond(tx));
#endif
    }
#ifdef NINE_TGSI_LAZY_DEVS
    ureg_BRK(ureg);
    tx_endcond(tx);
    ureg_ENDIF(ureg);
#else
    ureg_BREAKC(ureg, tx_src_scalar(tmp));
#endif

    if (tx->native_integers) {
        ureg_UADD(ureg, ctr, tx_src_scalar(ctr), ureg_imm1u(ureg, 1));
    } else {
        ureg_ADD(ureg, ctr, tx_src_scalar(ctr), ureg_imm1f(ureg, 1.0f));
    }

    return D3D_OK;
}

DECL_SPECIAL(ENDREP)
{
    ureg_ENDLOOP(tx->ureg, tx_endloop(tx));
    return D3D_OK;
}

DECL_SPECIAL(ENDIF)
{
    tx_endcond(tx);
    ureg_ENDIF(tx->ureg);
    return D3D_OK;
}

DECL_SPECIAL(IF)
{
    struct ureg_src src = tx_src_param(tx, &tx->insn.src[0]);

    if (tx->native_integers && tx->insn.src[0].file == D3DSPR_CONSTBOOL)
        ureg_UIF(tx->ureg, src, tx_cond(tx));
    else
        ureg_IF(tx->ureg, src, tx_cond(tx));

    return D3D_OK;
}

static INLINE unsigned
sm1_insn_flags_to_tgsi_setop(BYTE flags)
{
    switch (flags) {
    case NINED3DSHADER_REL_OP_GT: return TGSI_OPCODE_SGT;
    case NINED3DSHADER_REL_OP_EQ: return TGSI_OPCODE_SEQ;
    case NINED3DSHADER_REL_OP_GE: return TGSI_OPCODE_SGE;
    case NINED3DSHADER_REL_OP_LT: return TGSI_OPCODE_SLT;
    case NINED3DSHADER_REL_OP_NE: return TGSI_OPCODE_SNE;
    case NINED3DSHADER_REL_OP_LE: return TGSI_OPCODE_SLE;
    default:
        assert(!"invalid comparison flags");
        return TGSI_OPCODE_SFL;
    }
}

DECL_SPECIAL(IFC)
{
    const unsigned cmp_op = sm1_insn_flags_to_tgsi_setop(tx->insn.flags);
    struct ureg_src src[2];
    struct ureg_dst tmp = ureg_writemask(tx_scratch(tx), TGSI_WRITEMASK_X);
    src[0] = tx_src_param(tx, &tx->insn.src[0]);
    src[1] = tx_src_param(tx, &tx->insn.src[1]);
    ureg_insn(tx->ureg, cmp_op, &tmp, 1, src, 2);
    ureg_IF(tx->ureg, ureg_scalar(ureg_src(tmp), TGSI_SWIZZLE_X), tx_cond(tx));
    return D3D_OK;
}

DECL_SPECIAL(ELSE)
{
    ureg_ELSE(tx->ureg, tx_elsecond(tx));
    return D3D_OK;
}

DECL_SPECIAL(BREAKC)
{
    const unsigned cmp_op = sm1_insn_flags_to_tgsi_setop(tx->insn.flags);
    struct ureg_src src[2];
    struct ureg_dst tmp = ureg_writemask(tx_scratch(tx), TGSI_WRITEMASK_X);
    src[0] = tx_src_param(tx, &tx->insn.src[0]);
    src[1] = tx_src_param(tx, &tx->insn.src[1]);
    ureg_insn(tx->ureg, cmp_op, &tmp, 1, src, 2);
#ifdef NINE_TGSI_LAZY_DEVS
    ureg_IF(tx->ureg, ureg_scalar(ureg_src(tmp), TGSI_SWIZZLE_X), tx_cond(tx));
    ureg_BRK(tx->ureg);
    tx_endcond(tx);
    ureg_ENDIF(tx->ureg);
#else
    ureg_BREAKC(tx->ureg, ureg_scalar(ureg_src(tmp), TGSI_SWIZZLE_X));
#endif
    return D3D_OK;
}

static const char *sm1_declusage_names[] =
{
    [D3DDECLUSAGE_POSITION] = "POSITION",
    [D3DDECLUSAGE_BLENDWEIGHT] = "BLENDWEIGHT",
    [D3DDECLUSAGE_BLENDINDICES] = "BLENDINDICES",
    [D3DDECLUSAGE_NORMAL] = "NORMAL",
    [D3DDECLUSAGE_PSIZE] = "PSIZE",
    [D3DDECLUSAGE_TEXCOORD] = "TEXCOORD",
    [D3DDECLUSAGE_TANGENT] = "TANGENT",
    [D3DDECLUSAGE_BINORMAL] = "BINORMAL",
    [D3DDECLUSAGE_TESSFACTOR] = "TESSFACTOR",
    [D3DDECLUSAGE_POSITIONT] = "POSITIONT",
    [D3DDECLUSAGE_COLOR] = "COLOR",
    [D3DDECLUSAGE_FOG] = "FOG",
    [D3DDECLUSAGE_DEPTH] = "DEPTH",
    [D3DDECLUSAGE_SAMPLE] = "SAMPLE"
};

static INLINE unsigned
sm1_to_nine_declusage(struct sm1_semantic *dcl)
{
    return nine_d3d9_to_nine_declusage(dcl->usage, dcl->usage_idx);
}

static void
sm1_declusage_to_tgsi(struct tgsi_declaration_semantic *sem,
                      boolean tc,
                      struct sm1_semantic *dcl)
{
    const unsigned generic_base = tc ? 0 : 8; /* TEXCOORD[0..7] */

    sem->Name = TGSI_SEMANTIC_GENERIC;
    sem->Index = 0;

    /* TGSI_SEMANTIC_GENERIC assignments (+8 if !PIPE_CAP_TGSI_TEXCOORD):
     * Try to put frequently used semantics at low GENERIC indices.
     *
     * POSITION[1..4]: 17, 27, 28, 29
     * COLOR[2..4]: 14, 15, 26
     * TEXCOORD[8..15]: 10, 11, 18, 19, 20, 21, 22, 23
     * BLENDWEIGHT[0..3]: 0, 4, 8, 12
     * BLENDINDICES[0..3]: 1, 5, 9, 13
     * NORMAL[0..1]: 2, 6
     * TANGENT[0]: 3, 24
     * BINORMAL[0]: 7, 25
     * TESSFACTOR[0]: 16
     */

    switch (dcl->usage) {
    case D3DDECLUSAGE_POSITION:
    case D3DDECLUSAGE_POSITIONT:
    case D3DDECLUSAGE_DEPTH:
        sem->Name = TGSI_SEMANTIC_POSITION;
        assert(dcl->usage_idx <= 4);
        if (dcl->usage_idx == 1) {
            sem->Name = TGSI_SEMANTIC_GENERIC;
            sem->Index = generic_base + 17;
        } else
        if (dcl->usage_idx >= 2) {
            sem->Name = TGSI_SEMANTIC_GENERIC;
            sem->Index = generic_base + 27 + (dcl->usage_idx - 2);
        }
        break;
    case D3DDECLUSAGE_COLOR:
        assert(dcl->usage_idx <= 4);
        if (dcl->usage_idx < 2) {
            sem->Name = TGSI_SEMANTIC_COLOR;
            sem->Index = dcl->usage_idx;
        } else
        if (dcl->usage_idx < 4) {
            sem->Index = generic_base + 14 + (dcl->usage_idx - 2);
        } else {
            sem->Index = generic_base + 26;
        }
        break;
    case D3DDECLUSAGE_FOG:
        sem->Name = TGSI_SEMANTIC_FOG;
        assert(dcl->usage_idx == 0);
        break;
    case D3DDECLUSAGE_PSIZE:
        sem->Name = TGSI_SEMANTIC_PSIZE;
        assert(dcl->usage_idx == 0);
        break;
    case D3DDECLUSAGE_TEXCOORD:
        assert(dcl->usage_idx < 16);
        if (dcl->usage_idx < 8) {
            if (tc)
                sem->Name = TGSI_SEMANTIC_TEXCOORD;
            sem->Index = dcl->usage_idx;
        } else
        if (dcl->usage_idx < 10) {
            sem->Index = generic_base + 10 + (dcl->usage_idx - 8);
        } else {
            sem->Index = generic_base + 18 + (dcl->usage_idx - 10);
        }
        break;
    case D3DDECLUSAGE_BLENDWEIGHT: /* 0, 4, 8, 12 */
        assert(dcl->usage_idx < 4);
        sem->Index = generic_base + dcl->usage_idx * 4;
        break;
    case D3DDECLUSAGE_BLENDINDICES: /* 1, 5, 9, 13 */
        assert(dcl->usage_idx < 4);
        sem->Index = generic_base + dcl->usage_idx * 4 + 1;
        break;
    case D3DDECLUSAGE_NORMAL: /* 2, 3 */
        assert(dcl->usage_idx < 2);
        sem->Index = generic_base + 2 + dcl->usage_idx * 4;
        break;
    case D3DDECLUSAGE_TANGENT:
        /* Yes these are weird, but we try to fit the more frequently used
         * into lower slots. */
        assert(dcl->usage_idx <= 1);
        sem->Index = generic_base + (dcl->usage_idx ? 24 : 3);
        break;
    case D3DDECLUSAGE_BINORMAL:
        assert(dcl->usage_idx <= 1);
        sem->Index = generic_base + (dcl->usage_idx ? 25 : 7);
        break;
    case D3DDECLUSAGE_TESSFACTOR:
        assert(dcl->usage_idx == 0);
        sem->Index = generic_base + 16;
        break;
    case D3DDECLUSAGE_SAMPLE:
        sem->Name = TGSI_SEMANTIC_COUNT;
        break;
    default:
        assert(!"Invalid DECLUSAGE.");
        break;
    }
}

#define NINED3DSTT_1D     (D3DSTT_1D >> D3DSP_TEXTURETYPE_SHIFT)
#define NINED3DSTT_2D     (D3DSTT_2D >> D3DSP_TEXTURETYPE_SHIFT)
#define NINED3DSTT_VOLUME (D3DSTT_VOLUME >> D3DSP_TEXTURETYPE_SHIFT)
#define NINED3DSTT_CUBE   (D3DSTT_CUBE >> D3DSP_TEXTURETYPE_SHIFT)
static INLINE unsigned
d3dstt_to_tgsi_tex(BYTE sampler_type)
{
    switch (sampler_type) {
    case NINED3DSTT_1D:     return TGSI_TEXTURE_1D;
    case NINED3DSTT_2D:     return TGSI_TEXTURE_2D;
    case NINED3DSTT_VOLUME: return TGSI_TEXTURE_3D;
    case NINED3DSTT_CUBE:   return TGSI_TEXTURE_CUBE;
    default:
        assert(0);
        return TGSI_TEXTURE_UNKNOWN;
    }
}
static INLINE unsigned
d3dstt_to_tgsi_tex_shadow(BYTE sampler_type)
{
    switch (sampler_type) {
    case NINED3DSTT_1D: return TGSI_TEXTURE_SHADOW1D;
    case NINED3DSTT_2D: return TGSI_TEXTURE_SHADOW2D;
    case NINED3DSTT_VOLUME:
    case NINED3DSTT_CUBE:
    default:
        assert(0);
        return TGSI_TEXTURE_UNKNOWN;
    }
}
static INLINE unsigned
ps1x_sampler_type(const struct nine_shader_info *info, unsigned stage)
{
    switch ((info->sampler_ps1xtypes >> (stage * 2)) & 0x3) {
    case 1: return TGSI_TEXTURE_1D;
    case 0: return TGSI_TEXTURE_2D;
    case 3: return TGSI_TEXTURE_3D;
    default:
        return TGSI_TEXTURE_CUBE;
    }
}

static const char *
sm1_sampler_type_name(BYTE sampler_type)
{
    switch (sampler_type) {
    case NINED3DSTT_1D:     return "1D";
    case NINED3DSTT_2D:     return "2D";
    case NINED3DSTT_VOLUME: return "VOLUME";
    case NINED3DSTT_CUBE:   return "CUBE";
    default:
        return "(D3DSTT_?)";
    }
}

static INLINE unsigned
nine_tgsi_to_interp_mode(struct tgsi_declaration_semantic *sem)
{
    switch (sem->Name) {
    case TGSI_SEMANTIC_POSITION:
    case TGSI_SEMANTIC_NORMAL:
        return TGSI_INTERPOLATE_LINEAR;
    case TGSI_SEMANTIC_BCOLOR:
    case TGSI_SEMANTIC_COLOR:
    case TGSI_SEMANTIC_FOG:
    case TGSI_SEMANTIC_GENERIC:
    case TGSI_SEMANTIC_TEXCOORD:
    case TGSI_SEMANTIC_CLIPDIST:
    case TGSI_SEMANTIC_CLIPVERTEX:
        return TGSI_INTERPOLATE_PERSPECTIVE;
    case TGSI_SEMANTIC_EDGEFLAG:
    case TGSI_SEMANTIC_FACE:
    case TGSI_SEMANTIC_INSTANCEID:
    case TGSI_SEMANTIC_PCOORD:
    case TGSI_SEMANTIC_PRIMID:
    case TGSI_SEMANTIC_PSIZE:
    case TGSI_SEMANTIC_VERTEXID:
        return TGSI_INTERPOLATE_CONSTANT;
    default:
        assert(0);
        return TGSI_INTERPOLATE_CONSTANT;
    }
}

DECL_SPECIAL(DCL)
{
    struct ureg_program *ureg = tx->ureg;
    boolean is_input;
    boolean is_sampler;
    struct tgsi_declaration_semantic tgsi;
    struct sm1_semantic sem;
    sm1_read_semantic(tx, &sem);

    is_input = sem.reg.file == D3DSPR_INPUT;
    is_sampler =
        sem.usage == D3DDECLUSAGE_SAMPLE || sem.reg.file == D3DSPR_SAMPLER;

    DUMP("DCL ");
    sm1_dump_dst_param(&sem.reg);
    if (is_sampler)
        DUMP(" %s\n", sm1_sampler_type_name(sem.sampler_type));
    else
    if (tx->version.major >= 3)
        DUMP(" %s%i\n", sm1_declusage_names[sem.usage], sem.usage_idx);
    else
    if (sem.usage | sem.usage_idx)
        DUMP(" %u[%u]\n", sem.usage, sem.usage_idx);
    else
        DUMP("\n");

    if (is_sampler) {
        const unsigned m = 1 << sem.reg.idx;
        ureg_DECL_sampler(ureg, sem.reg.idx);
        tx->info->sampler_mask |= m;
        tx->sampler_targets[sem.reg.idx] = (tx->info->sampler_mask_shadow & m) ?
            d3dstt_to_tgsi_tex_shadow(sem.sampler_type) :
            d3dstt_to_tgsi_tex(sem.sampler_type);
        return D3D_OK;
    }

    sm1_declusage_to_tgsi(&tgsi, tx->want_texcoord, &sem);
    if (IS_VS) {
        if (is_input) {
            /* linkage outside of shader with vertex declaration */
            ureg_DECL_vs_input(ureg, sem.reg.idx);
            assert(sem.reg.idx < Elements(tx->info->input_map));
            tx->info->input_map[sem.reg.idx] = sm1_to_nine_declusage(&sem);
            tx->info->num_inputs = sem.reg.idx + 1;
            /* NOTE: preserving order in case of indirect access */
        } else
        if (tx->version.major >= 3) {
            /* SM2 output semantic determined by file */
            assert(sem.reg.mask != 0);
            if (sem.usage == D3DDECLUSAGE_POSITIONT)
                tx->info->position_t = TRUE;
            assert(sem.reg.idx < Elements(tx->regs.o));
            tx->regs.o[sem.reg.idx] = ureg_DECL_output_masked(
                ureg, tgsi.Name, tgsi.Index, sem.reg.mask);

            if (tgsi.Name == TGSI_SEMANTIC_PSIZE)
                tx->regs.oPts = tx->regs.o[sem.reg.idx];
        }
    } else {
        if (is_input && tx->version.major >= 3) {
            /* SM3 only, SM2 input semantic determined by file */
            assert(sem.reg.idx < Elements(tx->regs.v));
            tx->regs.v[sem.reg.idx] = ureg_DECL_fs_input_cyl_centroid(
                ureg, tgsi.Name, tgsi.Index,
                nine_tgsi_to_interp_mode(&tgsi),
                0, /* cylwrap */
                sem.reg.mod & NINED3DSPDM_CENTROID);
        } else
        if (!is_input && 0) { /* declare in COLOROUT/DEPTHOUT case */
            /* FragColor or FragDepth */
            assert(sem.reg.mask != 0);
            ureg_DECL_output_masked(ureg, tgsi.Name, tgsi.Index, sem.reg.mask);
        }
    }
    return D3D_OK;
}

DECL_SPECIAL(DEF)
{
    tx_set_lconstf(tx, tx->insn.dst[0].idx, tx->insn.src[0].imm.f);
    return D3D_OK;
}

DECL_SPECIAL(DEFB)
{
    tx_set_lconstb(tx, tx->insn.dst[0].idx, tx->insn.src[0].imm.b);
    return D3D_OK;
}

DECL_SPECIAL(DEFI)
{
    tx_set_lconsti(tx, tx->insn.dst[0].idx, tx->insn.src[0].imm.i);
    return D3D_OK;
}

DECL_SPECIAL(NRM)
{
    struct ureg_program *ureg = tx->ureg;
    struct ureg_dst tmp = tx_scratch_scalar(tx);
    struct ureg_src nrm = tx_src_scalar(tmp);
    struct ureg_src src = tx_src_param(tx, &tx->insn.src[0]);
    ureg_DP3(ureg, tmp, src, src);
    ureg_RSQ(ureg, tmp, nrm);
    ureg_MUL(ureg, tx_dst_param(tx, &tx->insn.dst[0]), src, nrm);
    return D3D_OK;
}

DECL_SPECIAL(DP2ADD)
{
#ifdef NINE_TGSI_LAZY_R600
    struct ureg_dst tmp = tx_scratch_scalar(tx);
    struct ureg_src dp2 = tx_src_scalar(tmp);
    struct ureg_dst dst = tx_dst_param(tx, &tx->insn.dst[0]);
    struct ureg_src src[3];
    int i;
    for (i = 0; i < 3; ++i)
        src[i] = tx_src_param(tx, &tx->insn.src[i]);
    assert_replicate_swizzle(&src[2]);

    ureg_DP2(tx->ureg, tmp, src[0], src[1]);
    ureg_ADD(tx->ureg, dst, src[2], dp2);

    return D3D_OK;
#else
    return NineTranslateInstruction_Generic(tx);
#endif
}

DECL_SPECIAL(TEXCOORD)
{
    struct ureg_program *ureg = tx->ureg;
    const unsigned s = tx->insn.dst[0].idx;
    struct ureg_dst dst = tx_dst_param(tx, &tx->insn.dst[0]);

    if (ureg_src_is_undef(tx->regs.vT[s]))
        tx->regs.vT[s] = ureg_DECL_fs_input(ureg, tx->texcoord_sn, s, TGSI_INTERPOLATE_PERSPECTIVE);
    ureg_MOV(ureg, dst, tx->regs.vT[s]); /* XXX is this sufficient ? */

    return D3D_OK;
}

DECL_SPECIAL(TEXCOORD_ps14)
{
    struct ureg_program *ureg = tx->ureg;
    const unsigned s = tx->insn.src[0].idx;
    struct ureg_dst dst = tx_dst_param(tx, &tx->insn.dst[0]);

    if (ureg_src_is_undef(tx->regs.vT[s]))
        tx->regs.vT[s] = ureg_DECL_fs_input(ureg, tx->texcoord_sn, s, TGSI_INTERPOLATE_PERSPECTIVE);
    ureg_MOV(ureg, dst, tx->regs.vT[s]); /* XXX is this sufficient ? */

    return D3D_OK;
}

DECL_SPECIAL(TEXKILL)
{
    struct ureg_src reg;

    if (tx->version.major > 1 || tx->version.minor > 3) {
        reg = tx_dst_param_as_src(tx, &tx->insn.dst[0]);
    } else {
        tx_texcoord_alloc(tx, tx->insn.dst[0].idx);
        reg = tx->regs.vT[tx->insn.dst[0].idx];
    }
    if (tx->version.major < 2)
        reg = ureg_swizzle(reg, NINE_SWIZZLE4(X,Y,Z,Z));
    ureg_KILL_IF(tx->ureg, reg);

    return D3D_OK;
}

DECL_SPECIAL(TEXBEM)
{
    STUB(D3DERR_INVALIDCALL);
}

DECL_SPECIAL(TEXBEML)
{
    STUB(D3DERR_INVALIDCALL);
}

DECL_SPECIAL(TEXREG2AR)
{
    STUB(D3DERR_INVALIDCALL);
}

DECL_SPECIAL(TEXREG2GB)
{
    STUB(D3DERR_INVALIDCALL);
}

DECL_SPECIAL(TEXM3x2PAD)
{
    STUB(D3DERR_INVALIDCALL);
}

DECL_SPECIAL(TEXM3x2TEX)
{
    STUB(D3DERR_INVALIDCALL);
}

DECL_SPECIAL(TEXM3x3PAD)
{
    return D3D_OK; /* this is just padding */
}

DECL_SPECIAL(TEXM3x3SPEC)
{
    STUB(D3DERR_INVALIDCALL);
}

DECL_SPECIAL(TEXM3x3VSPEC)
{
    STUB(D3DERR_INVALIDCALL);
}

DECL_SPECIAL(TEXREG2RGB)
{
    STUB(D3DERR_INVALIDCALL);
}

DECL_SPECIAL(TEXDP3TEX)
{
    STUB(D3DERR_INVALIDCALL);
}

DECL_SPECIAL(TEXM3x2DEPTH)
{
    STUB(D3DERR_INVALIDCALL);
}

DECL_SPECIAL(TEXDP3)
{
    STUB(D3DERR_INVALIDCALL);
}

DECL_SPECIAL(TEXM3x3)
{
    struct ureg_program *ureg = tx->ureg;
    struct ureg_dst dst = tx_dst_param(tx, &tx->insn.dst[0]);
    struct ureg_src src[4];
    int s;
    const int m = tx->insn.dst[0].idx - 2;
    const int n = tx->insn.src[0].idx;
    assert(m >= 0 && m > n);

    for (s = m; s <= (m + 2); ++s) {
        if (ureg_src_is_undef(tx->regs.vT[s]))
            tx->regs.vT[s] = ureg_DECL_fs_input(ureg, tx->texcoord_sn, s, TGSI_INTERPOLATE_PERSPECTIVE);
        src[s] = tx->regs.vT[s];
    }
    ureg_DP3(ureg, ureg_writemask(dst, TGSI_WRITEMASK_X), src[0], ureg_src(tx->regs.tS[n]));
    ureg_DP3(ureg, ureg_writemask(dst, TGSI_WRITEMASK_Y), src[1], ureg_src(tx->regs.tS[n]));
    ureg_DP3(ureg, ureg_writemask(dst, TGSI_WRITEMASK_Z), src[2], ureg_src(tx->regs.tS[n]));

    switch (tx->insn.opcode) {
    case D3DSIO_TEXM3x3:
        ureg_MOV(ureg, ureg_writemask(dst, TGSI_WRITEMASK_W), ureg_imm1f(ureg, 1.0f));
        break;
    case D3DSIO_TEXM3x3TEX:
        src[4] = ureg_DECL_sampler(ureg, m + 2);
        tx->info->sampler_mask |= 1 << (m + 2);
        ureg_TEX(ureg, dst, ps1x_sampler_type(tx->info, m + 2), ureg_src(dst), src[4]);
        break;
    default:
        return D3DERR_INVALIDCALL;
    }
    return D3D_OK;
}

DECL_SPECIAL(TEXDEPTH)
{
    STUB(D3DERR_INVALIDCALL);
}

DECL_SPECIAL(BEM)
{
    STUB(D3DERR_INVALIDCALL);
}

DECL_SPECIAL(TEXLD)
{
    struct ureg_program *ureg = tx->ureg;
    unsigned target;
    struct ureg_dst dst = tx_dst_param(tx, &tx->insn.dst[0]);
    struct ureg_src src[2] = {
        tx_src_param(tx, &tx->insn.src[0]),
        tx_src_param(tx, &tx->insn.src[1])
    };
    assert(tx->insn.src[1].idx >= 0 &&
           tx->insn.src[1].idx < Elements(tx->sampler_targets));
    target = tx->sampler_targets[tx->insn.src[1].idx];

    switch (tx->insn.flags) {
    case 0:
        ureg_TEX(ureg, dst, target, src[0], src[1]);
        break;
    case NINED3DSI_TEXLD_PROJECT:
        ureg_TXP(ureg, dst, target, src[0], src[1]);
        break;
    case NINED3DSI_TEXLD_BIAS:
        ureg_TXB(ureg, dst, target, src[0], src[1]);
        break;
    default:
        assert(0);
        return D3DERR_INVALIDCALL;
    }
    return D3D_OK;
}

DECL_SPECIAL(TEXLD_14)
{
    struct ureg_program *ureg = tx->ureg;
    struct ureg_dst dst = tx_dst_param(tx, &tx->insn.dst[0]);
    struct ureg_src src = tx_src_param(tx, &tx->insn.src[0]);
    const unsigned s = tx->insn.dst[0].idx;
    const unsigned t = ps1x_sampler_type(tx->info, s);

    tx->info->sampler_mask |= 1 << s;
    ureg_TEX(ureg, dst, t, src, ureg_DECL_sampler(ureg, s));

    return D3D_OK;
}

DECL_SPECIAL(TEX)
{
    struct ureg_program *ureg = tx->ureg;
    const unsigned s = tx->insn.dst[0].idx;
    const unsigned t = ps1x_sampler_type(tx->info, s);
    struct ureg_dst dst = tx_dst_param(tx, &tx->insn.dst[0]);
    struct ureg_src src[2];

    if (ureg_src_is_undef(tx->regs.vT[s]))
        tx->regs.vT[s] = ureg_DECL_fs_input(ureg, tx->texcoord_sn, s, TGSI_INTERPOLATE_PERSPECTIVE);

    src[0] = tx->regs.vT[s];
    src[1] = ureg_DECL_sampler(ureg, s);
    tx->info->sampler_mask |= 1 << s;

    ureg_TEX(ureg, dst, t, src[0], src[1]);

    return D3D_OK;
}

DECL_SPECIAL(TEXLDD)
{
    unsigned target;
    struct ureg_dst dst = tx_dst_param(tx, &tx->insn.dst[0]);
    struct ureg_src src[4] = {
        tx_src_param(tx, &tx->insn.src[0]),
        tx_src_param(tx, &tx->insn.src[1]),
        tx_src_param(tx, &tx->insn.src[2]),
        tx_src_param(tx, &tx->insn.src[3])
    };
    assert(tx->insn.src[3].idx >= 0 &&
           tx->insn.src[3].idx < Elements(tx->sampler_targets));
    target = tx->sampler_targets[tx->insn.src[1].idx];

    ureg_TXD(tx->ureg, dst, target, src[0], src[2], src[3], src[1]);
    return D3D_OK;
}

DECL_SPECIAL(TEXLDL)
{
    unsigned target;
    struct ureg_dst dst = tx_dst_param(tx, &tx->insn.dst[0]);
    struct ureg_src src[2] = {
       tx_src_param(tx, &tx->insn.src[0]),
       tx_src_param(tx, &tx->insn.src[1])
    };
    assert(tx->insn.src[3].idx >= 0 &&
           tx->insn.src[3].idx < Elements(tx->sampler_targets));
    target = tx->sampler_targets[tx->insn.src[1].idx];

    ureg_TXL(tx->ureg, dst, target, src[0], src[1]);
    return D3D_OK;
}

DECL_SPECIAL(SETP)
{
    STUB(D3DERR_INVALIDCALL);
}

DECL_SPECIAL(BREAKP)
{
    STUB(D3DERR_INVALIDCALL);
}

DECL_SPECIAL(PHASE)
{
    return D3D_OK; /* we don't care about phase */
}

DECL_SPECIAL(COMMENT)
{
    return D3D_OK; /* nothing to do */
}


#define _OPI(o,t,vv1,vv2,pv1,pv2,d,s,h) \
    { D3DSIO_##o, TGSI_OPCODE_##t, { vv1, vv2 }, { pv1, pv2, }, d, s, h }

struct sm1_op_info inst_table[] =
{
    _OPI(NOP, NOP, V(0,0), V(3,0), V(0,0), V(3,0), 0, 0, NULL), /* 0 */
    _OPI(MOV, MOV, V(0,0), V(1,1), V(0,0), V(0,0), 1, 1, SPECIAL(MOV_vs1x)),
    _OPI(MOV, MOV, V(2,0), V(3,0), V(0,0), V(3,0), 1, 1, NULL),
    _OPI(ADD, ADD, V(0,0), V(3,0), V(0,0), V(3,0), 1, 2, NULL), /* 2 */
    _OPI(SUB, SUB, V(0,0), V(3,0), V(0,0), V(3,0), 1, 2, NULL), /* 3 */
    _OPI(MAD, MAD, V(0,0), V(3,0), V(0,0), V(3,0), 1, 3, NULL), /* 4 */
    _OPI(MUL, MUL, V(0,0), V(3,0), V(0,0), V(3,0), 1, 2, NULL), /* 5 */
    _OPI(RCP, RCP, V(0,0), V(3,0), V(0,0), V(3,0), 1, 1, NULL), /* 6 */
    _OPI(RSQ, RSQ, V(0,0), V(3,0), V(0,0), V(3,0), 1, 1, NULL), /* 7 */
    _OPI(DP3, DP3, V(0,0), V(3,0), V(0,0), V(3,0), 1, 2, NULL), /* 8 */
    _OPI(DP4, DP4, V(0,0), V(3,0), V(0,0), V(3,0), 1, 2, NULL), /* 9 */
    _OPI(MIN, MIN, V(0,0), V(3,0), V(0,0), V(3,0), 1, 2, NULL), /* 10 */
    _OPI(MAX, MAX, V(0,0), V(3,0), V(0,0), V(3,0), 1, 2, NULL), /* 11 */
    _OPI(SLT, SLT, V(0,0), V(3,0), V(0,0), V(3,0), 1, 2, NULL), /* 12 */
    _OPI(SGE, SGE, V(0,0), V(3,0), V(0,0), V(3,0), 1, 2, NULL), /* 13 */
    _OPI(EXP, EX2, V(0,0), V(3,0), V(0,0), V(3,0), 1, 1, NULL), /* 14 */
    _OPI(LOG, LG2, V(0,0), V(3,0), V(0,0), V(3,0), 1, 1, NULL), /* 15 */
    _OPI(LIT, LIT, V(0,0), V(3,0), V(0,0), V(0,0), 1, 1, NULL), /* 16 */
    _OPI(DST, DST, V(0,0), V(3,0), V(0,0), V(3,0), 1, 2, NULL), /* 17 */
    _OPI(LRP, LRP, V(0,0), V(3,0), V(0,0), V(3,0), 1, 3, NULL), /* 18 */
    _OPI(FRC, FRC, V(0,0), V(3,0), V(0,0), V(3,0), 1, 1, NULL), /* 19 */

    _OPI(M4x4, NOP, V(0,0), V(3,0), V(0,0), V(3,0), 1, 2, SPECIAL(M4x4)),
    _OPI(M4x3, NOP, V(0,0), V(3,0), V(0,0), V(3,0), 1, 2, SPECIAL(M4x3)),
    _OPI(M3x4, NOP, V(0,0), V(3,0), V(0,0), V(3,0), 1, 2, SPECIAL(M3x4)),
    _OPI(M3x3, NOP, V(0,0), V(3,0), V(0,0), V(3,0), 1, 2, SPECIAL(M3x3)),
    _OPI(M3x2, NOP, V(0,0), V(3,0), V(0,0), V(3,0), 1, 2, SPECIAL(M3x2)),

    _OPI(CALL,    CAL,     V(2,0), V(3,0), V(2,1), V(3,0), 0, 0, SPECIAL(CALL)),
    _OPI(CALLNZ,  CAL,     V(2,0), V(3,0), V(2,1), V(3,0), 0, 0, SPECIAL(CALLNZ)),
    _OPI(LOOP,    BGNLOOP, V(2,0), V(3,0), V(3,0), V(3,0), 0, 2, SPECIAL(LOOP)),
    _OPI(RET,     RET,     V(2,0), V(3,0), V(2,1), V(3,0), 0, 0, SPECIAL(RET)),
    _OPI(ENDLOOP, ENDLOOP, V(2,0), V(3,0), V(3,0), V(3,0), 0, 0, SPECIAL(ENDLOOP)),
    _OPI(LABEL,   NOP,     V(2,0), V(3,0), V(2,1), V(3,0), 0, 0, SPECIAL(LABEL)),

    _OPI(DCL, NOP, V(0,0), V(3,0), V(0,0), V(3,0), 0, 0, SPECIAL(DCL)),

    _OPI(POW, POW, V(0,0), V(3,0), V(0,0), V(3,0), 1, 2, NULL),
    _OPI(CRS, XPD, V(0,0), V(3,0), V(0,0), V(3,0), 1, 2, NULL), /* XXX: .w */
    _OPI(SGN, SSG, V(2,0), V(3,0), V(0,0), V(0,0), 1, 3, SPECIAL(SGN)), /* ignore src1,2 */
    _OPI(ABS, ABS, V(0,0), V(3,0), V(0,0), V(3,0), 1, 1, NULL),
    _OPI(NRM, NRM, V(0,0), V(3,0), V(0,0), V(3,0), 1, 1, SPECIAL(NRM)), /* NRM doesn't fit */

    _OPI(SINCOS, SCS, V(2,0), V(2,1), V(2,0), V(2,1), 1, 3, SPECIAL(SINCOS)),
    _OPI(SINCOS, SCS, V(3,0), V(3,0), V(3,0), V(3,0), 1, 1, SPECIAL(SINCOS)),

    /* More flow control */
    _OPI(REP,    NOP,    V(2,0), V(3,0), V(2,1), V(3,0), 0, 1, SPECIAL(REP)),
    _OPI(ENDREP, NOP,    V(2,0), V(3,0), V(2,1), V(3,0), 0, 0, SPECIAL(ENDREP)),
    _OPI(IF,     IF,     V(2,0), V(3,0), V(2,1), V(3,0), 0, 1, SPECIAL(IF)),
    _OPI(IFC,    IF,     V(2,1), V(3,0), V(2,1), V(3,0), 0, 2, SPECIAL(IFC)),
    _OPI(ELSE,   ELSE,   V(2,0), V(3,0), V(2,1), V(3,0), 0, 0, SPECIAL(ELSE)),
    _OPI(ENDIF,  ENDIF,  V(2,0), V(3,0), V(2,1), V(3,0), 0, 0, SPECIAL(ENDIF)),
    _OPI(BREAK,  BRK,    V(2,1), V(3,0), V(2,1), V(3,0), 0, 0, NULL),
    _OPI(BREAKC, BREAKC, V(2,1), V(3,0), V(2,1), V(3,0), 0, 2, SPECIAL(BREAKC)),

    _OPI(MOVA, ARR, V(2,0), V(3,0), V(0,0), V(0,0), 1, 1, NULL),

    _OPI(DEFB, NOP, V(0,0), V(3,0) , V(0,0), V(3,0) , 1, 0, SPECIAL(DEFB)),
    _OPI(DEFI, NOP, V(0,0), V(3,0) , V(0,0), V(3,0) , 1, 0, SPECIAL(DEFI)),

    _OPI(TEXCOORD,     NOP, V(0,0), V(0,0), V(0,0), V(1,3), 1, 0, SPECIAL(TEXCOORD)),
    _OPI(TEXCOORD,     MOV, V(0,0), V(0,0), V(1,4), V(1,4), 1, 1, SPECIAL(TEXCOORD_ps14)),
    _OPI(TEXKILL,      KILL_IF, V(0,0), V(0,0), V(0,0), V(3,0), 1, 0, SPECIAL(TEXKILL)),
    _OPI(TEX,          TEX, V(0,0), V(0,0), V(0,0), V(1,3), 1, 0, SPECIAL(TEX)),
    _OPI(TEX,          TEX, V(0,0), V(0,0), V(1,4), V(1,4), 1, 1, SPECIAL(TEXLD_14)),
    _OPI(TEX,          TEX, V(0,0), V(0,0), V(2,0), V(3,0), 1, 2, SPECIAL(TEXLD)),
    _OPI(TEXBEM,       TEX, V(0,0), V(0,0), V(0,0), V(1,3), 0, 0, SPECIAL(TEXBEM)),
    _OPI(TEXBEML,      TEX, V(0,0), V(0,0), V(0,0), V(1,3), 0, 0, SPECIAL(TEXBEML)),
    _OPI(TEXREG2AR,    TEX, V(0,0), V(0,0), V(0,0), V(1,3), 0, 0, SPECIAL(TEXREG2AR)),
    _OPI(TEXREG2GB,    TEX, V(0,0), V(0,0), V(0,0), V(1,3), 0, 0, SPECIAL(TEXREG2GB)),
    _OPI(TEXM3x2PAD,   TEX, V(0,0), V(0,0), V(0,0), V(1,3), 0, 0, SPECIAL(TEXM3x2PAD)),
    _OPI(TEXM3x2TEX,   TEX, V(0,0), V(0,0), V(0,0), V(1,3), 0, 0, SPECIAL(TEXM3x2TEX)),
    _OPI(TEXM3x3PAD,   TEX, V(0,0), V(0,0), V(0,0), V(1,3), 0, 0, SPECIAL(TEXM3x3PAD)),
    _OPI(TEXM3x3TEX,   TEX, V(0,0), V(0,0), V(0,0), V(1,3), 0, 0, SPECIAL(TEXM3x3)),
    _OPI(TEXM3x3SPEC,  TEX, V(0,0), V(0,0), V(0,0), V(1,3), 0, 0, SPECIAL(TEXM3x3SPEC)),
    _OPI(TEXM3x3VSPEC, TEX, V(0,0), V(0,0), V(0,0), V(1,3), 0, 0, SPECIAL(TEXM3x3VSPEC)),

    _OPI(EXPP, EXP, V(0,0), V(1,1), V(0,0), V(0,0), 1, 1, NULL),
    _OPI(EXPP, EX2, V(2,0), V(3,0), V(0,0), V(0,0), 1, 1, NULL),
    _OPI(LOGP, LG2, V(0,0), V(3,0), V(0,0), V(0,0), 1, 1, NULL),
    _OPI(CND,  CND, V(0,0), V(0,0), V(0,0), V(1,4), 1, 3, SPECIAL(CND)),

    _OPI(DEF, NOP, V(0,0), V(3,0), V(0,0), V(3,0), 1, 0, SPECIAL(DEF)),

    /* More tex stuff */
    _OPI(TEXREG2RGB,   TEX, V(0,0), V(0,0), V(1,2), V(1,3), 0, 0, SPECIAL(TEXREG2RGB)),
    _OPI(TEXDP3TEX,    TEX, V(0,0), V(0,0), V(1,2), V(1,3), 0, 0, SPECIAL(TEXDP3TEX)),
    _OPI(TEXM3x2DEPTH, TEX, V(0,0), V(0,0), V(1,3), V(1,3), 0, 0, SPECIAL(TEXM3x2DEPTH)),
    _OPI(TEXDP3,       TEX, V(0,0), V(0,0), V(1,2), V(1,3), 0, 0, SPECIAL(TEXDP3)),
    _OPI(TEXM3x3,      TEX, V(0,0), V(0,0), V(1,2), V(1,3), 0, 0, SPECIAL(TEXM3x3)),
    _OPI(TEXDEPTH,     TEX, V(0,0), V(0,0), V(1,4), V(1,4), 0, 0, SPECIAL(TEXDEPTH)),

    /* Misc */
    _OPI(CMP,    CMP,  V(0,0), V(0,0), V(1,2), V(3,0), 1, 3, SPECIAL(CMP)), /* reversed */
    _OPI(BEM,    NOP,  V(0,0), V(0,0), V(1,4), V(1,4), 0, 0, SPECIAL(BEM)),
    _OPI(DP2ADD, DP2A, V(0,0), V(0,0), V(2,0), V(3,0), 1, 3, SPECIAL(DP2ADD)), /* for radeons */
    _OPI(DSX,    DDX,  V(0,0), V(0,0), V(2,1), V(3,0), 1, 1, NULL),
    _OPI(DSY,    DDY,  V(0,0), V(0,0), V(2,1), V(3,0), 1, 1, NULL),
    _OPI(TEXLDD, TXD,  V(0,0), V(0,0), V(2,1), V(3,0), 1, 4, SPECIAL(TEXLDD)),
    _OPI(SETP,   NOP,  V(0,0), V(3,0), V(2,1), V(3,0), 0, 0, SPECIAL(SETP)),
    _OPI(TEXLDL, TXL,  V(3,0), V(3,0), V(3,0), V(3,0), 1, 2, SPECIAL(TEXLDL)),
    _OPI(BREAKP, BRK,  V(0,0), V(3,0), V(2,1), V(3,0), 0, 0, SPECIAL(BREAKP))
};

struct sm1_op_info inst_phase =
    _OPI(PHASE, NOP, V(0,0), V(0,0), V(1,4), V(1,4), 0, 0, SPECIAL(PHASE));

struct sm1_op_info inst_comment =
    _OPI(COMMENT, NOP, V(0,0), V(3,0), V(0,0), V(3,0), 0, 0, SPECIAL(COMMENT));

static void
create_op_info_map(struct shader_translator *tx)
{
    const unsigned version = (tx->version.major << 8) | tx->version.minor;
    unsigned i;

    for (i = 0; i < Elements(tx->op_info_map); ++i)
        tx->op_info_map[i] = -1;

    if (tx->processor == TGSI_PROCESSOR_VERTEX) {
        for (i = 0; i < Elements(inst_table); ++i) {
            assert(inst_table[i].sio < Elements(tx->op_info_map));
            if (inst_table[i].vert_version.min <= version &&
                inst_table[i].vert_version.max >= version)
                tx->op_info_map[inst_table[i].sio] = i;
        }
    } else {
        for (i = 0; i < Elements(inst_table); ++i) {
            assert(inst_table[i].sio < Elements(tx->op_info_map));
            if (inst_table[i].frag_version.min <= version &&
                inst_table[i].frag_version.max >= version)
                tx->op_info_map[inst_table[i].sio] = i;
        }
    }
}

static INLINE HRESULT
NineTranslateInstruction_Generic(struct shader_translator *tx)
{
    struct ureg_dst dst[1];
    struct ureg_src src[4];
    unsigned i;

    for (i = 0; i < tx->insn.ndst && i < Elements(dst); ++i)
        dst[i] = tx_dst_param(tx, &tx->insn.dst[i]);
    for (i = 0; i < tx->insn.nsrc && i < Elements(src); ++i)
        src[i] = tx_src_param(tx, &tx->insn.src[i]);

    ureg_insn(tx->ureg, tx->insn.info->opcode,
              dst, tx->insn.ndst,
              src, tx->insn.nsrc);
    return D3D_OK;
}

static INLINE DWORD
TOKEN_PEEK(struct shader_translator *tx)
{
    return *(tx->parse);
}

static INLINE DWORD
TOKEN_NEXT(struct shader_translator *tx)
{
    return *(tx->parse)++;
}

static INLINE void
TOKEN_JUMP(struct shader_translator *tx)
{
    if (tx->parse_next && tx->parse != tx->parse_next) {
        WARN("parse(%p) != parse_next(%p) !\n", tx->parse, tx->parse_next);
        tx->parse = tx->parse_next;
    }
}

static INLINE boolean
sm1_parse_eof(struct shader_translator *tx)
{
    return TOKEN_PEEK(tx) == NINED3DSP_END;
}

static void
sm1_read_version(struct shader_translator *tx)
{
    const DWORD tok = TOKEN_NEXT(tx);

    tx->version.major = D3DSHADER_VERSION_MAJOR(tok);
    tx->version.minor = D3DSHADER_VERSION_MINOR(tok);

    switch (tok >> 16) {
    case NINED3D_SM1_VS: tx->processor = TGSI_PROCESSOR_VERTEX; break;
    case NINED3D_SM1_PS: tx->processor = TGSI_PROCESSOR_FRAGMENT; break;
    default:
       DBG("Invalid shader type: %x\n", tok);
       tx->processor = ~0;
       break;
    }
}

/* This is just to check if we parsed the instruction properly. */
static void
sm1_parse_get_skip(struct shader_translator *tx)
{
    const DWORD tok = TOKEN_PEEK(tx);

    if (tx->version.major >= 2) {
        tx->parse_next = tx->parse + 1 /* this */ +
            ((tok & D3DSI_INSTLENGTH_MASK) >> D3DSI_INSTLENGTH_SHIFT);
    } else {
        tx->parse_next = NULL; /* TODO: determine from param count */
    }
}

static void
sm1_print_comment(const char *comment, UINT size)
{
    if (!size)
        return;
    /* TODO */
}

static void
sm1_parse_comments(struct shader_translator *tx, BOOL print)
{
    DWORD tok = TOKEN_PEEK(tx);

    while ((tok & D3DSI_OPCODE_MASK) == D3DSIO_COMMENT)
    {
        const char *comment = "";
        UINT size = (tok & D3DSI_COMMENTSIZE_MASK) >> D3DSI_COMMENTSIZE_SHIFT;
        tx->parse += size + 1;

        if (print)
            sm1_print_comment(comment, size);

        tok = TOKEN_PEEK(tx);
    }
}

static void
sm1_parse_get_param(struct shader_translator *tx, DWORD *reg, DWORD *rel)
{
    *reg = TOKEN_NEXT(tx);

    if (*reg & D3DSHADER_ADDRMODE_RELATIVE)
    {
        if (tx->version.major < 2)
            *rel = (1 << 31) |
                ((D3DSPR_ADDR << D3DSP_REGTYPE_SHIFT2) & D3DSP_REGTYPE_MASK2) |
                ((D3DSPR_ADDR << D3DSP_REGTYPE_SHIFT)  & D3DSP_REGTYPE_MASK) |
                (D3DSP_NOSWIZZLE << D3DSP_SWIZZLE_SHIFT);
        else
            *rel = TOKEN_NEXT(tx);
    }
}

static void
sm1_parse_dst_param(struct sm1_dst_param *dst, DWORD tok)
{
    dst->file =
        (tok & D3DSP_REGTYPE_MASK)  >> D3DSP_REGTYPE_SHIFT |
        (tok & D3DSP_REGTYPE_MASK2) >> D3DSP_REGTYPE_SHIFT2;
    dst->type = PIPE_TYPE_FLOAT;
    dst->idx = tok & D3DSP_REGNUM_MASK;
    dst->rel = NULL;
    dst->mask = (tok & NINED3DSP_WRITEMASK_MASK) >> NINED3DSP_WRITEMASK_SHIFT;
    dst->mod = (tok & D3DSP_DSTMOD_MASK) >> D3DSP_DSTMOD_SHIFT;
    dst->shift = (tok & D3DSP_DSTSHIFT_MASK) >> D3DSP_DSTSHIFT_SHIFT;
}

static void
sm1_parse_src_param(struct sm1_src_param *src, DWORD tok)
{
    src->file =
        ((tok & D3DSP_REGTYPE_MASK)  >> D3DSP_REGTYPE_SHIFT) |
        ((tok & D3DSP_REGTYPE_MASK2) >> D3DSP_REGTYPE_SHIFT2);
    src->type = PIPE_TYPE_FLOAT;
    src->idx = tok & D3DSP_REGNUM_MASK;
    src->rel = NULL;
    src->swizzle = (tok & D3DSP_SWIZZLE_MASK) >> D3DSP_SWIZZLE_SHIFT;
    src->mod = (tok & D3DSP_SRCMOD_MASK) >> D3DSP_SRCMOD_SHIFT;

    switch (src->file) {
    case D3DSPR_CONST2: src->file = D3DSPR_CONST; src->idx += 2048; break;
    case D3DSPR_CONST3: src->file = D3DSPR_CONST; src->idx += 4096; break;
    case D3DSPR_CONST4: src->file = D3DSPR_CONST; src->idx += 6144; break;
    default:
        break;
    }
}

static void
sm1_parse_immediate(struct shader_translator *tx,
                    struct sm1_src_param *imm)
{
    imm->file = NINED3DSPR_IMMEDIATE;
    imm->idx = INT_MIN;
    imm->rel = NULL;
    imm->swizzle = NINED3DSP_NOSWIZZLE;
    imm->mod = 0;
    switch (tx->insn.opcode) {
    case D3DSIO_DEF:
        imm->type = NINED3DSPTYPE_FLOAT4;
        memcpy(&imm->imm.d[0], tx->parse, 4 * sizeof(DWORD));
        tx->parse += 4;
        break;
    case D3DSIO_DEFI:
        imm->type = NINED3DSPTYPE_INT4;
        memcpy(&imm->imm.d[0], tx->parse, 4 * sizeof(DWORD));
        tx->parse += 4;
        break;
    case D3DSIO_DEFB:
        imm->type = NINED3DSPTYPE_BOOL;
        memcpy(&imm->imm.d[0], tx->parse, 1 * sizeof(DWORD));
        tx->parse += 1;
        break;
    default:
       assert(0);
       break;
    }
}

static void
sm1_read_dst_param(struct shader_translator *tx,
                   struct sm1_dst_param *dst,
                   struct sm1_src_param *rel)
{
    DWORD tok_dst, tok_rel = 0;

    sm1_parse_get_param(tx, &tok_dst, &tok_rel);
    sm1_parse_dst_param(dst, tok_dst);
    if (tok_dst & D3DSHADER_ADDRMODE_RELATIVE) {
        sm1_parse_src_param(rel, tok_rel);
        dst->rel = rel;
    }
}

static void
sm1_read_src_param(struct shader_translator *tx,
                   struct sm1_src_param *src,
                   struct sm1_src_param *rel)
{
    DWORD tok_src, tok_rel = 0;

    sm1_parse_get_param(tx, &tok_src, &tok_rel);
    sm1_parse_src_param(src, tok_src);
    if (tok_src & D3DSHADER_ADDRMODE_RELATIVE) {
        assert(rel);
        sm1_parse_src_param(rel, tok_rel);
        src->rel = rel;
    }
}

static void
sm1_read_semantic(struct shader_translator *tx,
                  struct sm1_semantic *sem)
{
    const DWORD tok_usg = TOKEN_NEXT(tx);
    const DWORD tok_dst = TOKEN_NEXT(tx);

    sem->sampler_type = (tok_usg & D3DSP_TEXTURETYPE_MASK) >> D3DSP_TEXTURETYPE_SHIFT;
    sem->usage = (tok_usg & D3DSP_DCL_USAGE_MASK) >> D3DSP_DCL_USAGE_SHIFT;
    sem->usage_idx = (tok_usg & D3DSP_DCL_USAGEINDEX_MASK) >> D3DSP_DCL_USAGEINDEX_SHIFT;

    sm1_parse_dst_param(&sem->reg, tok_dst);
}

static void
sm1_parse_instruction(struct shader_translator *tx)
{
    struct sm1_instruction *insn = &tx->insn;
    DWORD tok;
    struct sm1_op_info *info = NULL;
    unsigned i;

    sm1_parse_comments(tx, TRUE);
    sm1_parse_get_skip(tx);

    tok = TOKEN_NEXT(tx);

    insn->opcode = tok & D3DSI_OPCODE_MASK;
    insn->flags = (tok & NINED3DSIO_OPCODE_FLAGS_MASK) >> NINED3DSIO_OPCODE_FLAGS_SHIFT;
    insn->coissue = !!(tok & D3DSI_COISSUE);
    insn->predicated = !!(tok & NINED3DSHADER_INST_PREDICATED);

    if (insn->opcode < Elements(tx->op_info_map)) {
        int k = tx->op_info_map[insn->opcode];
        if (k >= 0) {
            assert(k < Elements(inst_table));
            info = &inst_table[k];
        }
    } else {
       if (insn->opcode == D3DSIO_PHASE)   info = &inst_phase;
       if (insn->opcode == D3DSIO_COMMENT) info = &inst_comment;
    }
    if (!info) {
       DBG("illegal or unhandled opcode: %08x\n", insn->opcode);
       TOKEN_JUMP(tx);
       return;
    }
    insn->info = info;
    insn->ndst = info->ndst;
    insn->nsrc = info->nsrc;

    assert(!insn->predicated && "TODO: predicated instructions");

    /* check version */
    {
        unsigned min = IS_VS ? info->vert_version.min : info->frag_version.min;
        unsigned max = IS_VS ? info->vert_version.max : info->frag_version.max;
        unsigned ver = (tx->version.major << 8) | tx->version.minor;
        if (ver < min || ver > max) {
            DBG("opcode not supported in this shader version: %x <= %x <= %x\n",
                min, ver, max);
            return;
        }
    }

    for (i = 0; i < insn->ndst; ++i)
        sm1_read_dst_param(tx, &insn->dst[i], &insn->dst_rel[i]);
    if (insn->predicated)
        sm1_read_src_param(tx, &insn->pred, NULL);
    for (i = 0; i < insn->nsrc; ++i)
        sm1_read_src_param(tx, &insn->src[i], &insn->src_rel[i]);

    /* parse here so we can dump them before processing */
    if (insn->opcode == D3DSIO_DEF ||
        insn->opcode == D3DSIO_DEFI ||
        insn->opcode == D3DSIO_DEFB)
        sm1_parse_immediate(tx, &tx->insn.src[0]);

    sm1_dump_instruction(insn, tx->cond_depth + tx->loop_depth);
    sm1_instruction_check(insn);

    if (info->handler)
        info->handler(tx);
    else
       NineTranslateInstruction_Generic(tx);
    tx_apply_dst0_modifiers(tx);

    tx->num_scratch = 0; /* reset */

    TOKEN_JUMP(tx);
}

static void
tx_ctor(struct shader_translator *tx, struct nine_shader_info *info)
{
    unsigned i;

    tx->info = info;

    tx->byte_code = info->byte_code;
    tx->parse = info->byte_code;

    for (i = 0; i < Elements(info->input_map); ++i)
        info->input_map[i] = NINE_DECLUSAGE_NONE;
    info->num_inputs = 0;

    info->position_t = FALSE;
    info->point_size = FALSE;

    tx->info->const_used_size = 0;

    info->sampler_mask = 0x0;
    info->rt_mask = 0x0;

    info->lconstf.data = NULL;
    info->lconstf.ranges = NULL;

    for (i = 0; i < Elements(tx->regs.aL); ++i) {
        tx->regs.aL[i] = ureg_dst_undef();
        tx->regs.rL[i] = ureg_dst_undef();
    }
    tx->regs.a = ureg_dst_undef();
    tx->regs.p = ureg_dst_undef();
    tx->regs.oDepth = ureg_dst_undef();
    tx->regs.vPos = ureg_src_undef();
    tx->regs.vFace = ureg_src_undef();
    for (i = 0; i < Elements(tx->regs.o); ++i)
        tx->regs.o[i] = ureg_dst_undef();
    for (i = 0; i < Elements(tx->regs.oCol); ++i)
        tx->regs.oCol[i] = ureg_dst_undef();
    for (i = 0; i < Elements(tx->regs.vC); ++i)
        tx->regs.vC[i] = ureg_src_undef();
    for (i = 0; i < Elements(tx->regs.vT); ++i)
        tx->regs.vT[i] = ureg_src_undef();

    for (i = 0; i < Elements(tx->lconsti); ++i)
        tx->lconsti[i].idx = -1;
    for (i = 0; i < Elements(tx->lconstb); ++i)
        tx->lconstb[i].idx = -1;

    sm1_read_version(tx);

    info->version = (tx->version.major << 4) | tx->version.minor;

    create_op_info_map(tx);
}

static void
tx_dtor(struct shader_translator *tx)
{
    if (tx->num_inst_labels)
        FREE(tx->inst_labels);
    if (tx->lconstf)
        FREE(tx->lconstf);
    if (tx->regs.r)
        FREE(tx->regs.r);
    FREE(tx);
}

static INLINE unsigned
tgsi_processor_from_type(unsigned shader_type)
{
    switch (shader_type) {
    case PIPE_SHADER_VERTEX: return TGSI_PROCESSOR_VERTEX;
    case PIPE_SHADER_FRAGMENT: return TGSI_PROCESSOR_FRAGMENT;
    default:
        return ~0;
    }
}

#define GET_CAP(n) device->screen->get_param( \
      device->screen, PIPE_CAP_##n)
#define GET_SHADER_CAP(n) device->screen->get_shader_param( \
      device->screen, info->type, PIPE_SHADER_CAP_##n)

HRESULT
nine_translate_shader(struct NineDevice9 *device, struct nine_shader_info *info)
{
    struct shader_translator *tx;
    HRESULT hr = D3D_OK;
    const unsigned processor = tgsi_processor_from_type(info->type);

    user_assert(processor != ~0, D3DERR_INVALIDCALL);

    tx = CALLOC_STRUCT(shader_translator);
    if (!tx)
        return E_OUTOFMEMORY;
    tx_ctor(tx, info);

    if (((tx->version.major << 16) | tx->version.minor) > 0x00030000) {
        hr = D3DERR_INVALIDCALL;
        DBG("Unsupported shader version: %u.%u !\n",
            tx->version.major, tx->version.minor);
        goto out;
    }
    if (tx->processor != processor) {
        hr = D3DERR_INVALIDCALL;
        DBG("Shader type mismatch: %u / %u !\n", tx->processor, processor);
        goto out;
    }
    DUMP("%s%u.%u\n", processor == TGSI_PROCESSOR_VERTEX ? "VS" : "PS",
         tx->version.major, tx->version.minor);

    tx->ureg = ureg_create(processor);
    if (!tx->ureg) {
        hr = E_OUTOFMEMORY;
        goto out;
    }
    tx_decl_constants(tx);

    tx->native_integers = GET_SHADER_CAP(INTEGERS);
    tx->inline_subroutines = !GET_SHADER_CAP(SUBROUTINES);
    tx->lower_preds = !GET_SHADER_CAP(MAX_PREDS);
    tx->want_texcoord = GET_CAP(TGSI_TEXCOORD);
    tx->shift_wpos = !GET_CAP(TGSI_FS_COORD_PIXEL_CENTER_INTEGER);
    tx->texcoord_sn = tx->want_texcoord ?
        TGSI_SEMANTIC_TEXCOORD : TGSI_SEMANTIC_GENERIC;

    /* VS must always write position. Declare it here to make it the 1st output.
     * (Some drivers like nv50 are buggy and rely on that.)
     */
    if (IS_VS) {
        tx->regs.oPos = ureg_DECL_output(tx->ureg, TGSI_SEMANTIC_POSITION, 0);
    } else {
        ureg_property_fs_coord_origin(tx->ureg, TGSI_FS_COORD_ORIGIN_UPPER_LEFT);
        if (!tx->shift_wpos)
            ureg_property_fs_coord_pixel_center(tx->ureg, TGSI_FS_COORD_PIXEL_CENTER_INTEGER);
    }

    if (!ureg_dst_is_undef(tx->regs.oPts))
        info->point_size = TRUE;

    while (!sm1_parse_eof(tx))
        sm1_parse_instruction(tx);
    tx->parse++; /* for byte_size */

    if (IS_PS && (tx->version.major < 2) && tx->num_temp) {
        ureg_MOV(tx->ureg, ureg_DECL_output(tx->ureg, TGSI_SEMANTIC_COLOR, 0),
                 ureg_src(tx->regs.r[0]));
        info->rt_mask |= 0x1;
    }

    if (info->position_t)
        ureg_property_vs_window_space_position(tx->ureg, TRUE);

    ureg_END(tx->ureg);

    if (debug_get_bool_option("NINE_TGSI_DUMP", FALSE)) {
        unsigned count;
        const struct tgsi_token *toks = ureg_get_tokens(tx->ureg, &count);
        tgsi_dump(toks, 0);
        ureg_free_tokens(toks);
    }

    /* record local constants */
    if (tx->num_lconstf && tx->indirect_const_access) {
        struct nine_range *ranges;
        float *data;
        int *indices;
        unsigned i, k, n;

        hr = E_OUTOFMEMORY;

        data = MALLOC(tx->num_lconstf * 4 * sizeof(float));
        if (!data)
            goto out;
        info->lconstf.data = data;

        indices = MALLOC(tx->num_lconstf * sizeof(indices[0]));
        if (!indices)
            goto out;

        /* lazy sort, num_lconstf should be small */
        for (n = 0; n < tx->num_lconstf; ++n) {
            for (k = 0, i = 0; i < tx->num_lconstf; ++i) {
                if (tx->lconstf[i].idx < tx->lconstf[k].idx)
                    k = i;
            }
            indices[n] = tx->lconstf[k].idx;
            memcpy(&data[n * 4], &tx->lconstf[k].imm.f[0], 4 * sizeof(float));
            tx->lconstf[k].idx = INT_MAX;
        }

        /* count ranges */
        for (n = 1, i = 1; i < tx->num_lconstf; ++i)
            if (indices[i] != indices[i - 1] + 1)
                ++n;
        ranges = MALLOC(n * sizeof(ranges[0]));
        if (!ranges) {
            FREE(indices);
            goto out;
        }
        info->lconstf.ranges = ranges;

        k = 0;
        ranges[k].bgn = indices[0];
        for (i = 1; i < tx->num_lconstf; ++i) {
            if (indices[i] != indices[i - 1] + 1) {
                ranges[k].next = &ranges[k + 1];
                ranges[k].end = indices[i - 1] + 1;
                ++k;
                ranges[k].bgn = indices[i];
            }
        }
        ranges[k].end = indices[i - 1] + 1;
        ranges[k].next = NULL;
        assert(n == (k + 1));

        FREE(indices);
        hr = D3D_OK;
    }

    if (tx->indirect_const_access)
        info->const_used_size = ~0;

    info->cso = ureg_create_shader_and_destroy(tx->ureg, device->pipe);
    if (!info->cso) {
        hr = D3DERR_DRIVERINTERNALERROR;
        FREE(info->lconstf.data);
        FREE(info->lconstf.ranges);
        goto out;
    }

    info->byte_size = (tx->parse - tx->byte_code) * sizeof(DWORD);
out:
    tx_dtor(tx);
    return hr;
}