// // /* * File: simple_cc.c * Description: OWD-based Congestion Control Implementation */ 
// #include <doca_pcc_dev.h> 
// #include <doca_pcc_dev_event.h> 
// #include <doca_pcc_dev_algo_access.h> 
// #include <doca_pcc_dev_utils.h> 
// #include <stdint.h> 
// #include "simple_cc_ctxt.h" 
// #include "simple_cc_algo_params.h" 
// #include "simple_cc.h" 

// #pragma clang diagnostic ignored "-Wunused-parameter" 
// #define SIMPLE_CC_DEBUG 0 
// #define SIMPLE_CC_UNUSED(x) (void)(x) 

// /* --- OWD算法参数宏 --- */ 
// // OWD平滑因子 Alpha (EWMA: new = (1-alpha)*old + alpha*diff) 
// #define OWD_ALPHA 4  // 权重 1/4 
// // OWD增长阈值（ns，可根据网络延迟调整） 
// #define OWD_INCREASE_THRESH 500  // 示例值：5μs 
// // OWD下降阈值（ns，用于判断网络空闲） 
// #define OWD_DECREASE_THRESH -50  // 经验阈值暂定-150ns（类似TIMELY的T_low 调速）
// // HAI (Hyper Active Increase) 触发阈值 
// #define OWD_HAI_THRESH 10 // 连续10次负梯度触发快速增速 
// // OWD基线（用于归一化，可动态更新） 
// #define OWD_BASELINE_INIT 0  // 初始值0，后续根据实际测量动态调整

// /* --- 参数枚举 --- */ 
// typedef enum { 
//     SIMPLE_CC_PARAM_MD = 0, 
//     SIMPLE_CC_PARAM_AI_HIGH = 1, // 无拥塞/快速增速 
//     SIMPLE_CC_PARAM_AI_LOW = 2, // 轻度拥塞/慢速增速 
//     SIMPLE_CC_PARAM_MIN_RATE = 3, 
//     SIMPLE_CC_PARAM_NUM 
// } simple_cc_params_t; 

// enum { 
//     SIMPLE_CC_COUNTER_EVENTS = 0, 
//     SIMPLE_CC_COUNTER_NUM 
// }; 

// /* --- 描述信息 --- */ 
// static const volatile char simple_cc_desc[] = "OWD CC (v1.0 OWD-Based)"; 
// static const volatile char simple_cc_param_md_desc[] = "MD factor"; 
// static const volatile char simple_cc_param_ai_high_desc[] = "AI HIGH (No Congestion)"; 
// static const volatile char simple_cc_param_ai_low_desc[] = "AI LOW (Mild Congestion)"; 
// static const volatile char simple_cc_param_min_rate_desc[] = "Min Rate"; 

// /* --- 初始化函数 --- */ 
// void simple_cc_init(uint32_t algo_idx) { 
//     struct doca_pcc_dev_algo_meta_data algo_def = {0}; 
//     algo_def.algo_id = 0x7001; 
//     algo_def.algo_major_version = 0x01; 
//     algo_def.algo_minor_version = 0x00; 
//     algo_def.algo_desc_size = sizeof(simple_cc_desc); 
//     algo_def.algo_desc_addr = (uint64_t)simple_cc_desc; 
//     doca_pcc_dev_algo_init_metadata(algo_idx, &algo_def, SIMPLE_CC_PARAM_NUM, SIMPLE_CC_COUNTER_NUM); 

//     uint32_t param_num = 0; 
//     // MD: 0.5 (fxp16 32768) 
//     doca_pcc_dev_algo_init_param(algo_idx, param_num++, SIMPLE_CC_MD_FXP16, SIMPLE_CC_MD_MAX, 1, 1, sizeof(simple_cc_param_md_desc), (uint64_t)simple_cc_param_md_desc); 
//     // AI_HIGH: 
//     doca_pcc_dev_algo_init_param(algo_idx, param_num++, SIMPLE_CC_AI_FXP20, SIMPLE_CC_AI_MAX, 1, 1, sizeof(simple_cc_param_ai_high_desc), (uint64_t)simple_cc_param_ai_high_desc); 
//     // AI_LOW: 
//     doca_pcc_dev_algo_init_param(algo_idx, param_num++, SIMPLE_CC_AI_FXP20/2, SIMPLE_CC_AI_MAX/2, 1, 1, sizeof(simple_cc_param_ai_low_desc), (uint64_t)simple_cc_param_ai_low_desc); 
//     // MIN_RATE: 
//     doca_pcc_dev_algo_init_param(algo_idx, param_num++, SIMPLE_CC_MIN_RATE, SIMPLE_CC_RATE_MAX, SIMPLE_CC_MIN_RATE, 1, sizeof(simple_cc_param_min_rate_desc), (uint64_t)simple_cc_param_min_rate_desc); 

