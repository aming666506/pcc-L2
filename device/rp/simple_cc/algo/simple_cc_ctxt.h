#ifndef SIMPLE_CC_CTXT_H
#define SIMPLE_CC_CTXT_H

#include <stdint.h>

typedef struct {
    uint32_t cur_rate;

    /* Forward-delay trend state and persistence counters. */
    int32_t  owd_diff;
    uint32_t owd_hai_counter;
    uint8_t  fws_state;
    uint8_t  grow_count;
    uint8_t  relief_count;
    uint8_t  stable_count;

    /* Limit delay-triggered multiplicative decreases to one per RTT. */
    uint32_t last_md_timestamp;
    uint8_t  md_timestamp_valid;

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
