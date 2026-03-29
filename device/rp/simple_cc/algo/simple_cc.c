// /* * File: simple_cc.c * Description: OWD-based Congestion Control Implementation */ 
#include <doca_pcc_dev.h> 
#include <doca_pcc_dev_event.h> 
#include <doca_pcc_dev_algo_access.h> 
#include <doca_pcc_dev_utils.h> 
#include <stdint.h> 
#include "simple_cc_ctxt.h" 
#include "simple_cc_algo_params.h" 
#include "simple_cc.h" 

#pragma clang diagnostic ignored "-Wunused-parameter" 
#define SIMPLE_CC_DEBUG 0 
#define SIMPLE_CC_UNUSED(x) (void)(x) 

/* --- OWD算法参数宏 --- */ 
// OWD 上升阈值（ns，延迟增加超过此值判断为拥塞，降速）
#define OWD_HIGH_THRESH 150
// OWD 下降阈值（ns，延迟下降超过此值判断为空闲，加速） 
#define OWD_LOW_THRESH -50

// HAI (Hyper Active Increase) 触发阈值 
#define OWD_HAI_THRESH 10 // 连续10次低延迟触发快速增速 
// OWD基线（用于归一化，可动态更新） 
#define OWD_BASELINE_INIT 0  // 初始值0，后续根据实际测量动态调整

/* --- 参数枚举 --- */ 
typedef enum { 
    SIMPLE_CC_PARAM_MD = 0, 
    SIMPLE_CC_PARAM_AI_HIGH = 1, // 无拥塞/快速增速 
    SIMPLE_CC_PARAM_AI_LOW = 2, // 轻度拥塞/慢速增速 
    SIMPLE_CC_PARAM_MIN_RATE = 3, 
    SIMPLE_CC_PARAM_NUM 
} simple_cc_params_t; 

enum { 
    SIMPLE_CC_COUNTER_EVENTS = 0, 
    SIMPLE_CC_COUNTER_NUM 
}; 

/* --- 描述信息 --- */ 
static const volatile char simple_cc_desc[] = "OWD CC (v1.0 OWD-Based)"; 
static const volatile char simple_cc_param_md_desc[] = "MD factor"; 
static const volatile char simple_cc_param_ai_high_desc[] = "AI HIGH (No Congestion)"; 
static const volatile char simple_cc_param_ai_low_desc[] = "AI LOW (Mild Congestion)"; 
static const volatile char simple_cc_param_min_rate_desc[] = "Min Rate"; 

/* --- 初始化函数 --- */ 
void simple_cc_init(uint32_t algo_idx) { 
    struct doca_pcc_dev_algo_meta_data algo_def = {0}; 
    algo_def.algo_id = 0x7001; 
    algo_def.algo_major_version = 0x01; 
    algo_def.algo_minor_version = 0x00; 
    algo_def.algo_desc_size = sizeof(simple_cc_desc); 
    algo_def.algo_desc_addr = (uint64_t)simple_cc_desc; 
    doca_pcc_dev_algo_init_metadata(algo_idx, &algo_def, SIMPLE_CC_PARAM_NUM, SIMPLE_CC_COUNTER_NUM); 

    uint32_t param_num = 0; 
    // MD: 0.5 (fxp16 32768) 
    doca_pcc_dev_algo_init_param(algo_idx, param_num++, SIMPLE_CC_MD_FXP16, SIMPLE_CC_MD_MAX, 1, 1, sizeof(simple_cc_param_md_desc), (uint64_t)simple_cc_param_md_desc); 
    // AI_HIGH: 
    doca_pcc_dev_algo_init_param(algo_idx, param_num++, SIMPLE_CC_AI_FXP20, SIMPLE_CC_AI_MAX, 1, 1, sizeof(simple_cc_param_ai_high_desc), (uint64_t)simple_cc_param_ai_high_desc); 
    // AI_LOW: 
    doca_pcc_dev_algo_init_param(algo_idx, param_num++, SIMPLE_CC_AI_FXP20/2, SIMPLE_CC_AI_MAX/2, 1, 1, sizeof(simple_cc_param_ai_low_desc), (uint64_t)simple_cc_param_ai_low_desc); 
    // MIN_RATE: 
    doca_pcc_dev_algo_init_param(algo_idx, param_num++, SIMPLE_CC_MIN_RATE, SIMPLE_CC_RATE_MAX, SIMPLE_CC_MIN_RATE, 1, sizeof(simple_cc_param_min_rate_desc), (uint64_t)simple_cc_param_min_rate_desc); 

    uint32_t counter_num = 0; 
    doca_pcc_dev_algo_init_counter(algo_idx, counter_num++, UINT32_MAX, 2, sizeof("Events"), (uint64_t)"Events"); 
    doca_pcc_dev_printf("Simple CC with OWD init (v0.3): MIN_RATE=%u, AI_HIGH=%u, AI_LOW=%u, MD=%u, MAX_RATE=%u\n", SIMPLE_CC_MIN_RATE, SIMPLE_CC_AI_FXP20, SIMPLE_CC_AI_FXP20/2, SIMPLE_CC_MD_FXP16, SIMPLE_CC_RATE_MAX); 
} 

