#ifndef SIMPLE_CC_ALGO_PARAMS_H_
#define SIMPLE_CC_ALGO_PARAMS_H_

#include <stdint.h>

/*
 * Parameters are fixed-point as expected by DOCA PCC dev helpers.
 *
 * Rate format: 20-bit fixed point (DOCA_PCC_DEV_LOG_MAX_RATE = 20)
 * Core Mapping:
 *   2^14 = 1.47 Gbps
 *   2^20 = 100 Gbps
 */

/* 最大速率：fxp20格式，2^20 = 100Gbps */
#define SIMPLE_CC_RATE_MAX     ((1 << 20))

/* 参数最大值限制 */
#define SIMPLE_CC_MD_MAX       (1 << 16)
#define SIMPLE_CC_AI_MAX       ((1 << 20))

/*
 * 默认 RTT 档位。
 * 新流初始化时先按 10ms 档位启动，之后 RTT 事件到来后自动切换。
 */
#define SIMPLE_CC_DEFAULT_RTT_MS        (10)

/*
 * RTT-specific runtime parameters.
 *
 * 注意：
 *   rtt_ms 只用于 RTT 档位选择。
 *   owd_high_thresh / owd_low_thresh 单位是 ns，不需要换算。
 */
typedef struct simple_cc_rtt_params {
    uint32_t rtt_ms;

    int32_t  owd_high_thresh;  /* ns */
    int32_t  owd_low_thresh;   /* ns */

    uint32_t md_fxp16;
    uint32_t ai_fxp20;
    uint32_t min_rate;
} simple_cc_rtt_params_t;

/*
 * 根据测试得到的不同 RTT 参数表。
 *
 * RTT = 1ms:
 *   OWD_HIGH=300, OWD_LOW=-100, MD=0x8000, AI=800, MIN_RATE=1<<17
 *
 * RTT = 5ms:
 *   OWD_HIGH=200, OWD_LOW=-90, MD=0x8000, AI=100, MIN_RATE=1<<17
 *
 * RTT = 10ms:
 *   OWD_HIGH=220, OWD_LOW=-100, MD=0x8000, AI=50, MIN_RATE=1<<17
 *
 * RTT = 20ms:
 *   OWD_HIGH=80, OWD_LOW=-50, MD=0x8000, AI=50, MIN_RATE=1<<17
 *
 * RTT = 30ms:
 *   OWD_HIGH=80, OWD_LOW=-40, MD=0x8000, AI=50, MIN_RATE=1<<17
 *
 * RTT = 50ms:
 *   OWD_HIGH=80, OWD_LOW=-30, MD=0x8000, AI=50, MIN_RATE=1<<17
 */
static const simple_cc_rtt_params_t simple_cc_rtt_param_table[] = {
    /* RTT(ms), OWD_HIGH(ns), OWD_LOW(ns), MD,      AI,  MIN_RATE */
    { 1,       300,          -100,        0x8000,  800, (1 << 17) },
    { 5,       200,           -90,        0x8000,  100, (1 << 17) },
    { 10,      220,          -100,        0x8000,   50, (1 << 17) },
    { 20,       80,           -50,        0x8000,   50, (1 << 17) },
    { 30,       80,           -50,        0x8000,   50, (1 << 17) },
    { 50,       80,           -30,        0x8000,   50, (1 << 17) },
};

static inline const simple_cc_rtt_params_t *
simple_cc_select_params_by_rtt_class(uint32_t rtt_class_ms)
{
    switch (rtt_class_ms) {
    case 1:
        return &simple_cc_rtt_param_table[0];

    case 5:
        return &simple_cc_rtt_param_table[1];

    case 10:
        return &simple_cc_rtt_param_table[2];

    case 20:
        return &simple_cc_rtt_param_table[3];

    case 30:
        return &simple_cc_rtt_param_table[4];

    case 50:
        return &simple_cc_rtt_param_table[5];

    default:
        return &simple_cc_rtt_param_table[2]; /* default: 10 ms */
    }
}

/*
 * RTT EWMA.
 *
 * 输入和输出单位都是 ns。
 * new_ewma = 7/8 * old + 1/8 * sample
 */
static inline uint32_t
simple_cc_update_rtt_ewma_ns(uint32_t old_rtt_ns, uint32_t new_sample_ns)
{
    if (old_rtt_ns == 0)
        return new_sample_ns;

    return (uint32_t)((((uint64_t)old_rtt_ns * 7U) + new_sample_ns) >> 3);
}

/*
 * RTT 档位判断。
 *
 * RTT 档位：
 *   1ms, 5ms, 10ms, 20ms, 30ms, 50ms
 *
 * 判断方法：
 *   按相邻 RTT 档位的中点分类。
 *
 *   RTT < 3ms        -> 1ms
 *   3ms ~ 7.5ms      -> 5ms
 *   7.5ms ~ 15ms     -> 10ms
 *   15ms ~ 25ms      -> 20ms
 *   25ms ~ 40ms      -> 30ms
 *   RTT >= 40ms      -> 50ms
 */
static inline uint32_t
simple_cc_classify_rtt_ms(uint32_t rtt_ns)
{
    if (rtt_ns < 3000000U)
        return 1;

    if (rtt_ns < 7500000U)
        return 5;

    if (rtt_ns < 15000000U)
        return 10;

    if (rtt_ns < 25000000U)
        return 20;

    if (rtt_ns < 40000000U)
        return 30;

    return 50;
}

#endif /* SIMPLE_CC_ALGO_PARAMS_H_ */
