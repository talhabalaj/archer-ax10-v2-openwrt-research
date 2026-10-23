/* SPDX-License-Identifier: MIT */
#include "radio20704_pll.h"
#include "hnd_math_fp.h"
#define V(offset) values[(offset) / 2u]
/* Low word of the HND 64-bit multiply, as most actual call sites consume it. */
static uint32_t mul32(uint64_t a, uint64_t b, uint8_t qa, uint8_t qb, uint8_t qo)
{return (uint32_t)axd_hnd_fp_mult_64(a, b, qa, qb, qo);}
/* div_64 followed by round_32(quotient, format-16) as a byte shift. The actual
 * divide leaves its output untouched for divisors 0/1, which would read an
 * uninitialized stack slot: such inputs are refused (*ok cleared). */
static uint32_t div_round(uint64_t numerator, uint32_t divisor, uint8_t qn, uint8_t qd, unsigned *ok)
{
    uint32_t q = 0;
    uint8_t format;
    if (divisor <= 1) { *ok = 0; return 0; }
    format = axd_hnd_fp_div_64(numerator, divisor, qn, qd, &q);
    return axd_hnd_fp_round_32(q, (uint8_t)(format - 16u));
}
/* Clamped positive-difference field: 0 for a difference below1, otherwise the
 * rounded16.16 quotient (round shift is the returned format) limited to limit. */
