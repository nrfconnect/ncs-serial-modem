/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#if defined(CONFIG_SM_NRF_CLOUD)

#include <zephyr/kernel.h>
#include <stdio.h>
#include <zephyr/logging/log.h>
#include <date_time.h>
#include <net/nrf_cloud.h>
#include <net/nrf_cloud_agnss.h>
#include <net/nrf_cloud_pgps.h>
#include <net/nrf_cloud_coap.h>
#include "nrf_cloud_coap_transport.h"
#include <modem/at_parser.h>
#include <modem/nrf_modem_lib.h>
#include <modem/modem_info.h>
#include "sm_util.h"
#include "sm_at_host.h"
#include "sm_at_nrfcloud.h"

LOG_MODULE_REGISTER(sm_nrfcloud, CONFIG_SM_LOG_LEVEL);

#define CONFIG_LTE_NEIGHBOR_CELLS_MAX 10

#define MODEM_AT_RSP \
	"{\"appId\":\"MODEM\", \"messageType\":\"RSP\", \"data\":\"%s\"}"

	/* NCELLMEAS notification parameters */
#define AT_NCELLMEAS_START		     "AT%%NCELLMEAS"
#define AT_NCELLMEAS_STOP		     "AT%%NCELLMEASSTOP"
#define AT_NCELLMEAS_STATUS_INDEX	     1
#define AT_NCELLMEAS_STATUS_VALUE_SUCCESS    0
#define AT_NCELLMEAS_STATUS_VALUE_FAIL	     1
#define AT_NCELLMEAS_STATUS_VALUE_INCOMPLETE 2
#define AT_NCELLMEAS_CELL_ID_INDEX	     2
#define AT_NCELLMEAS_PLMN_INDEX		     3
#define AT_NCELLMEAS_TAC_INDEX		     4
#define AT_NCELLMEAS_TIMING_ADV_INDEX	     5
#define AT_NCELLMEAS_EARFCN_INDEX	     6
#define AT_NCELLMEAS_PHYS_CELL_ID_INDEX	     7
#define AT_NCELLMEAS_RSRP_INDEX		     8
#define AT_NCELLMEAS_RSRQ_INDEX		     9
#define AT_NCELLMEAS_MEASUREMENT_TIME_INDEX  10
#define AT_NCELLMEAS_PRE_NCELLS_PARAMS_COUNT 11
/* The rest of the parameters are in repeating arrays per neighboring cell.
 * The indices below refer to their index within such a repeating array.
 */
#define AT_NCELLMEAS_N_EARFCN_INDEX	     0
#define AT_NCELLMEAS_N_PHYS_CELL_ID_INDEX    1
#define AT_NCELLMEAS_N_RSRP_INDEX	     2
#define AT_NCELLMEAS_N_RSRQ_INDEX	     3
#define AT_NCELLMEAS_N_TIME_DIFF_INDEX	     4
#define AT_NCELLMEAS_N_PARAMS_COUNT	     5
#define AT_NCELLMEAS_N_MAX_ARRAY_SIZE	     CONFIG_LTE_NEIGHBOR_CELLS_MAX

#define AT_NCELLMEAS_PARAMS_COUNT_MAX                                                              \
	(AT_NCELLMEAS_PRE_NCELLS_PARAMS_COUNT +                                                    \
	 AT_NCELLMEAS_N_PARAMS_COUNT * CONFIG_LTE_NEIGHBOR_CELLS_MAX)

#define AT_NCELLMEAS_GCI_CELL_PARAMS_COUNT 12

static struct k_work cloud_cmd;
static K_SEM_DEFINE(sem_date_time, 0, 1);
uint8_t sm_at_buf[CONFIG_SM_AT_BUF_SIZE + 1];
static struct modem_pipe *nrfcloud_pipe;

#if defined(CONFIG_SM_NRF_CLOUD_LOCATION)

/* Cellular positioning services .*/
enum sm_nrfcloud_cellpos {
	CELLPOS_NONE = 0,
	CELLPOS_SINGLE_CELL,
	CELLPOS_MULTI_CELL,

	CELLPOS_COUNT /* Keep last. */
};
/* Whether cellular positioning is requested and if so, which. */
static enum sm_nrfcloud_cellpos nrfcloud_cell_pos = CELLPOS_NONE;

/* Whether Wi-Fi positioning is requested. */
static bool nrfcloud_wifi_pos;

/* Whether a location request is currently being sent to nRF Cloud. */
static bool nrfcloud_sending_loc_req;

static struct k_work nrfcloud_loc_req;
static struct k_work nrfcloud_conn_work;

/* Parameters saved before submitting connection work. */
static bool nrfcloud_connect;
static bool nrfcloud_conn_send_location;

/** Definitions for %NCELLMEAS notification
 * %NCELLMEAS: status [,<cell_id>, <plmn>, <tac>, <timing_advance>, <current_earfcn>,
 * <current_phys_cell_id>, <current_rsrp>, <current_rsrq>,<measurement_time>,]
 * [,<n_earfcn>1, <n_phys_cell_id>1, <n_rsrp>1, <n_rsrq>1,<time_diff>1]
 * [,<n_earfcn>2, <n_phys_cell_id>2, <n_rsrp>2, <n_rsrq>2,<time_diff>2] ...
 * [,<n_earfcn>17, <n_phys_cell_id>17, <n_rsrp>17, <n_rsrq>17,<time_diff>17
 *
 * Max 17 ncell
 * 11 number of parameters for current cell (including "%NCELLMEAS")
 * 5  number of parameters for one neighboring cell
 */
#define MAX_PARAM_CELL   11
#define MAX_PARAM_NCELL  5
/* Neighbor cells are not very useful for positioning so we don't need to support 10 or 17 */
#define NCELL_CNT 5

/* Neighboring cell measurements. */
static struct lte_lc_ncell nrfcloud_ncells[NCELL_CNT];

/* nRF Cloud location request cellular data. */
static struct lte_lc_cells_info nrfcloud_cell_data = {
	.neighbor_cells = nrfcloud_ncells,
	.gci_cells_count = 0
};
static bool nrfcloud_ncellmeas_done;

#define WIFI_APS_BEGIN_IDX 3

/* nRF Cloud location request Wi-Fi data. */
static struct wifi_scan_info nrfcloud_wifi_data;

#endif /* CONFIG_SM_NRF_CLOUD_LOCATION */

static char nrfcloud_device_id[NRF_CLOUD_CLIENT_ID_MAX_LEN];

bool sm_nrf_cloud_ready;
bool sm_nrf_cloud_send_location;