/* --- 速率调整核心函数 --- */ 
static inline uint32_t simple_cc_step(uint32_t cur_rate, uint32_t *param, int decrease, uint32_t ai_factor) { 
    uint32_t min_rate = SIMPLE_CC_MIN_RATE; 
    uint32_t max_rate = SIMPLE_CC_RATE_MAX; 

    if (decrease) { 
        // 乘性减: Rate = Rate * MD 
        cur_rate = doca_pcc_dev_fxp_mult(SIMPLE_CC_MD_FXP16, cur_rate); 
    } else { 
        // 加性增: Rate += AI (溢出保护) 
        if (cur_rate <= max_rate - ai_factor) { 
            cur_rate += ai_factor; 
        } else { 
            cur_rate = max_rate; 
        } 
    } 

    // 边界检查 
    if (cur_rate < min_rate) cur_rate = min_rate; 
    if (cur_rate > max_rate) cur_rate = max_rate; 
    return cur_rate; 
} 

/* --- 新流初始化 --- */ 
static inline void simple_cc_handle_new_flow(uint32_t *param, simple_cc_ctxt_t *ctx, doca_pcc_dev_results_t *results) { 
    ctx->cur_rate = SIMPLE_CC_MIN_RATE; 
    ctx->flags.was_cnp = 0; 
    ctx->flags.was_nack = 0; 

    // OWD 状态初始化 
    ctx->owd_baseline = OWD_BASELINE_INIT;  
    ctx->owd_diff = 0;                    
    ctx->owd_hai_counter = 0;             
    ctx->prev_owd = 0;                    

    results->rate = ctx->cur_rate; 
    results->rtt_req = 1; 
} 