//     uint32_t counter_num = 0; 
//     doca_pcc_dev_algo_init_counter(algo_idx, counter_num++, UINT32_MAX, 2, sizeof("Events"), (uint64_t)"Events"); 
//     doca_pcc_dev_printf("Simple CC with OWD init (v0.3): MIN_RATE=%u, AI_HIGH=%u, AI_LOW=%u, MD=%u, MAX_RATE=%u\n", SIMPLE_CC_MIN_RATE, SIMPLE_CC_AI_FXP20, SIMPLE_CC_AI_FXP20/2, SIMPLE_CC_MD_FXP16, SIMPLE_CC_RATE_MAX); 
// } 

// /* --- 速率调整核心函数 --- */ 
// static inline uint32_t simple_cc_step(uint32_t cur_rate, uint32_t *param, int decrease, uint32_t ai_factor) { 
//     uint32_t min_rate = SIMPLE_CC_MIN_RATE; //param[SIMPLE_CC_PARAM_MIN_RATE]; 
//     uint32_t max_rate = SIMPLE_CC_RATE_MAX; 

//     if (decrease) { 
//         // 乘性减: Rate = Rate * MD 
//         cur_rate = doca_pcc_dev_fxp_mult(SIMPLE_CC_PARAM_MD, cur_rate); 
//     } else { 
//         // 加性增: Rate += AI (溢出保护) 
//         if (cur_rate <= max_rate - ai_factor) { 
//             cur_rate += ai_factor; 
//         } else { 
//             cur_rate = max_rate; 
//         } 
//     } 

//     // 边界检查 
//     if (cur_rate < min_rate) cur_rate = min_rate; 
//     if (cur_rate > max_rate) cur_rate = max_rate; 
//     return cur_rate; 
// } 

// /* --- 新流初始化 --- */ 
// static inline void simple_cc_handle_new_flow(uint32_t *param, simple_cc_ctxt_t *ctx, doca_pcc_dev_results_t *results) { 
//     ctx->cur_rate = SIMPLE_CC_MIN_RATE; //param[SIMPLE_CC_PARAM_MIN_RATE]; 
//     ctx->flags.was_cnp = 0; 
//     ctx->flags.was_nack = 0; 

//     // OWD 状态初始化 
//     ctx->owd_baseline = OWD_BASELINE_INIT;  // 初始化OWD基线
//     ctx->owd_diff = 0;                    // 初始化OWD梯度
//     ctx->owd_hai_counter = 0;             // 初始化HAI计数器
//     ctx->prev_owd = 0;                    // 初始化前一次OWD值

//     results->rate = ctx->cur_rate; 
//     results->rtt_req = 1; 
// } 

// /* --- 主算法逻辑 --- */ 
// void simple_cc_algo(doca_pcc_dev_event_t *event, uint32_t *param, uint32_t *counter, doca_pcc_dev_algo_ctxt_t *algo_ctxt, doca_pcc_dev_results_t *results) { 
//     simple_cc_ctxt_t *ctx = (simple_cc_ctxt_t *)algo_ctxt; 
//     doca_pcc_dev_event_general_attr_t ev_attr = doca_pcc_dev_get_ev_attr(event); 
//     uint32_t ev_type = ev_attr.ev_type; 
//     uint32_t cur_rate = ctx->cur_rate; 

//     // 1. 新流处理 
//     if (cur_rate == 0) { 
//         simple_cc_handle_new_flow(param, ctx, results); 
//         return; 
//     } 

//     int decrease = 0; 
//     uint32_t ai_factor = SIMPLE_CC_AI_FXP20; // 默认快速增加 

//     // 2. 拥塞信号处理 
//     if (ev_type == DOCA_PCC_DEV_EVNT_ROCE_CNP || ev_type == DOCA_PCC_DEV_EVNT_ROCE_NACK) { 
//         // 显式拥塞信号：直接 MD 
//         // decrease = 1; 
//         // ctx->owd_hai_counter = 0; 
//     } else if (ev_type == DOCA_PCC_DEV_EVNT_RTT) { 
//         // OWD 核心：RTT 事件处理（仅用于获取OWD） 
//         uint32_t start_ts = doca_pcc_dev_get_rtt_req_send_timestamp(event); 
//         uint32_t req_rev_time = doca_pcc_dev_get_rtt_req_recv_timestamp(event); 
//         int32_t delta_owd = 0; 

//         if(ctx->last_req_send_time == 0 && ctx->last_rev_time == 0) { 
//             delta_owd = 0; 
//         } else { 
//             delta_owd = (int32_t)((req_rev_time - ctx->last_rev_time) - (start_ts - ctx->last_req_send_time)); 
//         } 
//         ctx->last_req_send_time = start_ts; 
//         ctx->last_rev_time = req_rev_time; 