#if defined(CONFIG_SM_NRF_CLOUD_LOCATION)

static int parse_ncellmeas_gci(const char *at_response, struct lte_lc_cells_info *cells);

static enum lte_lc_neighbor_search_type search_type;
static uint8_t gci_count;

int string_to_int(const char *str_buf, int base, int *output)
{
	int temp;
	char *end_ptr;

	__ASSERT_NO_MSG(str_buf != NULL);

	errno = 0;
	temp = strtol(str_buf, &end_ptr, base);

	if (end_ptr == str_buf || *end_ptr != '\0' ||
	    ((temp == LONG_MAX || temp == LONG_MIN) && errno == ERANGE)) {
		return -ENODATA;
	}

	*output = temp;

	return 0;
}

int string_param_to_int(struct at_parser *parser, size_t idx, int *output, int base)
{
	int err;
	char str_buf[16];
	size_t len = sizeof(str_buf);

	__ASSERT_NO_MSG(parser != NULL);
	__ASSERT_NO_MSG(output != NULL);

	err = at_parser_string_get(parser, idx, str_buf, &len);
	if (err) {
		return err;
	}

	if (string_to_int(str_buf, base, output)) {
		return -ENODATA;
	}

	return 0;
}

int plmn_param_string_to_mcc_mnc(struct at_parser *parser, size_t idx, int *mcc, int *mnc)
{
	int err;
	char str_buf[7];
	size_t len = sizeof(str_buf);

	err = at_parser_string_get(parser, idx, str_buf, &len);
	if (err) {
		LOG_ERR("Could not get PLMN, error: %d", err);
		return err;
	}

	str_buf[len] = '\0';

	/* Read MNC and store as integer. The MNC starts as the fourth character
	 * in the string, following three characters long MCC.
	 */
	err = string_to_int(&str_buf[3], 10, mnc);
	if (err) {
		LOG_ERR("Could not get MNC, error: %d", err);
		return err;
	}

	/* NUL-terminate MCC, read and store it. */
	str_buf[3] = '\0';

	err = string_to_int(str_buf, 10, mcc);
	if (err) {
		LOG_ERR("Could not get MCC, error: %d", err);
		return err;
	}

	return 0;
}

/* Notification subscriptions are reset on CFUN=0.
 * We intercept CFUN set commands to automatically subscribe.
 */
 AT_CMD_CUSTOM(at_ncellmeas_interceptor, "AT%NCELLMEAS=", at_ncellmeas_interceptor_fn);

 static int at_ncellmeas_interceptor_fn(char *buf, size_t len, char *at_cmd)
 {
	int ret;
	unsigned int search_type_tmp;
 
	LOG_DBG("at_ncellmeas_interceptor_fn: %s", at_cmd);
 
	/* sscanf() doesn't match if this is a test command (it also gets intercepted). */
	if (sscanf(at_cmd, "%*[^=]=%u", &search_type_tmp) == 1) {
		 LOG_DBG("search_type: %u", search_type_tmp);
		 search_type = search_type_tmp;
	}
 
	/* Forward AT%NCELLMEAS command to the modem. */
	ret = sm_util_at_cmd_no_intercept(buf, len, at_cmd);
	if (ret) {
		return ret;
	}
	return 0;
 }

AT_MONITOR(ncell_meas, "NCELLMEAS", at_handler_ncellmeas, PAUSED);

static void ncell_meas_mon(const char *notify)
{
	int err;
	uint32_t param_count;
	int ncellmeas_status;
	char cid[9] = {0};
	size_t size;
	char plmn[6] = {0};
	char mcc[4]  = {0};
	char tac[9] = {0};
	unsigned int ncells_count = 0;
	struct at_parser parser;

	nrfcloud_ncellmeas_done = false;
	err = at_parser_init(&parser, notify);
	if (err) {
		goto exit;
	}

	/* parse status, 0: success 1: fail */
	err = at_parser_num_get(&parser, 1, &ncellmeas_status);
	if (err) {
		goto exit;
	}
	if (ncellmeas_status != 0) {
		LOG_ERR("NCELLMEAS failed");
		err = -EAGAIN;
		goto exit;
	}

	err = at_parser_cmd_count_get(&parser, &param_count);
	if (err) {
		goto exit;
	}
	if (param_count < MAX_PARAM_CELL) { /* at least current cell */
		LOG_ERR("Missing param in NCELLMEAS notification");
		err = -EAGAIN;
		goto exit;
	}

	/* parse Cell ID */
	size = sizeof(cid);
	err = util_string_get(&parser, 2, cid, &size);
	if (err) {
		goto exit;
	}
	err = util_str_to_int(cid, 16, (int *)&nrfcloud_cell_data.current_cell.id);
	if (err) {
		goto exit;
	}

	/* parse PLMN */
	size = sizeof(plmn);
	err = util_string_get(&parser, 3, plmn, &size);
	if (err) {
		goto exit;
	}
	strncpy(mcc, plmn, 3); /* MCC always 3-digit */
	err = util_str_to_int(mcc, 10, &nrfcloud_cell_data.current_cell.mcc);
	if (err) {
		goto exit;
	}
	err = util_str_to_int(&plmn[3], 10, &nrfcloud_cell_data.current_cell.mnc);
	if (err) {
		goto exit;
	}

	/* parse TAC */
	size = sizeof(tac);
	err = util_string_get(&parser, 4, tac, &size);
	if (err) {
		goto exit;
	}
	err = util_str_to_int(tac, 16, (int *)&nrfcloud_cell_data.current_cell.tac);
	if (err) {
		goto exit;
	}

	/* omit timing_advance */
	nrfcloud_cell_data.current_cell.timing_advance = NRF_CLOUD_LOCATION_CELL_OMIT_TIME_ADV;

	/* parse EARFCN */
	err = at_parser_num_get(&parser, 6, &nrfcloud_cell_data.current_cell.earfcn);
	if (err) {
		goto exit;
	}

	/* parse PCI */
	err = at_parser_num_get(&parser, 7,
				&nrfcloud_cell_data.current_cell.phys_cell_id);
	if (err) {
		goto exit;
	}

	/* parse RSRP and RSRQ */
	err = at_parser_num_get(&parser, 8, &nrfcloud_cell_data.current_cell.rsrp);
	if (err < 0) {
		goto exit;
	}
	err = at_parser_num_get(&parser, 9, &nrfcloud_cell_data.current_cell.rsrq);
	if (err < 0) {
		goto exit;
	}

	/* omit measurement_time and parse neighboring cells */

	ncells_count = (param_count - MAX_PARAM_CELL) / MAX_PARAM_NCELL;
	for (unsigned int i = 0; i != ncells_count; ++i) {
		const unsigned int offset = MAX_PARAM_CELL + i * MAX_PARAM_NCELL;

		if (i >= NCELL_CNT) {
			LOG_INF("Too many neighboring cells (%d) for allocated buffer (%d)",
				i, NCELL_CNT);
			break;
		}

		/* parse n_earfcn */
		err = at_parser_num_get(&parser, offset, &nrfcloud_ncells[i].earfcn);
		if (err < 0) {
			goto exit;
		}

		/* parse n_phys_cell_id */
		err = at_parser_num_get(&parser, offset + 1,
					&nrfcloud_ncells[i].phys_cell_id);
		if (err < 0) {
			goto exit;
		}

		/* parse n_rsrp */
		err = at_parser_num_get(&parser, offset + 2, &nrfcloud_ncells[i].rsrp);
		if (err < 0) {
			goto exit;
		}

		/* parse n_rsrq */
		err = at_parser_num_get(&parser, offset + 3, &nrfcloud_ncells[i].rsrq);
		if (err < 0) {
			goto exit;
		}
		/* omit time_diff */
	}

	err = 0;
	nrfcloud_cell_data.ncells_count = ncells_count;
	nrfcloud_ncellmeas_done = true;

exit:
	LOG_INF("NCELLMEAS notification parse (err: %d)", err);
}

