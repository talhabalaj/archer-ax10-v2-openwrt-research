/* SPDX-License-Identifier: MIT */
#include "../driver/hnd_math_fp.h"
#include <assert.h>
#include <stdio.h>
/* Hand-derived literal vectors. The pinned-instruction comparison lives in
 * check_hnd_math_arm.py; this host test needs no private input. */
int main(void)
{
    uint32_t q=0xa5a5a5a5u;
    assert(axd_hnd_fp_head_room_32(0)==32 && axd_hnd_fp_head_room_32(1)==31 && axd_hnd_fp_head_room_32(0x80000000u)==0);
    assert(axd_hnd_fp_head_room_64(0)==64 && axd_hnd_fp_head_room_64((uint64_t)1<<40)==23 && axd_hnd_fp_head_room_64(0xffffffffu)==32);
    assert(axd_hnd_fp_round_32(5,1)==3 && axd_hnd_fp_round_32(4,1)==2 && axd_hnd_fp_round_32(9,0)==9);
    assert(axd_hnd_fp_round_32(0xffffffffu,32)==0 && axd_hnd_fp_floor_32(5,1)==2 && axd_hnd_fp_floor_32(5,256)==5);
    assert(axd_hnd_fp_floor_32(5,32)==0 && axd_hnd_fp_round_64(((uint64_t)3<<32)|1,32)==3);
    assert(axd_hnd_fp_round_64(7,0)==7 && axd_hnd_fp_round_64(6,2)==2);
    assert(axd_hnd_fp_ceil_64(5,1)==3 && axd_hnd_fp_ceil_64(4,1)==2 && axd_hnd_fp_ceil_64(9,0)==9);
    assert(axd_hnd_fp_mult_64(3,5,1,1,1)==8 && axd_hnd_fp_mult_64(3,5,0,0,0)==15);
    axd_hnd_uint64_divide(&q,1,0,1);assert(q==0xa5a5a5a5u);
    axd_hnd_uint64_divide(&q,1,0,0);assert(q==0xa5a5a5a5u);
    /* Actual quirk: the carry (0xffffffff%d+1)%d drops one unit when it equals
     * d, so 2^32/2 yields 0x7fffffff, exactly as the pinned instructions do. */
    axd_hnd_uint64_divide(&q,1,0,2);assert(q==0x7fffffffu);
    axd_hnd_uint64_divide(&q,1,0,3);assert(q==0x55555555u);
    axd_hnd_uint64_divide(&q,0,100,7);assert(q==14);
    q=0;assert(axd_hnd_fp_div_64((uint64_t)1<<20,2,0,0,&q)==12 && q==0x7fffffffu);
    puts("HND fixed-point helpers: literal shift/round/ceil/head-room/divide vectors passed");return 0;
}
