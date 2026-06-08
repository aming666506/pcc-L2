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

    /* RTT/OWD历史时间戳 */
    uint32_t last_req_send_time;
    uint32_t last_rev_time;

    /*
     * RTT运行时判断状态。
     *
     * rtt_ewma_ns:
     *   平滑后的 RTT，单位 ns。
     *
     * rtt_class_ms:
     *   当前选择的 RTT 档位，只能是 1/5/10/20/30/50ms。
     */
    uint32_t rtt_ewma_ns;
    uint32_t rtt_class_ms;

    /* 显式初始化状态 */
    uint8_t initialized;
    uint8_t rtt_valid;
} simple_cc_ctxt_t;

#endif /* SIMPLE_CC_CTXT_H */