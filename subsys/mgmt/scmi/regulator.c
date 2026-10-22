/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/drivers/regulator.h>
#include <zephyr/mgmt/scmi/regulator.h>

static int get_level(void *ctx, int32_t *uv)
{
	return regulator_get_voltage(ctx, uv);
}
static int set_level(void *ctx, int32_t uv)
{
	return regulator_set_voltage(ctx, uv, uv);
}
static int get_enabled(void *ctx, bool *on)
{
	return regulator_get_enabled(ctx, on);
}
static int set_enabled(void *ctx, bool on)
{
	const struct device *dev = ctx;
	bool current;
	int ret = regulator_get_enabled(dev, &current);

	if (ret || current == on) {
		return ret;
	}
	ret = on ? regulator_enable(dev) : regulator_disable(dev);
	if (!ret) {
		ret = regulator_get_enabled(dev, &current);
	}
	return ret ? ret : (current == on ? 0 : -EIO);
}
static const struct scmi_voltage_ops ops = {
	.get_level = get_level,
	.set_level = set_level,
	.get_enabled = get_enabled,
	.set_enabled = set_enabled,
};
int scmi_regulator_domain_init(struct scmi_voltage_domain *d, const struct device *dev,
			       const char *name, bool always_on)
{
	unsigned int count;
	int32_t low, high, value;
	int64_t step = 1;
	int ret;

	*d = (struct scmi_voltage_domain){.name = name,
					  .min_uv = 1,
					  .max_uv = 0,
					  .step_uv = 1,
					  .always_on = always_on,
					  .ops = &ops,
					  .context = (void *)dev};
	if (!device_is_ready(dev)) {
		return -ENODEV;
	}
	count = regulator_count_voltages(dev);
	if (!count) {
		return -ENOTSUP;
	}
	ret = regulator_list_voltage(dev, 0, &low);
	if (ret) {
		return ret;
	}
	ret = regulator_list_voltage(dev, count - 1, &high);
	if (ret) {
		return ret;
	}
	if (count > 1) {
		ret = regulator_list_voltage(dev, 1, &value);
		if (ret) {
			return ret;
		}
		step = (int64_t)value - low;
	}
	if (step <= 0 || step > INT32_MAX || high != low + (count - 1) * step) {
		return -EINVAL;
	}
	for (unsigned int i = 2; i < count; i++) {
		ret = regulator_list_voltage(dev, i, &value);
		if (ret) {
			return ret;
		}
		if (value != low + i * step) {
			return -EINVAL;
		}
	}
	d->min_uv = low;
	d->max_uv = high;
	d->step_uv = (int32_t)step;
	return 0;
}
