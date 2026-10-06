/* SPDX-License-Identifier: Apache-2.0 */

#include "uwb_radio.h"
#include "uwb_round_guard.h"
#include "uwb_cycle_policy.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/drivers/hwinfo.h>
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#if defined(RECEPTEC_DW3110_RADIO)
#include <receptec/dw3110_port.h>
#define RECEPTEC_RADIO_NAME "DW3110"
#define RECEPTEC_RADIO_DEVICE_ID 0xDECA0302U
#define receptec_radio_irq_clear dw3110_port_irq_clear
#define receptec_radio_irq_enable dw3110_port_irq_enable
#define receptec_radio_wait_event dw3110_port_wait_irq
#else
#include "dw3720/dw3720_deca_regs.h"
#include "dw3720_spi.h"
#define RECEPTEC_RADIO_NAME "QM33120W"
#define RECEPTEC_RADIO_DEVICE_ID DW3720_DEV_ID_QM33120W
#define receptec_radio_irq_clear dw3720_irq_clear
#define receptec_radio_irq_enable dw3720_irq_enable
#define receptec_radio_wait_event dw3720_wait_event
#endif
#include "uwb_peer_registry.h"
#include "uwb_peer_storage.h"
#if defined(CONFIG_BT)
#include "ble_range.h"
#endif
#include <receptec/uwb_protocol.h>
#include <receptec/uwb_test_frames.h>
#include <receptec/uwb_test_payload.h>
#include <receptec/uwb_test_result.h>

#include "deca_device_api.h"
#include "deca_interface.h"
#include "deca_private.h"

/*
 * DW3110 antenna delay, calibrated 2026-10-02 against two known M-A
 * distances (receptec-qorvo <-> fake-coletec-0, both DW3110): 0.54m and
 * 1.5m. Previous value (16385) was Qorvo's generic, uncalibrated default,
 * shared uncritically across every radio in this project regardless of
 * chip generation (also used as-is for the DW3720 tag, which this
 * calibration does not touch -- no independent ground-truth distance was
 * available for M-T/A-T, see atletec-1t2.8). Measured bias before this fix
 * was consistently short: M-A read ~520-580mm at 0.54m true and
 * ~1540-1620mm at 1.5m true. The two points disagree by more than their
 * own sampling noise (implied offset +41mm at 0.54m vs +71mm at 1.5m),
 * which a 2-point fit can't distinguish from a real inter-radio clock-rate
 * mismatch vs. imprecision in the 1.5m reference itself (not independently
 * re-measured). Pooling both under the assumption of no clock-rate error
 * (physically the sane default for DW3xxx-class crystals; a true rate
 * mismatch of the magnitude a naive 2-point slope fit implied would be
 * ~100x too large to be real) gives a weighted +58mm total offset, i.e.
 * ~6.2 ticks per radio. See atletec-vc6 for redoing this with a wider,
 * better-separated baseline (>=3m) once bench space allows, which would
 * let a 3-point fit actually test for a real clock-rate term instead of
 * assuming it away.
 */
#define UWB_TX_ANTENNA_DELAY 16391U
#define UWB_RX_ANTENNA_DELAY 16391U
#define UWB_RESPONSE_TIMEOUT_US 100000U
#define UWB_RADIO_EVENT_MASK (DWT_INT_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_ERR | SYS_STATUS_ALL_RX_TO)
#define UWB_RX_REARM_ERROR_MASK (SYS_STATUS_ALL_RX_ERR & ~SYS_STATUS_ALL_RX_TO)

/* Only the radio-owning thread uses this absolute transaction deadline. */
static int64_t cycle_deadline;
static int64_t bounded_deadline(int64_t requested)
{
	return cycle_deadline && cycle_deadline < requested ? cycle_deadline : requested;
}
static struct receptec_uwb_cycle_config cycle_config;
static bool cycle_config_valid, cycle_running;
static uint64_t cycle_next_round, cycle_round_limit;
static uint32_t cycle_success[3], cycle_failure[3], cycle_max_ms[3], cycle_overruns;
static int cycle_last_error[3];
static uint8_t cycle_last_phase[3];

static uint8_t receptec_eui[RECEPTEC_UWB_EUI64_SIZE];

#if !defined(RECEPTEC_DW3110_RADIO)
static const struct dwt_spi_s dw3720_spi_functions = {
    .readfromspi = dw3720_readfromspi,
    .writetospi = dw3720_writetospi,
    .writetospiwithcrc = dw3720_writetospiwithcrc,
    .setslowrate = dw3720_setslowrate,
    .setfastrate = dw3720_setfastrate,
};
#endif

extern const struct dwt_driver_s dw3000_driver;
extern const struct dwt_driver_s dw3720_driver;

static const struct dwt_driver_s *const driver_list[] = {
    &dw3000_driver,
#if !defined(RECEPTEC_DW3110_RADIO)
    &dw3720_driver,
#endif
};

static struct dwt_probe_s probe_interface = {
    .dw = NULL,
    .spi = NULL,
#if defined(RECEPTEC_DW3110_RADIO)
    .wakeup_device_with_io = dw3110_port_wakeup,
#else
    .wakeup_device_with_io = wakeup_device_with_io,
#endif
    .driver_list = (struct dwt_driver_s **)driver_list,
    .dw_driver_num = ARRAY_SIZE(driver_list),
};

static dwt_config_t radio_config = {
    .chan = 9U,
    .txPreambLength = DWT_PLEN_128,
    .rxPAC = DWT_PAC8,
    .txCode = 9U,
    .rxCode = 9U,
    .sfdType = DWT_SFD_DW_8,
    .dataRate = DWT_BR_6M8,
    .phrMode = DWT_PHRMODE_STD,
    .phrRate = DWT_PHRRATE_STD,
    .sfdTO = 129U,
    .stsMode = DWT_STS_MODE_OFF,
    .stsLength = DWT_STS_LEN_64,
    .pdoaMode = DWT_PDOA_M0,
};

static dwt_txconfig_t tx_config = {
    .PGdly = 0x34U,
    .power = 0xfdfdfdfdUL,
    .PGcount = 0U,
};

static bool radio_ready;
static bool poll_running;
static bool peer_registry_ready;
/* Persisted per-peer actions; zero is SYNC so unset peers have a safe default.
 */
static enum receptec_uwb_peer_action peer_actions[RECEPTEC_UWB_MAX_PEERS];
/* Registration is retained when disabled; the flag only gates radio
 * participation. */
static bool peer_enabled[RECEPTEC_UWB_MAX_PEERS];
/* Human-readable labels; retained alongside the EUI, never sent on-air. */
static char peer_nicknames[RECEPTEC_UWB_MAX_PEERS][RECEPTEC_UWB_PEER_NICKNAME_MAX_LEN];
static bool peer_nickname_set[RECEPTEC_UWB_MAX_PEERS];
/* Background range auto-responder; restored from settings at boot. */
static bool range_auto_enabled;
/* Boot-time round for a power-up-and-go master; empty unless configured. */
static struct receptec_uwb_range_autostart range_autostart;
static bool range_autostart_set;
static uint32_t schedule_period_ms = 1000U;
static struct receptec_uwb_peer_registry peer_registry;
static uint8_t tx_bytes[RECEPTEC_UWB_MAX_LOGICAL_SIZE];
static uint8_t rx_bytes[RECEPTEC_UWB_MAX_LOGICAL_SIZE];

K_MUTEX_DEFINE(uwb_peer_mutex);

static uint32_t us_to_rx_timeout(uint32_t us)
{
	return (uint32_t)(((uint64_t)us * 39ULL + 39ULL) / 40ULL);
}

static void clear_radio_events(uint32_t events)
{
	dwt_writesysstatuslo(events);
	receptec_radio_irq_clear();
}

static int receptec_radio_port_init(void)
{
#if defined(RECEPTEC_DW3110_RADIO)
	return dw3110_port_init();
#else
	return dw3720_spi_init();
#endif
}

static int receptec_radio_port_reset(void)
{
#if defined(RECEPTEC_DW3110_RADIO)
	return dw3110_port_reset();
#else
	return dw3720_hw_reset();
#endif
}

static void receptec_radio_set_slow_rate(void)
{
#if defined(RECEPTEC_DW3110_RADIO)
	dw3110_port_spi()->setslowrate();
#else
	dw3720_setslowrate();
#endif
}

static int initialize_qorvo_driver(void)
{
	uint32_t poll;
	uint32_t device_id;
	int ret;

	ret = receptec_radio_port_init();
	if (ret < 0) {
		return ret;
	}
	receptec_radio_set_slow_rate();
	ret = receptec_radio_port_reset();
	if (ret < 0) {
		return ret;
	}
	k_msleep(2);
	probe_interface.spi = (void *)
#if defined(RECEPTEC_DW3110_RADIO)
		dw3110_port_spi();
#else
		&dw3720_spi_functions;
#endif

	if (dwt_probe(&probe_interface) != DWT_SUCCESS) {
		printk("%s: Qorvo driver probe failed\n", RECEPTEC_RADIO_NAME);
		return -ENODEV;
	}
	for (poll = 0U; poll < 1000U; ++poll) {
		if (dwt_checkidlerc() != 0U) {
			break;
		}
		k_busy_wait(10U);
	}
	if (poll == 1000U) {
		printk("%s: IDLE_RC timeout\n", RECEPTEC_RADIO_NAME);
		return -ETIMEDOUT;
	}
	if (dwt_initialise(DWT_READ_OTP_ALL) != DWT_SUCCESS) {
		printk("%s: OTP/calibration initialization failed\n", RECEPTEC_RADIO_NAME);
		return -EIO;
	}

	device_id = dwt_readdevid();
	printk("%s: driver=%s device-id=0x%08X part-id=0x%08X\n",
	       RECEPTEC_RADIO_NAME,
	       dwt_version_string(), device_id, dwt_getpartid());
	if (device_id != RECEPTEC_RADIO_DEVICE_ID ||
	    dwt_check_dev_id() != DWT_SUCCESS) {
		return -ENODEV;
	}
	if (dwt_configure(&radio_config) != DWT_SUCCESS) {
		printk("%s: PHY configuration failed\n", RECEPTEC_RADIO_NAME);
		return -EIO;
	}
#if !defined(RECEPTEC_DW3110_RADIO)
	/*
	 * Hand QM33120W GPIO5 to the receive front-end. It powers up as a plain
	 * high-impedance GPIO; on this carrier GPIO5 feeds the QM14068 VCTRL
	 * through a 10k/12.1k divider (Projeto Receptor, sheet 4, net R_VCTRL), so
	 * left alone the LNAs stay off. The COLETEC had the same defect: enabling
	 * it measured +13.8 dB (nrf52-bringup-fw docs/bench.md).
	 */
	dwt_setlnapamode(DWT_LNA_ENABLE);
	{
		uint32_t gpio_mode = dwt_read_reg(GPIO_MODE_ID);

		printk("QM33120W: external LNA %s (GPIO_MODE=0x%08X)\n",
		       (gpio_mode & GPIO_MODE_MSGP5_MODE_BIT_MASK) != 0U ?
			       "enabled, GPIO5=PDoA_SW1" : "NOT enabled",
		       gpio_mode);
	}
#endif
	dwt_configuretxrf(&tx_config);
	dwt_settxantennadelay(UWB_TX_ANTENNA_DELAY);
	dwt_setrxantennadelay(UWB_RX_ANTENNA_DELAY);
	dwt_setinterrupt(UWB_RADIO_EVENT_MASK, 0U, DWT_ENABLE_INT_ONLY);
	ret = receptec_radio_irq_enable(false);
	if (ret < 0) {
		return ret;
	}
	dwt_forcetrxoff();
	clear_radio_events(UWB_RADIO_EVENT_MASK | DWT_INT_TXFRS_BIT_MASK);

	printk("%s PHY ready: Ch9 6.8M PRF64 preamble=128 code=9 STS=off\n",
	       RECEPTEC_RADIO_NAME);
	return 0;
}

int receptec_uwb_radio_init(void)
{
	uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE];
	uint8_t stored_action;
	bool stored_enabled;
	uint32_t stored_period_ms;
	ssize_t hwinfo_len;
	int ret;

	ret = receptec_uwb_peer_storage_init();
	if (ret < 0) {
		printk("Peer storage init failed: %d\n", ret);
		return ret;
	}
	ret = receptec_uwb_schedule_period_load(&stored_period_ms);
	if (ret == 0 && stored_period_ms >= 1U && stored_period_ms <= 60000U) {
		schedule_period_ms = stored_period_ms;
	} else if (ret == -ENOENT || ret == 0) {
		printk("UWB schedule period invalid/unavailable (%d); using 1000 ms\n",
		       ret == 0 ? -EINVAL : ret);
		schedule_period_ms = 1000U;
		ret = receptec_uwb_schedule_period_save(schedule_period_ms);
		if (ret < 0) {
			printk("Schedule: could not persist default period: %d\n", ret);
		}
	} else {
		printk("UWB schedule period load failed (%d); using 1000 ms\n", ret);
		schedule_period_ms = 1000U;
	}
	ret = receptec_uwb_identity_storage_load(receptec_eui);
	if (ret == -ENOENT) {
		hwinfo_len = hwinfo_get_device_id(receptec_eui, sizeof(receptec_eui));
		if (hwinfo_len != sizeof(receptec_eui)) {
			printk("Cannot derive local UWB identity from hardware ID (%d)\n",
			       (int)hwinfo_len);
			return hwinfo_len < 0 ? (int)hwinfo_len : -EIO;
		}
	} else if (ret < 0) {
		printk("Local UWB identity load failed: %d\n", ret);
		return ret;
	}

	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);

	ret = receptec_uwb_peer_registry_init(&peer_registry, receptec_eui);

	if (ret < 0) {
		k_mutex_unlock(&uwb_peer_mutex);
		return ret;
	}
	printk("UWB local EUI: %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\n", receptec_eui[0],
	       receptec_eui[1], receptec_eui[2], receptec_eui[3], receptec_eui[4], receptec_eui[5],
	       receptec_eui[6], receptec_eui[7]);

	for (uint8_t slot = 0U; slot < RECEPTEC_UWB_MAX_PEERS; ++slot) {

		ret = receptec_uwb_peer_storage_load(slot, eui64);

		if (ret == -ENOENT) {
			continue;
		}

		if (ret < 0) {
			printk("Peer storage: slot %u load failed: %d\n", slot, ret);
			continue;
		}

		ret = receptec_uwb_peer_registry_register(&peer_registry, slot, eui64);

		if (ret < 0) {
			printk("Peer storage: slot %u rejected: %d\n", slot, ret);
			continue;
		}

		printk("Peer storage: restored slot %u\n", slot);
		ret = receptec_uwb_peer_enabled_load(slot, &stored_enabled);
		if (ret == -ENOENT) {
			/* Existing registrations predate this flag and remain enabled. */
			stored_enabled = true;
			ret = receptec_uwb_peer_enabled_save(slot, stored_enabled);
		} else if (ret < 0) {
			printk("Peer enabled state invalid for slot %u (%d); enabling safely\n",
			       slot, ret);
			stored_enabled = true;
			ret = receptec_uwb_peer_enabled_save(slot, stored_enabled);
		}
		if (ret < 0) {
			printk("Peer enabled state could not be persisted for slot %u: %d\n", slot,
			       ret);
		}
		peer_enabled[slot] = stored_enabled;
		ret = receptec_uwb_peer_nickname_load(slot, peer_nicknames[slot],
						      sizeof(peer_nicknames[slot]));
		if (ret == 0) {
			peer_nickname_set[slot] = true;
		} else {
			if (ret != -ENOENT) {
				printk("Peer nickname invalid for slot %u (%d); clearing\n", slot,
				       ret);
				(void)receptec_uwb_peer_nickname_delete(slot);
			}
			peer_nicknames[slot][0] = '\0';
			peer_nickname_set[slot] = false;
		}
		ret = receptec_uwb_schedule_action_load(slot, &stored_action);
		if (ret == 0 && (stored_action == RECEPTEC_UWB_PEER_ACTION_SYNC ||
				 stored_action == RECEPTEC_UWB_PEER_ACTION_DATA_REQ)) {
			peer_actions[slot] = (enum receptec_uwb_peer_action)stored_action;
		} else if (ret == -ENOENT) {
			peer_actions[slot] = RECEPTEC_UWB_PEER_ACTION_SYNC;
			ret =
			    receptec_uwb_schedule_action_save(slot, RECEPTEC_UWB_PEER_ACTION_SYNC);
			if (ret < 0) {
				printk("Schedule: could not persist default SYNC for slot %u: %d\n",
				       slot, ret);
			}
		} else {
			printk("Schedule: invalid action for slot %u (%d); using SYNC\n", slot,
			       ret == 0 ? -EINVAL : ret);
			peer_actions[slot] = RECEPTEC_UWB_PEER_ACTION_SYNC;
			(void)receptec_uwb_schedule_action_save(slot,
								RECEPTEC_UWB_PEER_ACTION_SYNC);
		}
	}

	range_auto_enabled = false;
	if (receptec_uwb_range_auto_load(&range_auto_enabled) == 0 && range_auto_enabled) {
		printk("Range auto-responder restored: on\n");
	}
	int cycle_load_ret = receptec_uwb_cycle_load(&cycle_config);
	cycle_config_valid = cycle_load_ret == 0;
	range_autostart_set = receptec_uwb_range_autostart_load(&range_autostart) == 0;

	if (cycle_load_ret != 0 && cycle_load_ret != -ENOENT) {
		printk("Range cycle configuration invalid: %d; autostart blocked\n", cycle_load_ret);
		range_autostart_set = false;
	}
	peer_registry_ready = true;

	k_mutex_unlock(&uwb_peer_mutex);

	ret = initialize_qorvo_driver();

	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
	radio_ready = (ret == 0);
	k_mutex_unlock(&uwb_peer_mutex);

	if (ret == 0 && cycle_config_valid && cycle_config.autostart) {
		int start_ret = receptec_uwb_cycle_start();
		printk("Range cycle autostart: %d\n", start_ret);
	} else if (ret == 0 && range_autostart_set) {
		ret = receptec_uwb_range_start(range_autostart.slot, range_autostart.period_ms,
					       range_autostart.pair_index);
		if (ret < 0) {
			printk("Range autostart not started (%d); configure peers first\n", ret);
		} else {
			printk("Range autostart: slot %u pair %u every %u ms\n",
			       range_autostart.slot, range_autostart.pair_index,
			       range_autostart.period_ms);
		}
		ret = 0;
	}

	return ret;
}

int receptec_uwb_local_eui_get(uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE])
{
	if (eui64 == NULL) {
		return -EINVAL;
	}
	memcpy(eui64, receptec_eui, sizeof(receptec_eui));
	return 0;
}

int receptec_uwb_local_eui_set(
	const uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE])
{
	bool all_zero = true;
	bool all_ff = true;
	uint8_t peer_eui[RECEPTEC_UWB_EUI64_SIZE];

	if (eui64 == NULL) {
		return -EINVAL;
	}
	for (size_t i = 0U; i < RECEPTEC_UWB_EUI64_SIZE; ++i) {
		all_zero = all_zero && eui64[i] == 0U;
		all_ff = all_ff && eui64[i] == 0xFFU;
	}
	if (all_zero || all_ff) {
		return -EINVAL;
	}
	for (uint8_t slot = 0U; slot < RECEPTEC_UWB_MAX_PEERS; ++slot) {
		if (receptec_uwb_peer_get(slot, peer_eui) == 0 &&
		    memcmp(peer_eui, eui64, sizeof(peer_eui)) == 0) {
			return -EEXIST;
		}
	}
	return receptec_uwb_identity_storage_save(eui64);
}

int receptec_uwb_peer_register(
	uint8_t slot,
	const uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE])
{
	int ret;

	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);

	if (!peer_registry_ready) {
		ret = -EAGAIN;
		goto out;
	}

	/*
	 * Permite alteração em runtime, mas não no meio
	 * de um ciclo de polling.
	 */
	if (poll_running) {
		ret = -EBUSY;
		goto out;
	}

	/*
	 * Primeiro valida e adiciona à lista RAM.
	 */
	ret = receptec_uwb_peer_registry_register(
		&peer_registry,
		slot,
		eui64);

	if (ret < 0) {
		goto out;
	}

	/*
	 * Depois torna o cadastro persistente.
	 */
	ret = receptec_uwb_peer_storage_save(
		slot,
		eui64);

	if (ret < 0) {
		/*
		 * Não deixa RAM e flash divergirem.
		 */
		(void)receptec_uwb_peer_registry_unregister(
			&peer_registry,
			slot);
		goto out;
	}

	/* A new peer starts at SYNC, and its default is durable as well. */
	ret = receptec_uwb_schedule_action_save(
		slot, RECEPTEC_UWB_PEER_ACTION_SYNC);
	if (ret < 0) {
		(void)receptec_uwb_peer_storage_delete(slot);
		(void)receptec_uwb_peer_registry_unregister(&peer_registry, slot);
		goto out;
	}
	peer_actions[slot] = RECEPTEC_UWB_PEER_ACTION_SYNC;
	ret = receptec_uwb_peer_enabled_save(slot, true);
	if (ret < 0) {
		(void)receptec_uwb_schedule_action_delete(slot);
		(void)receptec_uwb_peer_storage_delete(slot);
		(void)receptec_uwb_peer_registry_unregister(&peer_registry, slot);
		goto out;
	}
	peer_enabled[slot] = true;
	/* A fresh registration starts unnamed; drop any stale label. */
	peer_nicknames[slot][0] = '\0';
	peer_nickname_set[slot] = false;
	(void)receptec_uwb_peer_nickname_delete(slot);