//         // A. 更新OWD基线（动态调整，类似min_rtt） ---- OWD基线待使用
//         // if (ctx->owd_baseline == OWD_BASELINE_INIT || delta_owd < ctx->owd_baseline) {
//         //     ctx->owd_baseline = delta_owd;
//         // }

//         // B. 计算OWD梯度并平滑
//         int32_t new_owd_diff = (int32_t)(delta_owd - ctx->prev_owd);
//         ctx->prev_owd = delta_owd;
//         ctx->owd_diff = (ctx->owd_diff * (OWD_ALPHA - 1) + new_owd_diff) / OWD_ALPHA;   // EWMA平滑优化梯度计算

//         // C. OWD状态机
//         // if (ctx->owd_diff <= OWD_DECREASE_THRESH) {
//         //     // 网络空闲：OWD下降明显
//         //     ctx->owd_hai_counter++;
//         //     if (ctx->owd_hai_counter >= OWD_HAI_THRESH) {
//         //         ai_factor = SIMPLE_CC_AI_FXP20 * 5;  // 5倍AI（超快速增速）
//         //     } else {
//         //         ai_factor = SIMPLE_CC_AI_FXP20;      // 标准AI
//         //     }
//         // } else if (ctx->owd_diff > OWD_INCREASE_THRESH) {
//         //     // 网络拥塞：OWD上升明显
//         //     decrease = 1;
//         //     ctx->owd_hai_counter = 0;
//         // } else {
//         //     // 中间态：OWD变化不大
//         //     ctx->owd_hai_counter = 0;
//         //     ai_factor = SIMPLE_CC_AI_FXP20 / 2;  // 慢速增速
//         // }

//         if (delta_owd <= OWD_DECREASE_THRESH) {
//             // 网络空闲：OWD下降明显
//             ctx->owd_hai_counter++;
//             if (ctx->owd_hai_counter >= OWD_HAI_THRESH) {
//                 ai_factor = SIMPLE_CC_AI_FXP20 * 5;  // 5倍AI（超快速增速）
//             } else {
//                 ai_factor = SIMPLE_CC_AI_FXP20;      // 标准AI
//             }
//         } else if (delta_owd > OWD_INCREASE_THRESH) {
//             // 网络拥塞：OWD上升明显
//             decrease = 1;
//             ctx->owd_hai_counter = 0;
//         } else {
//             // 中间态：OWD变化不大
//             ctx->owd_hai_counter = 0;
//             ai_factor = SIMPLE_CC_AI_FXP20 / 2;  // 慢速增速
//         }

//         // 打印调试信息（仅OWD）
//         static uint32_t owd_print = 0;
//         owd_print++;
//         if(owd_print < 500) {
//             doca_pcc_dev_printf("CC: OWD控制 - cur_owd=%d, owd_baseline=%d, owd_diff=%d (ns)\n", 
//                               delta_owd, ctx->owd_baseline, ctx->owd_diff);
//             doca_pcc_dev_printf("cur_rate=%u\n", cur_rate);
//         }
//     } 

//     static int print_once = 0;
//     if (print_once < 3) {
//         // 打印所有参数的实际生效值
//         doca_pcc_dev_printf("DEBUG PARAM: MD=%u, AI_HIGH=%u, AI_LOW=%u, MIN_RATE=%u\n", 
//                           param[SIMPLE_CC_PARAM_MD], param[SIMPLE_CC_PARAM_AI_HIGH], 
//                           param[SIMPLE_CC_PARAM_AI_LOW], param[SIMPLE_CC_PARAM_MIN_RATE]);
//         param[SIMPLE_CC_PARAM_MD] = SIMPLE_CC_MD_FXP16;
//         param[SIMPLE_CC_PARAM_AI_HIGH] = SIMPLE_CC_AI_FXP20;
//         param[SIMPLE_CC_PARAM_AI_LOW] = SIMPLE_CC_AI_FXP20/10;
//         param[SIMPLE_CC_PARAM_MIN_RATE] = SIMPLE_CC_MIN_RATE;
//         print_once++;
//     }

//     // 3. 执行速率更新
//     cur_rate = simple_cc_step(cur_rate, param, decrease, ai_factor);

//     // 4. 更新上下文与结果
//     ctx->cur_rate = cur_rate;
//     results->rate = cur_rate;
//     results->rtt_req = 1; // 持续请求 RTT
//     if (counter != NULL) counter[SIMPLE_CC_COUNTER_EVENTS]++;
// } 

// /* --- 参数设置接口 --- */ 
// doca_pcc_dev_error_t simple_cc_set_algo_params(uint32_t param_id_base, uint32_t param_num, const uint32_t *new_param_values, uint32_t *params) { 
//     if (param_num > SIMPLE_CC_PARAM_NUM || param_id_base >= SIMPLE_CC_PARAM_NUM) 
//         return DOCA_PCC_DEV_STATUS_FAIL; 
//     if (new_param_values == NULL || params == NULL) 
//         return DOCA_PCC_DEV_STATUS_FAIL; 
//     if (param_id_base + param_num > SIMPLE_CC_PARAM_NUM) 
//         return DOCA_PCC_DEV_STATUS_FAIL; 

