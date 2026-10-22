/* SPDX-License-Identifier: Apache-2.0 */
#include "cpu_dvfs.h"
#include <errno.h>
#include <string.h>

static bool valid_supplies(const struct cpu_dvfs *d, const int32_t *uv)
{
	if (uv[0] <= 0 || !d->supply_count || d->supply_count > 2) {
		return false;
	}
	/* Keep SRAM at or above core, including every intermediate step. */
	return d->supply_count == 1 || (uv[1] >= uv[0] && (int64_t)uv[1] - uv[0] <= 300000);
}

static int set_supply(struct cpu_dvfs *d, unsigned int supply, int32_t uv)
{
	int32_t actual;
	int ret = d->ops->set_voltage(d->context, supply, uv);

	if (ret) {
		return ret;
	}
	ret = d->ops->get_voltage(d->context, supply, &actual);
	if (ret || actual != uv) {
		d->faulted = true;
		return ret ? ret : -EIO;
	}
	return 0;
}

int cpu_dvfs_set_rate(struct cpu_dvfs *d, uint32_t hz)
{
	const struct cpu_dvfs_opp *opp = NULL;
	uint32_t old_hz, actual_hz;
	int32_t old[2] = {0}, target[2], trial[2];
	bool lowering_voltage = false;
	int ret;

	if (d->faulted) {
		return -EIO;
	}
	if (!d->supply_count || d->supply_count > 2) {
		return -EINVAL;
	}
	for (size_t i = 0; i < d->count; i++) {
		if (d->opps[i].hz == hz) {
			opp = &d->opps[i];
			break;
		}
	}
	if (!opp) {
		return -ERANGE;
	}
	target[0] = opp->uv;
	target[1] = opp->sram_uv;
	if (!valid_supplies(d, target)) {
		return -ERANGE;
	}
	ret = d->ops->get_rate(d->context, &old_hz);
	if (ret) {
		return ret;
	}
	for (unsigned int i = 0; i < d->supply_count; i++) {
		ret = d->ops->get_voltage(d->context, i, &old[i]);
		if (ret) {
			return ret;
		}
		lowering_voltage |= target[i] < old[i];
	}
	if (!valid_supplies(d, old)) {
		return -ERANGE;
	}
	/* Validate the entire voltage sequence before changing any hardware. */
	memcpy(trial, old, sizeof(trial));
	for (int i = d->supply_count - 1; i >= 0; i--) {
		if (target[i] > trial[i]) {
			trial[i] = target[i];
			if (!valid_supplies(d, trial)) {
				return -ERANGE;
			}
		}
	}
	for (unsigned int i = 0; i < d->supply_count; i++) {
		trial[i] = target[i];
		if (!valid_supplies(d, trial)) {
			return -ERANGE;
		}
	}
	/* Raise SRAM before core; all rises precede the frequency increase. */
	for (int i = d->supply_count - 1; i >= 0; i--) {
		if (target[i] > old[i]) {
			ret = set_supply(d, i, target[i]);
			if (ret) {
				return ret;
			}
		}
	}
	if (hz != old_hz || lowering_voltage) {
		ret = d->ops->set_rate(d->context, hz);
		if (ret) {
			/* Never lower voltage after an incomplete clock transition. */
			return ret;
		}
	}
	ret = d->ops->get_rate(d->context, &actual_hz);
	if (ret || actual_hz != hz) {
		d->faulted = true;
		return ret ? ret : -EIO;
	}
	/* Lower core before SRAM, after CPU and fabric clocks have settled. */
	for (unsigned int i = 0; i < d->supply_count; i++) {
		if (target[i] < old[i]) {
			ret = set_supply(d, i, target[i]);
			if (ret) {
				d->faulted = true;
				return ret;
			}
		}
	}
	return 0;
}
