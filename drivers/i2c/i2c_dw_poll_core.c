/* SPDX-License-Identifier: Apache-2.0 */
#include "i2c_dw_poll_core.h"
#include <errno.h>
#include <stddef.h>
#include <string.h>

#define IC_CON            0x00U
#define IC_TAR            0x04U
#define IC_DATA_CMD       0x10U
#define IC_INTR_MASK      0x30U
#define IC_RAW_INTR_STAT  0x34U
#define IC_CLR_INTR       0x40U
#define IC_CLR_STOP_DET   0x60U
#define IC_ENABLE         0x6cU
#define IC_STATUS         0x70U
#define IC_TXFLR          0x74U
#define IC_RXFLR          0x78U
#define IC_TX_ABRT_SOURCE 0x80U
#define IC_DMA_CR         0x88U
#define IC_ENABLE_STATUS  0x9cU
#define IC_COMP_TYPE      0xfcU
#define STATUS_TFNF       (1U << 1)
#define STATUS_RFNE       (1U << 3)
#define STATUS_ACTIVE     ((1U << 5) | 1U)
#define INTR_ABORT        (1U << 6)
#define INTR_STOP         (1U << 9)
#define CMD_READ          (1U << 8)
#define CMD_STOP          (1U << 9)
#define TIMEOUT_US        20000U

static int wait_register(struct dw_i2c_poll *bus, uint32_t reg, uint32_t mask, uint32_t expected,
			 bool check_abort)
{
	uint32_t start = bus->ticks(bus->context);

	for (uint32_t i = 0; i < bus->poll_limit; i++) {
		if (check_abort && (bus->read32(bus->context, IC_RAW_INTR_STAT) & INTR_ABORT)) {
			uint32_t cause = bus->read32(bus->context, IC_TX_ABRT_SOURCE);

			return (cause & 0x1fU) ? -ENXIO : -EIO;
		}
		if ((bus->read32(bus->context, reg) & mask) == expected) {
			return 0;
		}
		if ((uint32_t)(bus->ticks(bus->context) - start) >=
		    TIMEOUT_US * bus->ticks_per_us) {
			break;
		}
	}
	return -ETIMEDOUT;
}

int dw_i2c_poll_delay(void *context, uint32_t microseconds)
{
	struct dw_i2c_poll *bus = context;
	uint32_t start, duration;

	if (!bus->ticks_per_us || bus->ticks_per_us > UINT32_MAX / TIMEOUT_US ||
	    microseconds > TIMEOUT_US) {
		return -EINVAL;
	}
	start = bus->ticks(bus->context);
	duration = microseconds * bus->ticks_per_us;
	for (uint32_t i = 0; i < bus->poll_limit; i++) {
		if ((uint32_t)(bus->ticks(bus->context) - start) >= duration) {
			return 0;
		}
	}
	return -ETIMEDOUT;
}

int dw_i2c_poll_adopt(struct dw_i2c_poll *bus)
{
	uint32_t con, speed;

	bus->ready = false;
	if (!bus->ticks_per_us || bus->ticks_per_us > UINT32_MAX / TIMEOUT_US || !bus->poll_limit ||
	    bus->read32(bus->context, IC_COMP_TYPE) != 0x44570140) {
		return -ENODEV;
	}
	con = bus->read32(bus->context, IC_CON);
	speed = (con >> 1) & 3U;
	/* Boot firmware must already provide master mode and valid SCL timing. */
	if ((con & 0x71U) != 0x61U || (speed != 1 && speed != 2) ||
	    !bus->read32(bus->context, speed == 1 ? 0x14 : 0x1c) ||
	    !bus->read32(bus->context, speed == 1 ? 0x18 : 0x20)) {
		return -ENODEV;
	}
	if ((bus->read32(bus->context, IC_STATUS) & STATUS_ACTIVE) ||
	    bus->read32(bus->context, IC_TXFLR) || bus->read32(bus->context, IC_RXFLR) ||
	    bus->read32(bus->context, IC_INTR_MASK) || bus->read32(bus->context, IC_DMA_CR)) {
		return -EBUSY;
	}
	/* A stopped timebase must fail before any transaction is issued. */
	int ret = dw_i2c_poll_delay(bus, 1);

	if (!ret) {
		bus->ready = true;
	}
	return ret;
}