int get_single_cell_info(struct lte_lc_cell *const cell_inf)
{
	int err;
	struct modem_param_info modem_inf;

	err = modem_info_params_init(&modem_inf);
	if (err) {
		LOG_ERR("Could not initialize modem info module, error: %d", err);
		return err;
	}
	err = modem_info_params_get(&modem_inf);
	if (err) {
		LOG_ERR("Could not obtain information from modem, error: %d", err);
		return err;
	}
	cell_inf->mcc = modem_inf.network.mcc.value;
	cell_inf->mnc = modem_inf.network.mnc.value;
	cell_inf->tac = modem_inf.network.area_code.value;
	cell_inf->id = modem_inf.network.cellid_dec;
	cell_inf->rsrp = modem_inf.network.rsrp.value;

	return 0;
}

static void loc_req_wk(struct k_work *work)
{
	int err = 0;

	if (nrfcloud_cell_pos == CELLPOS_SINGLE_CELL) {
		LOG_DBG("Requesting single-cell location from nRF Cloud.");
		/* Obtain the single cell info from the modem */
		err = get_single_cell_info(&nrfcloud_cell_data.current_cell);
		if (err) {
			LOG_ERR("Failed to obtain single-cell cellular network information (%d).",
				err);
		} else {
			nrfcloud_cell_data.ncells_count = 0;
			/* Invalidate the last neighboring cell measurements
			 * because they have been partly overwritten.
			 */
			nrfcloud_ncellmeas_done = false;
		}
	}

	struct nrf_cloud_location_result result = {0};
	const struct nrf_cloud_rest_location_request request = {
		.config = NULL,
		.cell_info = nrfcloud_cell_pos ? &nrfcloud_cell_data : NULL,
		.wifi_info = nrfcloud_wifi_pos ? &nrfcloud_wifi_data : NULL};

	err = nrf_cloud_coap_location_get(&request, &result);
	if (err == 0) {
		urc_send_to(nrfcloud_pipe, "\r\n#XNRFCLOUDPOS: %d,%lf,%lf,%d\r\n",
			result.type, result.lat, result.lon, result.unc);
	} else {
		LOG_ERR("Failed to request nRF Cloud location (%d).", err);
		urc_send_to(nrfcloud_pipe, "\r\n#XNRFCLOUDPOS: %d\r\n", err < 0 ? -1 : err);
	}

	if (nrfcloud_wifi_pos) {
		k_free(nrfcloud_wifi_data.ap_info);
	}
	nrfcloud_sending_loc_req = false;
}

/* Counts the frequency of a character in a null-terminated string. */
static uint32_t get_char_frequency(const char *str, char c)
{
	uint32_t count = 0;

	__ASSERT_NO_MSG(str != NULL);

	do {
		if (*str == c) {
			count++;
		}
	} while (*(str++) != '\0');

	return count;
}

static uint32_t neighborcell_count_get(const char *at_response)
{
	uint32_t comma_count, ncell_elements, ncell_count;

	__ASSERT_NO_MSG(at_response != NULL);

	comma_count = get_char_frequency(at_response, ',');
	if (comma_count < AT_NCELLMEAS_PRE_NCELLS_PARAMS_COUNT) {
		return 0;
	}

	/* Add one, as there's no comma after the last element. */
	ncell_elements = comma_count - (AT_NCELLMEAS_PRE_NCELLS_PARAMS_COUNT - 1) + 1;
	ncell_count = ncell_elements / AT_NCELLMEAS_N_PARAMS_COUNT;

	return ncell_count;
}

