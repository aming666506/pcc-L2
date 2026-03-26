/*
 * Forward declarations for Simple CC.
 */

#ifndef SIMPLE_CC_H_
#define SIMPLE_CC_H_

#include <doca_pcc_dev.h>
#include <doca_pcc_dev_event.h>
#include <doca_pcc_dev_algo_access.h>

void simple_cc_init(uint32_t algo_idx);
void simple_cc_algo(doca_pcc_dev_event_t *event,
		   uint32_t *param,
		   uint32_t *counter,
		   doca_pcc_dev_algo_ctxt_t *algo_ctxt,
		   doca_pcc_dev_results_t *results);
doca_pcc_dev_error_t simple_cc_set_algo_params(uint32_t param_id_base,
					 uint32_t param_num,
					 const uint32_t *new_param_values,
					 uint32_t *params);

#endif /* SIMPLE_CC_H_ */
