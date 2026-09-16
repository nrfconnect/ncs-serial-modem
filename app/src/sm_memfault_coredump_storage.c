/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * Custom Memfault coredump storage backend, adapted from
 * nrf/modules/memfault-firmware-sdk/memfault_flash_coredump_storage.c to store coredumps
 * in slot1_s_partition instead of a dedicated partition, since there isn't enough flash
 * budget left for both. Despite the name, slot1_s_partition isn't TrustZone-protected; it's
 * just the region of slot1_partition where the secure (TF-M) image would otherwise live. It
 * is only 32 KiB, which makes erasing it (see prv_op_within_flash_bounds() users below) much
 * faster than erasing the whole slot.
 *
 * PROTOTYPE: slot1_partition (both its S and NS parts) also holds the previous image until
 * MCUboot's revert window closes (see mcuboot_swap_type()), or a newly-scheduled candidate
 * update. TF-M is always updated together with the application as a single combined MCUboot
 * image, so the swap state of image 0 covers slot1_s_partition too - no separate check is
 * needed for it. get_info() checks this, cached for a short window (see
 * prv_slot1_available()): mcuboot_swap_type() only reads both slot trailers
 * (flash_area_open()/flash_area_read(), see soc_flash_nrf.c's flash_nrf_read() and
 * flash_map.c's flash_area_open() - neither takes the flash write/erase lock), so it is safe
 * to call without going through Zephyr's RTOS primitives; the cache exists because the SDK's
 * own buffered-write helper calls get_info() again for every write chunk, and hundreds of
 * chunks/log lines per save is slow enough to matter in the fault handler. A crash while a
 * swap is pending simply loses the coredump (get_info() reports zero space, so the Memfault
 * SDK skips saving it) rather than risking corruption of slot1's contents.
 */

#include <zephyr/kernel.h>
#include <zephyr/dfu/mcuboot.h>
#include <nrfx_nvmc.h>
#include <zephyr/storage/flash_map.h>

#include <memfault/components.h>
#include <memfault/ports/buffered_coredump_storage.h>

#define MFLT_STORAGE_NODE DT_CHOSEN(nordic_memfault_coredump_partition)
BUILD_ASSERT(DT_NODE_EXISTS(MFLT_STORAGE_NODE),
	     "nordic,memfault-coredump-partition chosen property not set.");
#define MFLT_STORAGE_OFFSET PARTITION_NODE_ADDRESS(MFLT_STORAGE_NODE)
#define MFLT_STORAGE_SIZE   PARTITION_NODE_SIZE(MFLT_STORAGE_NODE)
#define MFLT_STORAGE_FA_ID  DT_PARTITION_ID(MFLT_STORAGE_NODE)


/* Note: While the system is running, flash writes for the nRF (soc_flash_nrf.c)
 *	 may be asynchronous so we use a static to track when a coredump clear
 *	 request has been issued.
 */
static bool last_coredump_cleared;

/* The SDK's own buffered-write helper (ports/include/memfault/ports/buffered_coredump_storage.h)
 * calls get_info() again for every single write chunk, on top of our own bounds checks below:
 * a whole coredump save would otherwise mean hundreds of mcuboot_swap_type() calls, each doing
 * 2 flash-trailer reads plus a BOOT_LOG_INF() - slow enough to matter in the fault handler.
 * Nothing can change the trailers mid-save (nothing else runs while we're in here), so caching
 * the result for a short window is safe and collapses that back down to about one real check.
 */
#define SWAP_CHECK_CACHE_MS 500

static bool prv_slot1_available(void)
{
	static int64_t last_check_uptime = -(SWAP_CHECK_CACHE_MS + 1);
	static bool cached_available;
	int64_t now = k_uptime_get();

	if ((now - last_check_uptime) >= SWAP_CHECK_CACHE_MS) {
		cached_available = (mcuboot_swap_type() == BOOT_SWAP_TYPE_NONE);
		last_check_uptime = now;
	}

	return cached_available;
}

void memfault_platform_coredump_storage_get_info(sMfltCoredumpStorageInfo *info)
{
	*info = (sMfltCoredumpStorageInfo){
		.size = prv_slot1_available() ? MFLT_STORAGE_SIZE : 0,
	};
}

/* Gates writes/erases: reuses slot1_partition only while nothing else needs it. */
static bool prv_op_within_flash_bounds(uint32_t offset, size_t data_len)
{
	sMfltCoredumpStorageInfo info = { 0 };

	memfault_platform_coredump_storage_get_info(&info);
	return (offset + data_len) <= info.size;
}

/* Note: This is _only_ called when the system has crashed and a coredump is
 *	 being saved. memfault_coredump_read() is called when the data is being
 *	 sent to the cloud for processing.
 */
bool memfault_platform_coredump_storage_read(uint32_t offset, void *data, size_t read_len)
{
	if (!prv_op_within_flash_bounds(offset, read_len)) {
		return false;
	}

	/* Note: internal flash is memory mapped so we can just memcpy it out */
	const uint32_t address = MFLT_STORAGE_OFFSET + offset;

	memcpy(data, (void *)address, read_len);
	return true;
}

/* Note: This is _only_ called when the system has crashed and a coredump is
 *	 being saved.
 */
bool memfault_platform_coredump_storage_erase(uint32_t offset, size_t erase_size)
{
	uint32_t page_size = nrfx_nvmc_flash_page_size_get();

	if (!prv_op_within_flash_bounds(offset, erase_size)) {
		return false;
	}

	if ((offset % page_size) != 0) {
		return false;
	}

	for (size_t page = offset; page < erase_size; page += page_size) {
		const uint32_t address = MFLT_STORAGE_OFFSET + page;

		nrfx_nvmc_page_erase(address);
	}

	return true;
}

/* Note: This is _only_ called when the system has crashed and a coredump is
 *	 being saved.
 */
bool memfault_platform_coredump_storage_buffered_write(sCoredumpWorkingBuffer *blk)
{
	const uint32_t addr = MFLT_STORAGE_OFFSET + blk->write_offset;

	if (!prv_op_within_flash_bounds(blk->write_offset, MEMFAULT_COREDUMP_STORAGE_WRITE_SIZE)) {
		return false;
	}

	MEMFAULT_STATIC_ASSERT(MEMFAULT_COREDUMP_STORAGE_WRITE_SIZE % sizeof(uint32_t) == 0,
			       "Write buffer must be word aligned");
	nrfx_nvmc_words_write(addr, &blk->data[0],
			      MEMFAULT_COREDUMP_STORAGE_WRITE_SIZE / sizeof(uint32_t));
	return true;
}

bool memfault_coredump_read(uint32_t offset, void *data, size_t read_len)
{
	if (last_coredump_cleared) {
		/* we've already read the coredump, return nothing */
		memset(data, 0x0, read_len);
		return true;
	}

	return memfault_platform_coredump_storage_read(offset, data, read_len);
}

/* Note: This is called after a coredump has been successfully sent to the
 *	 cloud for processing while the system is in normal operation mode.
 */
void memfault_platform_coredump_storage_clear(void)
{
	uint32_t empty_word = 0x0;
	const struct flash_area *flash_area;
	int err;

	err = flash_area_open(MFLT_STORAGE_FA_ID, &flash_area);
	if (err) {
		MEMFAULT_LOG_ERROR("Unable to open coredump storage: 0x%x", err);
		return;
	}

	err = flash_area_write(flash_area, 0x0, &empty_word, sizeof(empty_word));
	if (err) {
		MEMFAULT_LOG_ERROR("Unable to clear storage: 0x%x", err);
		return;
	}

	last_coredump_cleared = true;
}
