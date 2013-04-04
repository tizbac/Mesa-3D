
#ifndef __NVC0_PGRAPH_MACROS_H__
#define __NVC0_PGRAPH_MACROS_H__

/* extrinsrt r1, r2, src, size, dst: replace bits [dst:dst+size) in r1
 *  with bits [src:src+size) in r2
 *
 * bra(n)z annul: no delay slot
 */

/* Bitfield version of NVC0_3D_VERTEX_ARRAY_PER_INSTANCE[].
 * Args: size, bitfield
 */
static const uint32_t nvc0_9097_per_instance_bf[] =
{
   0x00000301, /* parm $r3 (the bitfield) */
   0x00000211, /* mov $r2 0 */
   0x05880021, /* maddr [NVC0_3D_VERTEX_ARRAY_PER_INSTANCE(0), increment = 4] */
   0xffffc911, /* mov $r1 (add $r1 -0x1) */
   0x0040d043, /* send (extrshl $r3 $r2 0x1 0) */
   0xffff8897, /* exit branz $r1 0x3 */
   0x00005211  /* mov $r2 (add $r2 0x1) */
};

/* The comments above the macros describe what they *should* be doing,
 * but we use less functionality for now.
 */

/*
 * for (i = 0; i < 8; ++i)
 *    [NVC0_3D_BLEND_ENABLE(i)] = BIT(i of arg);
 *
 * [3428] = arg;
 *
 * if (arg == 0 || [NVC0_3D_MULTISAMPLE_ENABLE] == 0)
 *    [0d9c] = 0;
 * else
 *    [0d9c] = [342c];
 */
static const uint32_t nvc0_9097_blend_enables[] =
{
   0x05360021, /* 0x00: maddr [NVC0_3D_BLEND_ENABLE(0), increment = 4] */
   0x00404042, /* 0x01: send extrinsrt 0 $r1 0 0x1 0 */
   0x00424042, /* 0x02: send extrinsrt 0 $r1 0x1 0x1 0 */
   0x00444042, /* 0x03: send extrinsrt 0 $r1 0x2 0x1 0 */
   0x00464042, /* 0x04: send extrinsrt 0 $r1 0x3 0x1 0 */
   0x00484042, /* 0x05: send extrinsrt 0 $r1 0x4 0x1 0 */
   0x004a4042, /* 0x06: send extrinsrt 0 $r1 0x5 0x1 0 */
   0x004c40c2, /* 0x07: exit send extrinsrt 0 $r1 0x6 0x1 0 */
   0x004e4042, /* 0x08: send extrinsrt 0 $r1 0x7 0x1 0 */
};

/*
 * uint64 limit = (parm(0) << 32) | parm(1);
 * uint64 start = (parm(2) << 32);
 *
 * if (limit) {
 *    start |= parm(3);
 *    --limit;
 * } else {
 *    start |= 1;
 * }
 *
 * [0x1c04 + (arg & 0xf) * 16 + 0] = (start >> 32) & 0xff;
 * [0x1c04 + (arg & 0xf) * 16 + 4] = start & 0xffffffff;
 * [0x1f00 + (arg & 0xf) * 8 + 0] = (limit >> 32) & 0xff;
 * [0x1f00 + (arg & 0xf) * 8 + 4] = limit & 0xffffffff;
 */
static const uint32_t nvc0_9097_vertex_array_select[] =
{
   0x00000201, /* 0x00: parm $r2 */
   0x00000301, /* 0x01: parm $r3 */
   0x00000401, /* 0x02: parm $r4 */
   0x00000501, /* 0x03: parm $r5 */
   0x11004612, /* 0x04: mov $r6 extrinsrt 0 $r1 0 4 2 */
   0x09004712, /* 0x05: mov $r7 extrinsrt 0 $r1 0 4 1 */
   0x05c07621, /* 0x06: maddr $r6 add $6 0x1701 */
   0x00002041, /* 0x07: send $r4 */
   0x00002841, /* 0x08: send $r5 */
   0x05f03f21, /* 0x09: maddr $r7 add $7 0x17c0 */
   0x000010c1, /* 0x0a: exit send $r2 */
   0x00001841, /* 0x0b: send $r3 */
};

