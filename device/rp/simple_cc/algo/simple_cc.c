/*
 * LongCC-L2:
 * Use NACK as a strong congestion signal with direct 10% decrease.
 * Use Delta OWD for RTT/OWD control.
 * Add runtime RTT classification and switch parameters according to measured RTT.
 */

#include <doca_pcc_dev.h>
#include <doca_pcc_dev_event.h>
#include <doca_pcc_dev_algo_access.h>
#include <doca_pcc_dev_utils.h>
#include <stdint.h>

#include "simple_cc_ctxt.h"
#include "simple_cc_algo_params.h"
#include "simple_cc.h"

_Static_assert(sizeof(simple_cc_ctxt_t) <= sizeof(doca_pcc_dev_algo_ctxt_t),
               "LongCC per-flow state exceeds the DOCA PCC context");

#pragma clang diagnostic ignored "-Wunused-parameter"

#define SIMPLE_CC_DEBUG 0
#define SIMPLE_CC_UNUSED(x) (void)(x)

/*
 * HAI触发阈值：
 * 连续 N 次低延迟趋势后更快加速。
 */
#define OWD_HAI_THRESH 8 

/*
 * OWD EWMA平滑参数： 
 * new = (old * (N - 1) + sample) / N
 */                                      
#define OWD_EWMA_N 8

#define FWS_PERSISTENCE_SAMPLES 2
#define LONGCC_MAX_SAMPLE_INTERVAL_NS 1000000000U

enum longcc_fws_state {
    LONGCC_FWS_GROWING = 0,
    LONGCC_FWS_STABLE = 1,
    LONGCC_FWS_RELIEVING = 2
};

/* The mode is a live DOCA parameter, so ablations use the same binary. */
enum longcc_mode {
    LONGCC_MODE_CORE = 0,       /* FWS and rate decrease cooldown */
    LONGCC_MODE_LEGACY = 1,     /* Original single-sample controller */
    LONGCC_MODE_FWS_ONLY = 2,   /* FWS without rate decrease cooldown */
    LONGCC_MODE_TRA_ONLY = 3    /* Cooldown without FWS persistence */
};

#ifndef LONGCC_DEFAULT_MODE
#define LONGCC_DEFAULT_MODE LONGCC_MODE_CORE
#endif

_Static_assert(LONGCC_DEFAULT_MODE <= LONGCC_MODE_TRA_ONLY,
               "LONGCC_DEFAULT_MODE must be between 0 and 3");

/*
 * NACK降速比例：
 * 降低10%，即保留当前速率的90%。
 */
#define NACK_RATE_KEEP_NUM 90
#define NACK_RATE_KEEP_DEN 100

/*
 * NACK打印限频：
 * 前20次NACK都打印；
 * 之后每100次打印一次。
 */
#define NACK_PRINT_FIRST_N 20
#define NACK_PRINT_INTERVAL 100

/* The RTT-specific rate parameters are compile-time values. */
typedef enum {
    SIMPLE_CC_PARAM_MODE = 0,
    SIMPLE_CC_PARAM_NUM
} simple_cc_params_t;

/* --- Counter枚举 --- */
enum {
    SIMPLE_CC_COUNTER_EVENTS = 0,
    SIMPLE_CC_COUNTER_NACKS = 1,
    SIMPLE_CC_COUNTER_NUM
};

/* --- 描述信息 --- */
static const volatile char simple_cc_desc[] =
    "OWD-only CC with NACK 10 percent decrease and runtime RTT parameter switching";

static const volatile char simple_cc_param_mode_desc[] = "LongCC ablation mode (0=core,1=legacy,2=FWS,3=TRA)";

static const volatile char simple_cc_counter_events_desc[] = "Events";
static const volatile char simple_cc_counter_nacks_desc[] = "NACKs";