/* --- 主算法逻辑 --- */ 
void simple_cc_algo(doca_pcc_dev_event_t *event, uint32_t *param, uint32_t *counter, doca_pcc_dev_algo_ctxt_t *algo_ctxt, doca_pcc_dev_results_t *results) { 
    simple_cc_ctxt_t *ctx = (simple_cc_ctxt_t *)algo_ctxt; 
    doca_pcc_dev_event_general_attr_t ev_attr = doca_pcc_dev_get_ev_attr(event); 
    uint32_t ev_type = ev_attr.ev_type; 
    uint32_t cur_rate = ctx->cur_rate; 

    // 1. 新流处理 
    if (cur_rate == 0) { 
        simple_cc_handle_new_flow(param, ctx, results); 
        return; 
    } 

    int decrease = 0; 
    uint32_t ai_factor = SIMPLE_CC_AI_FXP20; // 默认快速增加 

    // 2. 拥塞信号处理 
    if (ev_type == DOCA_PCC_DEV_EVNT_ROCE_CNP || ev_type == DOCA_PCC_DEV_EVNT_ROCE_NACK) { 
        // 显式拥塞信号：直接 MD 
        // decrease = 1; 
        // ctx->owd_hai_counter = 0; 
    } else if (ev_type == DOCA_PCC_DEV_EVNT_RTT) { 
        // OWD 核心：RTT 事件处理（仅用于获取OWD） 
        uint32_t start_ts = doca_pcc_dev_get_rtt_req_send_timestamp(event); 
        uint32_t req_rev_time = doca_pcc_dev_get_rtt_req_recv_timestamp(event); 
        int32_t delta_owd = 0; 

        if(ctx->last_req_send_time == 0 && ctx->last_rev_time == 0) { 
            delta_owd = 0; 
        } else { 
            delta_owd = (int32_t)((req_rev_time - ctx->last_rev_time) - (start_ts - ctx->last_req_send_time)); 
        } 
        ctx->last_req_send_time = start_ts; 
        ctx->last_rev_time = req_rev_time; 

        // 直接根据本次的 delta_owd 判断（移除了EWMA和动态阈值机制）
        if (delta_owd < OWD_LOW_THRESH) { // delta_owd < -50
            // 网络空闲：延迟下降明显，加速
            ctx->owd_hai_counter++;
            if (ctx->owd_hai_counter >= OWD_HAI_THRESH) {
                ai_factor = SIMPLE_CC_AI_FXP20 * 5;  // 5倍AI（超快速增速）
            } else {
                ai_factor = SIMPLE_CC_AI_FXP20;      // 标准AI
            }
        } else if (delta_owd > OWD_HIGH_THRESH) { // delta_owd > 150
            // 网络拥塞：延迟上升超过150ns，降速
            decrease = 1;
            ctx->owd_hai_counter = 0;
        } else {
            // 中间态：OWD变化不大 (-50 <= delta_owd <= 150)
            ctx->owd_hai_counter = 0;
            ai_factor = SIMPLE_CC_AI_FXP20 / 2;  // 慢速增速
        }

        // 打印调试信息，观测 delta_owd
        static uint32_t owd_print = 0;
        owd_print++;
        if(owd_print < 20) {
            doca_pcc_dev_printf("CC: OWD控制 - delta_owd=%d (ns)\n", delta_owd);
            doca_pcc_dev_printf("cur_rate=%u\n", cur_rate);
        }
    } 

    static int print_once = 0;
    if (print_once < 3) {
        // 打印所有参数的实际生效值
        doca_pcc_dev_printf("DEBUG PARAM: MD=%u, AI_HIGH=%u, AI_LOW=%u, MIN_RATE=%u\n", 
                          param[SIMPLE_CC_PARAM_MD], param[SIMPLE_CC_PARAM_AI_HIGH], 
                          param[SIMPLE_CC_PARAM_AI_LOW], param[SIMPLE_CC_PARAM_MIN_RATE]);
        param[SIMPLE_CC_PARAM_MD] = SIMPLE_CC_MD_FXP16;
        param[SIMPLE_CC_PARAM_AI_HIGH] = SIMPLE_CC_AI_FXP20;
        param[SIMPLE_CC_PARAM_AI_LOW] = SIMPLE_CC_AI_FXP20/10;
        param[SIMPLE_CC_PARAM_MIN_RATE] = SIMPLE_CC_MIN_RATE;
        print_once++;
    }

    // 3. 执行速率更新
    cur_rate = simple_cc_step(cur_rate, param, decrease, ai_factor);

    // 4. 更新上下文与结果
    ctx->cur_rate = cur_rate;
    results->rate = cur_rate;
    results->rtt_req = 1; // 持续请求 RTT
    if (counter != NULL) counter[SIMPLE_CC_COUNTER_EVENTS]++;
} 

/* --- 参数设置接口 --- */ 
doca_pcc_dev_error_t simple_cc_set_algo_params(uint32_t param_id_base, uint32_t param_num, const uint32_t *new_param_values, uint32_t *params) { 
    if (param_num > SIMPLE_CC_PARAM_NUM || param_id_base >= SIMPLE_CC_PARAM_NUM) 
        return DOCA_PCC_DEV_STATUS_FAIL; 
    if (new_param_values == NULL || params == NULL) 
        return DOCA_PCC_DEV_STATUS_FAIL; 
    if (param_id_base + param_num > SIMPLE_CC_PARAM_NUM) 
        return DOCA_PCC_DEV_STATUS_FAIL; 

    for (uint32_t i = 0; i < param_num; i++) { 
        params[param_id_base + i] = new_param_values[i]; 
    } 
    return DOCA_PCC_DEV_STATUS_OK; 
}