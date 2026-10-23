/* SPDX-License-Identifier: MIT */
#ifndef AXD_TEST_RADIO_REGTBL_TRACE_H
#define AXD_TEST_RADIO_REGTBL_TRACE_H
#include "../../driver/radio_regtbl.h"
static struct axd_radio_regtbl_context rrt_context(unsigned cold)
{
    /* Original synthetic payloads AND descriptors: ids/counts/widths are
     * arbitrary (count*stride<=600), not the stock rev51 rows, which stay
     * private. Only the nine/eight row counts follow the pinned globals. */
    static uint8_t payload[9][600];
    static struct axd_phy_table_write_request tables[9];
    static const struct axd_phy_table_write_request zeros[]={
        {9,300,0,8,NULL,0},{14,260,0,32,NULL,0},{44,96,0,32,NULL,0},{58,64,0,32,NULL,0},
        {72,140,0,16,NULL,0},{86,100,0,16,NULL,0},{120,320,0,32,NULL,0},{130,200,0,32,NULL,0}
    };
    static const unsigned ids[]={7,12,33,40,51,70,81,90,110};
    static const unsigned counts[]={100,60,70,120,90,24,200,180,40};
    static const unsigned widths[]={16,8,32,32,32,48,16,16,32};
    struct axd_radio_regtbl_context c={0};unsigned i;
    c.hardware=context().radio.hardware;c.chanspec=(struct axd_wake_u32){0xd024,1};
    c.bmac_pointer=(struct axd_wake_u32){0x18000,1};
    c.bmac_device_pointer=(struct axd_wake_u32){0x19000,1};c.bmac_word150=(struct axd_wake_u32){0,1};
    c.tpc_pointer=(struct axd_wake_u32){0x10000,1};c.tpc_data_pointer=(struct axd_wake_u32){0x11000,1};
    c.tpc_shared_pointer=(struct axd_wake_u32){0x12000,1};c.papd_pointer=(struct axd_wake_u32){0x13000,1};
    c.papd_data_pointer=(struct axd_wake_u32){0x14000,1};
    c.tpc_two_range2g=(struct axd_wake_u8){1,0};c.tpc_two_range5g=(struct axd_wake_u8){1,0};
    c.flag470=(struct axd_wake_u8){1,0};c.flag471=(struct axd_wake_u8){1,0};
    c.load_static=(struct axd_wake_u8){1,(uint8_t)cold};
    c.papd_byte0=(struct axd_wake_u8){1,0};c.papd_byte4=(struct axd_wake_u8){1,0};
    for(i=0;i<9;i++){
        unsigned stride=widths[i]/8;
        memset(payload[i],(int)(0x31+i),sizeof(payload[i]));
        tables[i]=(struct axd_phy_table_write_request){ids[i],counts[i],0,widths[i],payload[i],counts[i]*stride};
    }
    c.tables=(struct axd_radio_regtbl_span){tables,9,1};
    c.zeros=(struct axd_radio_regtbl_span){zeros,8,1};return c;
}
/* Independent immediate/source-order oracle. Shared radio_power_trace.h
 * supplies event allocation/barriers/packed helpers; production is not read. */