static uint16_t clamp_div(int64_t difference, uint32_t divisor, uint32_t limit)
{
    uint32_t q = 0;
    uint8_t format;
    int32_t v;
    if (difference < 1) return 0;
    format = axd_hnd_fp_div_64((uint64_t)difference, divisor, 16, 16, &q);
    v = (int32_t)axd_hnd_fp_round_32(q, format);
    if (v < 0) return 0;
    return (uint16_t)((uint32_t)v > limit ? limit : (uint32_t)v);
}
static uint32_t frequency_scale(uint32_t f)
{
    uint32_t k = f - 0x97bu, a = 1, b = f != 0x9a3u && f != 0x99eu;
    if (k < 0x1f) a = ((0x40100021u >> k) & 1u) ^ 1u;
    if ((f & ~0x20u) == 0x994u) b = 0;
    if (!(a & b)) return 0xaaaaaaaau;
    if (f < 0xbb9u) return 0xc0000000u;
    if (f == 0x143cu || f == 0x1351u) return 0x60000000u;
    if (f < 0x1892u) return 0x55555555u;
    return 0x40000000u;
}
int axd_radio20704_pll_compute(uint16_t frequency, const struct axd_radio20704_pll_config *config,
                               uint16_t values[AXD_R20704_PLL_FIELDS])
{
    uint32_t f = frequency, reference, q = 0, x44, x3c, ceil16, uv5, base, a_lo, c, d, e, g, l40, ic, k;
    uint32_t l38, l34, l30, l2c, lo1, lo2, mode13 = 0, u4, u10, u14, u18, u21, u22, v74;
    uint64_t product, a, lv26;
    unsigned ok = 1;
    uint8_t format;
    int32_t v;
    if (!config || !values) return -1;
    product = axd_hnd_fp_mult_64(f, frequency_scale(f), 0, 31, 19);
    if (config->mode > 2) return 1;	/* actual early return: no stores */
    mode13 = config->mode == 1;
    /* ARM register shift: a count of32 or more gives zero. */
    reference = (config->shift & 0xffu) >= 32 ? 0 : config->reference << (config->shift & 0xffu);
    if (reference <= 1 || (uint32_t)product <= 1) return -1;
    format = axd_hnd_fp_div_64((uint32_t)product, reference, 19, 24, &q);
    x44 = axd_hnd_fp_round_32(q, (uint8_t)(format - 21u));
    format = axd_hnd_fp_div_64(f, (uint32_t)product, 0, 19, &q);
    x3c = axd_hnd_fp_floor_32(q, (uint8_t)(format - 16u));
    lo1 = mul32(x3c << 1, config->reference, 16, 24, 16);
    if (lo1 <= 1) return -1;
    ceil16 = (uint16_t)axd_hnd_fp_ceil_64(lo1, 16);
    V(0x16) = (uint16_t)ceil16;
    uv5 = (uint16_t)axd_hnd_fp_round_64((uint32_t)((div_round(ceil16, lo1, 0, 16, &ok) - 0x10000u) << 13), 16);
    V(0x1a) = (uint16_t)uv5;
    {   /* Thresholds on the scaled frequency (low word). */
        uint32_t p = (uint32_t)product, hi, lo;
        if (p <= 0x63ffffffu) { hi = 0xde4; lo = 0xce4; }
        else if (p <= 0x6a3fffffu) { hi = 0xbf0; lo = 0xaf0; }
        else if (p <= 0x707fffffu) { hi = 0x89e; lo = 0x79e; }
        else if (p <= 0x76bfffffu) { hi = 0x614; lo = 0x514; }
        else if (p < 0x7d000000u) { hi = 0x3ee; lo = 0x2ee; }
        else { hi = 0x290; lo = 0x190; }
        V(0x1e) = (uint16_t)lo; V(0x20) = (uint16_t)hi;
    }
    if (f <= 0xbb8u) {
        base = 0x98a; V(0x1c) = 3; V(0x22) = 0xffd9; V(0x24) = 0x27; V(0x82) = 0x19;
    } else {
        base = f < 0x157cu ? (f < 0x1482u ? 0x13ce : 0x14fa) : f < 6000u ? 0x1644 : f < 0x1964u ? 0x17d4 :
               f < 7000u ? 0x19c8 : f < 0x1d4cu ? 0x1bbc : f < 8000u ? 0x1db0 : 0x1fa4;
        V(0x1c) = f < 0x157cu ? 5 : 8; V(0x22) = 0xffce; V(0x24) = 0x32; V(0x82) = 0x28;
    }
    V(0x28) = 1;
    V(0x38) = (uint16_t)(base + axd_hnd_fp_round_64((uint32_t)(base * uv5 * 8u), 16));
    V(0x46) = (uint16_t)f;
    /* Mode-dependent constants (mode2, mode0, mode1 as in the actual code). */
    if (config->mode == 2) { v74 = 1; u4 = 1; u21 = 1; u14 = 0; u10 = 0; u22 = 0; u18 = 0; }
    else if (!mode13) { v74 = 0; u18 = 0xfff; u14 = 0; u21 = 0; u10 = 0; u4 = 1; u22 = 1; }
    else { v74 = 0; u14 = 1; u10 = 0xfff; u4 = 0; u22 = 0; u21 = 1; u18 = 0; }
    V(0x74) = (uint16_t)v74; V(0x7a) = 3; V(0x6e) = 0;
    V(0x70) = (uint16_t)u4; V(0x72) = (uint16_t)u21; V(0x48) = (uint16_t)u14; V(0x4a) = (uint16_t)u10;
    V(0x86) = (uint16_t)u22; V(0x88) = (uint16_t)u18;
    V(0x4c) = (uint16_t)(x44 >> 16); V(0x4e) = (uint16_t)x44;
    V(0x50) = 9; V(0x52) = 10; V(0x54) = 1; V(0x56) = 3; V(0x58) = 5;
    /* Loop-filter chain: 64-bit pool constants, Q-format products, and the
     * actual low-word truncations (A<<1, the 1e9 product) kept exactly. */
    a = axd_hnd_fp_mult_64(0xa1fd8938u, 700, 30, 0, 20);
    a_lo = (uint32_t)a;
    c = mul32(0x225247cbu, 9, 32, 0, 26) + mul32(0x45ec80c7u, 1, 32, 0, 26);
    d = mul32(0x2f4b6bu, product, 32, 19, 26);
    e = mul32(d, d, 26, 26, 26);
    g = mul32(mul32(d, e, 26, 26, 26), c, 26, 26, 26);
    l40 = div_round(x44, g, 21, 26, &ok);
    ic = mul32(mul32(l40, 700, 16, 0, 16), 0x9d3e9du, 16, 32, 16);
    if (ic >= 0x28000u) ic = 0x28000u;
    k = mul32(div_round((uint32_t)(a_lo << 1), ic, 20, 16, &ok), l40, 16, 16, 16);
    lo2 = mul32(l40, mul32(a_lo, a_lo, 20, 20, 8), 16, 8, 8);
    l38 = div_round(mul32(ic, 1000000000u, 16, 0, 0), lo2, 0, 8, &ok);
    l34 = div_round(l38, 9, 16, 0, &ok);
    lv26 = axd_hnd_fp_mult_64(k, 0x12000, 16, 16, 16);
    if (!ok) return -1;
    V(0x5a) = V(0x5c) = clamp_div((int64_t)k - 0x1012e14, 0x11935c3u, 255);
    V(0x5e) = clamp_div((int64_t)(lv26 << 3) - 0x1012e14, 0x11935c3u, 255);
    V(0x60) = clamp_div((int64_t)lv26 - 0x1012e14, 0x11935c3u, 255);
    V(0x62) = clamp_div((int64_t)l34 - 0x3430c, 0x810d, 255);
    l30 = div_round(l38, 0x14, 16, 0, &ok);
    V(0x64) = clamp_div((int64_t)l30 - 0x7d1f9, 0x37b3, 255);
    l2c = div_round(l38, 0x28, 16, 0, &ok);
    V(0x66) = clamp_div((int64_t)l2c - 0x3e34d, 0x1c50, 255);
    V(0x68) = clamp_div((int64_t)(l2c >> 1) - 0x1c3d0, 0xe2d, 255);
    v = (int32_t)axd_hnd_fp_round_32(mul32(0x1cfd2fu, mul32(ic, ic, 16, 16, 16), 16, 16, 16) - 0xd26b1u +
                                     mul32(0x6477333u, ic, 16, 16, 16), 22);
    V(0x6c) = (uint16_t)(v < 0 ? 0 : v > 127 ? 127 : v);
    v = (int32_t)axd_hnd_fp_round_32(mul32(mul32(0x10000, reference, 16, 24, 16), 0x147ae, 16, 16, 16), 16);
    V(0x6a) = (uint16_t)(v < 0 ? 0 : v > 255 ? 255 : v);
    return 0;
}
