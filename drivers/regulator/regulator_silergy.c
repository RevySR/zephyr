/* SPDX-License-Identifier: Apache-2.0 */
#include "regulator_silergy.h"

static int read8(void *context, uint8_t address, uint8_t reg, uint8_t *value)
{
	return pmic_bus_read(context, address, reg, 1, value, 1, false);
}
static int write8(void *context, uint8_t address, uint8_t reg, uint8_t value)
{
	return pmic_bus_write(context, address, reg, 1, &value, 1);
}
int silergy_regulator_init(const struct device *dev)
{
	const struct silergy_config *config = dev->config;
	struct silergy_data *data = dev->data;
	struct pmic_output *output = &data->common.output;
	int ret;

	data->bus = (struct silergy_bus){(void *)dev, read8, write8, pmic_bus_delay};
	data->rail = (struct silergy_rail){
		.bus = &data->bus,
		.type = config->type,
		.address = config->common.bus.addr,
		.min_uv = output->min_uv,
		.max_uv = output->max_uv,
		.ramp_uv_per_us = config->ramp_uv_per_us,
		.enable_delay_us = config->common.settle_us,
		.always_on = output->always_on,
		.parent_address = config->parent_address,
		.parent_mask = config->parent_mask,
		.parent_reg = 0x40,
	};
	ret = silergy_rail_init(&data->rail, config->output);
	if (ret) {
		return ret;
	}
	output->step_uv = data->rail.selector_step_uv;
	output->ops = &silergy_voltage_ops;
	output->context = &data->rail;
	return 0;
}
