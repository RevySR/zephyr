/* SPDX-License-Identifier: Apache-2.0 */
#include "regulator_pmic.h"
#include <string.h>

int pmic_bus_read(void *context, uint8_t address, uint16_t reg, unsigned int width, uint8_t *value,
		  size_t size, bool restart)
{
	const struct device *dev = context;
	const struct pmic_regulator_config *config = dev->config;
	uint8_t index[] = {(uint8_t)(reg >> 8), (uint8_t)reg};
	uint8_t result[8];
	int ret;

	if (!width || width > 2 || !size || size > sizeof(result) || !value || address > 0x7f ||
	    (width == 1 && reg > UINT8_MAX)) {
		return -EINVAL;
	}
	struct i2c_msg messages[] = {
		{.buf = index + 2 - width, .len = width, .flags = restart ? 0 : I2C_MSG_STOP},
		{.buf = result,
		 .len = size,
		 .flags = I2C_MSG_READ | I2C_MSG_STOP | I2C_MSG_RESTART},
	};

	ret = i2c_transfer(config->bus.bus, messages, ARRAY_SIZE(messages), address);
	if (!ret) {
		memcpy(value, result, size);
	}
	return ret;
}

int pmic_bus_write(void *context, uint8_t address, uint16_t reg, unsigned int width,
		   const uint8_t *value, size_t size)
{
	const struct device *dev = context;
	const struct pmic_regulator_config *config = dev->config;
	uint8_t message[8];

	if (!width || width > 2 || !size || size > sizeof(message) - width || !value ||
	    address > 0x7f || (width == 1 && reg > UINT8_MAX)) {
		return -EINVAL;
	}
	message[0] = (uint8_t)(reg >> 8);
	message[width - 1] = (uint8_t)reg;
	memcpy(message + width, value, size);
	return i2c_write(config->bus.bus, message, width + size, address);
}

int pmic_bus_delay(void *context, uint32_t us)
{
	const struct device *dev = context;
	const struct pmic_regulator_config *config = dev->config;
	uint32_t start, now;
	uint32_t frequency = counter_get_frequency(config->counter);
	uint64_t duration;
	int ret;

	if (!frequency || us > 20000) {
		return -EINVAL;
	}
	duration = DIV_ROUND_UP((uint64_t)us * frequency, 1000000);
	if (duration > UINT32_MAX) {
		return -EINVAL;
	}
	ret = counter_get_value(config->counter, &start);
	if (ret) {
		return ret;
	}
	for (unsigned int i = 0; i < 1000000; i++) {
		ret = counter_get_value(config->counter, &now);
		if (ret) {
			return ret;
		}
		if ((uint32_t)(now - start) >= duration) {
			return 0;
		}
	}
	return -ETIMEDOUT;
}

static int get_enabled(const struct device *dev, bool *enabled)
{
	struct pmic_regulator_data *data = dev->data;

	return data->output.ops->get_enabled(data->output.context, enabled);
}
static int enable(const struct device *dev)
{
	struct pmic_regulator_data *data = dev->data;

	return data->output.ops->set_enabled(data->output.context, true);
}
static int disable(const struct device *dev)
{
	struct pmic_regulator_data *data = dev->data;

	return data->output.ops->set_enabled(data->output.context, false);
}
static int get_voltage(const struct device *dev, int32_t *uv)
{
	struct pmic_regulator_data *data = dev->data;

	return data->output.ops->get_level(data->output.context, uv);
}
static unsigned int count_voltages(const struct device *dev)
{
	struct pmic_regulator_data *data = dev->data;
	const struct pmic_output *output = &data->output;

	return (unsigned int)(((int64_t)output->max_uv - output->min_uv) / output->step_uv + 1);
}
static int list_voltage(const struct device *dev, unsigned int index, int32_t *uv)
{
	struct pmic_regulator_data *data = dev->data;
	const struct pmic_output *output = &data->output;

	if (index >= count_voltages(dev)) {
		return -EINVAL;
	}
	*uv = (int32_t)(output->min_uv + (int64_t)index * output->step_uv);
	return 0;
}
static int set_voltage(const struct device *dev, int32_t min_uv, int32_t max_uv)
{
	struct pmic_regulator_data *data = dev->data;
	const struct pmic_output *output = &data->output;
	int64_t uv = MAX(min_uv, output->min_uv);

	uv = output->min_uv + DIV_ROUND_UP(uv - output->min_uv, output->step_uv) * output->step_uv;
	if (uv > max_uv || uv > output->max_uv) {
		return -ERANGE;
	}
	return output->ops->set_level(output->context, (int32_t)uv);
}
DEVICE_API(regulator, pmic_regulator_api) = {
	.enable = enable,
	.disable = disable,
	.get_enabled = get_enabled,
	.count_voltages = count_voltages,
	.list_voltage = list_voltage,
	.set_voltage = set_voltage,
	.get_voltage = get_voltage,
};
int pmic_regulator_init(const struct device *dev)
{
	const struct pmic_regulator_config *config = dev->config;
	struct pmic_regulator_data *data = dev->data;
	bool enabled;
	int ret;

	if (!device_is_ready(config->bus.bus) || !device_is_ready(config->counter)) {
		return -ENODEV;
	}
	regulator_common_data_init(dev);
	data->output = (struct pmic_output){
		.min_uv = config->common.min_uv,
		.max_uv = config->common.max_uv,
		.always_on = config->common.flags & REGULATOR_ALWAYS_ON,
	};
	ret = config->init_output(dev);
	if (ret) {
		return ret;
	}
	/* Adopt hardware state without clamping voltage or enabling boot rails. */
	ret = get_enabled(dev, &enabled);
	if (ret) {
		return ret;
	}
	data->common.refcnt = enabled ? 1 : 0;
	return 0;
}
