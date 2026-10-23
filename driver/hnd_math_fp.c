/* SPDX-License-Identifier: MIT */
#include "hnd_math_fp.h"
/* ARM register-specified shifts use the low count byte; 32 or more gives 0. */
static uint32_t lsl32(uint32_t v,uint32_t n){n&=0xffu;return n>=32?0:v<<n;}
static uint32_t lsr32(uint32_t v,uint32_t n){n&=0xffu;return n>=32?0:v>>n;}
/* Local 64-bit shift routines: counts above31 shift the other word by the
 * byte-truncated remainder. */
static uint64_t shl64(uint64_t v,uint32_t n)
{
    if(n>31)return (uint64_t)lsl32((uint32_t)v,(n-32)&0xffu)<<32;
    return n?v<<n:v;
}
static uint64_t shr64(uint64_t v,uint32_t n)
{
    if(n>31)return lsr32((uint32_t)(v>>32),(n-32)&0xffu);
    return n?v>>n:v;
}
static unsigned bits8(uint32_t b){unsigned n=0;while(b){n++;b>>=1;}return n;}
static unsigned bits32(uint32_t x)
{
    if(x>>16)return x>>24?bits8(x>>24)+24:bits8((x>>16)&0xffu)+16;
    return x>>8?bits8(x>>8)+8:bits8(x);
}
uint8_t axd_hnd_fp_head_room_32(uint32_t value){return (uint8_t)(32u-(uint8_t)bits32(value));}
uint8_t axd_hnd_fp_head_room_64(uint64_t value)
{
    uint32_t high=(uint32_t)(value>>32);
    return (uint8_t)((high?32u:64u)-(uint8_t)bits32(high?high:(uint32_t)value));
}
/* math_uint64_multiple_add: (*high:*low) = a*b + c, full 64 bits. */
static void multiple_add(uint32_t *high,uint32_t *low,uint32_t a,uint32_t b,uint32_t c)
{uint64_t r=(uint64_t)a*b+c;*high=(uint32_t)(r>>32);*low=(uint32_t)r;}
void axd_hnd_uint64_divide(uint32_t *quotient,uint32_t high,uint32_t low,uint32_t divisor)
{
    uint32_t q=0,step,carry;
    if(divisor<=1)return;
    if(high){
        step=0xffffffffu/divisor;
        carry=(0xffffffffu%divisor+1u)%divisor;
        while(high){q+=high*step;multiple_add(&high,&low,carry,high,low);}
    }
    *quotient=low/divisor+q;
}
uint64_t axd_hnd_fp_mult_64(uint64_t a,uint64_t b,uint8_t qa,uint8_t qb,uint8_t qout)
{
    uint8_t shift=(uint8_t)(qa+qb-qout);uint8_t half=(uint8_t)(shift-1u);
    uint64_t product=a*b,round=half?shl64(1,half):1;
    if(!shift)return product;
    return shr64(round+product,shift);
}
uint8_t axd_hnd_fp_div_64(uint64_t numerator,uint32_t denominator,uint8_t qn,uint8_t qd,uint32_t *quotient)
{
    uint8_t room=(uint8_t)(axd_hnd_fp_head_room_64(numerator)-1u-axd_hnd_fp_head_room_32(denominator));
    uint8_t shift=(uint8_t)(qd-qn+31u);
    if(room<shift)shift=room;
    if(shift)numerator=shl64(numerator,shift);
    axd_hnd_uint64_divide(quotient,(uint32_t)(numerator>>32),(uint32_t)numerator,denominator);
    return (uint8_t)(shift+(uint8_t)(qn-qd));
}
uint32_t axd_hnd_fp_round_32(uint32_t value,uint32_t shift)
{return lsr32((shift?lsl32(1,shift-1u):0)+value,shift);}
uint32_t axd_hnd_fp_floor_32(uint32_t value,uint32_t shift){return lsr32(value,shift);}
uint32_t axd_hnd_fp_round_64(uint64_t value,uint32_t shift)
{
    uint32_t half=(shift-1u)&0xffu;uint64_t round=half?shl64(1,half):1;
    if(!shift)return (uint32_t)value;
    return (uint32_t)shr64(round+value,shift);
}
uint64_t axd_hnd_fp_ceil_64(uint64_t value,uint32_t shift)
{
    if(!shift)return value;
    return shr64(shl64(1,shift)+(value-1u),shift);
}