/* --- 初始化函数 --- */
void simple_cc_init(uint32_t algo_idx)
{
    struct doca_pcc_dev_algo_meta_data algo_def = {0};
    const simple_cc_rtt_params_t *default_params;
    uint32_t counter_num;

    default_params =
        simple_cc_select_params_by_rtt_class(SIMPLE_CC_DEFAULT_RTT_MS);

    algo_def.algo_id = 0x7001;
    algo_def.algo_major_version = 0x01;
    algo_def.algo_minor_version = 0x05;
    algo_def.algo_desc_size = sizeof(simple_cc_desc);
    algo_def.algo_desc_addr = (uint64_t)simple_cc_desc;

    doca_pcc_dev_algo_init_metadata(algo_idx,
                                    &algo_def,
                                    SIMPLE_CC_PARAM_NUM,
                                    SIMPLE_CC_COUNTER_NUM);

    doca_pcc_dev_algo_init_param(algo_idx,
                                 SIMPLE_CC_PARAM_MODE,
                                 LONGCC_DEFAULT_MODE,
                                 LONGCC_MODE_TRA_ONLY,
                                 LONGCC_MODE_CORE,
                                 1,
                                 sizeof(simple_cc_param_mode_desc),
                                 (uint64_t)simple_cc_param_mode_desc);

    counter_num = 0;

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

    doca_pcc_dev_printf(
        "RTT-switch CC init: default_rtt=%u ms, MIN_RATE=%u, AI_HIGH=%u, AI_LOW=%u, MD=%u, MAX_RATE=%u\n",
        SIMPLE_CC_DEFAULT_RTT_MS,
        default_params->min_rate,
        default_params->ai_fxp20,
        default_params->ai_fxp20 / 2,
        default_params->md_fxp16,
        SIMPLE_CC_RATE_MAX);
}