/*
 * [GL_POLYGON_MODE_FRONT] = arg;
 *
 * if (BIT(31 of [0x3410]))
 *    [1a24] = 0x7353;
 *
 * if ([NVC0_3D_SP_SELECT(3)] == 0x31 || [NVC0_3D_SP_SELECT(4)] == 0x41)
 *    [02ec] = 0;
 * else
 * if ([GL_POLYGON_MODE_BACK] == GL_LINE || arg == GL_LINE)
 *    [02ec] = BYTE(1 of [0x3410]) << 4;
 * else
 *    [02ec] = BYTE(0 of [0x3410]) << 4;
 */
static const uint32_t nvc0_9097_poly_mode_front[] =
{
   0x00db0215, /* 0x00: read $r2 [NVC0_3D_POLYGON_MODE_BACK] */
   0x020c0315, /* 0x01: read $r3 [NVC0_3D_SP_SELECT(3)] */
   0x00128f10, /* 0x02: mov $r7 or $r1 $r2 */
   0x02100415, /* 0x03: read $r4 [NVC0_3D_SP_SELECT(4)] */
   0x00004211, /* 0x04: mov $r2 0x1 */
   0x00180611, /* 0x05: mov $r6 0x60 */
   0x0014bf10, /* 0x06: mov $r7 and $r7 $r2 */
   0x0000f807, /* 0x07: braz $r7 0xa */
   0x00dac021, /* 0x08: maddr 0x36b */
   0x00800611, /* 0x09: mov $r6 0x200 */
   0x00131f10, /* 0x0a: mov $r7 or $r3 $r4 */
   0x0014bf10, /* 0x0b: mov $r7 and $r7 $r2 */
   0x0000f807, /* 0x0c: braz $r7 0xf */
   0x00000841, /* 0x0d: send $r1 */
   0x00000611, /* 0x0e: mov $r6 0 */
   0x002ec0a1, /* 0x0f: exit maddr [02ec] */
   0x00003041  /* 0x10: send $r6 */
};

/*
 * [GL_POLYGON_MODE_BACK] = arg;
 *
 * if (BIT(31 of [0x3410]))
 *    [1a24] = 0x7353;
 *
 * if ([NVC0_3D_SP_SELECT(3)] == 0x31 || [NVC0_3D_SP_SELECT(4)] == 0x41)
 *    [02ec] = 0;
 * else
 * if ([GL_POLYGON_MODE_FRONT] == GL_LINE || arg == GL_LINE)
 *    [02ec] = BYTE(1 of [0x3410]) << 4;
 * else
 *    [02ec] = BYTE(0 of [0x3410]) << 4;
 */
/* NOTE: 0x3410 = 0x80002006 by default,
 *  POLYGON_MODE == GL_LINE check replaced by (MODE & 1)
 *  SP_SELECT(i) == (i << 4) | 1 check replaced by SP_SELECT(i) & 1
 */
static const uint32_t nvc0_9097_poly_mode_back[] =
{
   0x00dac215, /* 0x00: read $r2 [NVC0_3D_POLYGON_MODE_FRONT] */
   0x020c0315, /* 0x01: read $r3 [NVC0_3D_SP_SELECT(3)] */
   0x00128f10, /* 0x02: mov $r7 or $r1 $r2 */
   0x02100415, /* 0x03: read $r4 [NVC0_3D_SP_SELECT(4)] */
   0x00004211, /* 0x04: mov $r2 0x1 */
   0x00180611, /* 0x05: mov $r6 0x60 */
   0x0014bf10, /* 0x06: mov $r7 and $r7 $r2 */
   0x0000f807, /* 0x07: braz $r7 0xa */
   0x00db0021, /* 0x08: maddr 0x36c */
   0x00800611, /* 0x09: mov $r6 0x200 */
   0x00131f10, /* 0x0a: mov $r7 or $r3 $r4 */
   0x0014bf10, /* 0x0b: mov $r7 and $r7 $r2 */
   0x0000f807, /* 0x0c: braz $r7 0xf */
   0x00000841, /* 0x0d: send $r1 */
   0x00000611, /* 0x0e: mov $r6 0 */
   0x002ec0a1, /* 0x0f: exit maddr [02ec] */
   0x00003041  /* 0x10: send $r6 */
};

