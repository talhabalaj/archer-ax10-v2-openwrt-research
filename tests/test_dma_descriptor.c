/* SPDX-License-Identifier: MIT */
#include "../driver/dma_descriptor.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#define N(a) (sizeof(a) / sizeof((a)[0]))
/* Literal fixtures derived from current assembly, not production helpers.
 * Mapped addresses are synthetic numbers, never CPU pointers or board values. */
static void format_literals(void)
{
    const struct {
        struct axd_dma64_format input;
        struct axd_dma64_descriptor expected;
    } cases[]={
        {{0x12345678,0,0x89abcdef,0xfedcba98,0x12345678,0},
         {0xfedcba98,0x5678,0x12345678,0x89abcdef}},
        {{0xc1234567,0,0x87654321,0x12345678,0x8001,0},
         {0x12345678,1,0xc1234567,0x87654321}},
        {{0xc1234567,0x40000000,0x87654321,0x12345678,0x8001,0},
         {0x12345678,0x30001,0x41234567,0x87654321}},
        {{0x81234567,0x40000000,0x11111111,0x80000000,0xffff,0},
         {0x80000000,0x27fff,0x41234567,0x11111111}},
        {{0x41234567,0x80000000,0x22222222,0x10000000,0x80000000,0},
         {0x10000000,0x10000,0x81234567,0x22222222}},
        /* Low addition wraps; there is no implicit high-word carry. */
        {{0x3fffffff,0xffffffff,0x12345678,0x20000000,0x10000,0},
         {0x20000000,0,0x3ffffffe,0x12345678}},
        {{0,0,0,0,1,1},{0,0x40001,0,0}},
        {{1,0,0,0,1,1},{0,1,1,0}},
        {{0xc0000000,0x40000000,0,0,0,1},{0,0x70000,0x40000000,0}},
        {{0,0,0,0,0,1},{0,0,0,0}}
    };
    size_t i;struct axd_dma64_descriptor d;
    for(i=0;i<N(cases);i++) {
        assert(axd_dma64_format(&d,&cases[i].input)==AXD_DMA64_OK);
        assert(!memcmp(&d,&cases[i].expected,sizeof(d)));
    }
    {
        union {struct axd_dma64_format f;struct axd_dma64_descriptor d;} a;
        a.f=cases[2].input;
        assert(axd_dma64_format(&a.d,&a.f)==AXD_DMA64_OK);
        assert(!memcmp(&a.d,&cases[2].expected,sizeof(a.d)));
    }
    {
        struct axd_dma64_format f=cases[0].input;
        struct axd_dma64_descriptor before={1,2,3,4};d=before;f.parity_enabled=2;
        assert(axd_dma64_format(&d,&f)==AXD_DMA64_ARGUMENT && !memcmp(&d,&before,sizeof(d)));
        assert(axd_dma64_format(NULL,&f)==AXD_DMA64_ARGUMENT);
        assert(axd_dma64_format(&d,NULL)==AXD_DMA64_ARGUMENT);
    }
}
static unsigned bit_count(uint32_t value)
{
    unsigned n=0;while(value) {n+=value&1u;value>>=1;}return n;
}
static void parity_exhaustive(void)
{
    unsigned word,bit;
    for(word=0;word<4;word++)for(bit=0;bit<32;bit++) {
        struct axd_dma64_format f={0,0,0,0,0,1};struct axd_dma64_descriptor d;
        uint32_t one=UINT32_C(1)<<bit;
        if(word==0)f.control1=one;
        if(word==1)f.byte_count=one;
        if(word==2)f.mapped_address_low=one;
        if(word==3)f.data_offset_high=one;
        assert(axd_dma64_format(&d,&f)==AXD_DMA64_OK);
        assert((bit_count(d.control1)+bit_count(d.control2)+bit_count(d.address_low)+bit_count(d.address_high))%2==0);
        assert(!!(d.control2&AXD_DMA64_PARITY)==(word!=1 || bit<15));
    }
}
static void codec(void)
{
    const uint8_t expected[]={0x67,0x45,0x23,0xf1,0xef,0xcd,0xab,0x89,
                             0x98,0xba,0xdc,0xfe,0x10,0x32,0x54,0x76};
    const struct axd_dma64_descriptor d={0xf1234567,0x89abcdef,0xfedcba98,0x76543210};
    uint8_t buffer[18];struct axd_dma64_descriptor out,before={1,2,3,4};size_t n;
    memset(buffer,0xa5,sizeof(buffer));
    assert(axd_dma64_encode(buffer+1,16,&d)==AXD_DMA64_OK);
    assert(buffer[0]==0xa5 && buffer[17]==0xa5 && !memcmp(buffer+1,expected,16));
    assert(axd_dma64_decode(&out,buffer+1,16)==AXD_DMA64_OK && !memcmp(&out,&d,sizeof(d)));
    for(n=0;n<18;n++)if(n!=16) {
        uint8_t b[18];memset(b,0xa5,sizeof(b));out=before;
        assert(axd_dma64_encode(b,n,&d)==AXD_DMA64_BOUNDS);
        assert(axd_dma64_decode(&out,expected,n)==AXD_DMA64_BOUNDS);
        assert(!memcmp(&out,&before,sizeof(out)));
        for(size_t i=0;i<N(b);i++)assert(b[i]==0xa5);
    }
    {
        union {struct axd_dma64_descriptor d;uint8_t b[16];} a;
        a.d=d;assert(axd_dma64_encode(a.b,16,&a.d)==AXD_DMA64_OK);
        assert(!memcmp(a.b,expected,16));
        assert(axd_dma64_decode(&a.d,a.b,16)==AXD_DMA64_OK && !memcmp(&a.d,&d,sizeof(d)));
    }
    assert(axd_dma64_encode(NULL,16,&d)==AXD_DMA64_ARGUMENT);
    assert(axd_dma64_decode(NULL,expected,16)==AXD_DMA64_ARGUMENT);
}
static void rings(void)
{
    const uint32_t sizes[]={2,4,8,16,512,4096,32768};
    size_t k;uint32_t in,out,used,available,pointer,index;
    for(k=0;k<N(sizes);k++) {
        uint32_t count=sizes[k];
        /* Full enumeration for small rings; representative wrap positions for
         * larger ones, avoiding a quadratic test masquerading as hardware QA. */
        for(in=0;in<count;in+=count>16 ? count-1 : 1)for(out=0;out<count;out+=count>16 ? count-1 : 1) {
            struct axd_dma64_ring r={count,0xfffff,0x12300000,in,out},before=r;
            uint32_t expected=out>=in ? out-in : count-in+out;
            assert(axd_dma64_ring_counts(&r,&used,&available)==AXD_DMA64_OK);
            assert(used==expected && available==count-expected-1);
            assert(axd_dma64_producer(&r,&pointer)==AXD_DMA64_OK && pointer==0x12300000+16*out);
            assert(axd_dma64_status_index(&r,0xa5a00000+16*out,&index)==AXD_DMA64_OK && index==out);
            assert(axd_dma64_post(&r,available+1)==AXD_DMA64_FULL && !memcmp(&r,&before,sizeof(r)));
            assert(axd_dma64_reclaim(&r,used+1)==AXD_DMA64_EMPTY && !memcmp(&r,&before,sizeof(r)));
            assert(axd_dma64_reclaim(&r,used)==AXD_DMA64_OK && r.in==out);
            assert(axd_dma64_post(&r,count-1)==AXD_DMA64_OK && r.out==(out ? out-1 : count-1));
            assert(axd_dma64_ring_counts(&r,&used,&available)==AXD_DMA64_OK && used==count-1 && !available);
        }
    }
    {
        struct axd_dma64_ring r={8,0x1fff,0xffffe100,6,2};
        assert(axd_dma64_producer(&r,&pointer)==AXD_DMA64_OK && pointer==0xffffe120);
        assert(axd_dma64_status_index(&r,0xf0000170,&index)==AXD_DMA64_OK && index==7);
        index=0xdeadbeef;assert(axd_dma64_status_index(&r,0x111,&index)==AXD_DMA64_BOUNDS && index==0xdeadbeef);
        assert(axd_dma64_status_index(&r,0x180,&index)==AXD_DMA64_BOUNDS);
    }
}
static void ring_rejections(void)
{
    const struct axd_dma64_ring invalid[]={
        {0,0x1fff,0,0,0},{1,0x1fff,0,0,0},{3,0x1fff,0,0,0},{65536,0xfffff,0,0,0},
        {8,0x1ff0,0,0,0},{8,0xf,0,0,0},{8,0x1fff,1,0,0},{8,0x1fff,0,8,0},
        {8,0x1fff,0,0,8},{512,0x1fff,0x10,0,0},{2,UINT32_MAX,0xfffffff0,0,0}
    };
    uint32_t a,b;size_t i;
    for(i=0;i<N(invalid);i++) {
        struct axd_dma64_ring r=invalid[i],before=r;
        assert(axd_dma64_ring_check(&r)==AXD_DMA64_BOUNDS);
        a=0xdeadbeef;b=0xcafebabe;
        assert(axd_dma64_ring_counts(&r,&a,&b)==AXD_DMA64_BOUNDS && a==0xdeadbeef && b==0xcafebabe);
        assert(axd_dma64_post(&r,1)==AXD_DMA64_BOUNDS && !memcmp(&r,&before,sizeof(r)));
        assert(axd_dma64_reclaim(&r,1)==AXD_DMA64_BOUNDS && !memcmp(&r,&before,sizeof(r)));
    }
    assert(axd_dma64_ring_check(NULL)==AXD_DMA64_ARGUMENT);
    assert(axd_dma64_post(NULL,1)==AXD_DMA64_ARGUMENT);
}
static void ranges(void)
{
    struct axd_dma64_ring r={8,0x1fff,0x180,6,3};uint32_t n=99;
    assert(axd_dma64_reclaimable(&r,AXD_DMA64_ALL,UINT32_MAX,UINT32_MAX,0,&n)==AXD_DMA64_OK && n==5);
    assert(axd_dma64_reclaimable(&r,AXD_DMA64_TRANSMITTED,0xf0000190,0,0,&n)==AXD_DMA64_OK && n==3);
    assert(axd_dma64_reclaimable(&r,AXD_DMA64_TRANSFERRED,0x190,0x190,0xffff,&n)==AXD_DMA64_OK && n==3);
    assert(axd_dma64_reclaimable(&r,AXD_DMA64_TRANSFERRED,0x1a0,0x190,0xffff,&n)==AXD_DMA64_OK && n==2);
    /* previous(active0) wraps to7 rather than underflowing outside the ring. */
    assert(axd_dma64_reclaimable(&r,AXD_DMA64_TRANSFERRED,0x190,0x180,0xffff,&n)==AXD_DMA64_OK && n==1);
    n=99;assert(axd_dma64_reclaimable(&r,AXD_DMA64_TRANSMITTED,0x1d0,0,0,&n)==AXD_DMA64_BOUNDS && n==99);
    assert(axd_dma64_reclaimable(&r,AXD_DMA64_TRANSFERRED,0x190,0x181,0xffff,&n)==AXD_DMA64_BOUNDS);
    assert(axd_dma64_reclaimable(&r,AXD_DMA64_ALL+3,0,0,0,&n)==AXD_DMA64_ARGUMENT);
    assert(axd_dma64_reclaimable(&r,AXD_DMA64_ALL,0,0,0,NULL)==AXD_DMA64_ARGUMENT);
}
int main(void)
{
    format_literals();parity_exhaustive();codec();rings();ring_rejections();ranges();
    puts("DMA64 current descriptor formatting, codec and explicit ring arithmetic passed");return 0;
}
