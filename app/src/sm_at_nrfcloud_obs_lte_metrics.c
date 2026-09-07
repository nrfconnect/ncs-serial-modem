/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "sm_util.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <memfault/metrics/metrics.h>
#include <memfault_ncs.h>
#include <modem/lte_lc.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(sm_nrfcloud_obs_lte_metrics, CONFIG_SM_LOG_LEVEL);

#define L(x) STRINGIFY(x)

#define SM_XMONITOR_TAC_QUOTED_LEN     10
#define SM_XMONITOR_CELL_ID_QUOTED_LEN 34
#define SM_XMONITOR_PSM_QUOTED_LEN     12

#define SM_CEDRX_VALUE_LEN 4

#define AT_CEDRXS_ACTT_WB     4
#define AT_CEDRXS_ACTT_NB     5
#define AT_CEDRXS_ACTT_NTN_NB 6

/* GPRS timer unit-to-seconds lookups, indexed by the 3 unit bits (see 3GPP TS 24.008).
 * Copied from the NCS lte_lc parsing; reserved unit codes use 60 as a filler value.
 */
static const uint32_t t3412_ext_lookup[8] = {600, 3600, 36000, 2, 30, 60, 1152000, 0};
static const uint32_t t3412_lookup[8] = {2, 60, 360, 60, 60, 60, 60, 0};
static const uint32_t t3324_lookup[8] = {2, 60, 360, 60, 60, 60, 60, 0};

static int psm_timer_parse(const char *timer_str, const uint32_t *lookup_table, int32_t *seconds)
{
	char unit_str[4];
	uint32_t lut_idx;
	uint32_t timer_unit;
	uint32_t timer_value;

	if (strlen(timer_str) != 8) {
		return -EINVAL;
	}

	memcpy(unit_str, timer_str, sizeof(unit_str) - 1);
	unit_str[sizeof(unit_str) - 1] = '\0';

	lut_idx = strtoul(unit_str, NULL, 2);
	if (lut_idx >= 8U) {
		return -EINVAL;
	}

	timer_unit = lookup_table[lut_idx];
	timer_value = strtoul(timer_str + 3, NULL, 2);
	*seconds = timer_unit ? (int32_t)(timer_unit * timer_value) : -1;

	return 0;
}

static int psm_parse(const char *active_time_str, const char *tau_ext_str,
		     const char *tau_legacy_str, int32_t *tau, int32_t *active_time)
{
	int err;
	char unit_str[4];
	uint32_t lut_idx;
	uint32_t timer_unit;
	uint32_t timer_value;

	if (strlen(active_time_str) != 8 || strlen(tau_ext_str) != 8 ||
	    (tau_legacy_str != NULL && strlen(tau_legacy_str) != 8)) {
		return -EINVAL;
	}

	err = psm_timer_parse(tau_ext_str, t3412_ext_lookup, tau);
	if (err) {
		return err;
	}

	if (*tau == -1 && tau_legacy_str != NULL) {
		memcpy(unit_str, tau_legacy_str, sizeof(unit_str) - 1);
		unit_str[sizeof(unit_str) - 1] = '\0';

		lut_idx = strtoul(unit_str, NULL, 2);
		if (lut_idx >= 8U) {
			return -EINVAL;
		}

		timer_unit = t3412_lookup[lut_idx];
		if (timer_unit == 0) {
			return -EINVAL;
		}

		timer_value = strtoul(tau_legacy_str + 3, NULL, 2);
		*tau = (int32_t)(timer_unit * timer_value);
	}

	memcpy(unit_str, active_time_str, sizeof(unit_str) - 1);
	unit_str[sizeof(unit_str) - 1] = '\0';

	lut_idx = strtoul(unit_str, NULL, 2);
	if (lut_idx >= 8U) {
		return -EINVAL;
	}

	timer_unit = t3324_lookup[lut_idx];
	timer_value = strtoul(active_time_str + 3, NULL, 2);
	*active_time = timer_unit ? (int32_t)(timer_unit * timer_value) : -1;

	return 0;
}

static void edrx_ptw_multiplier_get(enum lte_lc_lte_mode lte_mode, float *ptw_multiplier)
{
	if (lte_mode == LTE_LC_LTE_MODE_NBIOT || lte_mode == LTE_LC_LTE_MODE_NTN_NBIOT) {
		*ptw_multiplier = 2.56f;
	} else {
		*ptw_multiplier = 1.28f;
	}
}

