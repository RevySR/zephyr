/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/ztest.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/mgmt/scmi/regulator.h>

struct test_data {
	struct regulator_common_data common;
	bool enabled;
	int status;
	int list_error;
	int32_t middle;
};
static struct test_data data;
static struct device_state state = {.initialized = true};
static const struct regulator_common_config config;
static unsigned int count(const struct device *dev)
{
	ARG_UNUSED(dev);
	return 3;
}
static int list(const struct device *dev, unsigned int i, int32_t *uv)
{
	struct test_data *d = dev->data;

	if (d->list_error) {
		return d->list_error;
	}
	*uv = i == 1 ? d->middle : 800000 + i * 10000;
	return 0;
}
static int enabled(const struct device *dev, bool *on)
{
	struct test_data *d = dev->data;
	*on = d->enabled;
	return d->status;
}
static int enable(const struct device *dev)
{
	((struct test_data *)dev->data)->enabled = true;
	return 0;
}
static int disable(const struct device *dev)
{
	((struct test_data *)dev->data)->enabled = false;
	return 0;
}
static DEVICE_API(regulator, api) = {
	.count_voltages = count,
	.list_voltage = list,
	.get_enabled = enabled,
	.enable = enable,
	.disable = disable,
};
static const struct device dev = {
	.name = "test", .api = &api, .data = &data, .config = &config, .state = &state};
static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	data = (struct test_data){.middle = 810000};
	regulator_common_data_init(&dev);
}
ZTEST(scmi_regulator, test_linear_metadata)
{
	struct scmi_voltage_domain domain;

	zassert_ok(scmi_regulator_domain_init(&domain, &dev, "test", false));
	zassert_equal(domain.min_uv, 800000);
	zassert_equal(domain.max_uv, 820000);
	zassert_equal(domain.step_uv, 10000);
}
ZTEST(scmi_regulator, test_invalid_metadata)
{
	struct scmi_voltage_domain domain;

	data.middle = 815000;
	zassert_equal(scmi_regulator_domain_init(&domain, &dev, "test", false), -EINVAL);
	zassert_true(domain.min_uv > domain.max_uv);
	data.list_error = -EIO;
	zassert_equal(scmi_regulator_domain_init(&domain, &dev, "test", false), -EIO);
	zassert_true(domain.min_uv > domain.max_uv);
}
ZTEST(scmi_regulator, test_enable_idempotence_and_error)
{
	struct scmi_voltage_domain domain;
	bool on = false;

	zassert_ok(scmi_regulator_domain_init(&domain, &dev, "test", false));
	zassert_ok(domain.ops->set_enabled(domain.context, true));
	zassert_equal(data.common.refcnt, 1);
	zassert_ok(domain.ops->set_enabled(domain.context, true));
	zassert_equal(data.common.refcnt, 1);
	zassert_ok(domain.ops->set_enabled(domain.context, false));
	zassert_equal(data.common.refcnt, 0);
	data.enabled = true;
	data.status = -EIO;
	zassert_equal(domain.ops->get_enabled(domain.context, &on), -EIO);
	zassert_false(on);
	zassert_equal(domain.ops->set_enabled(domain.context, false), -EIO);
	zassert_true(data.enabled);
}
ZTEST_SUITE(scmi_regulator, NULL, NULL, before, NULL, NULL);