out:
	k_mutex_unlock(&uwb_peer_mutex);
	return ret;
}

int receptec_uwb_peer_unregister(uint8_t slot)
{
	uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE];
	enum receptec_uwb_peer_action previous_action;
	bool previous_enabled;
	char previous_nickname[RECEPTEC_UWB_PEER_NICKNAME_MAX_LEN];
	bool previous_nickname_set;
	int ret;

	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);

	if (!peer_registry_ready) {
		ret = -EAGAIN;
		goto out;
	}

	if (poll_running) {
		ret = -EBUSY;
		goto out;
	}

	/*
	 * Confirma que realmente existe peer nesse slot.
	 */
	ret = receptec_uwb_peer_registry_get(
		&peer_registry,
		slot,
		eui64);

	if (ret < 0) {
		goto out;
	}

	/*
	 * Primeiro remove a configuração persistente.
	 * Se a flash falhar, RAM permanece intacta.
	 */
	ret = receptec_uwb_peer_storage_delete(slot);

	if (ret < 0) {
		goto out;
	}
	previous_action = peer_actions[slot];
	previous_enabled = peer_enabled[slot];
	previous_nickname_set = peer_nickname_set[slot];
	if (previous_nickname_set) {
		memcpy(previous_nickname, peer_nicknames[slot],
		       sizeof(previous_nickname));
	}
	ret = receptec_uwb_schedule_action_delete(slot);
	if (ret < 0) {
		(void)receptec_uwb_peer_storage_save(slot, eui64);
		goto out;
	}
	ret = receptec_uwb_peer_enabled_delete(slot);
	if (ret < 0 && ret != -ENOENT) {
		(void)receptec_uwb_peer_storage_save(slot, eui64);
		(void)receptec_uwb_schedule_action_save(slot, previous_action);
		goto out;
	}
	/* Nicknames are per-registration; never leak one onto a future peer. */
	(void)receptec_uwb_peer_nickname_delete(slot);

	ret = receptec_uwb_peer_registry_unregister(
		&peer_registry,
		slot);
	if (ret == 0) {
		peer_actions[slot] = RECEPTEC_UWB_PEER_ACTION_SYNC;
		peer_enabled[slot] = false;
		peer_nicknames[slot][0] = '\0';
		peer_nickname_set[slot] = false;
	} else {
		(void)receptec_uwb_peer_storage_save(slot, eui64);
		(void)receptec_uwb_schedule_action_save(slot, previous_action);
		(void)receptec_uwb_peer_enabled_save(slot, previous_enabled);
		if (previous_nickname_set) {
			(void)receptec_uwb_peer_nickname_save(slot,
							     previous_nickname);
		}
	}

out:
	k_mutex_unlock(&uwb_peer_mutex);
	return ret;
}

int receptec_uwb_peer_get(uint8_t slot,
			  uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE])
{
	int ret;

	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
	ret = peer_registry_ready ?
		receptec_uwb_peer_registry_get(&peer_registry, slot, eui64) : -EAGAIN;
	k_mutex_unlock(&uwb_peer_mutex);
	return ret;
}

int receptec_uwb_peer_enabled_get(uint8_t slot, bool *enabled)
{
	uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE];
	int ret = 0;

	if (enabled == NULL || slot >= RECEPTEC_UWB_MAX_PEERS) {
		return -EINVAL;
	}
	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
	if (!peer_registry_ready) {
		ret = -EAGAIN;
	} else if (receptec_uwb_peer_registry_get(&peer_registry, slot, eui64) == 0) {
		*enabled = peer_enabled[slot];
	} else {
		ret = -ENOENT;
	}
	k_mutex_unlock(&uwb_peer_mutex);
	return ret;
}

int receptec_uwb_peer_enabled_set(uint8_t slot, bool enabled)
{
	uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE];
	int ret = 0;

	if (slot >= RECEPTEC_UWB_MAX_PEERS) {
		return -EINVAL;
	}
	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
	if (!peer_registry_ready) {
		ret = -EAGAIN;
	} else if (receptec_uwb_peer_registry_get(&peer_registry, slot, eui64) < 0) {
		ret = -ENOENT;
	} else if (peer_enabled[slot] != enabled) {
		ret = receptec_uwb_peer_enabled_save(slot, enabled);
		if (ret == 0) {
			peer_enabled[slot] = enabled;
		}
	}
	k_mutex_unlock(&uwb_peer_mutex);
	return ret;
}

static bool nickname_valid(const char *nickname, size_t len)
{
	size_t i;

	if (len == 0U || len >= RECEPTEC_UWB_PEER_NICKNAME_MAX_LEN) {
		return false;
	}
	for (i = 0U; i < len; ++i) {
		char c = nickname[i];

		if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
		      (c >= '0' && c <= '9') || c == '-' || c == '_' ||
		      c == '.')) {
			return false;
		}
	}
	return true;
}

int receptec_uwb_peer_nickname_get(uint8_t slot, char *nickname,
				   size_t capacity)
{
	uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE];
	int ret = 0;

	if (nickname == NULL || capacity == 0U ||
	    slot >= RECEPTEC_UWB_MAX_PEERS) {
		return -EINVAL;
	}
	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
	if (!peer_registry_ready) {
		ret = -EAGAIN;
	} else if (receptec_uwb_peer_registry_get(&peer_registry, slot,
						  eui64) < 0) {
		ret = -ENOENT;
	} else if (!peer_nickname_set[slot]) {
		ret = -ENOENT;
	} else if (strlen(peer_nicknames[slot]) + 1U > capacity) {
		ret = -ENOMEM;
	} else {
		memcpy(nickname, peer_nicknames[slot],
		       strlen(peer_nicknames[slot]) + 1U);
	}
	k_mutex_unlock(&uwb_peer_mutex);
	return ret;
}

int receptec_uwb_peer_nickname_set(uint8_t slot, const char *nickname)
{
	uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE];
	size_t len;
	int ret = 0;

	if (nickname == NULL || slot >= RECEPTEC_UWB_MAX_PEERS) {
		return -EINVAL;
	}
	len = strlen(nickname);
	if (!nickname_valid(nickname, len)) {
		return -EINVAL;
	}
	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
	if (!peer_registry_ready) {
		ret = -EAGAIN;
	} else if (receptec_uwb_peer_registry_get(&peer_registry, slot,
						  eui64) < 0) {
		ret = -ENOENT;
	} else {
		ret = receptec_uwb_peer_nickname_save(slot, nickname);
		if (ret == 0) {
			memcpy(peer_nicknames[slot], nickname, len + 1U);
			peer_nickname_set[slot] = true;
		}
	}
	k_mutex_unlock(&uwb_peer_mutex);
	return ret;
}

int receptec_uwb_peer_nickname_clear(uint8_t slot)
{
	uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE];
	int ret = 0;

	if (slot >= RECEPTEC_UWB_MAX_PEERS) {
		return -EINVAL;
	}
	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
	if (!peer_registry_ready) {
		ret = -EAGAIN;
	} else if (receptec_uwb_peer_registry_get(&peer_registry, slot,
						  eui64) < 0) {
		ret = -ENOENT;
	} else if (peer_nickname_set[slot] ||
		   receptec_uwb_peer_nickname_load(
			   slot, peer_nicknames[slot],
			   sizeof(peer_nicknames[slot])) == 0) {
		ret = receptec_uwb_peer_nickname_delete(slot);
		if (ret == 0 || ret == -ENOENT) {
			peer_nicknames[slot][0] = '\0';
			peer_nickname_set[slot] = false;
			ret = 0;
		}
	}
	k_mutex_unlock(&uwb_peer_mutex);
	return ret;
}

static int load_request_frame(enum receptec_uwb_packet_type type,
			      const uint8_t destination[RECEPTEC_UWB_EUI64_SIZE],
			      uint8_t sequence, uint16_t dt_sampling_us)
{
	uint8_t dt_sampling[RECEPTEC_UWB_DT_SAMPLING_SIZE];
	struct receptec_uwb_frame frame = {
		.sequence = sequence,
		.type = type,
		.payload = type == RECEPTEC_UWB_STOP ? NULL : dt_sampling,
		.payload_length = type == RECEPTEC_UWB_STOP ? 0U :
				  sizeof(dt_sampling),
	};
	size_t logical_length;
	uint16_t radio_length;

	receptec_uwb_encode_dt_sampling(dt_sampling_us, dt_sampling);
	memcpy(frame.destination, destination, RECEPTEC_UWB_EUI64_SIZE);
	memcpy(frame.source, receptec_eui, RECEPTEC_UWB_EUI64_SIZE);
	if (receptec_uwb_encode(tx_bytes, sizeof(tx_bytes), &frame,
				&logical_length) != RECEPTEC_UWB_OK) {
		return -EINVAL;
	}
	radio_length = (uint16_t)(logical_length + RECEPTEC_UWB_FCS_SIZE);
	if (dwt_writetxdata(radio_length, tx_bytes, 0U) != DWT_SUCCESS) {
		return -EIO;
	}
	dwt_writetxfctrl(radio_length, 0U, 0U);
	return 0;
}

static int matching_control_response_received(
	const uint8_t peer_eui64[RECEPTEC_UWB_EUI64_SIZE], uint8_t sequence,
	enum receptec_uwb_packet_type expected_type)
{
	uint16_t radio_length;
	uint8_t ranging;
	struct receptec_uwb_frame frame;
	int ret;

	radio_length = dwt_getframelength(&ranging);
	if (radio_length != RECEPTEC_UWB_HEADER_SIZE + RECEPTEC_UWB_FCS_SIZE) {
		return -EMSGSIZE;
	}
	dwt_readrxdata(rx_bytes, radio_length - RECEPTEC_UWB_FCS_SIZE, 0U);
	ret = receptec_uwb_decode(rx_bytes,
				  radio_length - RECEPTEC_UWB_FCS_SIZE, &frame);
	if (ret != RECEPTEC_UWB_OK ||
	    !receptec_uwb_control_response_matches(
		    &frame, expected_type, peer_eui64, receptec_eui, sequence)) {
		return -EBADMSG;
	}
	return 0;
}

static int await_matching_control_response(const uint8_t peer_eui64[RECEPTEC_UWB_EUI64_SIZE],
					   uint8_t sequence,
					   enum receptec_uwb_packet_type expected_type,
					   uint32_t timeout_us)
{
	int64_t deadline = bounded_deadline(k_uptime_get() + (timeout_us + 999U) / 1000U);

	while (true) {
		int64_t remaining_ms = deadline - k_uptime_get();
		uint32_t status;
		int ret;

		if (remaining_ms <= 0) {
			return -ETIMEDOUT;
		}
		ret = receptec_radio_wait_event(K_MSEC(MIN(remaining_ms, 60000)));
		status = dwt_readsysstatuslo();
		if (k_uptime_get() >= deadline) {
			return -ETIMEDOUT;
		}
		if ((status & DWT_INT_RXFCG_BIT_MASK) != 0U) {
			clear_radio_events(DWT_INT_RXFCG_BIT_MASK);
			ret =
			    matching_control_response_received(peer_eui64, sequence, expected_type);
			if (ret == 0) {
				return 0;
			}
			/* OI-07: an unrelated or malformed frame never extends
			 * the timeout, but the radio must be re-armed: after
			 * RXFCG it sits idle, and without re-arm every later
			 * frame in this window (including the awaited ACK) is
			 * missed. That starved any initiator sharing the air
			 * with another round's traffic.
			 */
			printk("UWB control response ignored: type=%u status=%d radio=0x%08X\n",
			       expected_type, ret, status);
			(void)dwt_rxenable(DWT_START_RX_IMMEDIATE);
		} else if ((status & SYS_STATUS_ALL_RX_TO) != 0U) {
			if (expected_type != RECEPTEC_UWB_STOP_ACK) {
				return -ETIMEDOUT;
			}
			clear_radio_events(status & SYS_STATUS_ALL_RX_TO);
		} else if ((status & UWB_RX_REARM_ERROR_MASK) != 0U) {
			printk("UWB control receive error: radio=0x%08X; continuing\n", status);
			clear_radio_events(status & UWB_RX_REARM_ERROR_MASK);
		} else if (ret < 0) {
			if (expected_type == RECEPTEC_UWB_STOP_ACK && ret == -EAGAIN) {
				/* The software deadline may span several bounded waits. */
				continue;
			}
			return expected_type == RECEPTEC_UWB_STOP_ACK ? ret : -ETIMEDOUT;
		}

		remaining_ms = deadline - k_uptime_get();
		if (remaining_ms <= 0) {
			return -ETIMEDOUT;
		}
		dwt_setrxtimeout(expected_type == RECEPTEC_UWB_STOP_ACK
				     ? 0U
				     : us_to_rx_timeout((uint32_t)remaining_ms * 1000U));
		if (dwt_rxenable(DWT_START_RX_IMMEDIATE) != DWT_SUCCESS) {
			return -EIO;
		}
	}
}

static int send_control_once(uint8_t peer_slot,
			     enum receptec_uwb_packet_type request_type,
			     enum receptec_uwb_packet_type response_type,
			     uint32_t timeout_us)
{
	uint8_t peer_eui64[RECEPTEC_UWB_EUI64_SIZE];
	uint8_t sequence;
	int status;
	int ret;

	if (timeout_us == 0U) {
		return -ENOTSUP;
	}
	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
	if (!radio_ready) {
		ret = -EAGAIN;
		goto unlock;
	}
	if (poll_running) {
		/* The poll thread owns the radio; stop it with `uwb poll stop`. */
		ret = -EBUSY;
		goto unlock;
	}
	ret = receptec_uwb_peer_registry_get(&peer_registry, peer_slot,
						 peer_eui64);
	if (ret < 0) {
		goto unlock;
	}
	if (!peer_enabled[peer_slot]) {
		ret = -EACCES;
		goto unlock;
	}
	ret = receptec_uwb_peer_registry_next_sequence(&peer_registry,
							      peer_slot, &sequence);
	if (ret < 0) {
		goto unlock;
	}
	ret = load_request_frame(request_type, peer_eui64, sequence, 0U);
	if (ret < 0) {
		goto unlock;
	}
	dwt_forcetrxoff();
	clear_radio_events(UWB_RADIO_EVENT_MASK | DWT_INT_TXFRS_BIT_MASK);
	dwt_setrxaftertxdelay(0U);
	/* STOP may wait longer than the radio's 20-bit RX_FWTO register permits. */
	dwt_setrxtimeout(response_type == RECEPTEC_UWB_STOP_ACK ? 0U :
			 us_to_rx_timeout(timeout_us));
	ret = receptec_radio_irq_enable(true);
	if (ret < 0) {
		goto unlock;
	}
	if (dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED) !=
	    DWT_SUCCESS) {
		(void)receptec_radio_irq_enable(false);
		ret = -EIO;
		goto unlock;
	}
	status = await_matching_control_response(peer_eui64, sequence,
						 response_type, timeout_us);
	dwt_forcetrxoff();
	clear_radio_events(UWB_RADIO_EVENT_MASK | DWT_INT_TXFRS_BIT_MASK);
	(void)receptec_radio_irq_enable(false);
	if (status == 0) {
		if (request_type == RECEPTEC_UWB_STOP) {
			printk("STOP/STOP_ACK OK: seq=%u peer=%u FCS=valid address=valid\n",
			       sequence, peer_slot);
		} else {
			printk("SYNC/ACK OK: seq=%u peer=%u FCS=valid address=valid\n",
			       sequence, peer_slot);
		}
	} else {
		if (request_type == RECEPTEC_UWB_STOP) {
			printk("STOP/STOP_ACK failed: seq=%u peer=%u status=%d\n",
			       sequence, peer_slot, status);
		} else {
			printk("SYNC/ACK failed: seq=%u peer=%u status=%d\n",
			       sequence, peer_slot, status);
		}
	}
	ret = status;

unlock:
	k_mutex_unlock(&uwb_peer_mutex);
	return ret;
}

int receptec_uwb_radio_send_sync(uint8_t peer_slot)
{
	return send_control_once(peer_slot, RECEPTEC_UWB_SYNC, RECEPTEC_UWB_ACK,
				 UWB_RESPONSE_TIMEOUT_US);
}

int receptec_uwb_radio_send_stop(uint8_t peer_slot)
{
	if (CONFIG_RECEPTEC_STOP_TIMEOUT_US <= 0) {
		return -ENOTSUP;
	}
	return send_control_once(peer_slot, RECEPTEC_UWB_STOP,
				 RECEPTEC_UWB_STOP_ACK,
				 CONFIG_RECEPTEC_STOP_TIMEOUT_US);
}

/*
 * Periodic DATA_REQ polling — protocol-v1.0 §2.3, §4.2 and §4.3.
 *
 * One poll thread walks the registered peers once per T_sampling. Peer N owns
 * the slot that starts N × T_slot after the cycle epoch. Inside a slot each
 * transaction makes up to UWB_POLL_ATTEMPTS attempts on a binary exponential
 * backoff schedule measured from the slot epoch, so a retransmission never
 * moves the next cycle, and every attempt carries its own offset as
 * DT_sampling.
 *
 * The timings are bench values (T_slot 20 ms, T_timeout 3 ms, T_base 5 ms,
 * T_jitter_max 0.5 ms); the specification leaves them TBD (OI-04, OI-05).
 * T_base must exceed T_timeout by more than the DATA_REQ airtime and the
 * per-attempt setup: with T_base 3 ms (equal to T_timeout) attempt 1 was still
 * waiting when attempt 2 was due, so every retry failed to schedule (late) and
 * a lost first attempt was never recovered (by_attempt=N/0/0). OI-01
 * (response-loss retry) still applies: a COLETEC that shuts its receiver after
 * answering will miss the retry of a lost reply.
 */
#define UWB_POLL_SLOT_US 20000U
#define UWB_POLL_RESPONSE_TIMEOUT_US 3000U
#define UWB_POLL_SETUP_US 5000U
#define UWB_POLL_ATTEMPTS 3U
#define UWB_POLL_BACKOFF_BASE_US 5000U
#define UWB_POLL_JITTER_MAX_US 500U
#define UWB_POLL_OFFSET_MAX_US                                                                     \
	(UWB_POLL_BACKOFF_BASE_US * ((1U << (UWB_POLL_ATTEMPTS - 1U)) - 1U) +                      \
	 (UWB_POLL_ATTEMPTS - 1U) * UWB_POLL_JITTER_MAX_US)
#define UWB_POLL_THREAD_STACK_SIZE 3072U
#define UWB_POLL_THREAD_PRIORITY 1
/* Diagnostic DATA payload of the fake COLETEC: own EUI-64, then a u32 counter.
 */
#define UWB_POLL_DIAG_COUNTER_OFFSET RECEPTEC_UWB_EUI64_SIZE

BUILD_ASSERT(UWB_POLL_OFFSET_MAX_US + UWB_POLL_RESPONSE_TIMEOUT_US <= UWB_POLL_SLOT_US,
	     "the last attempt and its response window must fit inside the slot");

static struct receptec_uwb_poll_status poll_status;
/* Latest DATA payload per peer, for `uwb poll last` and the PC viewer. */
static struct {
	uint8_t payload[RECEPTEC_UWB_MAX_PAYLOAD_SIZE];
	size_t length;
	uint32_t count;
	int64_t uptime_ms;
} poll_last[RECEPTEC_UWB_MAX_PEERS];
static atomic_t poll_stream_enabled = ATOMIC_INIT(0);
static atomic_t poll_stop_requested = ATOMIC_INIT(0);
static K_SEM_DEFINE(poll_start_sem, 0, 1);

/* DW3xxx system time is 499.2 MHz x 128: 1 us = 1248 / 5 units of the high
 * word. */
static uint32_t us_to_dx_time(uint32_t us)
{
	return (uint32_t)(((uint64_t)us * 1248ULL + 2ULL) / 5ULL);
}

static int64_t dx_diff_to_us(int32_t dx)
{
	return ((int64_t)dx * 5LL) / 1248LL;
}

static void draw_backoff_offsets(uint16_t offsets_us[UWB_POLL_ATTEMPTS])
{
	uint32_t offset = 0U;

	offsets_us[0] = 0U;
	for (size_t attempt = 1U; attempt < UWB_POLL_ATTEMPTS; ++attempt) {
		uint32_t wait = UWB_POLL_BACKOFF_BASE_US << (attempt - 1U);
		uint32_t jitter = sys_rand32_get() %
				  (UWB_POLL_JITTER_MAX_US + 1U);

		offset += wait + jitter;
		offsets_us[attempt] = (uint16_t)offset;
	}
}

static uint32_t decode_u32_le(const uint8_t *input)
{
	return (uint32_t)input[0] | ((uint32_t)input[1] << 8) |
	       ((uint32_t)input[2] << 16) | ((uint32_t)input[3] << 24);
}

static int poll_check_data(const uint8_t peer_eui64[RECEPTEC_UWB_EUI64_SIZE],
			   uint8_t slot, uint8_t sequence,
			   struct receptec_uwb_poll_peer_stats *stats)
{
	struct receptec_uwb_frame frame;
	uint16_t radio_length;
	uint16_t logical_length;
	uint8_t ranging;