static int parse_ncellmeas_gci(const char *at_response, struct lte_lc_cells_info *cells)
{
	struct at_parser parser;
	struct lte_lc_ncell *ncells = NULL;
	int err, status, tmp_int, len;
	int16_t tmp_short;
	char tmp_str[7];
	bool incomplete = false;
	int curr_index;
	size_t i = 0, j = 0, k = 0;

	/* Count the actual number of parameters in the AT response before
	 * allocating heap for it. This may save quite a bit of heap as the
	 * worst case scenario is 96 elements.
	 * 3 is added to account for the parameters that do not have a trailing
	 * comma.
	 */
	size_t param_count = get_char_frequency(at_response, ',') + 3;

	__ASSERT_NO_MSG(at_response != NULL);
	__ASSERT_NO_MSG(cells != NULL);
	__ASSERT_NO_MSG(cells->gci_cells != NULL);

	/* Fill the defaults */
	cells->gci_cells_count = 0;
	cells->ncells_count = 0;
	cells->current_cell.id = LTE_LC_CELL_EUTRAN_ID_INVALID;

	for (i = 0; i < gci_count; i++) {
		cells->gci_cells[i].id = LTE_LC_CELL_EUTRAN_ID_INVALID;
		cells->gci_cells[i].timing_advance = LTE_LC_CELL_TIMING_ADVANCE_INVALID;
	}

	/*
	 * Response format for GCI search types:
	 * High level:
	 * status[,
	 *	GCI_cell_info1,neighbor_count1[,neighbor_cell1_1,neighbor_cell1_2...],
	 *	GCI_cell_info2,neighbor_count2[,neighbor_cell2_1,neighbor_cell2_2...]...]
	 *
	 * Detailed:
	 * %NCELLMEAS: status
	 * [,<cell_id>,<plmn>,<tac>,<ta>,<ta_meas_time>,<earfcn>,<phys_cell_id>,<rsrp>,<rsrq>,
	 *		<meas_time>,<serving>,<neighbor_count>
	 *	[,<n_earfcn1>,<n_phys_cell_id1>,<n_rsrp1>,<n_rsrq1>,<time_diff1>]
	 *	[,<n_earfcn2>,<n_phys_cell_id2>,<n_rsrp2>,<n_rsrq2>,<time_diff2>]...],
	 *  <cell_id>,<plmn>,<tac>,<ta>,<ta_meas_time>,<earfcn>,<phys_cell_id>,<rsrp>,<rsrq>,
	 *		<meas_time>,<serving>,<neighbor_count>
	 *	[,<n_earfcn1>,<n_phys_cell_id1>,<n_rsrp1>,<n_rsrq1>,<time_diff1>]
	 *	[,<n_earfcn2>,<n_phys_cell_id2>,<n_rsrp2>,<n_rsrq2>,<time_diff2>]...]...
	 */

	err = at_parser_init(&parser, at_response);
	__ASSERT_NO_MSG(err == 0);

	/* Status code */
	curr_index = AT_NCELLMEAS_STATUS_INDEX;
	err = at_parser_num_get(&parser, curr_index, &status);
	if (err) {
		LOG_DBG("Cannot parse NCELLMEAS status");
		goto clean_exit;
	}

	if (status == AT_NCELLMEAS_STATUS_VALUE_FAIL) {
		err = 1;
		LOG_WRN("NCELLMEAS failed");
		goto clean_exit;
	} else if (status == AT_NCELLMEAS_STATUS_VALUE_INCOMPLETE) {
		LOG_WRN("NCELLMEAS interrupted; results incomplete");
		if (param_count == 3) {
			/* No results, skip parsing. */
			goto clean_exit;
		}
	}

	/* Go through the cells */
	for (i = 0; curr_index < (param_count - (AT_NCELLMEAS_GCI_CELL_PARAMS_COUNT + 1)) &&
		    i < gci_count;
	     i++) {
		struct lte_lc_cell parsed_cell;
		bool is_serving_cell;
		uint8_t parsed_ncells_count;

		/* <cell_id>  */
		curr_index++;
		err = string_param_to_int(&parser, curr_index, &tmp_int, 16);
		if (err) {
			LOG_ERR("Could not parse cell_id, index %d, i %d error: %d", curr_index, i,
				err);
			goto clean_exit;
		}

		if (tmp_int > LTE_LC_CELL_EUTRAN_ID_MAX) {
			LOG_WRN("cell_id = %d which is > LTE_LC_CELL_EUTRAN_ID_MAX; "
				"marking invalid",
				tmp_int);
			tmp_int = LTE_LC_CELL_EUTRAN_ID_INVALID;
		}
		parsed_cell.id = tmp_int;

		/* <plmn> */
		len = sizeof(tmp_str);

		curr_index++;
		err = at_parser_string_get(&parser, curr_index, tmp_str, &len);
		if (err) {
			LOG_ERR("Could not parse plmn, error: %d", err);
			goto clean_exit;
		}

		/* Read MNC and store as integer. The MNC starts as the fourth character
		 * in the string, following three characters long MCC.
		 */
		err = string_to_int(&tmp_str[3], 10, &parsed_cell.mnc);
		if (err) {
			LOG_ERR("string_to_int, error: %d", err);
			goto clean_exit;
		}

		/* Null-terminated MCC, read and store it. */
		tmp_str[3] = '\0';

		err = string_to_int(tmp_str, 10, &parsed_cell.mcc);
		if (err) {
			LOG_ERR("string_to_int, error: %d", err);
			goto clean_exit;
		}

		/* <tac> */
		curr_index++;
		err = string_param_to_int(&parser, curr_index, &tmp_int, 16);
		if (err) {
			LOG_ERR("Could not parse tracking_area_code in i %d, error: %d", i, err);
			goto clean_exit;
		}
		parsed_cell.tac = tmp_int;

		/* <ta> */
		curr_index++;
		err = at_parser_num_get(&parser, curr_index, &tmp_int);
		if (err) {
			LOG_ERR("Could not parse timing_advance, error: %d", err);
			goto clean_exit;
		}
		parsed_cell.timing_advance = tmp_int;

		/* <ta_meas_time> */
		curr_index++;
		err = at_parser_num_get(&parser, curr_index, &parsed_cell.timing_advance_meas_time);
		if (err) {
			LOG_ERR("Could not parse timing_advance_meas_time, error: %d", err);
			goto clean_exit;
		}

		/* <earfcn> */
		curr_index++;
		err = at_parser_num_get(&parser, curr_index, &parsed_cell.earfcn);
		if (err) {
			LOG_ERR("Could not parse earfcn, error: %d", err);
			goto clean_exit;
		}

		/* <phys_cell_id> */
		curr_index++;
		err = at_parser_num_get(&parser, curr_index, &parsed_cell.phys_cell_id);
		if (err) {
			LOG_ERR("Could not parse phys_cell_id, error: %d", err);
			goto clean_exit;
		}

		/* <rsrp> */
		curr_index++;
		err = at_parser_num_get(&parser, curr_index, &parsed_cell.rsrp);
		if (err) {
			LOG_ERR("Could not parse rsrp, error: %d", err);
			goto clean_exit;
		}

		/* <rsrq> */
		curr_index++;
		err = at_parser_num_get(&parser, curr_index, &parsed_cell.rsrq);
		if (err) {
			LOG_ERR("Could not parse rsrq, error: %d", err);
			goto clean_exit;
		}

		/* <meas_time> */
		curr_index++;
		err = at_parser_num_get(&parser, curr_index, &parsed_cell.measurement_time);
		if (err) {
			LOG_ERR("Could not parse meas_time, error: %d", err);
			goto clean_exit;
		}

		/* <serving> */
		curr_index++;
		err = at_parser_num_get(&parser, curr_index, &tmp_short);
		if (err) {
			LOG_ERR("Could not parse serving, error: %d", err);
			goto clean_exit;
		}
		is_serving_cell = tmp_short;

		/* <neighbor_count> */
		curr_index++;
		err = at_parser_num_get(&parser, curr_index, &tmp_short);
		if (err) {
			LOG_ERR("Could not parse neighbor_count, error: %d", err);
			goto clean_exit;
		}
		parsed_ncells_count = tmp_short;

		if (is_serving_cell) {
			int to_be_parsed_ncell_count = 0;

			/* This the current/serving cell.
			 * In practice the <neighbor_count> is always 0 for other than
			 * the serving cell, i.e. no neigbour cell list is available.
			 * Thus, handle neighbor cells only for the serving cell.
			 */
			cells->current_cell = parsed_cell;
			if (parsed_ncells_count != 0) {
				/* Allocate room for the parsed neighbor info. */
				if (parsed_ncells_count > CONFIG_LTE_NEIGHBOR_CELLS_MAX) {
					to_be_parsed_ncell_count = CONFIG_LTE_NEIGHBOR_CELLS_MAX;
					incomplete = true;
					LOG_WRN("Cutting response, because received neigbor cell"
						" count is bigger than configured max: %d",
						CONFIG_LTE_NEIGHBOR_CELLS_MAX);

				} else {
					to_be_parsed_ncell_count = parsed_ncells_count;
				}
				ncells = k_calloc(to_be_parsed_ncell_count,
						  sizeof(struct lte_lc_ncell));
				if (ncells == NULL) {
					LOG_WRN("Failed to allocate memory for the ncells"
						" (continue)");
					continue;
				}
				cells->neighbor_cells = ncells;
				cells->ncells_count = to_be_parsed_ncell_count;
			}

			/* Parse neighbors */
			for (j = 0; j < parsed_ncells_count; j++) {
				/* If maximum number of cells has been stored, skip the data for
				 * the remaining ncells to be able to continue from next GCI cell
				 */
				if (j >= to_be_parsed_ncell_count) {
					LOG_WRN("Ignoring ncell");
					curr_index += 5;
					continue;
				}
				/* <n_earfcn[j]> */
				curr_index++;
				err = at_parser_num_get(&parser, curr_index,
							&cells->neighbor_cells[j].earfcn);
				if (err) {
					LOG_ERR("Could not parse n_earfcn, error: %d", err);
					goto clean_exit;
				}

				/* <n_phys_cell_id[j]> */
				curr_index++;
				err = at_parser_num_get(&parser, curr_index,
							&cells->neighbor_cells[j].phys_cell_id);
				if (err) {
					LOG_ERR("Could not parse n_phys_cell_id, error: %d", err);
					goto clean_exit;
				}

				/* <n_rsrp[j]> */
				curr_index++;
				err = at_parser_num_get(&parser, curr_index, &tmp_int);
				if (err) {
					LOG_ERR("Could not parse n_rsrp, error: %d", err);
					goto clean_exit;
				}
				cells->neighbor_cells[j].rsrp = tmp_int;

				/* <n_rsrq[j]> */
				curr_index++;
				err = at_parser_num_get(&parser, curr_index, &tmp_int);
				if (err) {
					LOG_ERR("Could not parse n_rsrq, error: %d", err);
					goto clean_exit;
				}
				cells->neighbor_cells[j].rsrq = tmp_int;

				/* <time_diff[j]> */
				curr_index++;
				err = at_parser_num_get(&parser, curr_index,
							&cells->neighbor_cells[j].time_diff);
				if (err) {
					LOG_ERR("Could not parse time_diff, error: %d", err);
					goto clean_exit;
				}
			}
		} else {
			cells->gci_cells[k] = parsed_cell;
			cells->gci_cells_count++; /* Increase count for non-serving GCI cell */
			k++;
		}
	}

	if (incomplete) {
		err = -E2BIG;
		LOG_WRN("Buffer is too small; results incomplete: %d", err);
	}

clean_exit:
	return err;
}