//     for (uint32_t i = 0; i < param_num; i++) { 
//         params[param_id_base + i] = new_param_values[i]; 
//     } 
//     return DOCA_PCC_DEV_STATUS_OK; 
// }



//-----LCC v2
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
// OWD平滑因子 Alpha (EWMA: new = (1-alpha)*old + alpha*diff) 
#define OWD_ALPHA 4  // 权重 1/4 
// OWD下降阈值（ns，用于判断网络空闲） 
#define OWD_DECREASE_THRESH -50  // 经验阈值暂定-50ns
// HAI (Hyper Active Increase) 触发阈值 
#define OWD_HAI_THRESH 10 // 连续10次负梯度触发快速增速 
// OWD基线（用于归一化，可动态更新） 
#define OWD_BASELINE_INIT 0  // 初始值0，后续根据实际测量动态调整

/* --- 新增：GCC自适应阈值相关宏 --- */ 
#define OWD_THRESH_MIN 50       // 最小延迟梯度阈值(ns)，保持对轻微拥塞的敏感度
#define OWD_THRESH_MAX 1000     // 最大延迟梯度阈值(ns)，防止阈值无限放大失去保护作用
#define OWD_K_UP_SHIFT 4        // 阈值跟随上升的速率（右移4位相当于除以16，类似GCC的 ku）
#define OWD_K_DOWN_SHIFT 8      // 阈值跟随下降的速率（右移8位相当于除以256，类似GCC的 kd）

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
        cur_rate = doca_pcc_dev_fxp_mult(SIMPLE_CC_PARAM_MD, cur_rate); 
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
    ctx->owd_thresh = OWD_THRESH_MIN;     // 【新增】初始化动态阈值为最小值

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

        // B. 计算OWD梯度并平滑 (EWMA)
        int32_t new_owd_diff = (int32_t)(delta_owd - ctx->prev_owd);
        ctx->prev_owd = delta_owd;
        ctx->owd_diff = (ctx->owd_diff * (OWD_ALPHA - 1) + new_owd_diff) / OWD_ALPHA;

        // 【新增】C. 动态更新 OWD 阈值 (Adaptive Threshold)
        int32_t abs_diff = (ctx->owd_diff > 0) ? ctx->owd_diff : -ctx->owd_diff;

        if (abs_diff > ctx->owd_thresh) {
            // 波动偏大时，阈值以较快速度 (ku) 上浮，容忍突发并抢占带宽
            ctx->owd_thresh += (abs_diff - ctx->owd_thresh) >> OWD_K_UP_SHIFT;
        } else {
            // 波动平时，阈值以极慢速度 (kd) 衰减，保持警戒敏感度
            ctx->owd_thresh -= (ctx->owd_thresh - abs_diff) >> OWD_K_DOWN_SHIFT;
        }

        // 限制动态阈值边界
        if (ctx->owd_thresh < OWD_THRESH_MIN) ctx->owd_thresh = OWD_THRESH_MIN;
        if (ctx->owd_thresh > OWD_THRESH_MAX) ctx->owd_thresh = OWD_THRESH_MAX;

        // D. 结合动态阈值的 OWD 状态机 (注：恢复使用平滑后的 owd_diff 判断)
        if (ctx->owd_diff <= OWD_DECREASE_THRESH) {
            // 网络空闲：OWD下降明显
            ctx->owd_hai_counter++;
            if (ctx->owd_hai_counter >= OWD_HAI_THRESH) {
                ai_factor = SIMPLE_CC_AI_FXP20 * 5;  // 5倍AI（超快速增速）
            } else {
                ai_factor = SIMPLE_CC_AI_FXP20;      // 标准AI
            }
        } else if (ctx->owd_diff > ctx->owd_thresh) { // <--- 【关键修改】使用自适应阈值
            // 网络拥塞：OWD超过自适应警戒线
            decrease = 1;
            ctx->owd_hai_counter = 0;
        } else {
            // 中间态：OWD变化不大
            ctx->owd_hai_counter = 0;
            ai_factor = SIMPLE_CC_AI_FXP20 / 2;  // 慢速增速
        }

        // 打印调试信息，加入对 owd_thresh 的观测
        static uint32_t owd_print = 0;
        owd_print++;
        if(owd_print < 20) {
            doca_pcc_dev_printf("CC: OWD控制 - cur_owd=%d, owd_diff=%d, dyn_thresh=%d (ns)\n", 
                              delta_owd, ctx->owd_diff, ctx->owd_thresh);
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