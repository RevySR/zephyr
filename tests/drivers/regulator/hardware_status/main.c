/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/drivers/regulator.h>
#include <zephyr/ztest.h>

struct fake_state {
	struct regulator_common_data common;
	bool hardware_enabled;
	int error;
};

static int get_enabled(const struct device *dev, bool *enabled)
{
	struct fake_state *state = dev->data;

	*enabled = state->hardware_enabled;
	return state->error;
}

static const struct regulator_driver_api api = {.get_enabled = get_enabled};
static const struct regulator_driver_api unsupported_api;

ZTEST(regulator_status, test_hardware_state)
{
	struct fake_state state = {.common.refcnt = 3, .hardware_enabled = false};
	const struct device dev = {.api = &api, .data = &state};
	bool enabled = true;

	zassert_ok(regulator_get_enabled(&dev, &enabled));
	zassert_false(enabled);
	zassert_equal(state.common.refcnt, 3);
	state.common.refcnt = 0;
	state.hardware_enabled = true;
	zassert_ok(regulator_get_enabled(&dev, &enabled));
	zassert_true(enabled);
	zassert_equal(state.common.refcnt, 0);
}

ZTEST(regulator_status, test_error_preserves_output)
{
	struct fake_state state = {.hardware_enabled = true, .error = -EIO};
	const struct device dev = {.api = &api, .data = &state};
	bool enabled = false;

	zassert_equal(regulator_get_enabled(&dev, &enabled), -EIO);
	zassert_false(enabled);
}

ZTEST(regulator_status, test_missing_hook)
{
	const struct device dev = {.api = &unsupported_api};
	bool enabled = true;

	zassert_equal(regulator_get_enabled(&dev, &enabled), -ENOSYS);
	zassert_true(enabled);
}
ZTEST_SUITE(regulator_status, NULL, NULL, NULL, NULL, NULL);