	radio_length = dwt_getframelength(&ranging);
	if (radio_length < RECEPTEC_UWB_HEADER_SIZE + RECEPTEC_UWB_FCS_SIZE ||
	    radio_length > RECEPTEC_UWB_MAX_PSDU_SIZE) {
		stats->bad_frames++;
		return -EMSGSIZE;
	}
	logical_length = radio_length - RECEPTEC_UWB_FCS_SIZE;
	dwt_readrxdata(rx_bytes, logical_length, 0U);
	if (receptec_uwb_decode(rx_bytes, logical_length, &frame) !=
		    RECEPTEC_UWB_OK ||
	    frame.type != RECEPTEC_UWB_DATA ||
	    frame.sequence != sequence ||
	    memcmp(frame.source, peer_eui64, RECEPTEC_UWB_EUI64_SIZE) != 0 ||
	    memcmp(frame.destination, receptec_eui, RECEPTEC_UWB_EUI64_SIZE) != 0) {
		stats->bad_frames++;
		return -EBADMSG;
	}

	stats->payload_bytes += frame.payload_length;
	memcpy(poll_last[slot].payload, frame.payload, frame.payload_length);
	poll_last[slot].length = frame.payload_length;
	poll_last[slot].count++;
	poll_last[slot].uptime_ms = k_uptime_get();
	if (frame.payload_length >= UWB_POLL_DIAG_COUNTER_OFFSET + 4U &&
	    memcmp(frame.payload, peer_eui64, RECEPTEC_UWB_EUI64_SIZE) == 0) {
		uint32_t sample = decode_u32_le(
			&frame.payload[UWB_POLL_DIAG_COUNTER_OFFSET]);

		if (stats->have_sample && sample > stats->last_sample + 1U) {
			stats->sample_gaps += sample - stats->last_sample - 1U;
		}
		stats->last_sample = sample;
		stats->have_sample = true;
	}
	return 0;
}

/* One DATA_REQ attempt: delayed TX at target_dx, then wait for the DATA frame.
 */
static int poll_attempt(const uint8_t peer_eui64[RECEPTEC_UWB_EUI64_SIZE],
			uint8_t slot, uint8_t sequence, uint32_t target_dx,
			uint16_t dt_us,
			struct receptec_uwb_poll_peer_stats *stats)
{
	/* The delayed TX may be up to a slot ahead; never wait longer than that. */
	int64_t deadline = k_uptime_get() +
			   (int64_t)(UWB_POLL_SLOT_US * 4U / 1000U) + 50;
	int status;

	status = load_request_frame(RECEPTEC_UWB_DATA_REQ, peer_eui64, sequence,
				    dt_us);
	if (status < 0) {
		return status;
	}
	dwt_forcetrxoff();
	clear_radio_events(UWB_RADIO_EVENT_MASK | DWT_INT_TXFRS_BIT_MASK);
	dwt_setrxaftertxdelay(0U);
	dwt_setrxtimeout(us_to_rx_timeout(UWB_POLL_RESPONSE_TIMEOUT_US));
	dwt_setdelayedtrxtime(target_dx);
	if (dwt_starttx(DWT_START_TX_DELAYED | DWT_RESPONSE_EXPECTED) !=
	    DWT_SUCCESS) {
		stats->late_schedules++;
		return -ETIME;
	}
	stats->attempts++;

	for (;;) {
		int64_t remaining_ms = deadline - k_uptime_get();
		uint32_t radio_status;

		if (remaining_ms <= 0) {
			dwt_forcetrxoff();
			stats->timeouts++;
			status = -ETIMEDOUT;
			break;
		}
		(void)receptec_radio_wait_event(K_MSEC(remaining_ms));
		radio_status = dwt_readsysstatuslo();
		if ((radio_status & DWT_INT_RXFCG_BIT_MASK) != 0U) {
			status = poll_check_data(peer_eui64, slot, sequence, stats);
			break;
		}
		if ((radio_status & SYS_STATUS_ALL_RX_TO) != 0U) {
			stats->timeouts++;
			status = -ETIMEDOUT;
			break;
		}
		if ((radio_status & UWB_RX_REARM_ERROR_MASK) != 0U) {
			stats->rx_errors++;
			status = -EIO;
			break;
		}
	}
	dwt_forcetrxoff();
	clear_radio_events(UWB_RADIO_EVENT_MASK | DWT_INT_TXFRS_BIT_MASK);
	return status;
}

/* One line per received DATA: `DATA slot=N n=COUNT len=L HEX` (see
 * sensor-viewer.py). */
static void print_stream_line(uint8_t slot)
{
	char hex[2U * RECEPTEC_UWB_MAX_PAYLOAD_SIZE + 1U];

	for (size_t i = 0U; i < poll_last[slot].length; ++i) {
		snprintk(&hex[2U * i], 3U, "%02X", poll_last[slot].payload[i]);
	}
	hex[2U * poll_last[slot].length] = '\0';
	printk("DATA slot=%u n=%u len=%u %s\n", slot, poll_last[slot].count,
	       (uint32_t)poll_last[slot].length, hex);
}

/* One transaction: up to UWB_POLL_ATTEMPTS attempts inside the peer's slot. */
static bool poll_transaction(uint8_t slot,
			     const uint8_t peer_eui64[RECEPTEC_UWB_EUI64_SIZE],
			     uint32_t slot_epoch_dx)
{
	struct receptec_uwb_poll_peer_stats *stats = &poll_status.peer[slot];
	uint16_t offsets_us[UWB_POLL_ATTEMPTS];
	uint8_t sequence;

	if (receptec_uwb_peer_registry_next_sequence(&peer_registry, slot,
						      &sequence) < 0) {
		return false;
	}
	stats->transactions++;
	draw_backoff_offsets(offsets_us);
	for (size_t attempt = 0U; attempt < UWB_POLL_ATTEMPTS; ++attempt) {
		int ret = poll_attempt(peer_eui64, slot, sequence,
				       slot_epoch_dx +
					       us_to_dx_time(offsets_us[attempt]),
				       offsets_us[attempt], stats);

		if (ret == 0) {
			stats->completed++;
			stats->completed_on_attempt[attempt]++;
			if (atomic_get(&poll_stream_enabled) != 0) {
				print_stream_line(slot);
			}
			return true;
		}
	}
	stats->exhausted++;
	return false;
}

/* One scheduled SYNC attempt: the ACK must match peer, receiver and sequence.
 */
static int sync_attempt(const uint8_t peer_eui64[RECEPTEC_UWB_EUI64_SIZE],
			uint8_t slot, uint8_t sequence, uint32_t target_dx,
			uint16_t dt_us,
			struct receptec_uwb_poll_peer_stats *stats)
{
	int64_t deadline = k_uptime_get() +
			   (int64_t)(UWB_POLL_SLOT_US * 4U / 1000U) + 50;
	int status;

	status = load_request_frame(RECEPTEC_UWB_SYNC, peer_eui64, sequence, dt_us);
	if (status < 0) {
		return status;
	}
	dwt_forcetrxoff();
	clear_radio_events(UWB_RADIO_EVENT_MASK | DWT_INT_TXFRS_BIT_MASK);
	dwt_setrxaftertxdelay(0U);
	dwt_setrxtimeout(us_to_rx_timeout(UWB_POLL_RESPONSE_TIMEOUT_US));
	dwt_setdelayedtrxtime(target_dx);
	if (dwt_starttx(DWT_START_TX_DELAYED | DWT_RESPONSE_EXPECTED) !=
	    DWT_SUCCESS) {
		stats->late_schedules++;
		return -ETIME;
	}
	stats->attempts++;

	for (;;) {
		int64_t remaining_ms = deadline - k_uptime_get();
		uint32_t radio_status;

		if (remaining_ms <= 0) {
			dwt_forcetrxoff();
			stats->timeouts++;
			status = -ETIMEDOUT;
			break;
		}
		(void)receptec_radio_wait_event(K_MSEC(remaining_ms));
		radio_status = dwt_readsysstatuslo();
		if ((radio_status & DWT_INT_RXFCG_BIT_MASK) != 0U) {
			status = matching_control_response_received(
				peer_eui64, sequence, RECEPTEC_UWB_ACK);
			if (status < 0) {
				stats->bad_frames++;
			}
			break;
		}
		if ((radio_status & SYS_STATUS_ALL_RX_TO) != 0U) {
			stats->timeouts++;
			status = -ETIMEDOUT;
			break;
		}
		if ((radio_status & UWB_RX_REARM_ERROR_MASK) != 0U) {
			stats->rx_errors++;
			status = -EIO;
			break;
		}
	}
	dwt_forcetrxoff();
	clear_radio_events(UWB_RADIO_EVENT_MASK | DWT_INT_TXFRS_BIT_MASK);
	return status;
}

/* Keep one peer synchronized when the receiver is not collecting DATA. */
static bool sync_transaction(uint8_t slot,
			     const uint8_t peer_eui64[RECEPTEC_UWB_EUI64_SIZE],
			     uint32_t slot_epoch_dx,
			     uint16_t scheduler_lateness_us)
{
	struct receptec_uwb_poll_peer_stats *stats = &poll_status.peer[slot];
	uint16_t offsets_us[UWB_POLL_ATTEMPTS];
	uint8_t sequence;

	if (receptec_uwb_peer_registry_next_sequence(&peer_registry, slot,
						      &sequence) < 0) {
		return false;
	}
	stats->transactions++;
	draw_backoff_offsets(offsets_us);
	for (size_t attempt = 0U; attempt < UWB_POLL_ATTEMPTS; ++attempt) {
		uint32_t dt_us = (uint32_t)offsets_us[attempt] +
				 scheduler_lateness_us;
		int ret = sync_attempt(peer_eui64, slot, sequence,
				       slot_epoch_dx + us_to_dx_time(offsets_us[attempt]),
				       (uint16_t)MIN(dt_us, UINT16_MAX), stats);

		if (ret == 0) {
			stats->completed++;
			stats->completed_on_attempt[attempt]++;
			return true;
		}
	}
	stats->exhausted++;
	return false;
}

/*
 * Runtime UWB tests, injected into the poll cycle.
 *
 * The production COLETEC only listens in the rhythm the RECEPTEC sets, so a
 * test cannot be fired at an arbitrary moment: it replaces one transaction, in
 * the target peer's own slot, of a running poll. Each test is three phases on
 * that slot, one transaction per cycle: WARMUP (two ordinary transactions must
 * succeed, proving there is a baseline), RUN (the test itself), VERIFY (one
 * ordinary transaction must succeed, proving the test did not poison the peer).
 * Exactly one UWB_TEST line is printed at the end: PASS, FAIL with a reason, a
 * timeout or an abort. Everything runs on the poll thread, under the radio
 * mutex; the shell only arms a test and returns.
 */
#define UT_WARMUP_OK 2U
#define UT_MAX_WARMUP_CYCLES 12U
#define UT_MAX_CYCLES 40U
#define UT_VERIFY_TRIES 4U
/* Delayed TX lands on the requested time within the radio's own granularity (8
 * ns); 60 us is generous and still catches a schedule that was really missed.
 */
#define UT_TX_TOLERANCE_US 60
#define UT_PERIOD_MS 1000U
/* discovery: the COLETEC goes to discovery after 5 missed slots
 * (COLETEC_N_CNT_GUARD_MAX), so stay silent for one more than that, then probe
 * once per cycle until it answers again. */
#define UT_DISCOVERY_SILENCE_CYCLES 6U
#define UT_DISCOVERY_MAX_PROBES 24U

enum ut_kind {
	UT_MUTATION = 1,
	UT_RETRY,
	UT_DATA_PAYLOAD,
	UT_SEQUENCE_WRAP,
	UT_DISCOVERY,
};

enum ut_phase {
	UT_WARMUP = 0,
	UT_RUN,
	UT_VERIFY,
};

static struct {
	bool active;
	enum ut_kind kind;
	const char *name;
	enum receptec_uwb_test_mutation mutation;
	uint8_t ignored; /* valid responses to ignore before accepting one */
	uint8_t slot;
	enum ut_phase phase;
	uint32_t cycles;
	uint8_t warm_ok;
	uint8_t verify_fail;
	uint8_t step;
	uint8_t probes;
	uint8_t saved_next_sequence;
	bool restore_sequence;
	atomic_t abort;
	bool have_result;
	struct receptec_uwb_test_result result;
} ut;

struct ut_rx {
	int status; /* 0 frame in rx_bytes, -ETIMEDOUT, -EIO, -EMSGSIZE, -ETIME (late
		       schedule) */
	uint16_t logical_length;
	int32_t tx_offset_us; /* measured TX RMARKER relative to the slot epoch */
	int32_t rx_offset_us; /* measured RX RMARKER relative to the slot epoch */
};

static int32_t ut_abs(int32_t value)
{
	return value < 0 ? -value : value;
}

static void ut_pass(void)
{
	ut.result.outcome = RECEPTEC_UWB_TEST_PASS;
	ut.have_result = true;
}

static void ut_fail(const char *reason)
{
	ut.result.outcome = RECEPTEC_UWB_TEST_FAIL;
	ut.result.reason = reason;
	ut.have_result = true;
}

static void ut_finish(void)
{
	char line[RECEPTEC_UWB_TEST_LINE_SIZE];

	if (ut.restore_sequence) {
		(void)receptec_uwb_peer_registry_set_next_sequence(
			&peer_registry, ut.slot, ut.saved_next_sequence);
		ut.restore_sequence = false;
	}
	if (receptec_uwb_test_result_format(&ut.result, line, sizeof(line)) > 0) {
		printk("%s\n", line);
	} else {
		/* Not a UWB_TEST line on purpose: the runner must see no result. */
		printk("test engine error: result for %s does not format\n", ut.name);
	}
	ut.active = false;
	atomic_set(&poll_stop_requested, 1);
}

/* One delayed DATA_REQ-shaped transmission whose frame is already loaded, then
 * the wait. */
static void ut_attempt(uint32_t target_dx, uint32_t epoch_dx, struct ut_rx *rx)
{
	int64_t deadline = k_uptime_get() + (int64_t)(UWB_POLL_SLOT_US * 4U / 1000U) + 50;

	memset(rx, 0, sizeof(*rx));
	dwt_forcetrxoff();
	clear_radio_events(UWB_RADIO_EVENT_MASK | DWT_INT_TXFRS_BIT_MASK);
	dwt_setrxaftertxdelay(0U);
	dwt_setrxtimeout(us_to_rx_timeout(UWB_POLL_RESPONSE_TIMEOUT_US));
	dwt_setdelayedtrxtime(target_dx);
	if (dwt_starttx(DWT_START_TX_DELAYED | DWT_RESPONSE_EXPECTED) != DWT_SUCCESS) {
		rx->status = -ETIME;
		return;
	}
	for (;;) {
		int64_t remaining_ms = deadline - k_uptime_get();
		uint32_t radio_status;

		if (remaining_ms <= 0) {
			rx->status = -ETIMEDOUT;
			break;
		}
		(void)receptec_radio_wait_event(K_MSEC(remaining_ms));
		radio_status = dwt_readsysstatuslo();
		if ((radio_status & DWT_INT_RXFCG_BIT_MASK) != 0U) {
			uint8_t ranging;
			uint16_t radio_length = dwt_getframelength(&ranging);

			if (radio_length < RECEPTEC_UWB_FCS_SIZE ||
			    radio_length > RECEPTEC_UWB_MAX_PSDU_SIZE) {
				rx->status = -EMSGSIZE;
			} else {
				rx->logical_length = radio_length - RECEPTEC_UWB_FCS_SIZE;
				dwt_readrxdata(rx_bytes, rx->logical_length, 0U);
				rx->rx_offset_us = (int32_t)dx_diff_to_us(
					(int32_t)(dwt_readrxtimestamphi32() - epoch_dx));
				rx->status = 0;
			}
			break;
		}
		if ((radio_status & SYS_STATUS_ALL_RX_TO) != 0U) {
			rx->status = -ETIMEDOUT;
			break;
		}
		if ((radio_status & UWB_RX_REARM_ERROR_MASK) != 0U) {
			rx->status = -EIO;
			break;
		}
	}
	rx->tx_offset_us = (int32_t)dx_diff_to_us((int32_t)(dwt_readtxtimestamphi32() - epoch_dx));
	dwt_forcetrxoff();
	clear_radio_events(UWB_RADIO_EVENT_MASK | DWT_INT_TXFRS_BIT_MASK);
}

/* The received frame must be a DATA from this peer to us with the request's
 * sequence. */
static int ut_check_data(const uint8_t peer_eui64[RECEPTEC_UWB_EUI64_SIZE], uint8_t sequence,
			 const struct ut_rx *rx, struct receptec_uwb_frame *frame)
{
	if (rx->status != 0) {
		return rx->status;
	}
	if (receptec_uwb_decode(rx_bytes, rx->logical_length, frame) != RECEPTEC_UWB_OK ||
	    frame->type != RECEPTEC_UWB_DATA || frame->sequence != sequence ||
	    memcmp(frame->source, peer_eui64, RECEPTEC_UWB_EUI64_SIZE) != 0 ||
	    memcmp(frame->destination, receptec_eui, RECEPTEC_UWB_EUI64_SIZE) != 0) {
		return -EBADMSG;
	}
	return 0;
}

/*
 * retry, duplicate and data_payload: one transaction, one sequence number, the
 * request repeated on the ordinary backoff schedule after a valid response that
 * is deliberately ignored, as if it had been lost. PASS needs ignored+1 valid
 * responses with the request's sequence, byte-identical payloads, every attempt
 * at its generated offset and no response past the slot end. data_payload also
 * checks the payload v1 layout.
 */
static void ut_run_retry(const uint8_t peer_eui64[RECEPTEC_UWB_EUI64_SIZE], uint32_t epoch_dx)
{
	static uint8_t payload[UWB_POLL_ATTEMPTS][RECEPTEC_UWB_MAX_PAYLOAD_SIZE];
	size_t payload_length[UWB_POLL_ATTEMPTS] = { 0 };
	uint16_t offsets_us[UWB_POLL_ATTEMPTS];
	size_t needed = (size_t)ut.ignored + 1U;
	size_t valid = 0U;
	int32_t worst_tx_us = 0;
	int32_t latest_rx_us = 0;
	uint8_t sequence;

	if (receptec_uwb_peer_registry_next_sequence(&peer_registry, ut.slot, &sequence) < 0) {
		ut_fail("no-sequence");
		return;
	}
	draw_backoff_offsets(offsets_us);
	for (size_t attempt = 0U; attempt < UWB_POLL_ATTEMPTS && valid < needed; ++attempt) {
		struct receptec_uwb_frame frame;
		struct ut_rx rx;
		int32_t tx_error_us;

		if (load_request_frame(RECEPTEC_UWB_DATA_REQ, peer_eui64, sequence,
				       offsets_us[attempt]) < 0) {
			ut_fail("frame-load");
			return;
		}
		ut_attempt(epoch_dx + us_to_dx_time(offsets_us[attempt]), epoch_dx, &rx);
		if (rx.status == -ETIME) {
			ut_fail("late-schedule");
			return;
		}
		tx_error_us = ut_abs(rx.tx_offset_us - (int32_t)offsets_us[attempt]);
		worst_tx_us = MAX(worst_tx_us, tx_error_us);
		if (tx_error_us > UT_TX_TOLERANCE_US) {
			(void)receptec_uwb_test_result_add_u32(&ut.result, "attempt",
							       (uint32_t)attempt + 1U);
			(void)receptec_uwb_test_result_add_i32(&ut.result, "tx-error-us", tx_error_us);
			ut_fail("attempt-timing");
			return;
		}
		if (rx.status != 0) {
			continue;
		}
		if (ut_check_data(peer_eui64, sequence, &rx, &frame) != 0) {
			ut_fail("bad-response");
			return;
		}
		if (rx.rx_offset_us > (int32_t)UWB_POLL_SLOT_US) {
			ut_fail("late-response");
			return;
		}
		latest_rx_us = MAX(latest_rx_us, rx.rx_offset_us);
		memcpy(payload[valid], frame.payload, frame.payload_length);
		payload_length[valid] = frame.payload_length;
		valid++;
	}
	(void)receptec_uwb_test_result_add_u32(&ut.result, "valid", (uint32_t)valid);
	if (valid < needed) {
		(void)receptec_uwb_test_result_add_u32(&ut.result, "needed", (uint32_t)needed);
		ut_fail("missing-response");
		return;
	}
	for (size_t i = 1U; i < valid; ++i) {
		enum receptec_uwb_payload_duplicate same = receptec_uwb_payload_compare(
			payload[0], payload_length[0], payload[i], payload_length[i]);

		if (same != RECEPTEC_UWB_DUPLICATE_SAME) {
			ut_fail(receptec_uwb_payload_duplicate_reason(same));
			return;
		}
	}
	if (ut.kind == UT_DATA_PAYLOAD) {
		struct receptec_uwb_payload_info info;
		enum receptec_uwb_payload_status status = receptec_uwb_payload_v1_check(
			payload[0], payload_length[0], &info);

		if (status != RECEPTEC_UWB_PAYLOAD_OK) {
			ut_fail(receptec_uwb_payload_reason(status));
			return;
		}
		(void)receptec_uwb_test_result_add_u32(&ut.result, "seq", info.sequence);
		(void)receptec_uwb_test_result_add_u32(&ut.result, "age-ms", info.age_ms);
		(void)receptec_uwb_test_result_add_u32(&ut.result, "flags", info.flags);
	}
	(void)receptec_uwb_test_result_add_i32(&ut.result, "tx-error-us", worst_tx_us);
	(void)receptec_uwb_test_result_add_i32(&ut.result, "last-rx-us", latest_rx_us);
	ut_pass();
}

/* wrong_source, bad_type, bad_reserved, bad_length, bad_dt: no response is the
 * PASS. */