/* --- RTT/OWD事件下的速率调整函数 --- */
static inline uint32_t
simple_cc_step(uint32_t cur_rate,
               int decrease,
               uint32_t ai_factor,
               const simple_cc_rtt_params_t *cc_params)
{
    uint32_t min_rate = cc_params->min_rate;
    uint32_t max_rate = SIMPLE_CC_RATE_MAX;

    if (decrease) {
        /*
         * OWD判断为拥塞时，使用当前RTT档位对应的MD参数。
         * 注意：这里不是NACK的10%降速。
         */
        cur_rate = doca_pcc_dev_fxp_mult(cc_params->md_fxp16, cur_rate);
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

/* --- NACK专用降速函数：当前速率降低10% --- */
static inline uint32_t
simple_cc_nack_decrease_10(uint32_t cur_rate,
                           const simple_cc_rtt_params_t *cc_params)
{
    uint32_t min_rate = cc_params->min_rate;
    uint32_t max_rate = SIMPLE_CC_RATE_MAX;

    /*
     * NACK触发时：
     *   new_rate = cur_rate * 0.9
     *
     * 使用uint64_t防止cur_rate * 90时溢出。
     */
    cur_rate = (uint32_t)(((uint64_t)cur_rate * NACK_RATE_KEEP_NUM) /
                          NACK_RATE_KEEP_DEN);

    if (cur_rate < min_rate)
        cur_rate = min_rate;

    if (cur_rate > max_rate)
        cur_rate = max_rate;

    return cur_rate;
}

static inline uint8_t
longcc_update_fws_state(simple_cc_ctxt_t *ctx, int region)
{
    if (region > 0) {
        if (ctx->grow_count < FWS_PERSISTENCE_SAMPLES)
            ctx->grow_count++;
        ctx->relief_count = 0;
        ctx->stable_count = 0;
        if (ctx->grow_count >= FWS_PERSISTENCE_SAMPLES)
            ctx->fws_state = LONGCC_FWS_GROWING;
    } else if (region < 0) {
        if (ctx->relief_count < FWS_PERSISTENCE_SAMPLES)
            ctx->relief_count++;
        ctx->grow_count = 0;
        ctx->stable_count = 0;
        if (ctx->relief_count >= FWS_PERSISTENCE_SAMPLES)
            ctx->fws_state = LONGCC_FWS_RELIEVING;
    } else {
        if (ctx->stable_count < FWS_PERSISTENCE_SAMPLES)
            ctx->stable_count++;
        ctx->grow_count = 0;
        ctx->relief_count = 0;
        if (ctx->stable_count >= FWS_PERSISTENCE_SAMPLES)
            ctx->fws_state = LONGCC_FWS_STABLE;
    }

    return ctx->fws_state;
}

static inline int
longcc_delay_md_allowed(const simple_cc_ctxt_t *ctx, uint32_t event_ts)
{
    if (!ctx->md_timestamp_valid || ctx->rtt_ewma_ns == 0)
        return 1;

    /* Unsigned subtraction handles the 32-bit nanosecond timestamp wrap. */
    return (uint32_t)(event_ts - ctx->last_md_timestamp) >= ctx->rtt_ewma_ns;
}

/* --- 新流初始化 --- */
static inline void
simple_cc_handle_new_flow(simple_cc_ctxt_t *ctx,
                          doca_pcc_dev_results_t *results)
{
    const simple_cc_rtt_params_t *cc_params;

    cc_params = simple_cc_select_params_by_rtt_class(
        SIMPLE_CC_DEFAULT_RTT_MS);

    ctx->cur_rate = cc_params->min_rate;

    /* OWD / RTT状态初始化 */
    ctx->owd_diff = 0;
    ctx->owd_hai_counter = 0;
    ctx->fws_state = LONGCC_FWS_STABLE;
    ctx->grow_count = 0;
    ctx->relief_count = 0;
    ctx->stable_count = 0;
    ctx->last_md_timestamp = 0;
    ctx->md_timestamp_valid = 0;

    ctx->last_req_send_time = 0;
    ctx->last_rev_time = 0;

    /*
     * RTT运行时状态初始化。
     * 先按默认10ms启动，收到RTT事件后自动更新。
     */
    ctx->rtt_ewma_ns = 0;
    ctx->rtt_class_ms = SIMPLE_CC_DEFAULT_RTT_MS;

    ctx->rtt_valid = 0;
    ctx->initialized = 1;

    results->rate = ctx->cur_rate;
    results->rtt_req = 1;

    doca_pcc_dev_printf(
        "NEW FLOW INIT: cur_rate=%u, default_rtt_class=%u ms\n",
        ctx->cur_rate,
        ctx->rtt_class_ms);
}

/* --- 主算法逻辑 --- */
void simple_cc_algo(doca_pcc_dev_event_t *event,
                    uint32_t *param,
                    uint32_t *counter,
                    doca_pcc_dev_algo_ctxt_t *algo_ctxt,
                    doca_pcc_dev_results_t *results)
{
    simple_cc_ctxt_t *ctx = (simple_cc_ctxt_t *)algo_ctxt;
    doca_pcc_dev_event_general_attr_t ev_attr =
        doca_pcc_dev_get_ev_attr(event);

    uint32_t ev_type = ev_attr.ev_type;
    uint32_t cur_rate;
    uint32_t mode;
    uint32_t control_ts = 0;
    int decrease = 0;
    int hold_rate = 0;
    int region;
    int use_fws;
    int use_cooldown;

    const simple_cc_rtt_params_t *cc_params;
    uint32_t ai_factor;

    int32_t delta_owd = 0;
    int32_t filt_owd = 0;

    /* 1. 显式新流初始化 */
    if (!ctx->initialized) {
        simple_cc_handle_new_flow(ctx, results);
        return;
    }

    cur_rate = ctx->cur_rate;
    mode = (param != NULL) ? param[SIMPLE_CC_PARAM_MODE] : LONGCC_MODE_CORE;
    if (mode > LONGCC_MODE_TRA_ONLY)
        mode = LONGCC_MODE_CORE;
    use_fws = (mode == LONGCC_MODE_CORE || mode == LONGCC_MODE_FWS_ONLY);
    use_cooldown = (mode == LONGCC_MODE_CORE || mode == LONGCC_MODE_TRA_ONLY);

    /*
     * 根据当前RTT档位选择参数。
     * 如果还没有收到RTT事件，则使用初始化时的默认10ms档位。
     */
    cc_params = simple_cc_select_params_by_rtt_class(ctx->rtt_class_ms);
    ai_factor = cc_params->ai_fxp20 / 2;

    /* Print the active ablation mode and RTT-specific compile-time parameters. */
    {
        static int print_once = 0;

        if (param != NULL && print_once < 3) {
            doca_pcc_dev_printf(
                "DEBUG PARAM META: mode=%u (0=core,1=legacy,2=FWS,3=TRA)\n",
                mode);

            doca_pcc_dev_printf(
                "DEBUG RUNTIME PARAM: class=%u ms, OWD_HIGH=%d ns, OWD_LOW=%d ns, MD=%u, AI=%u, MIN_RATE=%u\n",
                ctx->rtt_class_ms,
                cc_params->owd_high_thresh,
                cc_params->owd_low_thresh,
                cc_params->md_fxp16,
                cc_params->ai_fxp20,
                cc_params->min_rate);

            print_once++;
        }
    }

    /*
     * 2. 事件分流
     *
     * NACK事件：
     *   表示已经发生丢包/重传相关反馈。
     *   这里直接将当前速率降低10%，然后return。
     *   注意：NACK事件不能继续进入RTT timestamp读取逻辑。
     *
     * RTT事件：
     *   进入RTT测量、RTT档位选择、Delta OWD + EWMA调速逻辑。
     *
     * 其他事件：
     *   忽略，保持当前速率。
     */
    if (ev_type == DOCA_PCC_DEV_EVNT_ROCE_NACK) {
        static uint32_t nack_print_cnt = 0;
        uint32_t old_rate = ctx->cur_rate;

        cc_params = simple_cc_select_params_by_rtt_class(ctx->rtt_class_ms);

        ctx->owd_hai_counter = 0;

        cur_rate = simple_cc_nack_decrease_10(ctx->cur_rate, cc_params);

        ctx->cur_rate = cur_rate;
        results->rate = cur_rate;
        results->rtt_req = 1;

        if (counter != NULL) {
            counter[SIMPLE_CC_COUNTER_EVENTS]++;
            counter[SIMPLE_CC_COUNTER_NACKS]++;
        }

        /*
         * 限频打印，避免高丢包时printf把DPA/Host日志系统打爆。
         */
        nack_print_cnt++;

        if (nack_print_cnt <= NACK_PRINT_FIRST_N ||
            (nack_print_cnt % NACK_PRINT_INTERVAL) == 0) {
            doca_pcc_dev_printf(
                "NACK: count=%u, rtt_class=%u ms, old_rate=%u, new_rate=%u\n",
                nack_print_cnt,
                ctx->rtt_class_ms,
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
     * 3. RTT测量 + RTT档位更新 + Delta OWD计算
     *
     * start_ts:
     *   RTT request发送时间戳。
     *
     * req_rev_time:
     *   RTT request在接收端被接收的时间戳。
     *
     * event_ts:
     *   当前RTT事件时间戳。
     *
     * rtt_sample_ns:
     *   RTT样本，单位ns。
     */
    {
        uint32_t start_ts;
        uint32_t req_rev_time;
        uint32_t event_ts;
        uint32_t rtt_sample_ns;
        uint32_t new_rtt_class_ms;

        start_ts = doca_pcc_dev_get_rtt_req_send_timestamp(event);
        req_rev_time = doca_pcc_dev_get_rtt_req_recv_timestamp(event);

        /*
         * RTT sample:
         *   当前RTT事件时间戳 - RTT request发送时间戳。
         *
         * uint32_t减法可以自然处理回绕，
         * 前提是真实RTT远小于2^32 ns。
         */
        event_ts = doca_pcc_dev_get_timestamp(event);
        rtt_sample_ns = event_ts - start_ts;

        if (rtt_sample_ns == 0 || rtt_sample_ns > LONGCC_MAX_SAMPLE_INTERVAL_NS) {
            ctx->rtt_valid = 0;
            ctx->owd_diff = 0;
            ctx->grow_count = 0;
            ctx->relief_count = 0;
            ctx->stable_count = 0;
            ctx->fws_state = LONGCC_FWS_STABLE;
            results->rate = ctx->cur_rate;
            results->rtt_req = 1;
            return;
        }

        ctx->rtt_ewma_ns = simple_cc_update_rtt_ewma_ns(
            ctx->rtt_ewma_ns,
            rtt_sample_ns);

        new_rtt_class_ms = simple_cc_classify_rtt_ms(ctx->rtt_ewma_ns);
        ctx->rtt_class_ms = new_rtt_class_ms;

        cc_params = simple_cc_select_params_by_rtt_class(ctx->rtt_class_ms);

        /*
         * 第一拍只建立Delta OWD时间基线，不计算Delta OWD。
         * 但是RTT EWMA和RTT档位已经在上面更新了。
         */
        if (!ctx->rtt_valid) {
            ctx->last_req_send_time = start_ts;
            ctx->last_rev_time = req_rev_time;
            ctx->rtt_valid = 1;

            results->rate = ctx->cur_rate;
            results->rtt_req = 1;

            doca_pcc_dev_printf(
                "RTT WARMUP: rtt_sample=%u ns, rtt_ewma=%u ns, class=%u ms, cur_rate=%u\n",
                rtt_sample_ns,
                ctx->rtt_ewma_ns,
                ctx->rtt_class_ms,
                ctx->cur_rate);

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
         * 这里利用uint32_t自然回绕计算，再转int32_t。
         */
        {
            uint32_t send_interval = start_ts - ctx->last_req_send_time;
            uint32_t recv_interval = req_rev_time - ctx->last_rev_time;
            int64_t interval_delta = (int64_t)recv_interval - send_interval;

            /* A missing or discontinuous timestamp must not drive a rate update. */
            if (send_interval == 0 || recv_interval == 0 ||
                send_interval > LONGCC_MAX_SAMPLE_INTERVAL_NS ||
                recv_interval > LONGCC_MAX_SAMPLE_INTERVAL_NS ||
                interval_delta > (int64_t)LONGCC_MAX_SAMPLE_INTERVAL_NS ||
                interval_delta < -(int64_t)LONGCC_MAX_SAMPLE_INTERVAL_NS) {
                ctx->last_req_send_time = start_ts;
                ctx->last_rev_time = req_rev_time;
                ctx->owd_diff = 0;
                ctx->grow_count = 0;
                ctx->relief_count = 0;
                ctx->stable_count = 0;
                ctx->fws_state = LONGCC_FWS_STABLE;
                results->rate = ctx->cur_rate;
                results->rtt_req = 1;
                return;
            }

            delta_owd = (int32_t)interval_delta;
        }

        ctx->last_req_send_time = start_ts;
        ctx->last_rev_time = req_rev_time;
        control_ts = event_ts;
    }

    /*
     * 4. EWMA平滑
     *
     * ctx->owd_diff作为平滑后的OWD梯度。
     */
    ctx->owd_diff = (int32_t)(((int64_t)ctx->owd_diff *
                               (OWD_EWMA_N - 1) +
                               delta_owd) /
                              OWD_EWMA_N);

    filt_owd = ctx->owd_diff;

    /*
     * 5. OWD决策
     *
     * filt_owd > 当前RTT档位的高阈值：
     *   前向路径排队趋势增强，执行乘性降速。
     *
     * filt_owd < 当前RTT档位的低阈值：
     *   前向路径排队趋势下降，执行加性增速。
     *   若连续多次低延迟趋势，则进入HAI。
     *
     * 中间态：
     *   小步加速，保持温和探测。
     */
    cc_params = simple_cc_select_params_by_rtt_class(ctx->rtt_class_ms);

    if (filt_owd > cc_params->owd_high_thresh)
        region = 1;
    else if (filt_owd < cc_params->owd_low_thresh)
        region = -1;
    else
        region = 0;

    longcc_update_fws_state(ctx, region);

    /* During a pending state transition, retain the rate until the trend persists. */
    if (use_fws &&
        ((region > 0 && ctx->fws_state != LONGCC_FWS_GROWING) ||
         (region < 0 && ctx->fws_state != LONGCC_FWS_RELIEVING) ||
         (region == 0 && ctx->fws_state != LONGCC_FWS_STABLE))) {
        hold_rate = 1;
    }

    if (region > 0) {
        decrease = 1;
        ctx->owd_hai_counter = 0;
    } else if (region < 0 && !hold_rate) {
        if (ctx->owd_hai_counter < OWD_HAI_THRESH)
            ctx->owd_hai_counter++;
        ai_factor = (ctx->owd_hai_counter >= OWD_HAI_THRESH) ?
                    cc_params->ai_fxp20 * 2 : cc_params->ai_fxp20;
    } else {
        ctx->owd_hai_counter = 0;
    }

    if (decrease && use_cooldown && !longcc_delay_md_allowed(ctx, control_ts))
        hold_rate = 1;

    {
        static uint32_t owd_print = 0;

        owd_print++;

        if (owd_print < 120) {
            doca_pcc_dev_printf(
                "CC: rtt_ewma=%u ns, class=%u ms, delta_owd=%d ns, filt_owd=%d ns\n",
                ctx->rtt_ewma_ns,
                ctx->rtt_class_ms,
                delta_owd,
                filt_owd);

            doca_pcc_dev_printf(
                "CC PARAM: OWD_HIGH=%d ns, OWD_LOW=%d ns, AI=%u, MD=%u, MIN_RATE=%u\n",
                cc_params->owd_high_thresh,
                cc_params->owd_low_thresh,
                cc_params->ai_fxp20,
                cc_params->md_fxp16,
                cc_params->min_rate);

            doca_pcc_dev_printf(
                "mode=%u, region=%d, state=%u, grow=%u, relief=%u, hold=%d, rate=%u\n",
                mode, region, ctx->fws_state, ctx->grow_count,
                ctx->relief_count, hold_rate, cur_rate);
        }
    }

    /*
     * 6. 执行RTT/OWD速率更新
     *
     * RTT/OWD拥塞判断使用simple_cc_step()
     * NACK降速使用simple_cc_nack_decrease_10()
     */
    if (!hold_rate) {
        cur_rate = simple_cc_step(cur_rate, decrease, ai_factor, cc_params);
        if (decrease) {
            ctx->last_md_timestamp = control_ts;
            ctx->md_timestamp_valid = 1;
        }
    }

    /*
     * 7. 更新上下文与结果
     */
    ctx->cur_rate = cur_rate;
    results->rate = cur_rate;
    results->rtt_req = 1;

    if (counter != NULL)
        counter[SIMPLE_CC_COUNTER_EVENTS]++;
}

/* --- 参数设置接口：保留原接口 --- */
doca_pcc_dev_error_t
simple_cc_set_algo_params(uint32_t param_id_base,
                          uint32_t param_num,
                          const uint32_t *new_param_values,
                          uint32_t *params)
{
    if (new_param_values == NULL || params == NULL)
        return DOCA_PCC_DEV_STATUS_FAIL;

    if (param_id_base != SIMPLE_CC_PARAM_MODE || param_num != 1 ||
        new_param_values[0] > LONGCC_MODE_TRA_ONLY)
        return DOCA_PCC_DEV_STATUS_FAIL;

    params[SIMPLE_CC_PARAM_MODE] = new_param_values[0];

    return DOCA_PCC_DEV_STATUS_OK;
}