static int parse_ncellmeas(const char *at_response, struct lte_lc_cells_info *cells)
{
	int err, status, tmp;
	struct at_parser parser;
	size_t count = 0;
	bool incomplete = false;

	__ASSERT_NO_MSG(at_response != NULL);
	__ASSERT_NO_MSG(cells != NULL);

	cells->ncells_count = 0;
	cells->current_cell.id = LTE_LC_CELL_EUTRAN_ID_INVALID;

	err = at_parser_init(&parser, at_response);
	__ASSERT_NO_MSG(err == 0);

	err = at_parser_cmd_count_get(&parser, &count);
	if (err) {
		LOG_ERR("Could not get NCELLMEAS param count, "
			"potentially malformed notification, error: %d",
			err);
		goto clean_exit;
	}

	/* Status code */
	err = at_parser_num_get(&parser, AT_NCELLMEAS_STATUS_INDEX, &status);
	if (err) {
		goto clean_exit;
	}

	if (status == AT_NCELLMEAS_STATUS_VALUE_FAIL) {
		err = 1;
		LOG_WRN("NCELLMEAS failed");
		goto clean_exit;
	} else if (status == AT_NCELLMEAS_STATUS_VALUE_INCOMPLETE) {
		LOG_WRN("NCELLMEAS interrupted; results incomplete");
		if (count == 2) {
			/* No results, skip parsing. */
			goto clean_exit;
		}
	}

	/* Current cell ID */
	err = string_param_to_int(&parser, AT_NCELLMEAS_CELL_ID_INDEX, &tmp, 16);
	if (err) {
		goto clean_exit;
	}

	if (tmp > LTE_LC_CELL_EUTRAN_ID_MAX) {
		tmp = LTE_LC_CELL_EUTRAN_ID_INVALID;
	}
	cells->current_cell.id = tmp;

	/* PLMN, that is, MCC and MNC */
	err = plmn_param_string_to_mcc_mnc(&parser, AT_NCELLMEAS_PLMN_INDEX,
					   &cells->current_cell.mcc, &cells->current_cell.mnc);
	if (err) {
		goto clean_exit;
	}

	/* Tracking area code */
	err = string_param_to_int(&parser, AT_NCELLMEAS_TAC_INDEX, &tmp, 16);
	if (err) {
		goto clean_exit;
	}

	cells->current_cell.tac = tmp;

	/* Timing advance */
	err = at_parser_num_get(&parser, AT_NCELLMEAS_TIMING_ADV_INDEX, &tmp);
	if (err) {
		goto clean_exit;
	}

	cells->current_cell.timing_advance = tmp;

	/* EARFCN */
	err = at_parser_num_get(&parser, AT_NCELLMEAS_EARFCN_INDEX, &cells->current_cell.earfcn);
	if (err) {
		goto clean_exit;
	}

	/* Physical cell ID */
	err = at_parser_num_get(&parser, AT_NCELLMEAS_PHYS_CELL_ID_INDEX,
				&cells->current_cell.phys_cell_id);
	if (err) {
		goto clean_exit;
	}

	/* RSRP */
	err = at_parser_num_get(&parser, AT_NCELLMEAS_RSRP_INDEX, &tmp);
	if (err) {
		goto clean_exit;
	}

	cells->current_cell.rsrp = tmp;

	/* RSRQ */
	err = at_parser_num_get(&parser, AT_NCELLMEAS_RSRQ_INDEX, &tmp);
	if (err) {
		goto clean_exit;
	}

	cells->current_cell.rsrq = tmp;

	/* Measurement time */
	err = at_parser_num_get(&parser, AT_NCELLMEAS_MEASUREMENT_TIME_INDEX,
				&cells->current_cell.measurement_time);
	if (err) {
		goto clean_exit;
	}

	/* Neighbor cell count */
	cells->ncells_count = neighborcell_count_get(at_response);

	/* Starting from modem firmware v1.3.1, timing advance measurement time
	 * information is added as the last parameter in the response.
	 */
	size_t ta_meas_time_index = AT_NCELLMEAS_PRE_NCELLS_PARAMS_COUNT +
				    cells->ncells_count * AT_NCELLMEAS_N_PARAMS_COUNT;

	if (count > ta_meas_time_index) {
		err = at_parser_num_get(&parser, ta_meas_time_index,
					&cells->current_cell.timing_advance_meas_time);
		if (err) {
			goto clean_exit;
		}
	} else {
		cells->current_cell.timing_advance_meas_time = 0;
	}

	if (cells->ncells_count == 0) {
		goto clean_exit;
	}

	__ASSERT_NO_MSG(cells->neighbor_cells != NULL);

	if (cells->ncells_count > CONFIG_LTE_NEIGHBOR_CELLS_MAX) {
		cells->ncells_count = CONFIG_LTE_NEIGHBOR_CELLS_MAX;
		incomplete = true;
		LOG_WRN("Cutting response, because received neigbor cell"
			" count is bigger than configured max: %d",
			CONFIG_LTE_NEIGHBOR_CELLS_MAX);
	}

	/* Neighboring cells */
	for (size_t i = 0; i < cells->ncells_count; i++) {
		size_t start_idx =
			AT_NCELLMEAS_PRE_NCELLS_PARAMS_COUNT + i * AT_NCELLMEAS_N_PARAMS_COUNT;

		/* EARFCN */
		err = at_parser_num_get(&parser, start_idx + AT_NCELLMEAS_N_EARFCN_INDEX,
					&cells->neighbor_cells[i].earfcn);
		if (err) {
			goto clean_exit;
		}

		/* Physical cell ID */
		err = at_parser_num_get(&parser, start_idx + AT_NCELLMEAS_N_PHYS_CELL_ID_INDEX,
					&cells->neighbor_cells[i].phys_cell_id);
		if (err) {
			goto clean_exit;
		}

		/* RSRP */
		err = at_parser_num_get(&parser, start_idx + AT_NCELLMEAS_N_RSRP_INDEX, &tmp);
		if (err) {
			goto clean_exit;
		}

		cells->neighbor_cells[i].rsrp = tmp;

		/* RSRQ */
		err = at_parser_num_get(&parser, start_idx + AT_NCELLMEAS_N_RSRQ_INDEX, &tmp);
		if (err) {
			goto clean_exit;
		}

		cells->neighbor_cells[i].rsrq = tmp;

		/* Time difference */
		err = at_parser_num_get(&parser, start_idx + AT_NCELLMEAS_N_TIME_DIFF_INDEX,
					&cells->neighbor_cells[i].time_diff);
		if (err) {
			goto clean_exit;
		}
	}

	if (incomplete) {
		err = -E2BIG;
		LOG_WRN("Buffer is too small; results incomplete: %d", err);
	}

clean_exit:
	return err;
}

