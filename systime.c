#include "date.h"
#include "defs.h"

// return how many clock tick interrupts have occurred
// since start.
int
sys_date(void)
{
    struct rtcdate * r;

    if( argptr(0, &r, sizeof(*r)) < 0 )
        return -1;

    cmostime( r );

    return 0;
}