/*
 * [NVC0_3D_SP_SELECT(4)] = arg
 *
 * if BIT(31 of [0x3410]) == 0
 *    [1a24] = 0x7353;
 *
 * if ([NVC0_3D_SP_SELECT(3)] == 0x31 || arg == 0x41)
 *    [02ec] = 0
 * else
 * if (any POLYGON MODE == LINE)
 *    [02ec] = BYTE(1 of [3410]) << 4;
 * else
 *    [02ec] = BYTE(0 of [3410]) << 4; // 02ec valid bits are 0xff1
 */
static const uint32_t nvc0_9097_gp_select[] = /* 0x0f */
{
   0x00dac215, /* 0x00: read $r2 0x36b */
   0x00db0315, /* 0x01: read $r3 0x36c */
   0x0012d710, /* 0x02: mov $r7 or $r2 $r3 */
   0x020c0415, /* 0x03: read $r4 0x830 */
   0x00004211, /* 0x04: mov $r2 0x1 */
   0x00180611, /* 0x05: mov $r6 0x60 */
   0x0014bf10, /* 0x06: mov $r7 and $r7 $r2 */
   0x0000f807, /* 0x07: braz $r7 0xa */
   0x02100021, /* 0x08: maddr 0x840 */
   0x00800611, /* 0x09: mov $r6 0x200 */
   0x00130f10, /* 0x0a: mov $r7 or $r1 $r4 */
   0x0014bf10, /* 0x0b: mov $r7 and $r7 $r2 */
   0x0000f807, /* 0x0c: braz $r7 0xf */
   0x00000841, /* 0x0d: send $r1 */
   0x00000611, /* 0x0e: mov $r6 0 */
   0x002ec0a1, /* 0x0f: exit maddr 0xbb */
   0x00003041, /* 0x10: send $r6 */
};

/*
 * [NVC0_3D_SP_SELECT(3)] = arg
 *
 * if BIT(31 of [0x3410]) == 0
 *    [1a24] = 0x7353;
 *
 * if (arg == 0x31) {
 *    if (BIT(2 of [0x3430])) {
 *       int i = 15; do { --i; } while(i);
 *       [0x1a2c] = 0;
 *    }
 * }
 *
 * if ([NVC0_3D_SP_SELECT(4)] == 0x41 || arg == 0x31)
 *    [02ec] = 0
 * else
 * if ([any POLYGON_MODE] == GL_LINE)
 *    [02ec] = BYTE(1 of [3410]) << 4;
 * else
 *    [02ec] = BYTE(0 of [3410]) << 4;
 */
static const uint32_t nvc0_9097_tep_select[] = /* 0x10 */
{
   0x00dac215, /* 0x00: read $r2 0x36b */
   0x00db0315, /* 0x01: read $r3 0x36c */
   0x0012d710, /* 0x02: mov $r7 or $r2 $r3 */
   0x02100415, /* 0x03: read $r4 0x840 */
   0x00004211, /* 0x04: mov $r2 0x1 */
   0x00180611, /* 0x05: mov $r6 0x60 */
   0x0014bf10, /* 0x06: mov $r7 and $r7 $r2 */
   0x0000f807, /* 0x07: braz $r7 0xa */
   0x020c0021, /* 0x08: maddr 0x830 */
   0x00800611, /* 0x09: mov $r6 0x200 */
   0x00130f10, /* 0x0a: mov $r7 or $r1 $r4 */
   0x0014bf10, /* 0x0b: mov $r7 and $r7 $r2 */
   0x0000f807, /* 0x0c: braz $r7 0xf */
   0x00000841, /* 0x0d: send $r1 */
   0x00000611, /* 0x0e: mov $r6 0 */
   0x002ec0a1, /* 0x0f: exit maddr 0xbb */
   0x00003041, /* 0x10: send $r6 */
};

/* NVC0_3D_MACRO_DRAW_ELEMENTS_INDIRECT
 *
 * NOTE: Saves and restores VB_ELEMENT,INSTANCE_BASE.
 *
 * arg     = mode
 * parm[0] = count
 * parm[1] = instance_count
 * parm[2] = start
 * parm[3] = index_bias
 * parm[4] = start_instance
 */