static void at_handler_ncellmeas_gci(const char *response)
{
	int err;
	const char *resp = response;
	struct lte_lc_cell *cells = NULL;

	__ASSERT_NO_MSG(response != NULL);
	//__ASSERT_NO_MSG(gci_count != 0); TODO

	LOG_DBG("%%NCELLMEAS GCI notification parsing starts");

	cells = k_calloc(10 /*ncellmeas_params.gci_count*/, sizeof(struct lte_lc_cell));
	if (cells == NULL) {
		LOG_ERR("Failed to allocate memory for the GCI cells");
		return;
	}

	nrfcloud_cell_data.gci_cells = cells;
	err = parse_ncellmeas_gci(resp, &nrfcloud_cell_data);
	LOG_DBG("parse_ncellmeas_gci returned %d", err);
	switch (err) {
	case -E2BIG:
		LOG_WRN("Not all neighbor cells could be parsed. "
			"More cells than the configured max count of %d were found",
			CONFIG_LTE_NEIGHBOR_CELLS_MAX);
		/* Fall through */
	case 0: /* Fall through */
	case 1:
		LOG_DBG("Neighbor cell count: %d, GCI cells count: %d", nrfcloud_cell_data.ncells_count,
			nrfcloud_cell_data.gci_cells_count);
		nrfcloud_ncellmeas_done = true;
		break;
	default:
		LOG_ERR("Parsing of neighbor cells failed, err: %d", err);
		break;
	}

	//TODO leaking memory
	//k_free(cells);
	//k_free(nrfcloud_cell_data.neighbor_cells);
}

static void at_handler_ncellmeas(const char *response)
{
	int err;

	__ASSERT_NO_MSG(response != NULL);
	nrfcloud_ncellmeas_done = false;

	if (search_type > LTE_LC_NEIGHBOR_SEARCH_TYPE_EXTENDED_COMPLETE) {
		at_handler_ncellmeas_gci(response);
		return;
	}

	int ncell_count = neighborcell_count_get(response);
	struct lte_lc_ncell *neighbor_cells = NULL;

	LOG_DBG("%%NCELLMEAS notification: neighbor cell count: %d", ncell_count);

	if (ncell_count != 0) {
		neighbor_cells = k_calloc(ncell_count, sizeof(struct lte_lc_ncell));
		if (neighbor_cells == NULL) {
			LOG_ERR("Failed to allocate memory for neighbor cells");
			return;
		}
	}

	nrfcloud_cell_data.neighbor_cells = neighbor_cells;

	err = parse_ncellmeas(response, &nrfcloud_cell_data);

	switch (err) {
	case -E2BIG:
		LOG_WRN("Not all neighbor cells could be parsed");
		LOG_WRN("More cells than the configured max count of %d were found",
			CONFIG_LTE_NEIGHBOR_CELLS_MAX);
		/* Fall through */
	case 0: /* Fall through */
	case 1:
		LOG_WRN("Neighbor cells parsed successfully");
		nrfcloud_ncellmeas_done = true;
		break;
	default:
		LOG_ERR("Parsing of neighbor cells failed, err: %d", err);
		break;
	}

	if (neighbor_cells) {
		k_free(neighbor_cells);
	}
}
#endif /* CONFIG_SM_NRF_CLOUD_LOCATION */

