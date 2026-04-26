
//LongCC -L2  use NACK as strong congestion signal, directly 10% decrease, and only use OWD for RTT control without EWMA, to avoid 0ms scenario misjudgment.


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

/* =========================
 * OWD-only + NACK 10% 降速版本
 * =========================
 *
 * 设计思路：
 * 1) RTT 事件：使用 Delta OWD + EWMA 进行主要调速
 * 2) NACK 事件：作为强拥塞信号，当前速率立即降低 10%
 * 3) NACK 不进入 RTT/OWD 时间戳计算逻辑，避免污染 OWD 状态
 * 4) 其他事件：忽略，仅保持当前速率
 * 5) 增加 NACK counter，避免只依赖 printf 判断 NACK 是否触发
 */

/* 平滑后的 OWD 梯度阈值，单位 ns */
#define OWD_HIGH_THRESH 300
#define OWD_LOW_THRESH  -100

/* HAI 触发阈值：连续 N 次低延迟趋势后更快加速 */
#define OWD_HAI_THRESH  8

/* EWMA 平滑参数：new = (old * (N-1) + sample) / N */
#define OWD_EWMA_N      8

/* OWD 基线初值 */
#define OWD_BASELINE_INIT 0

/* NACK 降速比例：降低 10%，即保留当前速率的 90% */
#define NACK_RATE_KEEP_NUM 90
#define NACK_RATE_KEEP_DEN 100

/*
 * NACK 打印限频：
 * 前 20 次 NACK 都打印；
 * 之后每 1000 次打印一次。
 */
#define NACK_PRINT_FIRST_N 20
#define NACK_PRINT_INTERVAL 100

/* --- 参数枚举 --- */
typedef enum {
    SIMPLE_CC_PARAM_MD = 0,
    SIMPLE_CC_PARAM_AI_HIGH = 1,
    SIMPLE_CC_PARAM_AI_LOW = 2,
    SIMPLE_CC_PARAM_MIN_RATE = 3,
    SIMPLE_CC_PARAM_NUM
} simple_cc_params_t;

/* --- Counter 枚举 --- */
enum {
    SIMPLE_CC_COUNTER_EVENTS = 0,
    SIMPLE_CC_COUNTER_NACKS  = 1,
    SIMPLE_CC_COUNTER_NUM
};

/* --- 描述信息 --- */
static const volatile char simple_cc_desc[] =
    "OWD-only CC (RTT OWD control + NACK 10 percent decrease)";

static const volatile char simple_cc_param_md_desc[] = "MD factor";
static const volatile char simple_cc_param_ai_high_desc[] = "AI HIGH";
static const volatile char simple_cc_param_ai_low_desc[] = "AI LOW";
static const volatile char simple_cc_param_min_rate_desc[] = "Min Rate";

static const volatile char simple_cc_counter_events_desc[] = "Events";
static const volatile char simple_cc_counter_nacks_desc[] = "NACKs";