static void ut_run_mutation(const uint8_t peer_eui64[RECEPTEC_UWB_EUI64_SIZE], uint32_t epoch_dx)
{
	struct ut_rx rx;
	size_t logical_length = 0U;
	uint16_t radio_length;
	uint8_t sequence;

	if (receptec_uwb_peer_registry_next_sequence(&peer_registry, ut.slot, &sequence) < 0) {
		ut_fail("no-sequence");
		return;
	}
	if (receptec_uwb_test_build_mutation(ut.mutation, peer_eui64, receptec_eui, sequence, 0U,
					     tx_bytes, sizeof(tx_bytes), &logical_length) < 0) {
		ut_fail("frame-build");
		return;
	}
	radio_length = (uint16_t)(logical_length + RECEPTEC_UWB_FCS_SIZE);
	if (dwt_writetxdata(radio_length, tx_bytes, 0U) != DWT_SUCCESS) {
		ut_fail("frame-load");
		return;
	}
	dwt_writetxfctrl(radio_length, 0U, 0U);
	ut_attempt(epoch_dx, epoch_dx, &rx);
	(void)receptec_uwb_test_result_add_u32(&ut.result, "length", (uint32_t)logical_length);
	switch (rx.status) {
	case -ETIMEDOUT:
		(void)receptec_uwb_test_result_add_token(&ut.result, "response", "none");
		ut_pass();
		break;
	case 0:
		ut_fail("unexpected-response");
		break;
	case -ETIME:
		ut_fail("late-schedule");
		break;
	default:
		ut_fail("rx-error");
		break;
	}
}

/* sequence_wrap: three ordinary transactions with sequences 254, 255 and 0, one
 * per cycle. */
static void ut_run_wrap(const uint8_t peer_eui64[RECEPTEC_UWB_EUI64_SIZE], uint32_t epoch_dx)
{
	struct receptec_uwb_frame frame;
	struct ut_rx rx;
	uint8_t sequence = receptec_uwb_test_wrap_sequence(ut.step);

	if (ut.step == 0U) {
		uint8_t next;

		if (receptec_uwb_peer_registry_next_sequence(&peer_registry, ut.slot, &next) < 0) {
			ut_fail("no-sequence");
			return;
		}
		/* next_sequence() consumed `next`; the counter now holds next + 1. */
		ut.saved_next_sequence = (uint8_t)(next + 1U);
		ut.restore_sequence = true;
	}
	(void)receptec_uwb_test_result_add_u32(&ut.result, "step", ut.step);
	if (load_request_frame(RECEPTEC_UWB_DATA_REQ, peer_eui64, sequence, 0U) < 0) {
		ut_fail("frame-load");
		return;
	}
	ut_attempt(epoch_dx, epoch_dx, &rx);
	if (rx.status == -ETIME) {
		ut_fail("late-schedule");
		return;
	}
	if (rx.status != 0) {
		ut_fail("missing-response");
		return;
	}
	if (ut_check_data(peer_eui64, sequence, &rx, &frame) != 0) {
		ut_fail("bad-response");
		return;
	}
	ut.result.field_count = 0U; /* keep only the final step in the PASS line */
	ut.step++;
	if (ut.step == 3U) {
		(void)receptec_uwb_test_result_add_token(&ut.result, "order", "254-255-0");
		ut_pass();
	}
}

/*
 * discovery: silence long enough to push the COLETEC into discovery, then one
 * ordinary probe per cycle until it answers. It is a bounded orchestration of
 * the COLETEC's own behaviour, not a discovery algorithm: from here the
 * RECEPTEC only sees "no response" and then "a response", so the transition
 * itself is inferred; the host runner adds the COLETEC's RTT lines as evidence.
 */
static void ut_run_discovery(const uint8_t peer_eui64[RECEPTEC_UWB_EUI64_SIZE], uint32_t epoch_dx)
{
	struct receptec_uwb_frame frame;
	struct ut_rx rx;
	uint8_t sequence;

	if (ut.step < UT_DISCOVERY_SILENCE_CYCLES) {
		ut.step++;
		return;
	}
	if (receptec_uwb_peer_registry_next_sequence(&peer_registry, ut.slot, &sequence) < 0) {
		ut_fail("no-sequence");
		return;
	}
	if (load_request_frame(RECEPTEC_UWB_DATA_REQ, peer_eui64, sequence, 0U) < 0) {
		ut_fail("frame-load");
		return;
	}
	ut_attempt(epoch_dx, epoch_dx, &rx);
	ut.probes++;
	if (rx.status == -ETIME) {
		ut_fail("late-schedule");
		return;
	}
	if (rx.status == 0) {
		if (ut_check_data(peer_eui64, sequence, &rx, &frame) != 0) {
			ut_fail("bad-response");
			return;
		}
		(void)receptec_uwb_test_result_add_u32(&ut.result, "silence-cycles",
						       UT_DISCOVERY_SILENCE_CYCLES);
		(void)receptec_uwb_test_result_add_u32(&ut.result, "probes", ut.probes);
		(void)receptec_uwb_test_result_add_token(&ut.result, "transition", "inferred");
		ut_pass();
		return;
	}
	if (ut.probes >= UT_DISCOVERY_MAX_PROBES) {
		(void)receptec_uwb_test_result_add_u32(&ut.result, "probes", ut.probes);
		ut_fail("no-reentry");
	}
}

/* One cycle of the armed test on its slot. Called by the poll thread, mutex
 * held. */
static void ut_cycle(uint8_t slot, const uint8_t peer_eui64[RECEPTEC_UWB_EUI64_SIZE],
		     uint32_t epoch_dx)
{
	ut.cycles++;
	if (atomic_get(&ut.abort) != 0) {
		ut.result.outcome = RECEPTEC_UWB_TEST_ABORT;
		ut.have_result = true;
		ut_finish();
		return;
	}
	if (ut.cycles > UT_MAX_CYCLES) {
		ut.result.outcome = RECEPTEC_UWB_TEST_TIMEOUT;
		ut.have_result = true;
		ut_finish();
		return;
	}
	switch (ut.phase) {
	case UT_WARMUP:
		ut.warm_ok = poll_transaction(slot, peer_eui64, epoch_dx) ? ut.warm_ok + 1U : 0U;
		if (ut.warm_ok >= UT_WARMUP_OK) {
			ut.phase = UT_RUN;
		} else if (ut.cycles >= UT_MAX_WARMUP_CYCLES) {
			ut_fail("no-baseline");
			ut_finish();
		}
		break;
	case UT_RUN:
		switch (ut.kind) {
		case UT_MUTATION:
			ut_run_mutation(peer_eui64, epoch_dx);
			break;
		case UT_RETRY:
		case UT_DATA_PAYLOAD:
			ut_run_retry(peer_eui64, epoch_dx);
			break;
		case UT_SEQUENCE_WRAP:
			ut_run_wrap(peer_eui64, epoch_dx);
			break;
		case UT_DISCOVERY:
			ut_run_discovery(peer_eui64, epoch_dx);
			break;
		default:
			ut_fail("internal");
			break;
		}
		if (ut.have_result) {
			ut.phase = UT_VERIFY;
		}
		break;
	case UT_VERIFY:
		if (poll_transaction(slot, peer_eui64, epoch_dx)) {
			ut_finish();
		} else if (++ut.verify_fail >= UT_VERIFY_TRIES) {
			if (ut.result.outcome == RECEPTEC_UWB_TEST_PASS) {
				ut_fail("recovery-failed");
			}
			ut_finish();
		}
		break;
	}
}

static const char *peer_action_name(enum receptec_uwb_peer_action action)
{
	return action == RECEPTEC_UWB_PEER_ACTION_DATA_REQ ? "DATA_REQ" : "SYNC";
}

/* A compact cumulative health snapshot; no per-attempt console noise. */
static void print_schedule_summary(void)
{
	for (uint8_t slot = 0U; slot < RECEPTEC_UWB_MAX_PEERS; ++slot) {
		const struct receptec_uwb_poll_peer_stats *p = &poll_status.peer[slot];
		uint8_t peer_eui64[RECEPTEC_UWB_EUI64_SIZE];
		uint32_t permille;

		if (p->transactions == 0U ||
		    receptec_uwb_peer_registry_get(&peer_registry, slot,
						   peer_eui64) < 0) {
			continue;
		}
		permille = (uint32_t)(((uint64_t)p->completed * 1000ULL) /
				      p->transactions);
		printk("UWB link slot=%u action=%s cycles=%u ok=%u/%u (%u.%u%%) "
		       "timeout=%u rx_err=%u bad=%u late=%u exhausted=%u\n",
		       slot, peer_action_name(poll_status.action[slot]),
		       poll_status.cycles, p->completed, p->transactions,
		       permille / 10U, permille % 10U, p->timeouts, p->rx_errors,
		       p->bad_frames, p->late_schedules, p->exhausted);
	}
}

int receptec_uwb_test_start(const char *name, uint8_t arg, uint8_t slot)
{
	uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE];
	int mutation = -1;
	int ret;

	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
	if (!radio_ready || !peer_registry_ready) {
		ret = -EAGAIN;
	} else if (poll_running || ut.active) {
		ret = -EBUSY;
	} else if (receptec_uwb_peer_registry_get(&peer_registry, slot, eui64) < 0) {
		ret = -ENOENT;
	} else {
		memset(&ut, 0, sizeof(ut));
		ret = 0;
		mutation = receptec_uwb_test_mutation_from_name(name);
		if (mutation >= 0) {
			ut.kind = UT_MUTATION;
			ut.mutation = (enum receptec_uwb_test_mutation)mutation;
			ut.name = receptec_uwb_test_mutation_name(ut.mutation);
		} else if (strcmp(name, "retry") == 0 && (arg == 1U || arg == 2U)) {
			ut.kind = UT_RETRY;
			ut.ignored = arg;
			ut.name = "retry";
		} else if (strcmp(name, "duplicate") == 0) {
			ut.kind = UT_RETRY;
			ut.ignored = 1U;
			ut.name = "duplicate";
		} else if (strcmp(name, "data_payload") == 0) {
			ut.kind = UT_DATA_PAYLOAD;
			ut.ignored = 1U;
			ut.name = "data_payload";
		} else if (strcmp(name, "sequence_wrap") == 0) {
			ut.kind = UT_SEQUENCE_WRAP;
			ut.name = "sequence_wrap";
		} else if (strcmp(name, "discovery") == 0) {
			ut.kind = UT_DISCOVERY;
			ut.name = "discovery";
		} else {
			ret = strcmp(name, "retry") == 0 ? -EINVAL : -ENOTSUP;
		}
		if (ret == 0) {
			ut.slot = slot;
			ut.phase = UT_WARMUP;
			receptec_uwb_test_result_init(&ut.result, ut.name, slot);
			if (ut.kind == UT_RETRY && ut.ignored != 0U) {
				(void)receptec_uwb_test_result_add_u32(&ut.result, "ignored", ut.ignored);
			}
			ut.active = true;
		}
	}
	k_mutex_unlock(&uwb_peer_mutex);
	if (ret != 0) {
		return ret;
	}
	ret = receptec_uwb_poll_start(UT_PERIOD_MS);
	if (ret < 0) {
		k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
		ut.active = false;
		k_mutex_unlock(&uwb_peer_mutex);
	}
	return ret;
}

int receptec_uwb_test_abort(void)
{
	bool active;

	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
	active = ut.active;
	k_mutex_unlock(&uwb_peer_mutex);
	if (!active) {
		return -EINVAL;
	}
	atomic_set(&ut.abort, 1);
	return 0;
}

/* ---- Minimal DS-TWR round (contract UWB v1.2 S5-S9), pair_index taken from
 * CONFIG_RECEPTEC_FIXED_ANCHOR_ID (0=M-T, 1=A-T) ----
 *
 * See the scope note on receptec_uwb_range_start() in uwb_radio.h: this is a
 * proof of concept, not the full master state machine. It owns the radio the
 * same way run_poll()'s thread does, but runs on its own dedicated thread so
 * run_poll() itself stays untouched; poll_running still gates mutual exclusion
 * between the two.
 */

#define UWB_RANGE_THREAD_STACK_SIZE 2048U
#define UWB_RANGE_THREAD_PRIORITY 7
#define UWB_RANGE_PLAN_ACK_TIMEOUT_US 40000U
#define UWB_RANGE_RESPONSE_TIMEOUT_US 20000U
#define UWB_RANGE_RESULT_TIMEOUT_US 60000U
/*
 * Contract UWB v1.2 S6.2 nominally schedules FINAL 1500 us after the response
 * RX timestamp, but that is a target for the hardware-timestamp math, not a
 * hard deadline: the actual values embedded in the frame (not this constant)
 * drive the ToF computation. Measured on hardware (receptec-qorvo, 6 clean
 * samples with no extra instrumentation in this path): decoding/validating
 * the RESPONSE and encoding/arming the delayed FINAL consistently takes
 * ~1648 us end to end, so 1500 us left dwt_starttx(DELAYED) a reproducible
 * ~148 us late every time. 3000 us leaves >1300 us of margin and is still far
 * inside the tag's 20 ms wait for FINAL (range_respond_poll) and the
 * contract's 20 ms DS-TWR phase timeout.
 */
#define UWB_RANGE_FINAL_DELAY_US 3000U
#define RANGE_TIMESTAMP_MASK ((1ULL << 40) - 1ULL)

static struct receptec_uwb_range_round_status range_status;
static bool range_log_quiet;
static K_SEM_DEFINE(range_start_sem, 0, 1);
static atomic_t range_stop_requested = ATOMIC_INIT(0);

/* Preserve RX status while waiting for completion; keep the established IRQ mask. */
static int range_wait_tx_done(void)
{
 int64_t deadline = bounded_deadline(k_uptime_get() + 20);
 /* RX may complete before this wait: preserve it for the next phase. */
 while (k_uptime_get() < deadline) {
  uint32_t status = dwt_readsysstatuslo();
  if (status & DWT_INT_TXFRS_BIT_MASK) return 0;
  int64_t remaining = deadline - k_uptime_get();
  if (remaining <= 0) break;
  (void)receptec_radio_wait_event(K_MSEC(remaining));
 }
 return dwt_readsysstatuslo() & DWT_INT_TXFRS_BIT_MASK ? 0 : -ETIMEDOUT;
}

static uint64_t range_timestamp_to_u64(const uint8_t timestamp[5])
{
	uint64_t value = 0U;

	for (int i = 4; i >= 0; --i) {
		value = (value << 8) | timestamp[i];
	}
	return value;
}

static uint64_t range_read_rx_timestamp(void)
{
	uint8_t timestamp[5];

	dwt_readrxtimestamp(timestamp, DWT_COMPAT_NONE);
	return range_timestamp_to_u64(timestamp);
}

static uint64_t range_read_tx_timestamp(void)
{
	uint8_t timestamp[5];

	dwt_readtxtimestamp(timestamp);
	return range_timestamp_to_u64(timestamp);
}

/* DW3xxx time is 499.2 MHz x 128: 1 us = 319488 / 5 full-resolution ticks
 * (us_to_dx_time() above gives the same ratio already divided by 256, for
 * the high-word-only delayed-TX register).
 */
static uint64_t range_us_to_dtu(uint32_t us)
{
	return ((uint64_t)us * 319488ULL + 2ULL) / 5ULL;
}

static void range_put_le(uint8_t *out, uint64_t value, size_t count)
{
	for (size_t i = 0U; i < count; ++i) {
		out[i] = (uint8_t)(value & 0xFFU);
		value >>= 8U;
	}
}

static int range_load_plan_frame(enum receptec_uwb_plan_phase phase,
				 uint8_t pair_index,
				 const uint8_t destination[RECEPTEC_UWB_EUI64_SIZE],
				 const uint8_t initiator[RECEPTEC_UWB_EUI64_SIZE],
				 const uint8_t responder[RECEPTEC_UWB_EUI64_SIZE],
				 uint64_t round_id, uint8_t sequence)
{
	uint8_t plan_bytes[RECEPTEC_UWB_PLAN_SIZE];
	struct receptec_uwb_plan plan = {
		.phase = phase,
		.round_id = round_id,
		.pair_index = pair_index,
		.report_mode = RECEPTEC_UWB_REPORT_AGGREGATE,
	};
	struct receptec_uwb_frame frame = {
		.sequence = sequence,
		.type = RECEPTEC_UWB_RANGE_PLAN,
		.payload = plan_bytes,
		.payload_length = sizeof(plan_bytes),
	};
	size_t logical_length;
	uint16_t radio_length;

	memcpy(plan.initiator, initiator, RECEPTEC_UWB_EUI64_SIZE);
	memcpy(plan.responder, responder, RECEPTEC_UWB_EUI64_SIZE);
	if (receptec_uwb_plan_encode(plan_bytes, &plan) != RECEPTEC_UWB_OK) {
		return -EINVAL;
	}
	memcpy(frame.destination, destination, RECEPTEC_UWB_EUI64_SIZE);
	memcpy(frame.source, receptec_eui, RECEPTEC_UWB_EUI64_SIZE);
	if (receptec_uwb_encode(tx_bytes, sizeof(tx_bytes), &frame,
				&logical_length) != RECEPTEC_UWB_OK) {
		return -EINVAL;
	}
	radio_length = (uint16_t)(logical_length + RECEPTEC_UWB_FCS_SIZE);
	if (dwt_writetxdata(radio_length, tx_bytes, 0U) != DWT_SUCCESS) {
		return -EIO;
	}
	dwt_writetxfctrl(radio_length, 0U, 0U);
	return 0;
}

/* Send one RANGE_PLAN (ROUND_ARM or a per-pair ARM) and wait for its ACK. The
 * ACK is a plain zero-payload ACK exactly like the legacy SYNC/DATA_REQ flow,
 * so the existing control-response matcher applies unchanged.
 */
static int range_send_plan_and_await_ack(uint8_t peer_slot,
					 const uint8_t peer_eui64[RECEPTEC_UWB_EUI64_SIZE],
					 enum receptec_uwb_plan_phase phase,
					 uint8_t pair_index,
					 const uint8_t initiator[RECEPTEC_UWB_EUI64_SIZE],
					 const uint8_t responder[RECEPTEC_UWB_EUI64_SIZE],
					 uint64_t round_id)
{
	uint8_t sequence;
	int status;
	uint32_t ack_wait_us = phase == RECEPTEC_UWB_PLAN_ARM ? UWB_RANGE_PLAN_ACK_TIMEOUT_US : 20000U;
	unsigned int attempts = phase == RECEPTEC_UWB_PLAN_START ? 1 : 2;

	status = receptec_uwb_peer_registry_next_sequence(&peer_registry,
							   peer_slot, &sequence);
	if (status < 0) {
		return status;
	}
	status = range_load_plan_frame(phase, pair_index, peer_eui64, initiator,
				       responder, round_id, sequence);
	if (status < 0) {
		return status;
	}
	/* Retry the identical frame/sequence once, within the caller's absolute
	 * deadline. Duplicate ROUND_ARM/ARM ACKs do not renew authorization. */
	status = -ETIMEDOUT;
	for (unsigned int attempt = 0; attempt < attempts; attempt++) {
	if (cycle_deadline && k_uptime_get() >= cycle_deadline) break;
	dwt_forcetrxoff();
	clear_radio_events(UWB_RADIO_EVENT_MASK | DWT_INT_TXFRS_BIT_MASK);
	dwt_setrxaftertxdelay(0U);
	dwt_setrxtimeout(us_to_rx_timeout(ack_wait_us));
	status = receptec_radio_irq_enable(true);
	if (status < 0) {
		return status;
	}
	if (cycle_deadline && k_uptime_get() >= cycle_deadline) {
		(void)receptec_radio_irq_enable(false); return -ETIMEDOUT;
	}
	if (dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED) !=
	    DWT_SUCCESS) {
		(void)receptec_radio_irq_enable(false);
		return -EIO;
	}
	status = await_matching_control_response(peer_eui64, sequence,
						 RECEPTEC_UWB_ACK,
						 ack_wait_us);
	dwt_forcetrxoff();
	clear_radio_events(UWB_RADIO_EVENT_MASK | DWT_INT_TXFRS_BIT_MASK);
	(void)receptec_radio_irq_enable(false);
	if (status == 0 || status != -ETIMEDOUT) break;
	}
	return status;
}

/* One DS-TWR exchange with this RECEPTEC as initiator: POLL (delayed TX,
 * rounded to the 512-tick boundary every on-air TX timestamp needs per
 * valid_u40_tx()) -> wait RESPONSE -> delayed FINAL at response_rx+1500us.
 * Returns 0 once FINAL is confirmed sent; the result itself arrives later as
 * a RANGE_RESULT uplink, read by the caller.
 */
