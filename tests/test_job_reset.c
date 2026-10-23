/* SPDX-License-Identifier: MIT */
#include "../driver/core_reset.h"
#include "../driver/phy_scheduler.h"
#include "../driver/job_reset.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Kernel-stack-safe begin: no stack snapshots. Distinct inputs are copied,
 * an input that is the job's own member is kept, every other byte is zeroed,
 * and inputs that partially overlap the job are rejected. */
int main(void)
{
    unsigned char raw[16];size_t k;
    struct axd_core_reset *j=calloc(1,sizeof(*j));
    struct axd_core_reset_context *c=calloc(1,sizeof(*c));
    struct axd_phy_scheduler *s=calloc(1,sizeof(*s));
    assert(j && c && s);
    memset(raw,0xa5,sizeof(raw));axd_job_zero_except(raw,16,2,3,10,2);
    for(k=0;k<16;k++)assert(raw[k]==(((k>=2&&k<5)||(k>=10&&k<12))?0xa5:0));
    assert(!axd_job_overlaps(raw,16,NULL,4,raw) && !axd_job_overlaps(raw,16,raw+2,4,raw+2));
    assert(axd_job_overlaps(raw,16,raw+14,4,raw) && !axd_job_overlaps(raw,8,raw+8,4,raw));
    /* Distinct context: copied, stale job state cleared. */
    c->clock.d11.core=130;c->clock.d11.minor=1;c->band_flags=0x1234;
    memset(j,0x5a,sizeof(*j));
    assert(!axd_core_reset_begin(j,7,c) && j->phase==AXD_CORE_RUNNING && j->flags==7);
    assert(j->context.band_flags==0x1234 && j->attempted_ops==0 && j->completed_ops==0);
    /* Own-member alias: preserved exactly. */
    {struct axd_core_reset_context keep=j->context;
        assert(!axd_core_reset_begin(j,9,&j->context) && !memcmp(&keep,&j->context,sizeof(keep)) && j->flags==9);}
    /* Partial overlap (input starting inside the job, not at .context): rejected. */
    assert(axd_core_reset_begin(j,1,(const struct axd_core_reset_context *)((unsigned char *)&j->context+8))<0);
    assert(j->phase==AXD_CORE_FAILED && j->error==AXD_CORE_BAD_ARGUMENT);
    assert(axd_core_reset_begin(j,1,NULL)<0 && j->phase==AXD_CORE_FAILED);
    /* Scheduler: both registry and context members survive their own aliases. */
    assert(axd_phy_scheduler_begin(s,&s->registry,&s->context)<0 && s->phase==AXD_PS_FAILED);
    assert(axd_phy_scheduler_begin(s,(const struct axd_phy_registry *)((unsigned char *)&s->context+4),&s->context)<0);
    free(j);free(c);free(s);
    puts("job reset: zero-except, overlap rejection, distinct copy and own-member alias passed");return 0;
}