/* --- 初始化函数 --- */
void simple_cc_init(uint32_t algo_idx)
{
    struct doca_pcc_dev_algo_meta_data algo_def = {0};

    algo_def.algo_id = 0x7001;
    algo_def.algo_major_version = 0x01;
    algo_def.algo_minor_version = 0x04;
    algo_def.algo_desc_size = sizeof(simple_cc_desc);
    algo_def.algo_desc_addr = (uint64_t)simple_cc_desc;

    doca_pcc_dev_algo_init_metadata(algo_idx,
                                    &algo_def,
                                    SIMPLE_CC_PARAM_NUM,
                                    SIMPLE_CC_COUNTER_NUM);

    {
        uint32_t param_num = 0;

        doca_pcc_dev_algo_init_param(algo_idx,
                                     param_num++,
                                     SIMPLE_CC_MD_FXP16,
                                     SIMPLE_CC_MD_MAX,
                                     1,
                                     1,
                                     sizeof(simple_cc_param_md_desc),
                                     (uint64_t)simple_cc_param_md_desc);

        doca_pcc_dev_algo_init_param(algo_idx,
                                     param_num++,
                                     SIMPLE_CC_AI_FXP20,
                                     SIMPLE_CC_AI_MAX,
                                     1,
                                     1,
                                     sizeof(simple_cc_param_ai_high_desc),
                                     (uint64_t)simple_cc_param_ai_high_desc);

        doca_pcc_dev_algo_init_param(algo_idx,
                                     param_num++,
                                     SIMPLE_CC_AI_FXP20 / 2,
                                     SIMPLE_CC_AI_MAX / 2,
                                     1,
                                     1,
                                     sizeof(simple_cc_param_ai_low_desc),
                                     (uint64_t)simple_cc_param_ai_low_desc);

        doca_pcc_dev_algo_init_param(algo_idx,
                                     param_num++,
                                     SIMPLE_CC_MIN_RATE,
                                     SIMPLE_CC_RATE_MAX,
                                     SIMPLE_CC_MIN_RATE,
                                     1,
                                     sizeof(simple_cc_param_min_rate_desc),
                                     (uint64_t)simple_cc_param_min_rate_desc);
    }

    {
        uint32_t counter_num = 0;

        doca_pcc_dev_algo_init_counter(algo_idx,
                                       counter_num++,
                                       UINT32_MAX,
                                       2,
                                       sizeof(simple_cc_counter_events_desc),
                                       (uint64_t)simple_cc_counter_events_desc);

        doca_pcc_dev_algo_init_counter(algo_idx,
                                       counter_num++,
                                       UINT32_MAX,
                                       2,
                                       sizeof(simple_cc_counter_nacks_desc),
                                       (uint64_t)simple_cc_counter_nacks_desc);
    }

    doca_pcc_dev_printf(
        "OWD-only + NACK init: MIN_RATE=%u, AI_HIGH=%u, AI_LOW=%u, MD=%u, MAX_RATE=%u\n",
        SIMPLE_CC_MIN_RATE,
        SIMPLE_CC_AI_FXP20,
        SIMPLE_CC_AI_FXP20 / 2,
        SIMPLE_CC_MD_FXP16,
        SIMPLE_CC_RATE_MAX);
}

/* --- RTT/OWD 事件下的速率调整函数 --- */
static inline uint32_t simple_cc_step(uint32_t cur_rate,
                                      int decrease,
                                      uint32_t ai_factor)
{
    uint32_t min_rate = SIMPLE_CC_MIN_RATE;
    uint32_t max_rate = SIMPLE_CC_RATE_MAX;

    if (decrease) {
        /*
         * OWD 判断为拥塞时，使用原来的 MD 参数。
         * 注意：这里不是 NACK 的 10% 降速。
         */
        cur_rate = doca_pcc_dev_fxp_mult(SIMPLE_CC_MD_FXP16, cur_rate);
    } else {
        /* 加性增 */
        if (cur_rate <= max_rate - ai_factor) {
            cur_rate += ai_factor;
        } else {
            cur_rate = max_rate;
        }
    }

    if (cur_rate < min_rate)
        cur_rate = min_rate;

    if (cur_rate > max_rate)
        cur_rate = max_rate;

    return cur_rate;
}

/* --- NACK 专用降速函数：当前速率降低 10% --- */
static inline uint32_t simple_cc_nack_decrease_10(uint32_t cur_rate)
{
    uint32_t min_rate = SIMPLE_CC_MIN_RATE;
    uint32_t max_rate = SIMPLE_CC_RATE_MAX;

    /*
     * NACK 触发时：
     * new_rate = cur_rate * 0.9
     *
     * 使用 uint64_t 防止 cur_rate * 90 时溢出。
     */
    cur_rate = (uint32_t)(((uint64_t)cur_rate * NACK_RATE_KEEP_NUM) /
                          NACK_RATE_KEEP_DEN);

    if (cur_rate < min_rate)
        cur_rate = min_rate;

    if (cur_rate > max_rate)
        cur_rate = max_rate;

    return cur_rate;
}

/* --- 新流初始化 --- */
static inline void simple_cc_handle_new_flow(simple_cc_ctxt_t *ctx,
                                             doca_pcc_dev_results_t *results)
{
    ctx->cur_rate = SIMPLE_CC_MIN_RATE;

    ctx->flags.was_cnp = 0;
    ctx->flags.was_nack = 0;

    /* OWD / RTT 状态初始化 */
    ctx->owd_baseline = OWD_BASELINE_INIT;
    ctx->owd_diff = 0;
    ctx->owd_hai_counter = 0;
    ctx->prev_owd = 0;

    ctx->last_req_send_time = 0;
    ctx->last_rev_time = 0;
    ctx->rtt_valid = 0;
    ctx->initialized = 1;

    results->rate = ctx->cur_rate;
    results->rtt_req = 1;

    doca_pcc_dev_printf("NEW FLOW INIT: cur_rate=%u\n", ctx->cur_rate);
}