static int range_run_dstwr_initiator(uint8_t peer_slot,
				     const uint8_t peer_eui64[RECEPTEC_UWB_EUI64_SIZE],
				     uint64_t round_id, uint8_t pair_index)
{
	struct receptec_uwb_frame frame;
	struct receptec_uwb_twr_final final_msg = {
	    .round_id = round_id,
	    .pair_index = pair_index,
	};
	uint8_t poll_payload[RECEPTEC_UWB_POLL_SIZE];
	uint8_t final_payload[RECEPTEC_UWB_FINAL_SIZE];
	uint16_t radio_length;
	size_t logical_length;
	uint64_t now_dtu, poll_target, response_rx_ts, final_target;
	uint8_t sequence;
	int64_t deadline;
	int status;

	status = receptec_uwb_peer_registry_next_sequence(&peer_registry, peer_slot, &sequence);
	if (status < 0) {
		return status;
	}
	range_put_le(poll_payload, round_id, 8U);
	poll_payload[8] = pair_index;
	memset(&frame, 0, sizeof(frame));
	frame.sequence = sequence;
	frame.type = RECEPTEC_UWB_RANGE_POLL;
	frame.payload = poll_payload;
	frame.payload_length = sizeof(poll_payload);
	memcpy(frame.destination, peer_eui64, RECEPTEC_UWB_EUI64_SIZE);
	memcpy(frame.source, receptec_eui, RECEPTEC_UWB_EUI64_SIZE);
	if (receptec_uwb_encode(tx_bytes, sizeof(tx_bytes), &frame, &logical_length) !=
	    RECEPTEC_UWB_OK) {
		return -EINVAL;
	}
	radio_length = (uint16_t)(logical_length + RECEPTEC_UWB_FCS_SIZE);
	if (dwt_writetxdata(radio_length, tx_bytes, 0U) != DWT_SUCCESS) {
		return -EIO;
	}
	dwt_writetxfctrl(radio_length, 0U, 1U);

	dwt_forcetrxoff();
	clear_radio_events(UWB_RADIO_EVENT_MASK | DWT_INT_TXFRS_BIT_MASK);
	dwt_setrxaftertxdelay(0U);
	dwt_setrxtimeout(us_to_rx_timeout(UWB_RANGE_RESPONSE_TIMEOUT_US));
	now_dtu = (uint64_t)dwt_readsystimestamphi32() << 8U;
	poll_target =
	    (now_dtu + range_us_to_dtu(1000U) + 511ULL) & (RANGE_TIMESTAMP_MASK & ~511ULL);
	{
		/*
		 * dwt_setdelayedtrxtime() programs when the PHY starts
		 * transmitting, not the RMARKER; the chip adds antenna delay
		 * on top (confirmed on hardware: the actual TX timestamp
		 * read back after a delayed send was consistently exactly
		 * UWB_TX_ANTENNA_DELAY past the programmed value when this
		 * subtraction was missing). poll_target/final_target stay
		 * the true RMARKER value embedded in the payload; only the
		 * register write is adjusted, mirroring coletec-tag's
		 * range_send_frame().
		 */
		uint64_t poll_raw =
		    (poll_target - UWB_TX_ANTENNA_DELAY + 511ULL) & RANGE_TIMESTAMP_MASK;

		dwt_setdelayedtrxtime((uint32_t)(poll_raw >> 8U));
	}
	status = receptec_radio_irq_enable(true);
	if (status < 0) {
		return status;
	}
	if (cycle_deadline && k_uptime_get() + 4 >= cycle_deadline) {
		(void)receptec_radio_irq_enable(false); return -ETIMEDOUT;
	}
	if (dwt_starttx(DWT_START_TX_DELAYED | DWT_RESPONSE_EXPECTED) != DWT_SUCCESS) {
		(void)receptec_radio_irq_enable(false);
		return -ETIME;
	}
	final_msg.poll_tx_ts = poll_target;

	deadline = bounded_deadline(k_uptime_get() + UWB_RANGE_RESPONSE_TIMEOUT_US / 1000 + 20);
	status = -ETIMEDOUT;
	for (;;) {
		int64_t remaining_ms = deadline - k_uptime_get();
		uint32_t radio_status;
		uint16_t radio_len;
		uint8_t ranging;
		struct receptec_uwb_twr_response response;

		if (remaining_ms <= 0) {
			break;
		}
		(void)receptec_radio_wait_event(K_MSEC(remaining_ms));
		radio_status = dwt_readsysstatuslo();
		if ((radio_status & DWT_INT_RXFCG_BIT_MASK) != 0U) {
			clear_radio_events(DWT_INT_RXFCG_BIT_MASK);
			radio_len = dwt_getframelength(&ranging);
			if (radio_len < RECEPTEC_UWB_HEADER_SIZE + RECEPTEC_UWB_FCS_SIZE ||
			    radio_len > RECEPTEC_UWB_MAX_PSDU_SIZE) {
				continue;
			}
			dwt_readrxdata(rx_bytes, radio_len - RECEPTEC_UWB_FCS_SIZE, 0U);
			if (receptec_uwb_decode(rx_bytes, radio_len - RECEPTEC_UWB_FCS_SIZE,
						&frame) != RECEPTEC_UWB_OK ||
			    frame.type != RECEPTEC_UWB_RANGE_RESPONSE ||
			    memcmp(frame.source, peer_eui64, RECEPTEC_UWB_EUI64_SIZE) != 0 ||
			    memcmp(frame.destination, receptec_eui, RECEPTEC_UWB_EUI64_SIZE) != 0 ||
			    receptec_uwb_twr_response_decode(frame.payload, frame.payload_length,
							     &response) != RECEPTEC_UWB_OK ||
			    response.round_id != round_id || response.pair_index != pair_index) {
				/*
				 * A rejected frame leaves the radio idle: DW3xxx
				 * does not auto-re-arm RX after an RXFCG event, so
				 * without this the real RESPONSE would never be
				 * heard once anything else (e.g. a stale RESULT
				 * retry from a previous round) arrives first.
				 */
				(void)dwt_rxenable(DWT_START_RX_IMMEDIATE);
				continue;
			}
			response_rx_ts = range_read_rx_timestamp();
			status = 0;
			break;
		}
		if ((radio_status & SYS_STATUS_ALL_RX_TO) != 0U ||
		    (radio_status & UWB_RX_REARM_ERROR_MASK) != 0U) {
			clear_radio_events(radio_status &
					   (SYS_STATUS_ALL_RX_TO | UWB_RX_REARM_ERROR_MASK));
			break;
		}
	}
	dwt_forcetrxoff();
	clear_radio_events(UWB_RADIO_EVENT_MASK | DWT_INT_TXFRS_BIT_MASK);
	if (status < 0) {
		(void)receptec_radio_irq_enable(false);
		return status;
	}

	final_msg.response_rx_ts = response_rx_ts;
	final_target = (response_rx_ts + range_us_to_dtu(UWB_RANGE_FINAL_DELAY_US) + 511ULL) &
		       (RANGE_TIMESTAMP_MASK & ~511ULL);
	final_msg.final_tx_ts = final_target;
	if (receptec_uwb_twr_final_encode(final_payload, &final_msg) != RECEPTEC_UWB_OK) {
		(void)receptec_radio_irq_enable(false);
		return -EINVAL;
	}
	memset(&frame, 0, sizeof(frame));
	frame.sequence = sequence;
	frame.type = RECEPTEC_UWB_RANGE_FINAL;
	frame.payload = final_payload;
	frame.payload_length = sizeof(final_payload);
	memcpy(frame.destination, peer_eui64, RECEPTEC_UWB_EUI64_SIZE);
	memcpy(frame.source, receptec_eui, RECEPTEC_UWB_EUI64_SIZE);
	if (receptec_uwb_encode(tx_bytes, sizeof(tx_bytes), &frame, &logical_length) !=
	    RECEPTEC_UWB_OK) {
		(void)receptec_radio_irq_enable(false);
		return -EINVAL;
	}
	radio_length = (uint16_t)(logical_length + RECEPTEC_UWB_FCS_SIZE);
	if (dwt_writetxdata(radio_length, tx_bytes, 0U) != DWT_SUCCESS) {
		(void)receptec_radio_irq_enable(false);
		return -EIO;
	}
	/* Contract UWB v1.2 S3.2: ranging bit set for RANGE_FINAL. */
	dwt_writetxfctrl(radio_length, 0U, 1U);
	/*
	 * forcetrxoff()+clear right before the delayed-time write/starttx,
	 * mirroring the POLL send above: without it dwt_starttx(DELAYED)
	 * refused every FINAL here, even with the target safely in the
	 * future (the RESPONSE wait loop leaves more software work between
	 * the last forcetrxoff() and this starttx() than POLL's path does).
	 */
	dwt_forcetrxoff();
	clear_radio_events(UWB_RADIO_EVENT_MASK | DWT_INT_TXFRS_BIT_MASK);
	{
		/* See the matching comment on the POLL send above. */
		uint64_t final_raw =
		    (final_target - UWB_TX_ANTENNA_DELAY + 511ULL) & RANGE_TIMESTAMP_MASK;

		dwt_setdelayedtrxtime((uint32_t)(final_raw >> 8U));
	}
	dwt_setrxaftertxdelay(0U);
	dwt_setrxtimeout(us_to_rx_timeout(UWB_RANGE_RESULT_TIMEOUT_US));
	if (cycle_deadline && k_uptime_get() + 4 >= cycle_deadline) return -ETIMEDOUT;
	if (cycle_deadline && k_uptime_get() + 4 >= cycle_deadline) {
		(void)receptec_radio_irq_enable(false); return -ETIMEDOUT;
	}
	if (dwt_starttx(DWT_START_TX_DELAYED | DWT_RESPONSE_EXPECTED) != DWT_SUCCESS) {
		(void)receptec_radio_irq_enable(false);
		return -ETIME;
	}
	(void)range_wait_tx_done();
	if ((dwt_readsysstatuslo() & DWT_INT_TXFRS_BIT_MASK) == 0U) {
		dwt_forcetrxoff();
		clear_radio_events(UWB_RADIO_EVENT_MASK | DWT_INT_TXFRS_BIT_MASK);
		(void)receptec_radio_irq_enable(false);
		return -ETIMEDOUT;
	}
	/* Confirm the delayed TX actually landed on its programmed timestamp,
	 * the same check coletec-tag's range_send_frame() makes: a mismatch
	 * here means the radio missed the slot even though starttx() accepted it.
	 */
	if (((range_read_tx_timestamp() ^ final_target) & (RANGE_TIMESTAMP_MASK & ~511ULL)) != 0U) {
		clear_radio_events(DWT_INT_TXFRS_BIT_MASK);
		(void)receptec_radio_irq_enable(false);
		return -ETIME;
	}
	clear_radio_events(DWT_INT_TXFRS_BIT_MASK);
	(void)receptec_radio_irq_enable(false);
	return 0;
}

/* Plain zero-payload ACK, matching the uplink's sequence/EUIs (contract S4:
 * ACK of RANGE_RESULT means the frame was received and validated).
 */
static void range_send_result_ack(const uint8_t peer_eui64[RECEPTEC_UWB_EUI64_SIZE],
				  uint8_t sequence)
{
	struct receptec_uwb_frame ack = {
	    .sequence = sequence,
	    .type = RECEPTEC_UWB_ACK,
	};
	size_t ack_length;
	uint16_t ack_radio_length;

	memcpy(ack.destination, peer_eui64, RECEPTEC_UWB_EUI64_SIZE);
	memcpy(ack.source, receptec_eui, RECEPTEC_UWB_EUI64_SIZE);
	if (receptec_uwb_encode(tx_bytes, sizeof(tx_bytes), &ack, &ack_length) != RECEPTEC_UWB_OK) {
		return;
	}
	ack_radio_length = (uint16_t)(ack_length + RECEPTEC_UWB_FCS_SIZE);
	if (dwt_writetxdata(ack_radio_length, tx_bytes, 0U) != DWT_SUCCESS) {
		return;
	}
	dwt_writetxfctrl(ack_radio_length, 0U, 0U);
	dwt_forcetrxoff();
	clear_radio_events(UWB_RADIO_EVENT_MASK | DWT_INT_TXFRS_BIT_MASK);
	if (cycle_deadline && k_uptime_get() >= cycle_deadline) return;
	if (dwt_starttx(DWT_START_TX_IMMEDIATE) == DWT_SUCCESS) {
		(void)range_wait_tx_done();
		clear_radio_events(DWT_INT_TXFRS_BIT_MASK);
	}
}

/* Wait for the responder's RANGE_RESULT uplink for this round's pair. */
static int range_await_result(uint8_t peer_slot, const uint8_t peer_eui64[RECEPTEC_UWB_EUI64_SIZE],
			      uint64_t round_id, uint8_t pair_index,
			      enum receptec_uwb_range_status *out_status, uint32_t *out_distance_mm)
{
	struct receptec_uwb_frame frame;
	struct receptec_uwb_result result;
	int64_t deadline = bounded_deadline(k_uptime_get() + UWB_RANGE_RESULT_TIMEOUT_US / 1000);
	int status = -ETIMEDOUT;

	/* FINAL armed RX in hardware. Keep an early RESULT and its RX timestamp
	 * across the handoff instead of clearing a frame that already arrived. */
	status = receptec_radio_irq_enable(true);
	if (status < 0) return status;
	status = -ETIMEDOUT;
	for (;;) {
		int64_t remaining_ms = deadline - k_uptime_get();
		uint32_t radio_status;
		uint16_t radio_len;
		uint8_t ranging;

		if (remaining_ms <= 0) {
			break;
		}
		(void)receptec_radio_wait_event(K_MSEC(remaining_ms));
		radio_status = dwt_readsysstatuslo();
		if ((radio_status & DWT_INT_RXFCG_BIT_MASK) != 0U) {
			clear_radio_events(DWT_INT_RXFCG_BIT_MASK);
			radio_len = dwt_getframelength(&ranging);
			if (radio_len < RECEPTEC_UWB_HEADER_SIZE + RECEPTEC_UWB_FCS_SIZE ||
			    radio_len > RECEPTEC_UWB_MAX_PSDU_SIZE) {
				(void)dwt_rxenable(DWT_START_RX_IMMEDIATE);
				continue;
			}
			dwt_readrxdata(rx_bytes, radio_len - RECEPTEC_UWB_FCS_SIZE, 0U);
			if (receptec_uwb_decode(rx_bytes, radio_len - RECEPTEC_UWB_FCS_SIZE,
						&frame) != RECEPTEC_UWB_OK ||
			    frame.type != RECEPTEC_UWB_RANGE_RESULT ||
			    memcmp(frame.source, peer_eui64, RECEPTEC_UWB_EUI64_SIZE) != 0 ||
			    memcmp(frame.destination, receptec_eui, RECEPTEC_UWB_EUI64_SIZE) != 0 ||
			    receptec_uwb_result_decode(frame.payload, frame.payload_length,
						       &result) != RECEPTEC_UWB_OK ||
			    result.round_id != round_id || result.record_count != 1U ||
			    result.records[0].pair_index != pair_index) {
				/* See the same re-arm note in range_run_dstwr_initiator(). */
				(void)dwt_rxenable(DWT_START_RX_IMMEDIATE);
				continue;
			}
			*out_status = result.records[0].status;
			*out_distance_mm = result.records[0].distance_mm;
			status = 0;
			break;
		}
		if ((radio_status & SYS_STATUS_ALL_RX_TO) != 0U ||
		    (radio_status & UWB_RX_REARM_ERROR_MASK) != 0U) {
			clear_radio_events(radio_status &
					   (SYS_STATUS_ALL_RX_TO | UWB_RX_REARM_ERROR_MASK));
			break;
		}
	}
	if (status == 0) {
		range_send_result_ack(peer_eui64, frame.sequence);
	}
	dwt_forcetrxoff();
	clear_radio_events(UWB_RADIO_EVENT_MASK | DWT_INT_TXFRS_BIT_MASK);
	(void)receptec_radio_irq_enable(false);
	return status;
}

/*
 * Passively wait for a RANGE_RESULT from `peer_eui64` for `pair_index`,
 * without having initiated anything this round -- used on the master to
 * see the A-T pair's result, which contract UWB v1.2 S2.1 always
 * addresses to the master even though B (not A) initiated that pair.
 * Unlike range_await_result(), there is no round_id to match: the master
 * never participated in B's ROUND_ARM/ARM for a pair it did not initiate,
 * so it accepts whichever round_id the result carries.
 */
static int range_listen_once(const uint8_t peer_eui64[RECEPTEC_UWB_EUI64_SIZE],
			     uint8_t pair_index, uint32_t timeout_ms,
			     enum receptec_uwb_range_status *out_status,
			     uint32_t *out_distance_mm, uint64_t *out_round_id,
			     uint64_t expected_round, const uint8_t *expected_initiator)
{
	struct receptec_uwb_frame frame;
	struct receptec_uwb_result result;
	int64_t deadline = bounded_deadline(k_uptime_get() + timeout_ms);
	int status;

	dwt_forcetrxoff();
	clear_radio_events(UWB_RADIO_EVENT_MASK | DWT_INT_TXFRS_BIT_MASK);
	dwt_setrxaftertxdelay(0U);
	dwt_setrxtimeout(0U);
	status = receptec_radio_irq_enable(true);
	if (status < 0) {
		return status;
	}
	if (dwt_rxenable(DWT_START_RX_IMMEDIATE) != DWT_SUCCESS) {
		(void)receptec_radio_irq_enable(false);
		return -EIO;
	}
	status = -ETIMEDOUT;
	for (;;) {
		int64_t remaining_ms = deadline - k_uptime_get();
		uint32_t radio_status;
		uint16_t radio_len;
		uint8_t ranging;

		if (remaining_ms <= 0) {
			break;
		}
		(void)receptec_radio_wait_event(K_MSEC(remaining_ms));
		if (k_uptime_get() >= deadline) break;
		radio_status = dwt_readsysstatuslo();
		if ((radio_status & SYS_STATUS_ALL_RX_TO) != 0U ||
		    (radio_status & UWB_RX_REARM_ERROR_MASK) != 0U) {
			clear_radio_events(radio_status &
					   (SYS_STATUS_ALL_RX_TO | UWB_RX_REARM_ERROR_MASK));
			(void)dwt_rxenable(DWT_START_RX_IMMEDIATE);
			continue;
		}
		if ((radio_status & DWT_INT_RXFCG_BIT_MASK) == 0U) {
			continue;
		}
		clear_radio_events(DWT_INT_RXFCG_BIT_MASK);
		radio_len = dwt_getframelength(&ranging);
		if (radio_len < RECEPTEC_UWB_HEADER_SIZE + RECEPTEC_UWB_FCS_SIZE ||
		    radio_len > RECEPTEC_UWB_MAX_PSDU_SIZE) {
			(void)dwt_rxenable(DWT_START_RX_IMMEDIATE);
			continue;
		}
		dwt_readrxdata(rx_bytes, radio_len - RECEPTEC_UWB_FCS_SIZE, 0U);
		if (receptec_uwb_decode(rx_bytes, radio_len - RECEPTEC_UWB_FCS_SIZE,
					&frame) != RECEPTEC_UWB_OK ||
		    frame.type != RECEPTEC_UWB_RANGE_RESULT ||
		    memcmp(frame.source, peer_eui64, RECEPTEC_UWB_EUI64_SIZE) != 0 ||
		    memcmp(frame.destination, receptec_eui, RECEPTEC_UWB_EUI64_SIZE) != 0 ||
		    receptec_uwb_result_decode(frame.payload, frame.payload_length,
					       &result) != RECEPTEC_UWB_OK ||
		    result.record_count != 1U ||
		    result.records[0].pair_index != pair_index ||
		    (expected_round && (result.round_id != expected_round ||
		     result.records[0].master_rx_time_ms != 0 ||
		     memcmp(result.records[0].responder, peer_eui64, 8) != 0 ||
		     memcmp(result.records[0].initiator, expected_initiator, 8) != 0))) {
			(void)dwt_rxenable(DWT_START_RX_IMMEDIATE);
			continue;
		}
		*out_status = result.records[0].status;
		*out_distance_mm = result.records[0].distance_mm;
		*out_round_id = result.round_id;
		status = 0;
#if defined(CONFIG_BT)
		/* Overheard pair (e.g. A-T uplinked to us): same dashboard feed. */
		receptec_ble_range_publish_extended(pair_index,
					   *out_status == 0 ? 0 : 2, *out_status, result.round_id,
					   *out_status == RECEPTEC_UWB_RANGE_OK ?
					   *out_distance_mm :
					   RECEPTEC_UWB_DISTANCE_INVALID);
#endif
		break;
	}
	if (status == 0) {
		range_send_result_ack(peer_eui64, frame.sequence);
	}
	dwt_forcetrxoff();
	clear_radio_events(UWB_RADIO_EVENT_MASK | DWT_INT_TXFRS_BIT_MASK);
	(void)receptec_radio_irq_enable(false);
	return status;
}

static uint64_t range_get_le(const uint8_t *in, size_t count)
{
	uint64_t value = 0U;

	for (size_t i = count; i > 0U; --i) {
		value = (value << 8) | in[i - 1U];
	}
	return value;
}

/*
 * Generic frame send, delayed or immediate, with the antenna-delay
 * compensation and TX-landed verification already proven on this board by
 * the POLL/FINAL sends above (dwt_writetxdata -> dwt_writetxfctrl ->
 * forcetrxoff()+clear right before arming the delayed time -> starttx).
 * Unlike coletec-tag's DW3720, this board's DW3110 accepts the separate
 * dwt_writetxfctrl()+dwt_setdelayedtrxtime() calls for delayed TX.
 */