static void rrt_read(struct script *s,uint16_t selector,uint16_t sample)
{st(s);add(s,AXD_D11_WRITE16,0x3fc,selector,0);add(s,AXD_D11_READ16,0x3fe,0,sample);sy(s);}
static void rrt_modify(struct script *s,uint16_t selector,uint16_t mask,uint16_t value)
{
    st(s);add(s,AXD_D11_WRITE16,0x3fc,selector,0);st(s);
    add(s,AXD_D11_READ16,0x3fe,0,0x1a5a5);sy(s);
    add(s,AXD_D11_WRITE16,0x3fe,(0xa5a5u&~mask)|(value&mask),0);
}
static void rrt_table(struct script *s,uint16_t id,uint16_t offset,unsigned width,
                         const uint8_t *data,unsigned count,unsigned ext)
{
    unsigned i,k,stride=width==8?1:width==16?2:width==32?4:width<=48?6:8;
    if(!ext){add(s,AXD_D11_READ32,0x120,0,0);sy(s);rrt_read(s,0x19e,2);}
    packed(s,0xd,id);packed(s,0xe,offset);
    for(i=0;i<count;i++){
        const uint8_t *b=data+i*stride;
        if(width==8)packed(s,0xf,b[0]);
        else if(width==16)packed(s,0xf,(uint16_t)(b[0]|((unsigned)b[1]<<8)));
        else if(width==32){packed(s,0x10,(uint16_t)(b[2]|((unsigned)b[3]<<8)));packed(s,0xf,(uint16_t)(b[0]|((unsigned)b[1]<<8)));}
        else for(k=0;k<stride/2;k++){
            uint16_t v=(uint16_t)(b[2*k]|((unsigned)b[2*k+1]<<8));
            if(!k)packed(s,0x11,v);else{st(s);add(s,AXD_D11_WRITE16,0x3fe,v,0);}
        }
    }
}
static void rrt_full(struct script *s,unsigned cores,unsigned tssi,unsigned zero_tssi,
                         const struct axd_radio_regtbl_context *context)
{
    static const uint16_t first[]={0x410,0x473e,0x4725,0x4722,0x4723,0x4724,0x4725,0x4726,0x4727,0x4750,0x4728,0x4720,
        0x474d,0x474e,0x474f,0x4746,0x4689,0x5700,0x5702,0x5704,0x570a,0x570b};
    static const uint16_t last[]={0x416,0x408,0x410,0x4749,0x473e,0x4725,0x4720,0x4721,0x4722,0x4723,0x4724,
        0x5704,0x474d,0x4726,0x4727,0x5702,0x5700,0x4750,0x417,0x416};
    static const uint8_t zeros[8]={0};unsigned i,k;
    for(i=0;i<sizeof(first)/sizeof(first[0]);i++)packed(s,first[i],i==0?0x77:i==10?0x80:i==11?0x180:0);
    rrt_modify(s,0x408,0x800,0);
    for(i=0;i<cores;i++)rrt_modify(s,(uint16_t)(0x747+i*0x200),0xf,0xf);
    packed(s,0x4729,0);packed(s,0x4721,0x5000);rrt_read(s,0x19e,0x1232);rrt_modify(s,0x19e,2,2);
    if(context->load_static.value){
        for(i=0;i<9;i++){
            const struct axd_phy_table_write_request *q=&context->tables.entries[i];
            rrt_table(s,(uint16_t)q->table_id,(uint16_t)q->offset,q->width,q->data,q->count,1);
        }
        for(i=0;i<8;i++){
            const struct axd_phy_table_write_request *q=&context->zeros.entries[i];
            for(k=0;k<q->count;k++)rrt_table(s,(uint16_t)q->table_id,(uint16_t)k,q->width,zeros,1,0);
        }
    }
    rrt_modify(s,0x19e,2,2);packed(s,0x4645,0x25c);if(tssi)packed(s,0x464c,0x25c);
    if(zero_tssi)for(i=0;i<128;i++){rrt_table(s,0x5d,(uint16_t)i,8,zeros,1,0);rrt_table(s,0x7d,(uint16_t)i,8,zeros,1,0);}
    for(i=0;i<sizeof(last)/sizeof(last[0]);i++)packed(s,last[i],i==1?0x306:i==2?1:i==18?4:i==19?0xd:0);
    rrt_modify(s,0x408,2,2);packed(s,0x474c,0);rrt_modify(s,0x129f,1,1);
    for(i=0;i<228;i++)rrt_table(s,0x3f,(uint16_t)i,32,zeros,1,0);
    for(i=0;i<7;i++){
        /* Six actual stack bytes constructed by immediate instructions, not
         * a borrowed proprietary table. Each row's untouched fields persist. */
        static const uint8_t words[7][6]={{0x3c,0x70,0x1c,0,0,0},{0x14,0x70,0x1c,0,0,0},
            {0x0e,0x70,0x2c,0,0,0},{0x2c,0x70,0x2c,0,0,0},{0x20,0x60,0x20,0,0,0},
            {0,4,0,0,0,0},{0}};
        rrt_table(s,0x14,(uint16_t)i,48,words[i],1,0);
    }
}
#endif
