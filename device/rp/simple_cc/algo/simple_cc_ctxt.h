
//---LCCv2.0改造版本---

/*
 * File: simple_cc_ctxt.h
 * Description: Flow context for Simple CC -> OWD Algorithm
 * * 改造要点：
 * 1. 完全移除RTT相关字段（min_rtt, t_low, t_high, prev_rtt, rtt_diff, hai_counter）
 * 2. 保留并强化OWD相关字段（last_req_send_time, last_rev_time）
 * 3. 新增OWD梯度字段（owd_diff, owd_hai_counter）
 * 4. 引入自适应阈值 owd_thresh，完全复用原有reserved空间，保持总大小40B不变。
 */
// #ifndef SIMPLE_CC_CTXT_H_
// #define SIMPLE_CC_CTXT_H_

// #include <stdint.h>

// /* 原有：拥塞标记位段（完全保留） */
// typedef struct {
//     uint8_t was_nack : 1;   // 原有：是否收到NACK
//     uint8_t was_cnp : 1;    // 原有：是否收到CNP
//     uint8_t reserved : 6;   // 原有：预留位
// } simple_cc_flags_t;

// /* 改造：OWD 算法流上下文结构体 */
// typedef struct {
//     /* --- 原有字段布局 (0-11 Bytes) --- */
//     uint32_t cur_rate;          // 原有：当前速率
//     simple_cc_flags_t flags;    // 原有：拥塞标记 (1B数据，编译器对齐后占4B)

//     /* --- OWD 核心字段 (12-27 Bytes) --- */
//     uint32_t last_req_send_time; // 上次请求发送时间戳（ticks），用于计算单程时延（OWD）
//     uint32_t last_rev_time;      // 上次请求接收时间戳（ticks），用于计算单程时延（OWD）
    
//     /* --- OWD 状态字段 (28-39 Bytes，复用原reserved空间) --- */
//     int32_t  owd_diff;          // 平滑后的OWD梯度 (EWMA结果，可为负)
//     uint32_t owd_hai_counter;   // OWD HAI计数器 (连续负梯度次数)
//     int32_t  owd_baseline;      // OWD基线值（用于归一化）
//     uint32_t prev_owd;          // 上一次测量的OWD值
//     int32_t  owd_thresh;        // 【新增】自适应 OWD 阈值 (动态调整)
//     uint32_t reserved[1];       // 剩余预留空间 (保持总大小40B对齐)
// } simple_cc_ctxt_t;

// #endif /* SIMPLE_CC_CTXT_H_ */






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