static int do_cloud_send_msg(const char *message, int len)
{
	int err;
	const char *resource = "msg/d2c";

	err = nrf_cloud_coap_post(resource, NULL, message, len, COAP_CONTENT_FORMAT_APP_JSON, true,
				  NULL, NULL);
	if (err) {
		LOG_ERR("nrf_cloud_coap_post JSON message send failed, error: %d", err);
	}

	return err;
}

static void on_cloud_ready(void)
{
	sm_nrf_cloud_ready = true;
	urc_send_to(nrfcloud_pipe, "\r\n#XNRFCLOUD: %d,%d\r\n", sm_nrf_cloud_ready,
		    sm_nrf_cloud_send_location);
#if defined(CONFIG_SM_NRF_CLOUD_LOCATION)
	at_monitor_resume(&ncell_meas);
#endif
}

static void on_cloud_disconnected(void)
{
	sm_nrf_cloud_ready = false;
	urc_send_to(nrfcloud_pipe, "\r\n#XNRFCLOUD: %d,%d\r\n", sm_nrf_cloud_ready,
		    sm_nrf_cloud_send_location);
#if defined(CONFIG_SM_NRF_CLOUD_LOCATION)
	at_monitor_pause(&ncell_meas);
#endif
}

static void date_time_event_handler(const struct date_time_evt *evt)
{
	switch (evt->type) {
	case DATE_TIME_OBTAINED_MODEM:
	case DATE_TIME_OBTAINED_NTP:
	case DATE_TIME_OBTAINED_EXT:
		LOG_DBG("DATE_TIME OBTAINED");
		k_sem_give(&sem_date_time);
		break;
	case DATE_TIME_NOT_OBTAINED:
		LOG_INF("DATE_TIME_NOT_OBTAINED");
		break;
	default:
		break;
	}
}

static int nrf_cloud_datamode_callback(uint8_t op, const uint8_t *data, int len, uint8_t flags)
{
	int ret = 0;

	if (op == DATAMODE_SEND) {
		if ((flags & SM_DATAMODE_FLAGS_MORE_DATA) != 0) {
			LOG_ERR("Data mode buffer overflow");
			exit_datamode_handler(sm_at_host_get_current(), -EOVERFLOW);
			return -EOVERFLOW;
		}
		ret = do_cloud_send_msg(data, len);
		if (ret < 0) {
			LOG_ERR("Send failed: %d", ret);
			return ret;
		}
		/* Return the amount of data sent. */
		return len;

	} else if (op == DATAMODE_EXIT) {
		LOG_DBG("Data mode exit");
	}

	return 0;
}

static void conn_wk(struct k_work *work)
{
	int err;

	if (nrfcloud_connect) {
		LOG_DBG("Connecting to nRF Cloud.");
		err = nrf_cloud_coap_connect(NULL);
		if (err) {
			LOG_ERR("Cloud connection failed, error: %d", err);
			urc_send_to(nrfcloud_pipe, "\r\n#XNRFCLOUD: %d,%d\r\n", 0,
				    nrfcloud_conn_send_location);
			return;
		}
		sm_nrf_cloud_send_location = nrfcloud_conn_send_location;
		/* A-GNSS & P-GPS needs date_time, trigger to update current time */
		date_time_update_async(date_time_event_handler);
		if (k_sem_take(&sem_date_time, K_SECONDS(10)) != 0) {
			LOG_WRN("Failed to get current time");
		}
		on_cloud_ready();
	} else {
		LOG_DBG("Disconnecting from nRF Cloud.");
		err = nrf_cloud_coap_disconnect();
		if (err) {
			LOG_ERR("Cloud disconnection failed, error: %d", err);
			urc_send_to(nrfcloud_pipe, "\r\n#XNRFCLOUD: %d,%d\r\n", 1,
				    sm_nrf_cloud_send_location);
			return;
		}
		on_cloud_disconnected();
	}
}

SM_AT_CMD_CUSTOM(xnrfcloud, "AT#XNRFCLOUD", handle_at_nrf_cloud);
static int handle_at_nrf_cloud(enum at_parser_cmd_type cmd_type, struct at_parser *parser,
			       uint32_t param_count)
{
	enum sm_nrfcloud_operation {
		SM_NRF_CLOUD_DISCONNECT,
		SM_NRF_CLOUD_CONNECT,
		SM_NRF_CLOUD_SEND,
	};
	int err = -EINVAL;
	uint16_t op;
	uint16_t send_location = 0;

	switch (cmd_type) {
	case AT_PARSER_CMD_TYPE_SET:
		nrfcloud_pipe = sm_at_host_get_current_pipe();
		err = at_parser_num_get(parser, 1, &op);
		if (err < 0) {
			return err;
		}
		if (op == SM_NRF_CLOUD_CONNECT && !sm_nrf_cloud_ready) {
			if (param_count > 2) {
				err = at_parser_num_get(parser, 2, &send_location);
				if (send_location != 0 && send_location != 1) {
					err = -EINVAL;
				}
				if (err < 0) {
					return err;
				}
			}

			nrfcloud_connect = true;
			nrfcloud_conn_send_location = send_location;
			k_work_submit_to_queue(&sm_work_q, &nrfcloud_conn_work);
			err = 0;
		} else if (op == SM_NRF_CLOUD_SEND && sm_nrf_cloud_ready) {
			/* enter data mode */
			err = enter_datamode(nrf_cloud_datamode_callback, 0);
		} else if (op == SM_NRF_CLOUD_DISCONNECT && sm_nrf_cloud_ready) {
			nrfcloud_connect = false;
			k_work_submit_to_queue(&sm_work_q, &nrfcloud_conn_work);
			err = 0;
		} else {
			err = -EINVAL;
		} break;

	case AT_PARSER_CMD_TYPE_READ: {
		rsp_send("\r\n#XNRFCLOUD: %d,%d,%d,\"%s\"\r\n", sm_nrf_cloud_ready,
			sm_nrf_cloud_send_location, CONFIG_NRF_CLOUD_SEC_TAG, nrfcloud_device_id);
		err = 0;
	} break;

	case AT_PARSER_CMD_TYPE_TEST:
		rsp_send("\r\n#XNRFCLOUD: (%d,%d,%d),<send_location>\r\n",
			SM_NRF_CLOUD_DISCONNECT, SM_NRF_CLOUD_CONNECT, SM_NRF_CLOUD_SEND);
		err = 0;
		break;

	default:
		break;
	}

	return err;
}

