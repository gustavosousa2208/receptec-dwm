/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * DW3720/QM33120W SPI Communication & Porting Layer
 * Provides Zephyr SPI implementation for the Qorvo dwt_uwb_driver.
 */

#ifndef DW3720_SPI_H
#define DW3720_SPI_H

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>

/* Known Device IDs */
#define DW3720_DEV_ID_QM33120W  0xDECA0314  /* QM33120W (DW3720 PDOA) */
#define DW3720_DEV_ID_DW3000P   0xDECA0312  /* DW3000 PDOA */
#define DW3720_DEV_ID_QM33110   0xDECA0304  /* QM33110 (non-PDOA) */
#define DW3720_DEV_ID_DW3000    0xDECA0302  /* DW3000 (non-PDOA) */

/**
 * @brief Initialize GPIOs (reset, wakeup, irq) for UWB radio
 * @return 0 on success, negative errno on failure
 */
int dw3720_spi_init(void);

/**
 * @brief Hardware reset the UWB chip via RESET pin
 * @return 0 on success, negative errno on failure
 */
int dw3720_hw_reset(void);

/**
 * @brief Read Device ID register (address 0x00) from UWB chip
 * @param dev_id Pointer to store the 32-bit device ID
 * @return 0 on success, negative errno on failure
 */
int dw3720_read_dev_id(uint32_t *dev_id);

/**
 * @brief Get human-readable string for a device ID
 * @param id 32-bit device ID value
 * @return String like "QM33120W", "DW3000", or "Unknown"
 */
const char *dw3720_dev_id_str(uint32_t id);

/*
 * Qorvo dwt_spi_s porting layer — called by the Qorvo driver via function pointers.
 * These match the signatures in deca_interface.h struct dwt_spi_s.
 */

int32_t dw3720_readfromspi(uint16_t headerLength, uint8_t *headerBuffer,
			    uint16_t readlength, uint8_t *readBuffer);
int32_t dw3720_writetospi(uint16_t headerLength, const uint8_t *headerBuffer,
			   uint16_t bodyLength, const uint8_t *bodyBuffer);
int32_t dw3720_writetospiwithcrc(uint16_t headerLength, const uint8_t *headerBuffer,
				  uint16_t bodyLength, const uint8_t *bodyBuffer,
				  uint8_t crc8);
void dw3720_setslowrate(void);
void dw3720_setfastrate(void);

/** Wakeup device via WAKEUP pin (drive high ~500us, then release) */
void wakeup_device_with_io(void);

/** Enable or disable the nRF GPIO interrupt driven by the DW3720 IRQ pin. */
int dw3720_irq_enable(bool enable);

/** Clear the software IRQ wakeup semaphore. */
void dw3720_irq_clear(void);

/** Wait for the IRQ pin, a control wakeup, or timeout. */
int dw3720_wait_event(k_timeout_t timeout);

/** Wake the UWB owner thread for a non-radio control request. */
void dw3720_wake_owner(void);

/** CPU-cycle timestamp captured at the latest DW3720 GPIO IRQ edge. */
uint32_t dw3720_last_irq_cycles(void);

#endif /* DW3720_SPI_H */
