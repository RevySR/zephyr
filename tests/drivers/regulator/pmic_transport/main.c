/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/ztest.h>
#include <string.h>
#include "regulator_pmic.h"

static struct {
	unsigned int status_reads;
	uint8_t packet[8];
	uint8_t flags[2];
	uint16_t address;
	unsigned int calls, size, messages;
	int error, timer_error, enabled_error;
	uint32_t frequency, ticks;
} model;

static int transfer(const struct device *dev, struct i2c_msg *messages, uint8_t count,
		    uint16_t address)
{
	ARG_UNUSED(dev);
	model.calls++;
	model.address = address;
	model.messages = count;
	model.size = messages[0].len;
	memcpy(model.packet, messages[0].buf, messages[0].len);
	for (unsigned int i = 0; i < count; i++) {
		model.flags[i] = messages[i].flags;
		if (messages[i].flags & I2C_MSG_READ) {
			memset(messages[i].buf, 0x99, messages[i].len);
		}
	}
	return model.error;
}
static int get_value(const struct device *dev, uint32_t *ticks)
{
	ARG_UNUSED(dev);
	model.ticks += 3;
	*ticks = model.ticks;
	return model.timer_error;
}
static uint32_t get_frequency(const struct device *dev)
{
	ARG_UNUSED(dev);
	return model.frequency;
}
static DEVICE_API(i2c, bus_api) = {.transfer = transfer};
static DEVICE_API(counter, timer_api) = {.get_value = get_value, .get_freq = get_frequency};
static struct device_state ready = {.initialized = true};
static const struct device bus = {.api = &bus_api, .state = &ready};
static const struct device timer = {.api = &timer_api, .state = &ready};
static int enabled(void *context, bool *on)
{
	ARG_UNUSED(context);
	model.status_reads++;
	*on = true;
	return model.enabled_error;
}
static const struct pmic_output_api output_api = {.get_enabled = enabled};
static int init_output(const struct device *dev)
{
	struct pmic_regulator_data *data = dev->data;

	data->output.ops = &output_api;
	data->output.step_uv = 1;
	return 0;
}
static struct pmic_regulator_data output_data;
static const struct pmic_regulator_config config = {
	.bus = {.bus = &bus},
	.counter = &timer,
	.init_output = init_output,
};
static const struct device pmic = {.config = &config, .data = &output_data};

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	memset(&model, 0, sizeof(model));
	model.frequency = 3000000;
}
ZTEST(pmic_transport, test_register_encoding_and_restart)
{
	uint8_t value[4];
	const uint8_t packet[] = {0x20, 0x60, 1, 2};

	zassert_ok(pmic_bus_read((void *)&pmic, 0x75, 0x2060, 2, value, 4, true));
	zassert_equal(model.address, 0x75);
	zassert_equal(model.messages, 2);
	zassert_equal(model.size, 2);
	zassert_equal(model.packet[0], 0x20);
	zassert_equal(model.packet[1], 0x60);
	zassert_false(model.flags[0] & I2C_MSG_STOP);
	zassert_equal(model.flags[1], I2C_MSG_READ | I2C_MSG_RESTART | I2C_MSG_STOP);
	zassert_equal(value[3], 0x99);
	zassert_ok(pmic_bus_read((void *)&pmic, 6, 0x0e, 1, value, 1, false));
	zassert_equal(model.packet[0], 0x0e);
	zassert_true(model.flags[0] & I2C_MSG_STOP);
	zassert_ok(pmic_bus_write((void *)&pmic, 0x75, 0x2060, 2, packet + 2, 2));
	zassert_mem_equal(model.packet, packet, sizeof(packet));
}
ZTEST(pmic_transport, test_invalid_index_does_not_touch_bus)
{
	uint8_t value = 0xa5;

	zassert_equal(pmic_bus_read((void *)&pmic, 6, 0x100, 1, &value, 1, false), -EINVAL);
	zassert_equal(pmic_bus_write((void *)&pmic, 6, 0x100, 1, &value, 1), -EINVAL);
	zassert_equal(pmic_bus_read((void *)&pmic, 6, 0, 1, NULL, 1, false), -EINVAL);
	zassert_equal(pmic_bus_write((void *)&pmic, 6, 0, 1, NULL, 1), -EINVAL);
	zassert_equal(pmic_bus_read((void *)&pmic, 0x80, 0, 1, &value, 1, false), -EINVAL);
	zassert_equal(pmic_bus_write((void *)&pmic, 6, 0, 3, &value, 1), -EINVAL);
	zassert_equal(model.calls, 0);
}
ZTEST(pmic_transport, test_failed_read_does_not_publish_partial_data)
{
	uint8_t value[4] = {0xa5, 0xa5, 0xa5, 0xa5};
	const uint8_t expected[] = {0xa5, 0xa5, 0xa5, 0xa5};

	model.error = -EIO;
	zassert_equal(pmic_bus_read((void *)&pmic, 0x75, 0x2060, 2, value, 4, false), -EIO);
	zassert_mem_equal(value, expected, sizeof(expected));
}
ZTEST(pmic_transport, test_invalid_timebase)
{
	model.frequency = 0;
	zassert_equal(pmic_bus_delay((void *)&pmic, 10), -EINVAL);
}
ZTEST(pmic_transport, test_timer_error_and_wrap)
{
	model.ticks = UINT32_MAX - 6;
	zassert_ok(pmic_bus_delay((void *)&pmic, 10));
	model.timer_error = -EIO;
	zassert_equal(pmic_bus_delay((void *)&pmic, 10), -EIO);
}
ZTEST(pmic_transport, test_unsupported_hardware_mode_fails_initialization)
{
	model.enabled_error = -ENOTSUP;
	zassert_equal(pmic_regulator_init(&pmic), -ENOTSUP);
	zassert_equal(output_data.common.refcnt, 0);
}
ZTEST_SUITE(pmic_transport, NULL, NULL, before, NULL, NULL);