#if defined(CONFIG_SM_NRF_CLOUD_LOCATION)

SM_AT_CMD_CUSTOM(xnrfcloudpos, "AT#XNRFCLOUDPOS", handle_at_nrf_cloud_pos);
static int handle_at_nrf_cloud_pos(enum at_parser_cmd_type cmd_type,
				   struct at_parser *parser, uint32_t param_count)
{
	int err;
	uint16_t cell_pos, wifi_pos;

	if (cmd_type != AT_PARSER_CMD_TYPE_SET) {
		return -ENOTSUP;
	}

	if (!sm_nrf_cloud_ready) {
		LOG_ERR("Not connected to nRF Cloud.");
		return -ENOTCONN;
	}

	if (nrfcloud_sending_loc_req) {
		/* Avoid potential concurrency issues writing to global variables. */
		LOG_ERR("nRF Cloud location request sending already ongoing.");
		return -EBUSY;
	}

	if (param_count < WIFI_APS_BEGIN_IDX) {
		return -EINVAL;
	}

	err = at_parser_num_get(parser, 1, &cell_pos);
	if (err) {
		return err;
	}

	err = at_parser_num_get(parser, 2, &wifi_pos);
	if (err) {
		return err;
	}

	if (cell_pos >= CELLPOS_COUNT || wifi_pos > 1) {
		return -EINVAL;
	}

	if (!cell_pos && !wifi_pos) {
		LOG_ERR("At least one of cellular/Wi-Fi information must be included.");
		return -EINVAL;
	}

	if (cell_pos == CELLPOS_MULTI_CELL && !nrfcloud_ncellmeas_done) {
		LOG_ERR("%s", "No neighboring cell measurement. Did you run `AT%NCELLMEAS`?");
		return -EAGAIN;
	}

	if (!wifi_pos && param_count > WIFI_APS_BEGIN_IDX) {
		/* No Wi-Fi AP allowed if no Wi-Fi positioning. */
		return -E2BIG;
	}

	if (wifi_pos) {
		nrfcloud_wifi_data.ap_info = k_malloc(
			sizeof(*nrfcloud_wifi_data.ap_info) * (param_count - WIFI_APS_BEGIN_IDX));
		if (!nrfcloud_wifi_data.ap_info) {
			return -ENOMEM;
		}
		/* Parse the AP parameters. */
		nrfcloud_wifi_data.cnt = 0;
		for (unsigned int param_idx = WIFI_APS_BEGIN_IDX; param_idx < param_count;
		++param_idx) {
			struct wifi_scan_result *const ap =
				&nrfcloud_wifi_data.ap_info[nrfcloud_wifi_data.cnt];
			char mac_addr_str[WIFI_MAC_ADDR_STR_LEN + 1];
			unsigned int mac_addr[WIFI_MAC_ADDR_LEN];
			int32_t rssi = NRF_CLOUD_LOCATION_WIFI_OMIT_RSSI;
			size_t len;

			++nrfcloud_wifi_data.cnt;

			/* Parse the MAC address. */
			len = sizeof(mac_addr_str);
			err = util_string_get(parser, param_idx, mac_addr_str, &len);
			if (!err && (len != strlen(mac_addr_str)
				|| sscanf(mac_addr_str, WIFI_MAC_ADDR_TEMPLATE,
						&mac_addr[0], &mac_addr[1], &mac_addr[2],
						&mac_addr[3], &mac_addr[4], &mac_addr[5])
					!= WIFI_MAC_ADDR_LEN)) {
				err = -EBADMSG; /* A different error code to differentiate. */
			}
			if (err) {
				LOG_ERR("MAC address %u malformed (%d).",
					nrfcloud_wifi_data.cnt, err);
				break;
			}
			for (unsigned int i = 0; i != WIFI_MAC_ADDR_LEN; ++i) {
				ap->mac[i] = mac_addr[i];
			}
			ap->mac_length = WIFI_MAC_ADDR_LEN;

			/* Parse the RSSI, if present. */
			if (!at_parser_num_get(parser, param_idx + 1, &rssi)) {
				++param_idx;
				const int rssi_min = -128;
				const int rssi_max = 0;

				if (rssi < rssi_min || rssi > rssi_max) {
					err = -EINVAL;
					LOG_ERR("RSSI %u out of bounds ([%d,%d]).",
						nrfcloud_wifi_data.cnt, rssi_min, rssi_max);
					break;
				}
			}
			ap->rssi = rssi;

			ap->band     = 0;
			ap->security = WIFI_SECURITY_TYPE_UNKNOWN;
			ap->mfp      = WIFI_MFP_UNKNOWN;
			/* CONFIG_NRF_CLOUD_WIFI_LOCATION_ENCODE_OPT excludes the other members. */
		}

		if (nrfcloud_wifi_data.cnt < NRF_CLOUD_LOCATION_WIFI_AP_CNT_MIN) {
			err = -EINVAL;
			LOG_ERR("Insufficient access point count (got %u, min %u).",
				nrfcloud_wifi_data.cnt, NRF_CLOUD_LOCATION_WIFI_AP_CNT_MIN);
		}
		if (err) {
			k_free(nrfcloud_wifi_data.ap_info);
			return err;
		}
	}

	nrfcloud_cell_pos = cell_pos;
	nrfcloud_wifi_pos = wifi_pos;

	nrfcloud_sending_loc_req = true;
	k_work_submit_to_queue(&sm_work_q, &nrfcloud_loc_req);
	return 0;
}

#endif /* CONFIG_SM_NRF_CLOUD_LOCATION */

static void sm_at_nrfcloud_init(int ret, void *ctx)
{
	static bool initialized;
	int err;

	if (initialized) {
		return;
	}
	initialized = true;

	err = nrf_cloud_coap_init();
	if (err) {
		LOG_ERR("Failed to initialize nRF Cloud CoAP library: %d", err);
		return;
	}

	k_work_init(&nrfcloud_conn_work, conn_wk);
#if defined(CONFIG_SM_NRF_CLOUD_LOCATION)
	k_work_init(&nrfcloud_loc_req, loc_req_wk);
#endif
	nrf_cloud_client_id_get(nrfcloud_device_id, sizeof(nrfcloud_device_id));
}
NRF_MODEM_LIB_ON_INIT(sm_nrfcloud_init_hook, sm_at_nrfcloud_init, NULL);

#endif /* CONFIG_SM_NRF_CLOUD */