static void edrx_value_get(enum lte_lc_lte_mode lte_mode, uint8_t idx, float *edrx_value)
{
	static const uint16_t edrx_lookup_ltem[16] = {0,  1,  2,  4,  6,   8,   10,  12,
						      14, 16, 32, 64, 128, 256, 256, 256};
	static const uint16_t edrx_lookup_nbiot[16] = {2, 2,  2,  4,  2,   8,   2,   2,
						       2, 16, 32, 64, 128, 256, 512, 1024};
	uint16_t multiplier;

	if (idx >= ARRAY_SIZE(edrx_lookup_ltem)) {
		*edrx_value = 0;
		return;
	}

	if (lte_mode == LTE_LC_LTE_MODE_LTEM) {
		multiplier = edrx_lookup_ltem[idx];
	} else {
		multiplier = edrx_lookup_nbiot[idx];
	}

	*edrx_value = multiplier == 0 ? 5.12f : (float)multiplier * 10.24f;
}

static void strip_quotes(char *str)
{
	size_t len = strlen(str);

	if (len >= 2 && str[0] == '"') {
		memmove(str, str + 1, len - 2);
		str[len - 2] = '\0';
	}
}

static uint32_t parse_hex_quoted_field(const char *quoted_str)
{
	char value_str[SM_XMONITOR_CELL_ID_QUOTED_LEN + 1];

	if (quoted_str[0] == '\0') {
		return UINT32_MAX;
	}

	strncpy(value_str, quoted_str, sizeof(value_str) - 1);
	value_str[sizeof(value_str) - 1] = '\0';
	strip_quotes(value_str);

	if (value_str[0] == '\0') {
		return UINT32_MAX;
	}

	return strtoul(value_str, NULL, 16);
}

static int collect_xmonitor(void)
{
	/* Buffers are one byte larger than the scanf field width to fit the NUL, and
	 * zero-initialized so unmatched fields stay empty on a partial response.
	 */
	char tac_str[SM_XMONITOR_TAC_QUOTED_LEN + 1] = {0};
	char cell_id_str[SM_XMONITOR_CELL_ID_QUOTED_LEN + 1] = {0};
	char active_time_str[SM_XMONITOR_PSM_QUOTED_LEN + 1] = {0};
	char tau_ext_str[SM_XMONITOR_PSM_QUOTED_LEN + 1] = {0};
	char tau_legacy_str[SM_XMONITOR_PSM_QUOTED_LEN + 1] = {0};
	int ret;
	unsigned int reg_status = 0;
	int lte_mode = -1;
	int32_t tau = -1;
	int32_t active_time = -1;
	uint32_t tac;
	uint32_t cell_id;

	ret = sm_util_at_scanf(
		"AT%XMONITOR",
		"%%XMONITOR: "
		"%u,"                                     /* <reg_status> */
		"%*[^,],"                                 /* <full_name> */
		"%*[^,],"                                 /* <short_name> */
		"%*[^,],"                                 /* <plmn> */
		"%" L(SM_XMONITOR_TAC_QUOTED_LEN) "[^,]," /* <tac> */
		"%d,"                                     /* <AcT> */
		"%*u,"                                    /* <band> */
		"%" L(SM_XMONITOR_CELL_ID_QUOTED_LEN) "[^,]," /* <cell_id> */
		"%*u,"                                    /* <phys_cell_id> */
		"%*u,"                                    /* <EARFCN> */
		"%*d,"                                    /* <rsrp> */
		"%*d,"                                    /* <snr> */
		"%*[^,],"                                 /* <NW-provided_eDRX_value> */
		"%" L(SM_XMONITOR_PSM_QUOTED_LEN) "[^,]," /* <Active-Time> */
		"%" L(SM_XMONITOR_PSM_QUOTED_LEN) "[^,]," /* <Periodic-TAU-ext> */
		"%" L(SM_XMONITOR_PSM_QUOTED_LEN) "[^,]", /* <Periodic-TAU> */
		&reg_status, tac_str, &lte_mode, cell_id_str, active_time_str, tau_ext_str,
		tau_legacy_str);

	if (ret < 0) {
		LOG_DBG("AT%%XMONITOR failed (%d)", ret);
		return ret;
	}

	/* reg_status is always the first field on a successful (ret >= 1) parse. */
	if (reg_status != LTE_LC_NW_REG_REGISTERED_HOME &&
	    reg_status != LTE_LC_NW_REG_REGISTERED_ROAMING) {
		return -EAGAIN;
	}

	tac = parse_hex_quoted_field(tac_str);
	cell_id = parse_hex_quoted_field(cell_id_str);

	if (lte_mode >= 0) {
		if (MEMFAULT_METRIC_SET_UNSIGNED(ncs_lte_mode, (uint32_t)lte_mode)) {
			LOG_ERR("Failed to set ncs_lte_mode");
		}
	}

	if (cell_id != LTE_LC_CELL_EUTRAN_ID_INVALID) {
		if (MEMFAULT_METRIC_SET_SIGNED(ncs_lte_cell_id, (int32_t)cell_id)) {
			LOG_ERR("Failed to set ncs_lte_cell_id");
		}
	}

	if (tac != LTE_LC_CELL_TAC_INVALID) {
		if (MEMFAULT_METRIC_SET_SIGNED(ncs_lte_tracking_area_code, (int32_t)tac)) {
			LOG_ERR("Failed to set ncs_lte_tracking_area_code");
		}
	}

	/* PSM fields are empty ("") when PSM is not granted and absent on a short
	 * response; psm_parse() fails in both cases and the PSM metrics are skipped.
	 */
	strip_quotes(active_time_str);
	strip_quotes(tau_ext_str);
	strip_quotes(tau_legacy_str);

	if (psm_parse(active_time_str, tau_ext_str, tau_legacy_str, &tau, &active_time) == 0) {
		if (MEMFAULT_METRIC_SET_SIGNED(ncs_lte_psm_tau_seconds, tau)) {
			LOG_ERR("Failed to set ncs_lte_psm_tau_seconds");
		}

		if (MEMFAULT_METRIC_SET_SIGNED(ncs_lte_psm_active_time_seconds, active_time)) {
			LOG_ERR("Failed to set ncs_lte_psm_active_time_seconds");
		}
	}

	return 0;
}