static const uint32_t nvc0_9097_draw_elts_indirect[] =
{
   /* 0x00: parm $r3 (count) */
   /* 0x01: parm $r2 (instance_count) */
   /* 0x02: parm $r4 maddr 0x5f7 (INDEX_BATCH_FIRST, start) */
   /* 0x04: parm $r4 send $r4 (index_bias, send start) */
   /* 0x05: braz $r2 END */
   /* 0x06: parm $r5 (start_instance) */
   /* 0x07: read $r6 0x50d (VB_ELEMENT_BASE) */
   /* 0x08: read $r7 0x50e (VB_INSTANCE_BASE) */
   /* 0x09: maddr 0x150d (VB_ELEMENT,INSTANCE_BASE) */
   /* 0x0a: send $r4 */
   /* 0x0b: send $r5 */
   /* 0x0c: mov $r4 0x1 */
   /* 0x0d: maddr 0x586 (VERTEX_BEGIN_GL) */
   /* 0x0e: send $r1 (mode) */
   /* 0x0f: maddr 0x5f8 (INDEX_BATCH_COUNT) */
   /* 0x10: send $r3 (count) */
   /* 0x11: mov $r2 (sub $r2 $r4) */
   /* 0x12: maddrsend 0x585 (VERTEX_END_GL) */
   /* 0x13: branz $r2 AGAIN */
   /* 0x14: mov $r1 (extrinsrt $r1 $r4 0 1 26) (set INSTANCE_NEXT) */
   /* 0x15: maddr 0x150d (VB_ELEMENT,INSTANCE_BASE) */
   /* 0x16: exit send $r6 */
   /* 0x17: send $r7 */
   /* 0x18: exit */
   /* 0x19: nop */
   0x00000301,
   0x00000201,
   0x017dc451,
   0x00002431,
   0x0004d007,
   0x00000501,
   0x01434615,
   0x01438715,
   0x05434021,
   0x00002041,
   0x00002841,
   0x00004411,
   0x01618021,
   0x00000841,
   0x017e0021,
   0x00001841,
   0x00051210,
   0x01614071,
   0xfffe9017,
   0xd0410912,
   0x05434021,
   0x000030c1,
   0x00003841,
   0x00000091,
   0x00000011
};

/* NVC0_3D_MACRO_DRAW_ARRAYS_INDIRECT:
 *
 * NOTE: Saves and restores VB_INSTANCE_BASE.
 *
 * arg     = mode
 * parm[0] = count
 * parm[1] = instance_count
 * parm[2] = start
 * parm[3] = start_instance
 */
static const uint32_t nvc0_9097_draw_arrays_indirect[] =
{
   /* 0x00: parm $r2 (count) */
   /* 0x01: parm $r3 (instance_count) */
   /* 0x02: parm $r4 maddr 0x35d (VERTEX_BUFFER_FIRST, start) */
   /* 0x04: parm $r4 send $r4 (start_instance) */
   /* 0x05: braz $r3 END */
   /* 0x06: read $r6 0x50e (VB_INSTANCE_BASE) */
   /* 0x07: maddr 0x50e (VB_INSTANCE_BASE) */
   /* 0x08: mov $r5 0x1 */
   /* 0x09: send $r4 */
   /* 0x0a: maddr 0x586 (VERTEX_BEGIN_GL) */
   /* 0x0b: send $r1 (mode) */
   /* 0x0c: maddr 0x35e (VERTEX_BUFFER_COUNT) */
   /* 0x0d: send $r2 */
   /* 0x0e: mov $r3 (sub $r3 $r5) */
   /* 0x0f: maddrsend 0x585 (VERTEX_END_GL) */
   /* 0x10: branz $r3 AGAIN */
   /* 0x11: mov $r1 (extrinsrt $r1 $r5 0 1 26) (set INSTANCE_NEXT) */
   /* 0x12: exit maddr 0x50e (VB_INSTANCE_BASE to restore) */
   /* 0x13: send $r6 */
   /* 0x14: exit nop */
   /* 0x15: nop */
   0x00000201,
   0x00000301,
   0x00d74451,
   0x00002431,
   0x0003d807,
   0x01438615,
   0x01438021,
   0x00004511,
   0x00002041,
   0x01618021,
   0x00000841,
   0x00d78021,
   0x00001041,
   0x00055b10,
   0x01614071,
   0xfffe9817,
   0xd0414912,
   0x014380a1,
   0x00003041,
   0x00000091,
   0x00000011
};

#endif
