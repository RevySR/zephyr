/* SPDX-License-Identifier: Apache-2.0 */
#include "regulator_pmic_engine.h"

int pmic_engine_init(const struct device *dev)
{
	const struct pmic_engine_config *config = dev->config;
	struct pmic_engine_data *data = dev->data;
	struct pmic_output *output = &data->common.output;
	int ret;

	data->io = (struct pmic_backend_io){(void *)dev, pmic_bus_read, pmic_bus_write,
					    pmic_bus_delay};
	data->chip = (struct pmic_backend){&data->io, config->driver, config->common.bus.addr,
					   config->upper_address};
	ret = pmic_rail_init(&data->rail, &data->chip, config->output, output->min_uv,
			     output->max_uv, config->common.settle_us, output->always_on,
			     config->high_range);
	if (ret) {
		return ret;
	}
	output->step_uv = data->rail.step_uv;
	output->ops = &pmic_backend_voltage_ops;
	output->context = &data->rail;
	return 0;
}
