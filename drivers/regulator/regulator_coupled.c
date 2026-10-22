/* SPDX-License-Identifier: Apache-2.0 */
#define DT_DRV_COMPAT zephyr_regulator_coupled
#include <zephyr/device.h>
#include <zephyr/drivers/regulator.h>
#include "coupled_backend.h"

struct coupled_config {
	struct regulator_common_config common;
	const struct device *members[3];
	size_t count;
	int32_t min_delta, max_delta, step_uv;
};
struct coupled_data {
	struct regulator_common_data common;
	struct pmic_output members[3];
	struct regulator_coupling constraints[2];
	struct regulator_coupled_group group;
};

static int member_get(void *ctx, int32_t *uv)
{
	return regulator_get_voltage(ctx, uv);
}
static int member_set(void *ctx, int32_t uv)
{
	return regulator_set_voltage(ctx, uv, uv);
}
static int member_enabled(void *ctx, bool *enabled)
{
	return regulator_get_enabled(ctx, enabled);
}
static const struct pmic_output_api member_ops = {
	.get_level = member_get,
	.set_level = member_set,
	.get_enabled = member_enabled,
};
static int coupled_get(const struct device *dev, int32_t *uv)
{
	struct coupled_data *d = dev->data;

	return regulator_coupled_ops.get_level(&d->group, uv);
}
static int coupled_set(const struct device *dev, int32_t min_uv, int32_t max_uv)
{
	const struct coupled_config *c = dev->config;
	struct coupled_data *d = dev->data;
	int64_t uv = MAX(min_uv, c->common.min_uv);

	uv = c->common.min_uv + DIV_ROUND_UP(uv - c->common.min_uv, c->step_uv) * c->step_uv;
	if (uv > max_uv || uv > c->common.max_uv) {
		return -ERANGE;
	}
	return regulator_coupled_ops.set_level(&d->group, (int32_t)uv);
}
static int coupled_enabled(const struct device *dev, bool *enabled)
{
	struct coupled_data *d = dev->data;

	return regulator_coupled_ops.get_enabled(&d->group, enabled);
}
static int coupled_enable(const struct device *dev)
{
	struct coupled_data *d = dev->data;

	return regulator_coupled_ops.set_enabled(&d->group, true);
}
static int coupled_disable(const struct device *dev)
{
	ARG_UNUSED(dev);
	return -EACCES;
}
static unsigned int coupled_count(const struct device *dev)
{
	const struct coupled_config *c = dev->config;

	return (unsigned int)(((int64_t)c->common.max_uv - c->common.min_uv) / c->step_uv + 1);
}
static int coupled_list(const struct device *dev, unsigned int index, int32_t *uv)
{
	const struct coupled_config *c = dev->config;

	if (index >= coupled_count(dev)) {
		return -EINVAL;
	}
	*uv = (int32_t)(c->common.min_uv + (int64_t)index * c->step_uv);
	return 0;
}
static int coupled_init(const struct device *dev)
{
	const struct coupled_config *c = dev->config;
	struct coupled_data *d = dev->data;
	bool enabled;
	int ret;

	regulator_common_data_init(dev);
	if (c->count < 2 || c->count > 3 || c->step_uv <= 0) {
		return -EINVAL;
	}
	for (size_t i = 0; i < c->count; i++) {
		unsigned int count;
		int32_t low, high, next;

		if (!device_is_ready(c->members[i])) {
			return -ENODEV;
		}
		count = regulator_count_voltages(c->members[i]);
		if (count < 2 || regulator_list_voltage(c->members[i], 0, &low) ||
		    regulator_list_voltage(c->members[i], 1, &next) ||
		    regulator_list_voltage(c->members[i], count - 1, &high) || next <= low) {
			return -EINVAL;
		}
		int64_t step = (int64_t)next - low;

		if (step > INT32_MAX || (int64_t)high != low + (count - 1) * step) {
			return -EINVAL;
		}
		for (unsigned int j = 2; j < count; j++) {
			int32_t value;

			if (regulator_list_voltage(c->members[i], j, &value) ||
			    value != low + j * step) {
				return -EINVAL;
			}
		}
		d->members[i] = (struct pmic_output){
			NULL, low, high, (int32_t)step, true, &member_ops, (void *)c->members[i]};
		d->group.rails[i] = &d->members[i];
		if (i + 1 < c->count) {
			d->constraints[i] = (struct regulator_coupling){i, c->count - 1,
									c->min_delta, c->max_delta};
		}
	}
	d->group.count = c->count;
	d->group.constraints = d->constraints;
	d->group.constraint_count = c->count - 1;
	ret = coupled_enabled(dev, &enabled);
	if (!ret) {
		d->common.refcnt = enabled ? 1 : 0;
	}
	return ret;
}
static DEVICE_API(regulator, coupled_api) = {
	.enable = coupled_enable,
	.disable = coupled_disable,
	.get_enabled = coupled_enabled,
	.count_voltages = coupled_count,
	.list_voltage = coupled_list,
	.get_voltage = coupled_get,
	.set_voltage = coupled_set,
};
#define MEMBER(node, prop, i) DEVICE_DT_GET(DT_PHANDLE_BY_IDX(node, prop, i)),
#define COUPLED_DEFINE(inst)                                                                       \
	static struct coupled_data data_##inst;                                                    \
	static const struct coupled_config config_##inst = {                                       \
		.common = REGULATOR_DT_COMMON_CONFIG_INIT(DT_DRV_INST(inst)),                      \
		.members = {DT_FOREACH_PROP_ELEM(DT_DRV_INST(inst), regulators, MEMBER)},          \
		.count = DT_INST_PROP_LEN(inst, regulators),                                       \
		.min_delta = DT_INST_PROP(inst, min_spread_microvolt),                             \
		.max_delta = DT_INST_PROP(inst, max_spread_microvolt),                             \
		.step_uv = DT_INST_PROP(inst, voltage_step_microvolt),                             \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(inst, coupled_init, NULL, &data_##inst, &config_##inst, POST_KERNEL, \
			      CONFIG_REGULATOR_COUPLED_INIT_PRIORITY, &coupled_api);
DT_INST_FOREACH_STATUS_OKAY(COUPLED_DEFINE)
