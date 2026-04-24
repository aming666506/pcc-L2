
// ---------------------------------------------4.7 - 14 25 - v2

#ifndef SIMPLE_CC_CTXT_H
#define SIMPLE_CC_CTXT_H

#include <stdint.h>

typedef struct {
    struct {
        uint8_t was_cnp;
        uint8_t was_nack;
    } flags;

    uint32_t cur_rate;

    /* OWD相关状态 */
    uint32_t owd_baseline;
    int32_t  owd_diff;
    uint32_t owd_hai_counter;
    uint32_t prev_owd;

    /* RTT历史 */
    uint32_t last_req_send_time;
    uint32_t last_rev_time;

    /* 新增：显式初始化状态 */
    uint8_t initialized;
    uint8_t rtt_valid;
} simple_cc_ctxt_t;

#endif /* SIMPLE_CC_CTXT_H */