static int range_send_frame(enum receptec_uwb_packet_type type,
			    const uint8_t destination[RECEPTEC_UWB_EUI64_SIZE], uint8_t sequence,
			    const uint8_t *payload, size_t payload_length, bool ranging,
			    bool delayed, uint64_t target_ts, uint64_t *actual_ts,
			    bool response_expected)
{
	struct receptec_uwb_frame frame = {
	    .sequence = sequence,
	    .type = type,
	    .payload = payload,
	    .payload_length = payload_length,
	};
	size_t logical_length;
	uint16_t radio_length;

	memcpy(frame.destination, destination, RECEPTEC_UWB_EUI64_SIZE);
	memcpy(frame.source, receptec_eui, RECEPTEC_UWB_EUI64_SIZE);
	if (receptec_uwb_encode(tx_bytes, sizeof(tx_bytes), &frame, &logical_length) !=
	    RECEPTEC_UWB_OK) {
		return -EINVAL;
	}
	radio_length = (uint16_t)(logical_length + RECEPTEC_UWB_FCS_SIZE);
	if (dwt_writetxdata(radio_length, tx_bytes, 0U) != DWT_SUCCESS) {
		return -EIO;
	}
	dwt_writetxfctrl(radio_length, 0U, ranging ? 1U : 0U);
	dwt_forcetrxoff();
	clear_radio_events(UWB_RADIO_EVENT_MASK | DWT_INT_TXFRS_BIT_MASK);
	if (delayed) {
		uint64_t raw = (target_ts - UWB_TX_ANTENNA_DELAY + 511ULL) & RANGE_TIMESTAMP_MASK;

		dwt_setdelayedtrxtime((uint32_t)(raw >> 8U));
	}
	if (dwt_starttx((delayed ? DWT_START_TX_DELAYED : DWT_START_TX_IMMEDIATE) |
			(response_expected ? DWT_RESPONSE_EXPECTED : 0)) != DWT_SUCCESS) {
		return -ETIME;
	}
	(void)range_wait_tx_done();
	if ((dwt_readsysstatuslo() & DWT_INT_TXFRS_BIT_MASK) == 0U) {
		dwt_forcetrxoff();
		clear_radio_events(UWB_RADIO_EVENT_MASK | DWT_INT_TXFRS_BIT_MASK);
		return -ETIMEDOUT;
	}
	if (actual_ts != NULL) {
		*actual_ts = range_read_tx_timestamp();
		if (delayed &&
		    ((*actual_ts ^ target_ts) & (RANGE_TIMESTAMP_MASK & ~511ULL)) != 0U) {
			clear_radio_events(DWT_INT_TXFRS_BIT_MASK);
			return -ETIME;
		}
	}
	clear_radio_events(DWT_INT_TXFRS_BIT_MASK);
	return 0;
}

/* ACK, 1 ms after the confirmed frame's RX timestamp (contract UWB v1.2 S4). */
/*
 * Contract nominal is 1 ms (UWB v1.2 S4), but 1000us left dwt_starttx(DELAYED)
 * rejected every time on hardware (ack_rc=-ETIME, confirmed with no other
 * instrumentation in the path) -- the same symptom, same root cause (not
 * enough margin for decode+validate+encode before the delayed TX arms), as
 * the two prior ACK/FINAL margin bugs fixed earlier in this feature
 * (coletec-tag's range_ack() and this file's own FINAL send). 2000us is the
 * same proven value coletec-tag's range_ack() uses.
 */
static int range_send_ack(const uint8_t destination[RECEPTEC_UWB_EUI64_SIZE],
			  uint8_t sequence, uint64_t request_rx_ts,
			  bool response_expected)
{
	uint64_t target = (request_rx_ts + range_us_to_dtu(2000U) + 511ULL) &
			  (RANGE_TIMESTAMP_MASK & ~511ULL);

	return range_send_frame(RECEPTEC_UWB_ACK, destination, sequence, NULL,
				0U, false, true, target, NULL, response_expected);
}

/*
 * One full DS-TWR round as RESPONDER (contract UWB v1.2 S5-S9): accept
 * ROUND_ARM then ARM for `pair_index` from the registered master peer,
 * then POLL -> RESPONSE, then FINAL -> compute distance
 * (receptec_uwb_distance_mm(), already implemented and tested but never
 * called in production before this) -> RANGE_RESULT uplink to the master.
 *
 * This is the first responder-side implementation in the RECEPTEC codebase:
 * until now only coletec-tag (the COLETEC tag, which is always a responder
 * for M-T and A-T) had this half of the protocol. Pair M-A (pair_index=2,
 * initiator=M, responder=A) needs a RECEPTEC-family board to play
 * responder, which is why this exists. Same proof-of-concept scope as the
 * initiator side: one round, no retry, software deadline instead of the
 * contract's exact per-phase windows.
 */
static int range_respond_once(uint8_t master_slot, uint8_t pair_index, uint32_t timeout_ms,
			      enum receptec_uwb_range_status *out_status, uint32_t *out_distance_mm)
{
	uint8_t master_eui64[RECEPTEC_UWB_EUI64_SIZE];
	struct receptec_uwb_frame frame;
	uint64_t round_id = 0U;
	bool have_round = false;
	bool delegated = false;
	bool delegate_armed = false;
	uint8_t delegate_peer[8] = {0};
	int64_t delegate_arm_ms = 0;
	bool armed = false;
	int64_t arm_ms = 0;
	uint64_t poll_rx_ts = 0U, response_actual_ts = 0U;
	struct receptec_uwb_twr_response response_msg = {0};
	int64_t deadline;
	int status;

	if (receptec_uwb_peer_registry_get(&peer_registry, master_slot, master_eui64) < 0) {
		return -ENOENT;
	}
	*out_status = RECEPTEC_UWB_RANGE_NO_RESULT;
	*out_distance_mm = RECEPTEC_UWB_DISTANCE_INVALID;

	dwt_forcetrxoff();
	clear_radio_events(UWB_RADIO_EVENT_MASK | DWT_INT_TXFRS_BIT_MASK);
	dwt_setrxaftertxdelay(0U);
	dwt_setrxtimeout(0U);
	status = receptec_radio_irq_enable(true);
	if (status < 0) {
		return status;
	}
	if (dwt_rxenable(DWT_START_RX_IMMEDIATE) != DWT_SUCCESS) {
		(void)receptec_radio_irq_enable(false);
		return -EIO;
	}

	deadline = k_uptime_get() + timeout_ms;
	status = -ETIMEDOUT;
	for (;;) {
		int64_t remaining_ms = deadline - k_uptime_get();
		uint32_t radio_status;
		uint16_t radio_len;
		uint8_t ranging;
		uint64_t rx_ts;

		if (remaining_ms <= 0) {
			break;
		}
		if (receptec_round_arm_expired(armed, arm_ms, k_uptime_get())) {
			armed = false;
		}
		if (armed) {
			/* Wake to expire an abandoned exchange even on quiet air. */
			remaining_ms = MIN(remaining_ms, arm_ms + RECEPTEC_UWB_ARM_WINDOW_MS + 1 -
							     k_uptime_get());
			if (remaining_ms <= 0) {
				continue;
			}
		}
		if (!(dwt_readsysstatuslo() & DWT_INT_RXFCG_BIT_MASK))
			(void)receptec_radio_wait_event(K_MSEC(remaining_ms));
		if (receptec_round_arm_expired(armed, arm_ms, k_uptime_get())) {
			armed = false;
		}
		radio_status = dwt_readsysstatuslo();
		if ((radio_status & SYS_STATUS_ALL_RX_TO) != 0U ||
		    (radio_status & UWB_RX_REARM_ERROR_MASK) != 0U) {
			clear_radio_events(radio_status &
					   (SYS_STATUS_ALL_RX_TO | UWB_RX_REARM_ERROR_MASK));
			(void)dwt_rxenable(DWT_START_RX_IMMEDIATE);
			continue;
		}
		if ((radio_status & DWT_INT_RXFCG_BIT_MASK) == 0U) {
			continue;
		}
		clear_radio_events(DWT_INT_RXFCG_BIT_MASK);
		radio_len = dwt_getframelength(&ranging);
		if (radio_len < RECEPTEC_UWB_HEADER_SIZE + RECEPTEC_UWB_FCS_SIZE ||
		    radio_len > RECEPTEC_UWB_MAX_PSDU_SIZE) {
			(void)dwt_rxenable(DWT_START_RX_IMMEDIATE);
			continue;
		}
		rx_ts = range_read_rx_timestamp();
		dwt_readrxdata(rx_bytes, radio_len - RECEPTEC_UWB_FCS_SIZE, 0U);
		{
			int decode_rc = receptec_uwb_decode(
			    rx_bytes, radio_len - RECEPTEC_UWB_FCS_SIZE, &frame);

			if (decode_rc != RECEPTEC_UWB_OK ||
			    memcmp(frame.source, master_eui64, RECEPTEC_UWB_EUI64_SIZE) != 0 ||
			    memcmp(frame.destination, receptec_eui, RECEPTEC_UWB_EUI64_SIZE) != 0) {
				(void)dwt_rxenable(DWT_START_RX_IMMEDIATE);
				continue;
			}
		}

		if (frame.type == RECEPTEC_UWB_RANGE_PLAN) {
			struct receptec_uwb_plan plan;

			if (receptec_uwb_plan_decode(frame.payload, frame.payload_length, &plan) !=
				RECEPTEC_UWB_OK) {
				(void)dwt_rxenable(DWT_START_RX_IMMEDIATE);
				continue;
			}
			/* M delegates A-T to this anchor. T reports the official distance
			 * directly to M; the anchor never publishes a duplicate result. */
			if ((plan.phase == RECEPTEC_UWB_PLAN_ARM || plan.phase == RECEPTEC_UWB_PLAN_START) &&
			    plan.report_mode == RECEPTEC_UWB_REPORT_AGGREGATE && plan.pair_index == 1 &&
			    have_round && !armed && plan.round_id == round_id &&
			    memcmp(plan.initiator, receptec_eui, 8) == 0) {
				uint8_t tag_slot = RECEPTEC_UWB_MAX_PEERS;
				k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
				for (uint8_t slot = 0; slot < RECEPTEC_UWB_MAX_PEERS; slot++) {
					uint8_t eui[8];
					if (receptec_uwb_peer_registry_get(&peer_registry, slot, eui) == 0 &&
					    memcmp(eui, plan.responder, 8) == 0 &&
					    memcmp(eui, master_eui64, 8) != 0 &&
					    memcmp(eui, receptec_eui, 8) != 0) tag_slot = slot;
				}
				k_mutex_unlock(&uwb_peer_mutex);
				if (tag_slot < RECEPTEC_UWB_MAX_PEERS) {
					bool identity_match = !delegate_armed || memcmp(delegate_peer, plan.responder, 8) == 0;
					enum receptec_uwb_delegate_action action = receptec_uwb_delegate_decide(
						identity_match, have_round, plan.pair_index, plan.phase,
						round_id, plan.round_id, delegate_armed, delegated,
						delegate_arm_ms, k_uptime_get());
					if (action == RECEPTEC_UWB_DELEGATE_ARM) {
						delegate_armed = true;
						delegate_arm_ms = k_uptime_get();
						memcpy(delegate_peer, plan.responder, 8);
					}
					bool rearm = action == RECEPTEC_UWB_DELEGATE_RUN || action == RECEPTEC_UWB_DELEGATE_IGNORE;
					if (action != RECEPTEC_UWB_DELEGATE_IGNORE) {
						int ret = range_send_ack(master_eui64, frame.sequence, rx_ts, true);
						rearm = rearm || ret < 0;
						k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
						cycle_last_error[1] = ret;
						cycle_last_phase[1] = plan.phase + 10;
						k_mutex_unlock(&uwb_peer_mutex);
						/* A valid START authorizes execution even if its ACK TX
						 * misses the delayed margin; M keeps the result window open. */
						if (action == RECEPTEC_UWB_DELEGATE_RUN) {
							delegated = true;
							cycle_deadline = delegate_arm_ms + 200;
							ret = range_run_dstwr_initiator(tag_slot, delegate_peer, round_id, 1);
							cycle_deadline = 0;
							k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
							cycle_last_error[1] = ret;
							cycle_last_phase[1] = 2;
							if (ret == 0) cycle_success[1]++;
							else cycle_failure[1]++;
							k_mutex_unlock(&uwb_peer_mutex);
						}
					}
					(void)receptec_radio_irq_enable(true);
					if (rearm)
						(void)dwt_rxenable(DWT_START_RX_IMMEDIATE);
					continue;
				}
			}
			if (memcmp(plan.responder, receptec_eui, 8) != 0) {
				(void)dwt_rxenable(DWT_START_RX_IMMEDIATE);
				continue;
			}
			if (plan.phase == RECEPTEC_UWB_PLAN_ROUND_ARM) {
				int ack_rc;

				/*
				 * Only the named recipient accepts a ROUND_ARM.
				 * Anything else on the air (e.g. the master's
				 * copy for another peer) is ignored without an
				 * ACK, so two responders never answer the same
				 * frame.
				 */
				if (memcmp(plan.responder, receptec_eui, RECEPTEC_UWB_EUI64_SIZE) !=
				    0) {
					(void)dwt_rxenable(DWT_START_RX_IMMEDIATE);
					continue;
				}
				if (memcmp(plan.initiator, master_eui64, RECEPTEC_UWB_EUI64_SIZE) ==
				    0) {
					enum receptec_round_arm_action action =
					    receptec_round_arm_action(have_round, armed, round_id,
								      plan.round_id);

					if (action == RECEPTEC_ROUND_IGNORE_BUSY) {
						(void)dwt_rxenable(DWT_START_RX_IMMEDIATE);
						continue;
					}
					if (action == RECEPTEC_ROUND_ADOPT) {
						round_id = plan.round_id;
						have_round = true;
						delegated = false;
						delegate_armed = false;
						armed = false;
						deadline = k_uptime_get() + 1100;
					}
				}
				/*
				 * response_expected=true: DWT_RESPONSE_EXPECTED arms
				 * RX in hardware the instant this delayed ACK's TX
				 * completes. A manual dwt_rxenable() after the fact
				 * was too slow -- the master, auto-armed the same
				 * way after its own TX, reacts fast enough that the
				 * follow-up ARM consistently arrived before this
				 * software's re-arm ran (confirmed: ack always
				 * succeeded, but zero ARM frames were ever seen).
				 */
				ack_rc = range_send_ack(master_eui64, frame.sequence, rx_ts, true);
				if (ack_rc < 0) {
					(void)dwt_rxenable(DWT_START_RX_IMMEDIATE);
				}
				continue;
			}
			if (plan.phase != RECEPTEC_UWB_PLAN_ARM || !have_round ||
			    plan.round_id != round_id || plan.pair_index != pair_index ||
			    memcmp(plan.initiator, master_eui64, RECEPTEC_UWB_EUI64_SIZE) != 0) {
				(void)dwt_rxenable(DWT_START_RX_IMMEDIATE);
				continue;
			}
			if (!armed) {
				arm_ms = k_uptime_get();
				armed = true;
			}
			if (range_send_ack(master_eui64, frame.sequence, rx_ts, true) < 0) {
				(void)dwt_rxenable(DWT_START_RX_IMMEDIATE);
			}
			continue;
		}

		if (frame.type == RECEPTEC_UWB_RANGE_POLL && armed) {
			uint8_t response_payload[RECEPTEC_UWB_RESPONSE_SIZE];
			uint64_t response_target;

			if (frame.payload_length != RECEPTEC_UWB_POLL_SIZE ||
			    range_get_le(frame.payload, 8U) != round_id ||
			    frame.payload[8] != pair_index) {
				(void)dwt_rxenable(DWT_START_RX_IMMEDIATE);
				continue;
			}
			poll_rx_ts = rx_ts;
			/*
			 * Contract nominal is 1500us (S6.2), but that's a target for
			 * the ToF math, not a hard deadline (same note as
			 * UWB_RANGE_FINAL_DELAY_US above). Confirmed on hardware:
			 * dwt_starttx(DELAYED) for RESPONSE was rejected every single
			 * time at 1500us; reusing the already-proven 3000us budget
			 * from this file's own FINAL send fixes it.
			 */
			response_target =
			    (poll_rx_ts + range_us_to_dtu(UWB_RANGE_FINAL_DELAY_US) + 511ULL) &
			    (RANGE_TIMESTAMP_MASK & ~511ULL);
			response_msg.round_id = round_id;
			response_msg.pair_index = pair_index;
			response_msg.poll_rx_ts = poll_rx_ts;
			response_msg.response_tx_ts = response_target;
			if (receptec_uwb_twr_response_encode(response_payload, &response_msg) !=
			    RECEPTEC_UWB_OK) {
				(void)dwt_rxenable(DWT_START_RX_IMMEDIATE);
				continue;
			}
			if (range_send_frame(RECEPTEC_UWB_RANGE_RESPONSE, master_eui64,
					     frame.sequence, response_payload,
					     sizeof(response_payload), true, true, response_target,
					     &response_actual_ts, true) < 0) {
				*out_status = RECEPTEC_UWB_RANGE_RADIO_LATE;
				(void)dwt_rxenable(DWT_START_RX_IMMEDIATE);
				continue;
			}
			/*
			 * Keep response_msg.response_tx_ts as the aligned target
			 * (already assigned above, matching what the wire payload
			 * carries) -- NOT response_actual_ts. receptec_uwb_distance_mm()
			 * requires response_tx_ts to pass valid_u40_tx() (bits 8:0
			 * zero); the raw hardware TX readback almost never is
			 * (confirmed: distance_rc=-1/ERR_ARGUMENT every time while
			 * this overwrote it with the unaligned actual timestamp).
			 * coletec_uwb.c's range_respond_poll() keeps the same
			 * distinction (wire/record value vs. its own actual-ts local
			 * var, which its distance function has no such gate on).
			 */
			continue;
		}

		if (frame.type == RECEPTEC_UWB_RANGE_FINAL && armed && response_actual_ts != 0U) {
			struct receptec_uwb_twr_final final_msg;
			struct receptec_uwb_result result = {
			    .round_id = round_id,
			    .report_mode = RECEPTEC_UWB_REPORT_AGGREGATE,
			    .record_count = 1U,
			};
			uint8_t result_payload[RECEPTEC_UWB_RESULT_HEADER_SIZE +
					       RECEPTEC_UWB_RESULT_RECORD_SIZE];
			size_t result_length;
			uint32_t distance_mm = 0U;
			enum receptec_uwb_range_status final_status;

			if (receptec_uwb_twr_final_decode(frame.payload, frame.payload_length,
							  &final_msg) != RECEPTEC_UWB_OK ||
			    final_msg.round_id != round_id || final_msg.pair_index != pair_index) {
				(void)dwt_rxenable(DWT_START_RX_IMMEDIATE);
				continue;
			}
			final_status = receptec_uwb_distance_mm(&response_msg, &final_msg, rx_ts,
								&distance_mm) < 0
					   ? RECEPTEC_UWB_RANGE_BAD_MEASUREMENT
					   : RECEPTEC_UWB_RANGE_OK;
			result.records[0].pair_index = pair_index;
			memcpy(result.records[0].initiator, master_eui64, RECEPTEC_UWB_EUI64_SIZE);
			memcpy(result.records[0].responder, receptec_eui, RECEPTEC_UWB_EUI64_SIZE);
			result.records[0].status = final_status;
			result.records[0].distance_mm = final_status == RECEPTEC_UWB_RANGE_OK
							    ? distance_mm
							    : RECEPTEC_UWB_DISTANCE_INVALID;
			/*
			 * Contract UWB v1.2 S4: RANGE_RESULT gets up to two total
			 * transmissions, each followed by a short wait for its ACK.
			 * The first cut of this uplink sent once and ignored the
			 * return value entirely -- coletec_uwb.c's range_report()
			 * already does the two-attempt retry; this mirrors it.
			 */
			if (receptec_uwb_result_encode(result_payload, sizeof(result_payload),
						       &result,
						       &result_length) == RECEPTEC_UWB_OK) {
				bool acked = false;

				for (unsigned int attempt = 0; attempt < 2U && !acked; ++attempt) {
					int64_t ack_deadline;

					if (range_send_frame(RECEPTEC_UWB_RANGE_RESULT,
							     master_eui64, frame.sequence,
							     result_payload, result_length, false,
							     false, 0U, NULL, true) < 0) {
						break;
					}
					ack_deadline = k_uptime_get() + 20;
					while (k_uptime_get() < ack_deadline) {
						uint32_t ack_radio_status;
						int64_t ack_remaining_ms =
						    ack_deadline - k_uptime_get();

						if (ack_remaining_ms <= 0) {
							break;
						}
						(void)receptec_radio_wait_event(
						    K_MSEC(ack_remaining_ms));
						ack_radio_status = dwt_readsysstatuslo();
						if ((ack_radio_status & DWT_INT_RXFCG_BIT_MASK) !=
						    0U) {
							struct receptec_uwb_frame ack_frame;
							uint16_t ack_len;
							uint8_t ack_ranging;

							clear_radio_events(DWT_INT_RXFCG_BIT_MASK);
							ack_len = dwt_getframelength(&ack_ranging);
							if (ack_len >= RECEPTEC_UWB_HEADER_SIZE +
									   RECEPTEC_UWB_FCS_SIZE &&
							    ack_len <= RECEPTEC_UWB_MAX_PSDU_SIZE) {
								dwt_readrxdata(
								    rx_bytes,
								    ack_len - RECEPTEC_UWB_FCS_SIZE,
								    0U);
								if (receptec_uwb_decode(
									rx_bytes,
									ack_len -
									    RECEPTEC_UWB_FCS_SIZE,
									&ack_frame) ==
									RECEPTEC_UWB_OK &&
								    ack_frame.type ==
									RECEPTEC_UWB_ACK &&
								    ack_frame.sequence ==
									frame.sequence &&
								    memcmp(
									ack_frame.source,
									master_eui64,
									RECEPTEC_UWB_EUI64_SIZE) ==
									0) {
									acked = true;
									break;
								}
							}
							(void)dwt_rxenable(DWT_START_RX_IMMEDIATE);
						} else if ((ack_radio_status &
							    (SYS_STATUS_ALL_RX_TO |
							     UWB_RX_REARM_ERROR_MASK)) != 0U) {
							clear_radio_events(
							    ack_radio_status &
							    (SYS_STATUS_ALL_RX_TO |
							     UWB_RX_REARM_ERROR_MASK));
							(void)dwt_rxenable(DWT_START_RX_IMMEDIATE);
						}
					}
				}
			}
			*out_status = final_status;
			*out_distance_mm = result.records[0].distance_mm;
			status = final_status == RECEPTEC_UWB_RANGE_OK ? 0 : -EIO;
			break;
		}
		(void)dwt_rxenable(DWT_START_RX_IMMEDIATE);
	}
	dwt_forcetrxoff();
	clear_radio_events(UWB_RADIO_EVENT_MASK | DWT_INT_TXFRS_BIT_MASK);
	(void)receptec_radio_irq_enable(false);
	printk("UWB respond: pair=%u status=%u distance_mm=%u\n", pair_index, *out_status,
	       *out_distance_mm);
	return status;
}

