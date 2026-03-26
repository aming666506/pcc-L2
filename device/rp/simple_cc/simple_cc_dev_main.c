/*
 * Device-side entry for Simple CC.
 */

#include <doca_pcc_dev.h>
#include <doca_pcc_dev_event.h>
#include <doca_pcc_dev_algo_access.h>

#include "algo/simple_cc.h"

#define DOCA_PCC_DEV_EVNT_ROCE_ACK_MASK (1 << DOCA_PCC_DEV_EVNT_ROCE_ACK)

void doca_pcc_dev_user_algo(doca_pcc_dev_algo_ctxt_t *algo_ctxt,
			    doca_pcc_dev_event_t *event,
			    const doca_pcc_dev_attr_t *attr,
			    doca_pcc_dev_results_t *results)
{
	uint32_t port_num = doca_pcc_dev_get_ev_attr(event).port_num;
	uint32_t *param = doca_pcc_dev_get_algo_params(port_num, attr->algo_slot);
	uint32_t *counter = doca_pcc_dev_get_counters(port_num, attr->algo_slot);

	simple_cc_algo(event, param, counter, algo_ctxt, results);
}

void doca_pcc_dev_user_init(uint32_t *disable_event_bitmask)
{
	uint32_t algo_idx = 0;
	uint32_t algo_slot = 0;
	uint32_t algo_en = 1;

	simple_cc_init(algo_idx);

	for (int port_num = 0; port_num < DOCA_PCC_DEV_MAX_NUM_PORTS; ++port_num) {
		doca_pcc_dev_init_algo_slot(port_num, algo_slot, algo_idx, algo_en);
		doca_pcc_dev_trace_5(0, port_num, algo_idx, algo_slot, algo_en, DOCA_PCC_DEV_EVNT_ROCE_ACK_MASK);
	}

	*disable_event_bitmask = DOCA_PCC_DEV_EVNT_ROCE_ACK_MASK;
	if (DOCA_PCC_DEV_ACK_NACK_TX_EVENT_DISABLED_SUPPORTED == 1)
		*disable_event_bitmask |= (1 << DOCA_PCC_DEV_EVNT_ROCE_TX_FOR_ACK_NACK);

	doca_pcc_dev_trace_flush();
}

doca_pcc_dev_error_t doca_pcc_dev_user_set_algo_params(uint32_t port_num,
					 uint32_t algo_slot,
					 uint32_t param_id_base,
					 uint32_t param_num,
					 const uint32_t *new_param_values,
					 uint32_t *params)
{
	doca_pcc_dev_error_t ret = DOCA_PCC_DEV_STATUS_OK;
	uint32_t algo_idx = doca_pcc_dev_get_algo_index(port_num, algo_slot);

	if (algo_idx == 0)
		ret = simple_cc_set_algo_params(param_id_base, param_num, new_param_values, params);
	else
		ret = DOCA_PCC_DEV_STATUS_FAIL;

	return ret;
}