static int cedrxrdp_act_type_to_lte_mode(unsigned int act_type, enum lte_lc_lte_mode *lte_mode)
{
	switch (act_type) {
	case 0:
		*lte_mode = LTE_LC_LTE_MODE_NONE;
		return 0;
	case AT_CEDRXS_ACTT_WB:
		*lte_mode = LTE_LC_LTE_MODE_LTEM;
		return 0;
	case AT_CEDRXS_ACTT_NB:
		*lte_mode = LTE_LC_LTE_MODE_NBIOT;
		return 0;
	case AT_CEDRXS_ACTT_NTN_NB:
		*lte_mode = LTE_LC_LTE_MODE_NTN_NBIOT;
		return 0;
	default:
		return -ENODATA;
	}
}

static int collect_edrx(void)
{
	char edrx_str[SM_CEDRX_VALUE_LEN + 1];
	char ptw_str[SM_CEDRX_VALUE_LEN + 1];
	enum lte_lc_lte_mode lte_mode;
	float edrx_seconds;
	float ptw_seconds;
	float ptw_multiplier;
	uint8_t idx;
	int ret;
	unsigned int act_type;
	int err;

	ret = sm_util_at_scanf(
		"AT+CEDRXRDP",
		"+CEDRXRDP: "
		"%u,"                                  /* <AcT-type> */
		"\"%*[^\"]\","                         /* <Requested_eDRX_value> */
		"\"%" L(SM_CEDRX_VALUE_LEN) "[^\"]\"," /* <NW-provided_eDRX_value> */
		"\"%" L(SM_CEDRX_VALUE_LEN) "[^\"]\"", /* <Paging_time_window> */
		&act_type, edrx_str, ptw_str);

	if (ret < 0) {
		LOG_DBG("AT+CEDRXRDP failed (%d)", ret);
		return ret;
	}

	if (ret < 1) {
		return -EBADMSG;
	}

	err = cedrxrdp_act_type_to_lte_mode(act_type, &lte_mode);
	if (err) {
		return err;
	}

	if (lte_mode == LTE_LC_LTE_MODE_NONE) {
		return 0;
	}

	if (ret < 3 || edrx_str[0] == '\0') {
		return 0;
	}

	idx = (uint8_t)strtoul(edrx_str, NULL, 2);
	edrx_value_get(lte_mode, idx, &edrx_seconds);

	if (ptw_str[0] == '\0') {
		return -EBADMSG;
	}

	idx = (uint8_t)strtoul(ptw_str, NULL, 2);
	if (idx > 15) {
		return -EBADMSG;
	}

	edrx_ptw_multiplier_get(lte_mode, &ptw_multiplier);
	ptw_seconds = (idx + 1) * ptw_multiplier;

	if (MEMFAULT_METRIC_SET_UNSIGNED(ncs_lte_edrx_interval_ms,
					 (uint32_t)(edrx_seconds * MSEC_PER_SEC))) {
		LOG_ERR("Failed to set ncs_lte_edrx_interval_ms");
	}

	if (MEMFAULT_METRIC_SET_UNSIGNED(ncs_lte_edrx_ptw_ms,
					 (uint32_t)(ptw_seconds * MSEC_PER_SEC))) {
		LOG_ERR("Failed to set ncs_lte_edrx_ptw_ms");
	}

	return 0;
}

static void sm_memfault_lte_metrics_collect(void)
{
	(void)collect_xmonitor();
	(void)collect_edrx();
}

void memfault_metrics_heartbeat_collect_data(void)
{
	memfault_ncs_metrics_collect_data();
	sm_memfault_lte_metrics_collect();
}