int receptec_uwb_range_respond(uint8_t master_slot, uint8_t pair_index,
			       uint32_t timeout_ms,
			       enum receptec_uwb_range_status *out_status,
			       uint32_t *out_distance_mm)
{
	uint8_t peer_eui64[RECEPTEC_UWB_EUI64_SIZE];
	int ret;

	if (pair_index > 2U || timeout_ms == 0U || timeout_ms > 60000U) {
		return -EINVAL;
	}
	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
	if (!radio_ready || !peer_registry_ready) {
		k_mutex_unlock(&uwb_peer_mutex);
		return -EAGAIN;
	}
	if (poll_running) {
		k_mutex_unlock(&uwb_peer_mutex);
		return -EBUSY;
	}
	if (master_slot >= RECEPTEC_UWB_MAX_PEERS ||
	    receptec_uwb_peer_registry_get(&peer_registry, master_slot,
					   peer_eui64) < 0) {
		k_mutex_unlock(&uwb_peer_mutex);
		return -ENOENT;
	}
	if (!peer_enabled[master_slot]) {
		k_mutex_unlock(&uwb_peer_mutex);
		return -EACCES;
	}
	poll_running = true;
	k_mutex_unlock(&uwb_peer_mutex);

	ret = range_respond_once(master_slot, pair_index, timeout_ms, out_status,
				 out_distance_mm);

	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
	poll_running = false;
	k_mutex_unlock(&uwb_peer_mutex);
	return ret;
}

int receptec_uwb_range_listen(uint8_t peer_slot, uint8_t pair_index,
			      uint32_t timeout_ms,
			      enum receptec_uwb_range_status *out_status,
			      uint32_t *out_distance_mm, uint64_t *out_round_id)
{
	uint8_t peer_eui64[RECEPTEC_UWB_EUI64_SIZE];
	int ret;

	if (pair_index > 2U || timeout_ms == 0U || timeout_ms > 60000U) {
		return -EINVAL;
	}
	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
	if (!radio_ready || !peer_registry_ready) {
		k_mutex_unlock(&uwb_peer_mutex);
		return -EAGAIN;
	}
	if (poll_running) {
		k_mutex_unlock(&uwb_peer_mutex);
		return -EBUSY;
	}
	if (receptec_uwb_peer_registry_get(&peer_registry, peer_slot, peer_eui64) <
	    0) {
		k_mutex_unlock(&uwb_peer_mutex);
		return -ENOENT;
	}
	if (!peer_enabled[peer_slot]) {
		k_mutex_unlock(&uwb_peer_mutex);
		return -EACCES;
	}
	poll_running = true;
	k_mutex_unlock(&uwb_peer_mutex);

	ret = range_listen_once(peer_eui64, pair_index, timeout_ms, out_status,
				out_distance_mm, out_round_id, 0, NULL);

	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
	poll_running = false;
	k_mutex_unlock(&uwb_peer_mutex);
	return ret;
}

/* One full round: ROUND_ARM -> pair ARM -> DS-TWR initiator -> result uplink.
 */
static void range_pair(uint8_t peer_slot, const uint8_t peer_eui64[RECEPTEC_UWB_EUI64_SIZE],
		       uint8_t pair_index, uint64_t round_id, bool prepared)
{
	enum receptec_uwb_range_status outcome_status = RECEPTEC_UWB_RANGE_NO_RESULT;
	uint32_t distance_mm = RECEPTEC_UWB_DISTANCE_INVALID;
	bool log_round = false;
	int status;
	uint8_t phase = 1;

	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
	range_status.round_id = round_id;
	range_status.rounds++;
	k_mutex_unlock(&uwb_peer_mutex);

	status = prepared ? 0
			  : range_send_plan_and_await_ack(peer_slot, peer_eui64,
							  RECEPTEC_UWB_PLAN_ROUND_ARM, 0xFFU,
							  receptec_eui, peer_eui64, round_id);
	if (status < 0) {
		k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
		range_status.round_arm_timeouts++;
		range_status.last_valid = false;
		k_mutex_unlock(&uwb_peer_mutex);
		outcome_status = RECEPTEC_UWB_RANGE_PLAN_TIMEOUT;
		goto publish;
	}
	status = range_send_plan_and_await_ack(peer_slot, peer_eui64, RECEPTEC_UWB_PLAN_ARM,
					       pair_index, receptec_eui, peer_eui64, round_id);
	if (status < 0) {
		k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
		range_status.last_arm_send_status = status;
		range_status.plan_timeouts++;
		range_status.last_valid = false;
		k_mutex_unlock(&uwb_peer_mutex);
		outcome_status = RECEPTEC_UWB_RANGE_PLAN_TIMEOUT;
		goto publish;
	}
	phase = 2;
	status = range_run_dstwr_initiator(peer_slot, peer_eui64, round_id, pair_index);
	if (status == 0) {
		phase = 3;
		status = range_await_result(peer_slot, peer_eui64, round_id, pair_index,
					    &outcome_status, &distance_mm);
	}
	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
	if (status == 0) {
		range_status.successes++;
		range_status.last_valid = true;
		range_status.last_status = (uint8_t)outcome_status;
		range_status.last_distance_mm = distance_mm;
	} else {
		range_status.range_failures++;
		range_status.last_valid = false;
	}
	range_status.last_update_ms = k_uptime_get();
	log_round = !range_log_quiet;
	k_mutex_unlock(&uwb_peer_mutex);
publish:
	if (status == -ETIME) outcome_status = RECEPTEC_UWB_RANGE_RADIO_LATE;
	else if (status < 0 && phase == 2) outcome_status = RECEPTEC_UWB_RANGE_TIMEOUT;
	if (status < 0 && outcome_status == RECEPTEC_UWB_RANGE_OK)
		outcome_status = RECEPTEC_UWB_RANGE_NO_RESULT;
	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
	if (cycle_running) {
		cycle_last_error[pair_index] = status; cycle_last_phase[pair_index] = phase;
		if (status == 0 && outcome_status == 0)
			cycle_success[pair_index]++;
		else
			cycle_failure[pair_index]++;
	}
	k_mutex_unlock(&uwb_peer_mutex);
#if defined(CONFIG_BT)
	receptec_ble_range_publish_extended(pair_index, 0, (uint8_t)outcome_status, round_id,
					    status == 0 ? distance_mm
							: RECEPTEC_UWB_DISTANCE_INVALID);
#endif
	if (log_round) {
		printk("UWB range: round=%llu status=%u distance_mm=%u\n", round_id,
		       (unsigned int)outcome_status, distance_mm);
	}
}

static void range_round(const uint8_t peer[RECEPTEC_UWB_EUI64_SIZE], uint8_t pair)
{
	range_pair(range_status.peer_slot, peer, pair, range_status.round_id + 1, false);
}

static bool cycle_wait_until(int64_t target)
{
	while (!atomic_get(&range_stop_requested)) {
		int32_t remaining = receptec_uwb_cycle_wait_ms(target, k_uptime_get());
		if (!remaining) break;
		k_msleep(remaining);
	}
	return !atomic_get(&range_stop_requested);
}

static void cycle_execute(void)
{
	int64_t start = k_uptime_get();
	while (!atomic_get(&range_stop_requested)) {
		struct receptec_uwb_cycle_config cfg;
		uint8_t peers[2][RECEPTEC_UWB_EUI64_SIZE];
		bool ready[2] = {false, false};
		bool enabled[2];
		uint64_t round;
		k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
		cfg = cycle_config;
		uint8_t slots[2] = {cfg.tag_slot, cfg.base_slot};
		for (int i = 0; i < 2; i++) {
			enabled[i] = peer_enabled[slots[i]];
			ready[i] = enabled[i] && receptec_uwb_peer_registry_get(&peer_registry, slots[i], peers[i]) == 0;
		}
		k_mutex_unlock(&uwb_peer_mutex);
		if (cycle_next_round == cycle_round_limit) {
			if (receptec_uwb_round_reserve(&cycle_next_round, &cycle_round_limit) < 0)
				break;
		}
		round = cycle_next_round++;
		cycle_deadline = start + RECEPTEC_UWB_CYCLE_ARM_MS;
		for (int i = 0; i < 2; i++) {
			cycle_deadline = MIN(start + RECEPTEC_UWB_CYCLE_ARM_MS, k_uptime_get() + 40);
			if (ready[i])
				ready[i] = range_send_plan_and_await_ack(
					       slots[i], peers[i], RECEPTEC_UWB_PLAN_ROUND_ARM,
					       0xff, receptec_eui, peers[i], round) == 0;
		}
		for (uint8_t pair = 0; pair < 3; pair++) {
			int i = pair == 0 ? 0 : 1;
			int64_t offset = receptec_uwb_cycle_offset(pair);
			if (!cycle_wait_until(start + offset))
				goto stopped;
			cycle_deadline = receptec_uwb_cycle_slot_end(start, pair);
			int64_t before = k_uptime_get();
			if (pair == 2 && cfg.fixed) {
#if defined(CONFIG_BT)
				receptec_ble_range_publish_extended(pair, 1, 0, round,
								    cfg.baseline_mm);
#endif
				k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
				cycle_success[pair]++;
				k_mutex_unlock(&uwb_peer_mutex);
			} else if (pair == 1 && ready[0] && ready[1] && before < cycle_deadline) {
				enum receptec_uwb_range_status outcome = RECEPTEC_UWB_RANGE_PLAN_TIMEOUT;
				uint32_t distance = RECEPTEC_UWB_DISTANCE_INVALID;
				uint64_t received_round = 0;
				int ret = range_send_plan_and_await_ack(slots[0], peers[0],
					RECEPTEC_UWB_PLAN_ARM, 1, peers[1], peers[0], round);
				if (ret == 0) ret = range_send_plan_and_await_ack(slots[1], peers[1],
					RECEPTEC_UWB_PLAN_ARM, 1, peers[1], peers[0], round);
				if (ret == 0) {
					ret = range_send_plan_and_await_ack(slots[1], peers[1],
						RECEPTEC_UWB_PLAN_START, 1, peers[1], peers[0], round);
					/* A may execute even when its START ACK is lost. */
					if (ret == -ETIMEDOUT) ret = 0;
				}
				if (ret == 0) {
					outcome = RECEPTEC_UWB_RANGE_NO_RESULT;
					int64_t remaining = cycle_deadline - k_uptime_get();
					ret = remaining > 0 ? range_listen_once(peers[0], 1, remaining,
						&outcome, &distance, &received_round, round, peers[1]) : -ETIMEDOUT;
				}
				k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
				cycle_last_error[1] = ret;
				cycle_last_phase[1] = outcome == RECEPTEC_UWB_RANGE_PLAN_TIMEOUT ? 1 : 3;
				if (ret == 0 && outcome == 0) cycle_success[1]++;
				else cycle_failure[1]++;
				k_mutex_unlock(&uwb_peer_mutex);
#if defined(CONFIG_BT)
				if (ret < 0) receptec_ble_range_publish_extended(1, 2, outcome, round,
					RECEPTEC_UWB_DISTANCE_INVALID);
#endif
			} else if (pair != 1 && ready[i] && before < cycle_deadline) {
				range_pair(slots[i], peers[i], pair, round, true);
			} else {
				k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
				cycle_failure[pair]++;
				k_mutex_unlock(&uwb_peer_mutex);
#if defined(CONFIG_BT)
				receptec_ble_range_publish_extended(
				    pair, 2, (!enabled[i] || (pair == 1 && !enabled[0])) ? RECEPTEC_UWB_RANGE_NOT_SCHEDULED :
				    before >= cycle_deadline ? RECEPTEC_UWB_RANGE_NOT_RUN_DEADLINE : RECEPTEC_UWB_RANGE_PLAN_TIMEOUT, round,
				    RECEPTEC_UWB_DISTANCE_INVALID);
#endif
			}
			k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
			cycle_max_ms[pair] = MAX(cycle_max_ms[pair], k_uptime_get() - before);
			k_mutex_unlock(&uwb_peer_mutex);
			dwt_forcetrxoff();
			clear_radio_events(UWB_RADIO_EVENT_MASK | DWT_INT_TXFRS_BIT_MASK);
			(void)receptec_radio_irq_enable(false);
		}
		cycle_deadline = 0;
		bool overrun;
		start = receptec_uwb_cycle_next_epoch(start, k_uptime_get(), &overrun);
		if (overrun) {
			k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
			cycle_overruns++;
			k_mutex_unlock(&uwb_peer_mutex);
		}
		if (!cycle_wait_until(start))
			break;
	}
stopped:
	cycle_deadline = 0;
}

/*
 * Background auto-responder: one listen window per commissioned, enabled
 * peer for the M-A pair (the only pair where a RECEPTEC-family board is the
 * responder). Answers rounds without a manual `uwb range respond` per round.
 * poll_running is held across each window so an initiator command fails with
 * -EBUSY instead of fighting for the radio mid-exchange.
 *
 * The window is deliberately NOT a round multiple (700 ms vs the usual
 * 100/1000 ms rounds): equal periods phase-lock the listener against the
 * initiator's grid and most ARMs land inside exchange processing instead of
 * an open window.
 */
#define RANGE_AUTO_PAIR_INDEX 2U
#define RANGE_AUTO_SLOT_WINDOW_MS 700U

static void range_auto_window(void)
{
	uint8_t slot;
	bool listened = false;

	for (slot = 0U; slot < RECEPTEC_UWB_MAX_PEERS; ++slot) {
		uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE];
		enum receptec_uwb_range_status out_status;
		uint32_t out_distance_mm;
		bool usable;
		int64_t window_ms;
		int ret;

		k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
		if (!range_auto_enabled || !radio_ready || !peer_registry_ready || poll_running) {
			k_mutex_unlock(&uwb_peer_mutex);
			/* Back off only for unavailable hardware or another radio owner. */
			k_msleep(200);
			return;
		}
		usable = receptec_uwb_peer_registry_get(&peer_registry, slot, eui64) == 0 &&
			 peer_enabled[slot];
		if (usable) {
			poll_running = true;
		}
		k_mutex_unlock(&uwb_peer_mutex);
		if (!usable) {
			/* Empty/disabled slots must not create gaps in radio listening. */
			continue;
		}
		listened = true;
		window_ms = k_uptime_get();
		ret = range_respond_once(slot, RANGE_AUTO_PAIR_INDEX, RANGE_AUTO_SLOT_WINDOW_MS,
					 &out_status, &out_distance_mm);
		window_ms = k_uptime_get() - window_ms;
		k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
		poll_running = false;
		k_mutex_unlock(&uwb_peer_mutex);
		if (ret != -ETIMEDOUT) {
			printk("UWB range auto: answered slot %u status=%u distance_mm=%u "
			       "dt=%lld ms\n",
			       slot, (unsigned int)out_status, out_distance_mm, window_ms);
		}
		k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
		if (!range_auto_enabled) {
			k_mutex_unlock(&uwb_peer_mutex);
			break;
		}
		k_mutex_unlock(&uwb_peer_mutex);
	}
	if (!listened) {
		/* No commissioned enabled peers: avoid a busy loop. */
		k_msleep(200);
	}
}

static void range_thread_entry(void *unused_a, void *unused_b, void *unused_c)
{
	ARG_UNUSED(unused_a);
	ARG_UNUSED(unused_b);
	ARG_UNUSED(unused_c);

	for (;;) {
		uint32_t period_us;
		uint8_t peer_slot;
		uint8_t pair_index;
		uint8_t peer_eui64[RECEPTEC_UWB_EUI64_SIZE];
		int64_t next_tick_ms;
		bool auto_on;

		k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
		auto_on = range_auto_enabled;
		k_mutex_unlock(&uwb_peer_mutex);
		if (auto_on) {
			range_auto_window();
		}
		if (k_sem_take(&range_start_sem, auto_on ? K_NO_WAIT : K_FOREVER) != 0) {
			continue;
		}
		k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
		period_us = range_status.period_us;
		peer_slot = range_status.peer_slot;
		pair_index = range_status.pair_index;
		k_mutex_unlock(&uwb_peer_mutex);
		next_tick_ms = k_uptime_get();

		if (cycle_running)
			cycle_execute();
		while (!cycle_running && atomic_get(&range_stop_requested) == 0) {
			int64_t sleep_ms;
			bool enabled;

			k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
			if (receptec_uwb_peer_registry_get(&peer_registry, peer_slot, peer_eui64) <
			    0) {
				k_mutex_unlock(&uwb_peer_mutex);
				break;
			}
			enabled = peer_enabled[peer_slot];
			k_mutex_unlock(&uwb_peer_mutex);

			if (enabled) {
				range_round(peer_eui64, pair_index);
			}
			next_tick_ms += period_us / 1000;
			sleep_ms = next_tick_ms - k_uptime_get();
			if (sleep_ms < 0) {
				next_tick_ms = k_uptime_get();
				continue;
			}
			while (sleep_ms > 0 && atomic_get(&range_stop_requested) == 0) {
				int32_t chunk_ms = (int32_t)MIN(sleep_ms, 100);

				k_msleep(chunk_ms);
				sleep_ms -= chunk_ms;
			}
		}
		k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
		dwt_forcetrxoff();
		clear_radio_events(UWB_RADIO_EVENT_MASK | DWT_INT_TXFRS_BIT_MASK);
		cycle_running = false;
		range_status.running = false;
		poll_running = false;
		k_mutex_unlock(&uwb_peer_mutex);
	}
}

K_THREAD_DEFINE(uwb_range_thread, UWB_RANGE_THREAD_STACK_SIZE, range_thread_entry, NULL, NULL, NULL,
		UWB_RANGE_THREAD_PRIORITY, 0, 0);

int receptec_uwb_range_start(uint8_t peer_slot, uint32_t period_ms,
			     uint8_t pair_index)
{
	uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE];
	int ret = 0;

	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
	if (!radio_ready || !peer_registry_ready) {
		ret = -EAGAIN;
		goto unlock;
	}
	if (poll_running) {
		ret = -EBUSY;
		goto unlock;
	}
	if (peer_slot >= RECEPTEC_UWB_MAX_PEERS || period_ms == 0U ||
	    period_ms > 60000U || pair_index > 2U) {
		ret = -EINVAL;
		goto unlock;
	}
	if (receptec_uwb_peer_registry_get(&peer_registry, peer_slot, eui64) < 0) {
		ret = -ENOENT;
		goto unlock;
	}
	if (!peer_enabled[peer_slot]) {
		ret = -EACCES;
		goto unlock;
	}
	range_status.running = true;
	range_status.peer_slot = peer_slot;
	range_status.pair_index = pair_index;
	range_status.period_us = period_ms * 1000U;
	range_status.rounds = 0U;
	range_status.successes = 0U;
	range_status.plan_timeouts = 0U;
	range_status.round_arm_timeouts = 0U;
	range_status.last_arm_send_status = 0;
	range_status.range_failures = 0U;
	range_status.last_valid = false;
	atomic_set(&range_stop_requested, 0);
	poll_running = true;
	k_sem_give(&range_start_sem);
unlock:
	k_mutex_unlock(&uwb_peer_mutex);
	return ret;
}

int receptec_uwb_range_stop(void)
{
	int ret = 0;

	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
	if (!range_status.running) {
		ret = -EALREADY;
	} else {
		atomic_set(&range_stop_requested, 1);
	}
	k_mutex_unlock(&uwb_peer_mutex);
	return ret;
}

void receptec_uwb_range_get(struct receptec_uwb_range_round_status *out)
{
	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
	range_status.auto_responder = range_auto_enabled;
	*out = range_status;
	k_mutex_unlock(&uwb_peer_mutex);
}

void receptec_uwb_range_set_quiet(bool quiet)
{
	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
	range_log_quiet = quiet;
	k_mutex_unlock(&uwb_peer_mutex);
}

int receptec_uwb_range_auto_set(bool enable)
{
	int ret;

	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
	if (!peer_registry_ready) {
		ret = -EAGAIN;
	} else {
		ret = receptec_uwb_range_auto_save(enable);
		if (ret == 0) {
			range_auto_enabled = enable;
			range_status.auto_responder = enable;
		}
	}
	k_mutex_unlock(&uwb_peer_mutex);
	return ret;
}

int receptec_uwb_range_auto_get(bool *enable)
{
	int ret = 0;

	if (enable == NULL) {
		return -EINVAL;
	}
	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
	if (!peer_registry_ready) {
		ret = -EAGAIN;
	} else {
		*enable = range_auto_enabled;
	}
	k_mutex_unlock(&uwb_peer_mutex);
	return ret;
}

