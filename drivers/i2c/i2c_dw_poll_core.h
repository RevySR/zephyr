/* SPDX-License-Identifier: Apache-2.0 */
#ifndef AON_DW_I2C_H
#define AON_DW_I2C_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Early polling adapter: exclusive bus ownership, inherited clock/timing. */
struct dw_i2c_poll {
	void *context;
	uint32_t (*read32)(void *context, uint32_t offset);
	void (*write32)(void *context, uint32_t offset, uint32_t value);
	uint32_t (*ticks)(void *context);
	uint32_t ticks_per_us;
	uint32_t poll_limit;
	bool ready;
};
int dw_i2c_poll_adopt(struct dw_i2c_poll *bus);
int dw_i2c_poll_read8(void *context, uint8_t address, uint8_t reg, uint8_t *value);
int dw_i2c_poll_write8(void *context, uint8_t address, uint8_t reg, uint8_t value);
int dw_i2c_poll_delay(void *context, uint32_t microseconds);
int dw_i2c_poll_read_reg(void *context, uint8_t address, uint16_t reg, unsigned int reg_bytes,
			 uint8_t *data, size_t size, bool restart);
int dw_i2c_poll_write_reg(void *context, uint8_t address, uint16_t reg, unsigned int reg_bytes,
			  const uint8_t *data, size_t size);
int dw_i2c_poll_transfer(struct dw_i2c_poll *bus, uint8_t address, const uint8_t *tx,
			 size_t tx_count, uint8_t *rx, size_t rx_count);
#endif