/* --- 主算法逻辑 --- */
void simple_cc_algo(doca_pcc_dev_event_t *event,
                    uint32_t *param,
                    uint32_t *counter,
                    doca_pcc_dev_algo_ctxt_t *algo_ctxt,
                    doca_pcc_dev_results_t *results)
{
    simple_cc_ctxt_t *ctx = (simple_cc_ctxt_t *)algo_ctxt;
    doca_pcc_dev_event_general_attr_t ev_attr = doca_pcc_dev_get_ev_attr(event);
    uint32_t ev_type = ev_attr.ev_type;

    uint32_t cur_rate;
    int decrease = 0;
    uint32_t ai_factor = SIMPLE_CC_AI_FXP20 / 2;

    int32_t delta_owd = 0;
    int32_t filt_owd = 0;

    /* 1. 显式新流初始化 */
    if (!ctx->initialized) {
        simple_cc_handle_new_flow(ctx, results);
        return;
    }

    cur_rate = ctx->cur_rate;

    /* 调试打印参数，避免 param 为 NULL 时访问 */
    {
        static int print_once = 0;

        if (param != NULL && print_once < 3) {
            doca_pcc_dev_printf("DEBUG PARAM: MD=%u, AI_HIGH=%u, AI_LOW=%u, MIN_RATE=%u\n",
                                param[SIMPLE_CC_PARAM_MD],
                                param[SIMPLE_CC_PARAM_AI_HIGH],
                                param[SIMPLE_CC_PARAM_AI_LOW],
                                param[SIMPLE_CC_PARAM_MIN_RATE]);
            print_once++;
        }
    }

    /*
     * 2. 事件分流
     *
     * NACK 事件：
     *   表示已经发生丢包/重传相关反馈。
     *   这里直接将当前速率降低 10%，然后 return。
     *   注意：NACK 事件不能继续进入 RTT timestamp 读取逻辑。
     *
     * RTT 事件：
     *   进入 Delta OWD + EWMA 调速逻辑。
     *
     * 其他事件：
     *   忽略，保持当前速率。
     */
    if (ev_type == DOCA_PCC_DEV_EVNT_ROCE_NACK) {
        static uint32_t nack_print_cnt = 0;
        uint32_t old_rate = ctx->cur_rate;

        ctx->flags.was_nack = 1;
        ctx->owd_hai_counter = 0;

        cur_rate = simple_cc_nack_decrease_10(ctx->cur_rate);

        ctx->cur_rate = cur_rate;
        results->rate = cur_rate;
        results->rtt_req = 1;

        if (counter != NULL) {
            counter[SIMPLE_CC_COUNTER_EVENTS]++;
            counter[SIMPLE_CC_COUNTER_NACKS]++;
        }

        /*
         * 限频打印，避免高丢包时 printf 把 DPA/Host 日志系统打爆。
         * 注意这里有 \n，避免日志全部连在一起。
         */
        nack_print_cnt++;
        if (nack_print_cnt <= NACK_PRINT_FIRST_N ||
            (nack_print_cnt % NACK_PRINT_INTERVAL) == 0) {
            doca_pcc_dev_printf("NACK: count=%u, old_rate=%u, new_rate=%u\n",
                                nack_print_cnt,
                                old_rate,
                                cur_rate);
        }

        return;
    }

    if (ev_type != DOCA_PCC_DEV_EVNT_RTT) {
        results->rate = ctx->cur_rate;
        results->rtt_req = 1;
        return;
    }

    /*
     * 3. RTT warmup
     *
     * 第一拍只建立时间基线，不计算 Delta OWD。
     */
    {
        uint32_t start_ts = doca_pcc_dev_get_rtt_req_send_timestamp(event);
        uint32_t req_rev_time = doca_pcc_dev_get_rtt_req_recv_timestamp(event);

        if (!ctx->rtt_valid) {
            ctx->last_req_send_time = start_ts;
            ctx->last_rev_time = req_rev_time;
            ctx->rtt_valid = 1;

            results->rate = ctx->cur_rate;
            results->rtt_req = 1;

            doca_pcc_dev_printf("RTT WARMUP: keep cur_rate=%u\n", ctx->cur_rate);
            return;
        }

        /*
         * Delta OWD:
         *
         * delta_owd =
         *   当前接收端请求接收间隔 - 当前发送端请求发送间隔
         *
         * 即：
         *   (recv_i - recv_{i-1}) - (send_i - send_{i-1})
         *
         * 这里利用 uint32_t 自然回绕计算，再转 int32_t。
         */
        delta_owd = (int32_t)((req_rev_time - ctx->last_rev_time) -
                              (start_ts - ctx->last_req_send_time));

        ctx->last_req_send_time = start_ts;
        ctx->last_rev_time = req_rev_time;
    }

    /*
     * 4. EWMA 平滑
     *
     * ctx->owd_diff 作为平滑后的 OWD 梯度。
     */
    ctx->owd_diff = (int32_t)(((int64_t)ctx->owd_diff * (OWD_EWMA_N - 1) +
                               delta_owd) / OWD_EWMA_N);

    filt_owd = ctx->owd_diff;

    /*
     * 5. OWD 决策
     *
     * filt_owd > 高阈值：
     *   前向路径排队趋势增强，执行乘性降速。
     *
     * filt_owd < 低阈值：
     *   前向路径排队趋势下降，执行加性增速。
     *   若连续多次低延迟趋势，则进入 HAI。
     *
     * 中间态：
     *   小步加速，保持温和探测。
     */
    if (filt_owd > OWD_HIGH_THRESH) {
        decrease = 1;
        ctx->owd_hai_counter = 0;
        ai_factor = SIMPLE_CC_AI_FXP20 / 2;
    } else if (filt_owd < OWD_LOW_THRESH) {
        ctx->owd_hai_counter++;

        if (ctx->owd_hai_counter >= OWD_HAI_THRESH) {
            /* 保守 HAI：2x AI */
            ai_factor = SIMPLE_CC_AI_FXP20 * 2;
        } else {
            ai_factor = SIMPLE_CC_AI_FXP20;
        }
    } else {
        ctx->owd_hai_counter = 0;
        ai_factor = SIMPLE_CC_AI_FXP20 / 2;
    }

    {
        static uint32_t owd_print = 0;

        owd_print++;
        if (owd_print < 120) {
            doca_pcc_dev_printf("CC: delta_owd=%d ns, filt_owd=%d ns\n",
                                delta_owd,
                                filt_owd);
            doca_pcc_dev_printf("cur_rate(before step)=%u\n", cur_rate);
        }
    }

    /*
     * 6. 执行 RTT/OWD 速率更新
     *
     * 注意：
     *   RTT/OWD 拥塞判断使用 simple_cc_step()
     *   NACK 降速使用 simple_cc_nack_decrease_10()
     */
    cur_rate = simple_cc_step(cur_rate, decrease, ai_factor);

    /*
     * 7. 更新上下文与结果
     */
    ctx->cur_rate = cur_rate;
    results->rate = cur_rate;
    results->rtt_req = 1;

    if (counter != NULL)
        counter[SIMPLE_CC_COUNTER_EVENTS]++;
}

/* --- 参数设置接口 --- */
doca_pcc_dev_error_t simple_cc_set_algo_params(uint32_t param_id_base,
                                               uint32_t param_num,
                                               const uint32_t *new_param_values,
                                               uint32_t *params)
{
    uint32_t i;

    if (param_num > SIMPLE_CC_PARAM_NUM || param_id_base >= SIMPLE_CC_PARAM_NUM)
        return DOCA_PCC_DEV_STATUS_FAIL;

    if (new_param_values == NULL || params == NULL)
        return DOCA_PCC_DEV_STATUS_FAIL;

    if (param_id_base + param_num > SIMPLE_CC_PARAM_NUM)
        return DOCA_PCC_DEV_STATUS_FAIL;

    for (i = 0; i < param_num; i++) {
        params[param_id_base + i] = new_param_values[i];
    }

    return DOCA_PCC_DEV_STATUS_OK;
}