int receptec_uwb_range_autostart_set(uint8_t peer_slot, uint32_t period_ms,
				     uint8_t pair_index)
{
	struct receptec_uwb_range_autostart cfg = {
		.slot = peer_slot,
		.pair_index = pair_index,
		.period_ms = period_ms,
	};
	uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE];
	int ret;

	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
	if (!peer_registry_ready) {
		ret = -EAGAIN;
	} else if (receptec_uwb_peer_registry_get(&peer_registry, peer_slot,
						  eui64) < 0) {
		ret = -ENOENT;
	} else {
		ret = receptec_uwb_range_autostart_save(&cfg);
		if (ret == 0) {
			range_autostart = cfg;
			range_autostart_set = true;
		}
	}
	k_mutex_unlock(&uwb_peer_mutex);
	return ret;
}

int receptec_uwb_range_autostart_clear(void)
{
	int ret;

	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
	if (!peer_registry_ready) {
		ret = -EAGAIN;
	} else {
		ret = receptec_uwb_range_autostart_delete();
		if (ret == 0 || ret == -ENOENT) {
			range_autostart_set = false;
			ret = 0;
		}
	}
	k_mutex_unlock(&uwb_peer_mutex);
	return ret;
}

int receptec_uwb_range_autostart_get(
	struct receptec_uwb_range_autostart *out)
{
	int ret = 0;

	if (out == NULL) {
		return -EINVAL;
	}
	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
	if (!peer_registry_ready) {
		ret = -EAGAIN;
	} else if (!range_autostart_set) {
		ret = -ENOENT;
	} else {
		*out = range_autostart;
	}
	k_mutex_unlock(&uwb_peer_mutex);
	return ret;
}

static void run_poll(uint32_t period_us)
{
	uint32_t epoch_dx;
	bool enabled_snapshot[RECEPTEC_UWB_MAX_PEERS];
	uint64_t sync_period_ticks = k_us_to_ticks_ceil64(period_us);
	uint64_t next_sync_prepare_tick;

	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
	dwt_forcetrxoff();
	clear_radio_events(UWB_RADIO_EVENT_MASK | DWT_INT_TXFRS_BIT_MASK);
	(void)receptec_radio_irq_enable(true);
	epoch_dx = dwt_readsystimestamphi32() + us_to_dx_time(UWB_POLL_SETUP_US);
	poll_status.started_ms = k_uptime_get();
	next_sync_prepare_tick = (uint64_t)k_uptime_ticks();
	k_mutex_unlock(&uwb_peer_mutex);

	while (atomic_get(&poll_stop_requested) == 0) {
		int64_t remaining_us;
		uint32_t now_dx;

		uint16_t scheduler_lateness_us = 0U;

		if ((poll_status.sync_mode || poll_status.schedule_mode) &&
		    poll_status.cycles > 0U) {
			int64_t remaining_ticks = (int64_t)next_sync_prepare_tick -
					   k_uptime_ticks();

			remaining_us = remaining_ticks > 0 ?
				k_ticks_to_us_floor64((uint64_t)remaining_ticks) : 0;
			if (remaining_us > (int64_t)UWB_POLL_SETUP_US + 2000) {
				int64_t sleep_ms =
					(remaining_us - UWB_POLL_SETUP_US) / 1000;

				k_msleep((int32_t)MIN(sleep_ms, 100));
				continue;
			}
			if (remaining_us > UWB_POLL_SETUP_US) {
				k_busy_wait((uint32_t)(remaining_us -
						       UWB_POLL_SETUP_US));
				continue;
			}
			if (remaining_ticks < 0) {
				scheduler_lateness_us = (uint16_t)MIN(
					k_ticks_to_us_floor64(
						(uint64_t)-remaining_ticks), UINT16_MAX);
			}
			k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
			epoch_dx = dwt_readsystimestamphi32() +
				   us_to_dx_time(UWB_POLL_SETUP_US);
			k_mutex_unlock(&uwb_peer_mutex);
			remaining_us = 0;
		} else {
			k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
			now_dx = dwt_readsystimestamphi32();
			k_mutex_unlock(&uwb_peer_mutex);
			remaining_us = dx_diff_to_us((int32_t)(epoch_dx - now_dx));
			if (remaining_us < 0) {
				/* The previous cycle overran; restart the schedule from now. */
				k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
				poll_status.overruns++;
				k_mutex_unlock(&uwb_peer_mutex);
				epoch_dx = now_dx + us_to_dx_time(UWB_POLL_SETUP_US);
				continue;
			}
			if (remaining_us > (int64_t)UWB_POLL_SETUP_US + 2000) {
				/* Sleep in short pieces so `uwb poll stop` stays responsive. */
				int64_t sleep_ms =
					(remaining_us - UWB_POLL_SETUP_US) / 1000;

				k_msleep((int32_t)MIN(sleep_ms, 100));
				continue;
			}
		}

		k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
		memcpy(enabled_snapshot, peer_enabled, sizeof(enabled_snapshot));
		k_mutex_unlock(&uwb_peer_mutex);

		for (uint8_t slot = 0U; slot < RECEPTEC_UWB_MAX_PEERS; ++slot) {
			uint8_t peer_eui64[RECEPTEC_UWB_EUI64_SIZE];

			if (atomic_get(&poll_stop_requested) != 0) {
				break;
			}
			if (!enabled_snapshot[slot]) {
				continue;
			}
			if (poll_status.sync_mode && slot != poll_status.peer_slot) {
				continue;
			}
			k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
			if (receptec_uwb_peer_registry_get(&peer_registry, slot,
							   peer_eui64) == 0) {
				uint32_t slot_epoch_dx = epoch_dx + us_to_dx_time(
					(uint32_t)slot * UWB_POLL_SLOT_US);

				if (poll_status.sync_mode) {
					(void)sync_transaction(slot, peer_eui64,
							       slot_epoch_dx,
							       scheduler_lateness_us);
				} else if (poll_status.schedule_mode && !ut.active) {
					if (poll_status.action[slot] ==
					    RECEPTEC_UWB_PEER_ACTION_DATA_REQ) {
						(void)poll_transaction(slot, peer_eui64,
								       slot_epoch_dx);
					} else {
						(void)sync_transaction(slot, peer_eui64,
								       slot_epoch_dx,
								       scheduler_lateness_us);
					}
				} else if (!ut.active) {
					(void)poll_transaction(slot, peer_eui64,
							       slot_epoch_dx);
				} else if (slot == ut.slot) {
					ut_cycle(slot, peer_eui64, slot_epoch_dx);
				}
			}
			k_mutex_unlock(&uwb_peer_mutex);
		}
		k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
		poll_status.cycles++;
		if (poll_status.schedule_mode &&
		    poll_status.cycles % 10U == 0U) {
			print_schedule_summary();
		}
		k_mutex_unlock(&uwb_peer_mutex);
		if (poll_status.sync_mode || poll_status.schedule_mode) {
			next_sync_prepare_tick += sync_period_ticks;
		} else {
			epoch_dx += us_to_dx_time(period_us);
		}
	}

	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
	if (ut.active) {
		/* `uwb poll stop` (or a stop request) ended the poll under an armed test. */
		if (!ut.have_result) {
			ut.result.outcome = RECEPTEC_UWB_TEST_ABORT;
			ut.have_result = true;
		}
		ut_finish();
	}
	dwt_forcetrxoff();
	clear_radio_events(UWB_RADIO_EVENT_MASK | DWT_INT_TXFRS_BIT_MASK);
	(void)receptec_radio_irq_enable(false);
	poll_status.stopped_ms = k_uptime_get();
	poll_running = false;
	k_mutex_unlock(&uwb_peer_mutex);
}

static void poll_thread_entry(void *unused_a, void *unused_b, void *unused_c)
{
	ARG_UNUSED(unused_a);
	ARG_UNUSED(unused_b);
	ARG_UNUSED(unused_c);

	for (;;) {
		k_sem_take(&poll_start_sem, K_FOREVER);
		run_poll(poll_status.period_us);
	}
}

K_THREAD_DEFINE(uwb_poll_thread, UWB_POLL_THREAD_STACK_SIZE, poll_thread_entry, NULL, NULL, NULL,
		UWB_POLL_THREAD_PRIORITY, 0, 0);

int receptec_uwb_poll_start(uint32_t period_ms)
{
	uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE];
	uint32_t period_us = period_ms * 1000U;
	int highest = -1;
	int enabled_count = 0;
	int ret = 0;

	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
	if (!radio_ready || !peer_registry_ready) {
		ret = -EAGAIN;
		goto unlock;
	}
	if (poll_running) {
		ret = -EBUSY;
		goto unlock;
	}
	for (uint8_t slot = 0U; slot < RECEPTEC_UWB_MAX_PEERS; ++slot) {
		if (receptec_uwb_peer_registry_get(&peer_registry, slot,
						   eui64) == 0 && peer_enabled[slot]) {
			highest = slot;
			enabled_count++;
		}
	}
	if (highest < 0 || enabled_count == 0) {
		ret = -ENOENT;
		goto unlock;
	}
	/* The last registered slot has to end before the next cycle begins. */
	if (period_ms == 0U ||
	    period_us < (uint32_t)(highest + 1) * UWB_POLL_SLOT_US) {
		ret = -EINVAL;
		goto unlock;
	}
	memset(&poll_status, 0, sizeof(poll_status));
	memset(poll_last, 0, sizeof(poll_last));
	poll_status.period_us = period_us;
	for (uint8_t slot = 0U; slot < RECEPTEC_UWB_MAX_PEERS; ++slot) {
		poll_status.action[slot] = RECEPTEC_UWB_PEER_ACTION_DATA_REQ;
	}
	atomic_set(&poll_stop_requested, 0);
	poll_running = true;
	k_sem_give(&poll_start_sem);
unlock:
	k_mutex_unlock(&uwb_peer_mutex);
	return ret;
}

int receptec_uwb_sync_start(uint8_t peer_slot, uint32_t period_ms)
{
	uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE];
	uint32_t period_us = period_ms * 1000U;
	int ret = 0;

	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
	if (!radio_ready || !peer_registry_ready) {
		ret = -EAGAIN;
		goto unlock;
	}
	if (poll_running) {
		ret = (poll_status.sync_mode && poll_status.peer_slot == peer_slot &&
		       poll_status.period_us == period_us) ? -EALREADY : -EBUSY;
		goto unlock;
	}
	if (peer_slot >= RECEPTEC_UWB_MAX_PEERS || period_ms == 0U ||
	    period_ms > 60000U ||
	    period_us < (uint32_t)(peer_slot + 1U) * UWB_POLL_SLOT_US) {
		ret = -EINVAL;
		goto unlock;
	}
	if (receptec_uwb_peer_registry_get(&peer_registry, peer_slot, eui64) < 0) {
		ret = -ENOENT;
		goto unlock;
	}
	if (!peer_enabled[peer_slot]) {
		ret = -EACCES;
		goto unlock;
	}
	memset(&poll_status, 0, sizeof(poll_status));
	memset(poll_last, 0, sizeof(poll_last));
	poll_status.action[peer_slot] = RECEPTEC_UWB_PEER_ACTION_SYNC;
	poll_status.sync_mode = true;
	poll_status.peer_slot = peer_slot;
	poll_status.period_us = period_us;
	atomic_set(&poll_stop_requested, 0);
	poll_running = true;
	k_sem_give(&poll_start_sem);
unlock:
	k_mutex_unlock(&uwb_peer_mutex);
	return ret;
}

int receptec_uwb_schedule_set(uint8_t peer_slot,
			      enum receptec_uwb_peer_action action)
{
	uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE];
	int ret = 0;

	if (peer_slot >= RECEPTEC_UWB_MAX_PEERS ||
	    (action != RECEPTEC_UWB_PEER_ACTION_SYNC &&
	     action != RECEPTEC_UWB_PEER_ACTION_DATA_REQ)) {
		return -EINVAL;
	}
	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
	if (!radio_ready || !peer_registry_ready) {
		ret = -EAGAIN;
	} else if (poll_running) {
		ret = -EBUSY;
	} else if (receptec_uwb_peer_registry_get(&peer_registry, peer_slot,
						 eui64) < 0) {
		ret = -ENOENT;
	} else {
		ret = receptec_uwb_schedule_action_save(peer_slot,
							(uint8_t)action);
		if (ret == 0) {
			peer_actions[peer_slot] = action;
			poll_status.action[peer_slot] = action;
		}
	}
	k_mutex_unlock(&uwb_peer_mutex);
	return ret;
}

int receptec_uwb_schedule_get(uint8_t peer_slot,
			      enum receptec_uwb_peer_action *action)
{
	int ret = 0;

	if (peer_slot >= RECEPTEC_UWB_MAX_PEERS || action == NULL) {
		return -EINVAL;
	}
	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
	if (!peer_registry_ready) {
		ret = -EAGAIN;
	} else {
		*action = peer_actions[peer_slot];
	}
	k_mutex_unlock(&uwb_peer_mutex);
	return ret;
}

int receptec_uwb_schedule_period_set(uint32_t period_ms)
{
	int ret;

	if (period_ms == 0U || period_ms > 60000U) {
		return -EINVAL;
	}
	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
	if (!peer_registry_ready) {
		ret = -EAGAIN;
	} else if (poll_running) {
		ret = -EBUSY;
	} else {
		ret = receptec_uwb_schedule_period_save(period_ms);
		if (ret == 0) {
			schedule_period_ms = period_ms;
		}
	}
	k_mutex_unlock(&uwb_peer_mutex);
	return ret;
}

int receptec_uwb_schedule_period_get(uint32_t *period_ms)
{
	if (period_ms == NULL) {
		return -EINVAL;
	}
	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
	if (!peer_registry_ready) {
		k_mutex_unlock(&uwb_peer_mutex);
		return -EAGAIN;
	}
	*period_ms = schedule_period_ms;
	k_mutex_unlock(&uwb_peer_mutex);
	return 0;
}

int receptec_uwb_schedule_start(void)
{
	uint8_t eui64[RECEPTEC_UWB_EUI64_SIZE];
	uint32_t period_us;
	int highest = -1;
	int enabled_count = 0;
	int ret = 0;

	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
	if (!radio_ready || !peer_registry_ready) {
		ret = -EAGAIN;
		goto unlock;
	}
	if (poll_running) {
		ret = -EBUSY;
		goto unlock;
	}
	period_us = schedule_period_ms * 1000U;
	for (uint8_t slot = 0U; slot < RECEPTEC_UWB_MAX_PEERS; ++slot) {
		if (receptec_uwb_peer_registry_get(&peer_registry, slot,
						   eui64) == 0 && peer_enabled[slot]) {
			highest = slot;
			enabled_count++;
		}
	}
	if (highest < 0 || enabled_count == 0) {
		ret = -ENOENT;
		goto unlock;
	}
	if (period_us < (uint32_t)(highest + 1) * UWB_POLL_SLOT_US) {
		ret = -EINVAL;
		goto unlock;
	}
	memset(&poll_status, 0, sizeof(poll_status));
	memset(poll_last, 0, sizeof(poll_last));
	poll_status.schedule_mode = true;
	poll_status.period_us = period_us;
	memcpy(poll_status.action, peer_actions, sizeof(peer_actions));
	atomic_set(&poll_stop_requested, 0);
	poll_running = true;
	k_sem_give(&poll_start_sem);
unlock:
	k_mutex_unlock(&uwb_peer_mutex);
	return ret;
}

int receptec_uwb_sync_stop(void)
{
	int ret;

	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
	if (!poll_running || !poll_status.sync_mode) {
		k_mutex_unlock(&uwb_peer_mutex);
		return -EALREADY;
	}
	k_mutex_unlock(&uwb_peer_mutex);
	ret = receptec_uwb_poll_stop();
	return ret;
}

int receptec_uwb_poll_stop(void)
{
	atomic_set(&poll_stop_requested, 1);
	for (int i = 0; i < 40; ++i) {
		bool running;

		k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
		running = poll_running;
		k_mutex_unlock(&uwb_peer_mutex);
		if (!running) {
			return 0;
		}
		k_msleep(50);
	}
	return -ETIMEDOUT;
}

void receptec_uwb_poll_get(struct receptec_uwb_poll_status *out)
{
	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
	*out = poll_status;
	out->running = poll_running;
	if (poll_running) {
		out->stopped_ms = k_uptime_get();
	}
	k_mutex_unlock(&uwb_peer_mutex);
}

void receptec_uwb_poll_stream(bool enable)
{
	atomic_set(&poll_stream_enabled, enable ? 1 : 0);
}

int receptec_uwb_poll_last(uint8_t slot, uint8_t *payload, size_t capacity,
			   size_t *length, uint32_t *count, uint32_t *age_ms)
{
	int ret = 0;

	if (slot >= RECEPTEC_UWB_MAX_PEERS) {
		return -EINVAL;
	}
	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
	if (poll_last[slot].count == 0U) {
		ret = -ENODATA;
	} else if (capacity < poll_last[slot].length) {
		ret = -EMSGSIZE;
	} else {
		memcpy(payload, poll_last[slot].payload, poll_last[slot].length);
		*length = poll_last[slot].length;
		*count = poll_last[slot].count;
		*age_ms = (uint32_t)(k_uptime_get() - poll_last[slot].uptime_ms);
	}
	k_mutex_unlock(&uwb_peer_mutex);
	return ret;
}

/* Bench A/B switch for the external receive LNA (QM33120W GPIO5). */
int receptec_uwb_radio_set_lna(bool enable)
{
	int ret = 0;

	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
	if (!radio_ready) {
		ret = -EAGAIN;
	} else {
#if defined(RECEPTEC_DW3110_RADIO)
		ret = -ENOTSUP;
#else
		dwt_setlnapamode(enable ? DWT_LNA_ENABLE : 0);
#endif
	}
	k_mutex_unlock(&uwb_peer_mutex);
	return ret;
}

int receptec_uwb_cycle_get(struct receptec_uwb_cycle_config *out, bool *running,
			   uint32_t success[3], uint32_t failure[3], uint32_t max_ms[3],
			   uint32_t *overruns)
{
	if (!out || !running || !success || !failure || !max_ms || !overruns) return -EINVAL;
	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
	*out = cycle_config;
	*running = cycle_running;
	bool valid = cycle_config_valid;
	memcpy(success, cycle_success, sizeof(cycle_success));
	memcpy(failure, cycle_failure, sizeof(cycle_failure));
	memcpy(max_ms, cycle_max_ms, sizeof(cycle_max_ms));
	*overruns = cycle_overruns;
	k_mutex_unlock(&uwb_peer_mutex);
	return valid ? 0 : -ENOENT;
}
int receptec_uwb_cycle_configure(const struct receptec_uwb_cycle_config *cfg)
{
	uint8_t tag[8], base[8];
	int ret;
	if (!cfg)
		return -EINVAL;
	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
	if (!peer_registry_ready) {
		ret = -EAGAIN;
		goto out;
	}
	if (cfg->tag_slot >= RECEPTEC_UWB_MAX_PEERS || cfg->base_slot >= RECEPTEC_UWB_MAX_PEERS ||
	    cfg->tag_slot == cfg->base_slot ||
	    receptec_uwb_peer_registry_get(&peer_registry, cfg->tag_slot, tag) < 0 ||
	    receptec_uwb_peer_registry_get(&peer_registry, cfg->base_slot, base) < 0 ||
	    !memcmp(tag, base, 8) || !memcmp(tag, receptec_eui, 8) ||
	    !memcmp(base, receptec_eui, 8)) {
		ret = -EINVAL;
		goto out;
	}
	ret = receptec_uwb_cycle_save(cfg);
	if (!ret) {
		cycle_config = *cfg;
		cycle_config_valid = true;
	}
out:
	k_mutex_unlock(&uwb_peer_mutex);
	return ret;
}
int receptec_uwb_cycle_start(void)
{
	int ret;
	uint8_t peer[8], tag[8];
	k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
	if (!radio_ready || !cycle_config_valid) {
		ret = -EAGAIN;
		goto out;
	}
	if (poll_running) {
		ret = -EBUSY;
		goto out;
	}
	if (receptec_uwb_peer_registry_get(&peer_registry, cycle_config.tag_slot, peer) < 0 ||
	    !memcmp(peer, receptec_eui, 8) ||
	    receptec_uwb_peer_registry_get(&peer_registry, cycle_config.base_slot, peer) < 0 ||
	    !memcmp(peer, receptec_eui, 8)) {
		ret = -EINVAL;
		goto out;
	}
	receptec_uwb_peer_registry_get(&peer_registry, cycle_config.tag_slot, tag);
	if (!memcmp(tag, peer, 8)) { ret = -EINVAL; goto out; }
	ret = receptec_uwb_round_reserve(&cycle_next_round, &cycle_round_limit);
	if (ret)
		goto out;
	memset(cycle_success, 0, sizeof(cycle_success));
	memset(cycle_failure, 0, sizeof(cycle_failure));
	memset(cycle_max_ms, 0, sizeof(cycle_max_ms));
	cycle_overruns = 0;
	cycle_running = true;
	range_status.running = true;
	poll_running = true;
	range_status.period_us = 1000000;
	atomic_set(&range_stop_requested, 0);
	k_sem_give(&range_start_sem);
out:
	k_mutex_unlock(&uwb_peer_mutex);
	return ret;
}

void receptec_uwb_cycle_errors(int error[3], uint8_t phase[3])
{
 k_mutex_lock(&uwb_peer_mutex, K_FOREVER);
 memcpy(error, cycle_last_error, sizeof(cycle_last_error));
 memcpy(phase, cycle_last_phase, sizeof(cycle_last_phase));
 k_mutex_unlock(&uwb_peer_mutex);
}