int dw_i2c_poll_transfer(struct dw_i2c_poll *bus, uint8_t address, const uint8_t *tx,
			 size_t tx_count, uint8_t *rx, size_t rx_count)
{
	int ret, cleanup;
	size_t count = tx_count + rx_count;

	if (!bus->ready) {
		return -ENODEV;
	}
	if (address > 0x7f || !count || tx_count > 8 || rx_count > 8 || (tx_count && !tx) ||
	    (rx_count && !rx)) {
		return -EINVAL;
	}
	if ((bus->read32(bus->context, IC_STATUS) & STATUS_ACTIVE) ||
	    bus->read32(bus->context, IC_TXFLR) || bus->read32(bus->context, IC_RXFLR)) {
		bus->ready = false;
		return -EBUSY;
	}
	bus->write32(bus->context, IC_ENABLE, 0);
	ret = wait_register(bus, IC_ENABLE_STATUS, 1, 0, false);
	if (ret) {
		bus->ready = false;
		return ret;
	}
	(void)bus->read32(bus->context, IC_CLR_INTR);
	bus->write32(bus->context, IC_TAR, address);
	bus->write32(bus->context, IC_ENABLE, 1);
	ret = wait_register(bus, IC_ENABLE_STATUS, 1, 1, false);
	for (size_t i = 0; !ret && i < count; i++) {
		ret = wait_register(bus, IC_STATUS, STATUS_TFNF, STATUS_TFNF, true);
		if (ret) {
			break;
		}
		bool reading = i >= tx_count;
		uint32_t command = reading ? CMD_READ : tx[i];

		if (reading && i == tx_count && tx_count) {
			command |= 1U << 10; /* RESTART before the first read byte */
		}

		if (i + 1 == count) {
			command |= CMD_STOP;
		}
		bus->write32(bus->context, IC_DATA_CMD, command);
		if (reading) {
			ret = wait_register(bus, IC_STATUS, STATUS_RFNE, STATUS_RFNE, true);
			if (!ret) {
				rx[i - tx_count] = (uint8_t)bus->read32(bus->context, IC_DATA_CMD);
			}
		}
	}
	if (!ret) {
		ret = wait_register(bus, IC_RAW_INTR_STAT, INTR_STOP, INTR_STOP, true);
	}
	if (!ret) {
		ret = wait_register(bus, IC_STATUS, STATUS_ACTIVE, 0, true);
	}
	(void)bus->read32(bus->context, IC_CLR_STOP_DET);
	bus->write32(bus->context, IC_ENABLE, 0);
	cleanup = wait_register(bus, IC_ENABLE_STATUS, 1, 0, false);
	if (ret || cleanup) {
		/* No automatic replay after an indeterminate write or bus fault. */
		bus->ready = false;
	}
	return ret ? ret : cleanup;
}

int dw_i2c_poll_read8(void *context, uint8_t address, uint8_t reg, uint8_t *value)
{
	struct dw_i2c_poll *bus = context;
	uint8_t result;
	int ret = dw_i2c_poll_transfer(bus, address, &reg, 1, NULL, 0);

	if (!ret) {
		/* Vendor PMIC reads use register-write STOP, then current-address read. */
		ret = dw_i2c_poll_transfer(bus, address, NULL, 0, &result, 1);
	}
	if (!ret) {
		*value = result;
	}
	return ret;
}

int dw_i2c_poll_write8(void *context, uint8_t address, uint8_t reg, uint8_t value)
{
	const uint8_t tx[] = {reg, value};

	return dw_i2c_poll_transfer(context, address, tx, sizeof(tx), NULL, 0);
}

int dw_i2c_poll_read_reg(void *context, uint8_t address, uint16_t reg, unsigned int reg_bytes,
			 uint8_t *data, size_t size, bool restart)
{
	uint8_t index[2] = {(uint8_t)(reg >> 8), (uint8_t)reg};
	uint8_t result[8];
	int ret;

	if ((reg_bytes != 1 && reg_bytes != 2) || !size || size > sizeof(result) || !data ||
	    (reg_bytes == 1 && reg > 255)) {
		return -EINVAL;
	}
	if (restart) {
		ret = dw_i2c_poll_transfer(context, address, index + 2 - reg_bytes, reg_bytes,
					   result, size);
	} else {
		ret = dw_i2c_poll_transfer(context, address, index + 2 - reg_bytes, reg_bytes, NULL,
					   0);
		if (!ret) {
			ret = dw_i2c_poll_transfer(context, address, NULL, 0, result, size);
		}
	}
	if (!ret) {
		memcpy(data, result, size);
	}
	return ret;
}

int dw_i2c_poll_write_reg(void *context, uint8_t address, uint16_t reg, unsigned int reg_bytes,
			  const uint8_t *data, size_t size)
{
	uint8_t message[8];

	if ((reg_bytes != 1 && reg_bytes != 2) || !size || size > sizeof(message) - reg_bytes ||
	    !data || (reg_bytes == 1 && reg > 255)) {
		return -EINVAL;
	}
	if (reg_bytes == 2) {
		message[0] = (uint8_t)(reg >> 8);
	}
	message[reg_bytes - 1] = (uint8_t)reg;
	memcpy(message + reg_bytes, data, size);
	return dw_i2c_poll_transfer(context, address, message, reg_bytes + size, NULL, 0);
}
