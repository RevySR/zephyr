/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/sys/util.h>
#include "coupled_backend.h"
#include <errno.h>
#include <string.h>

static bool valid(const struct regulator_coupled_group *g, const int32_t *values)
{
	if (!g->count || g->count > AON_COUPLED_MAX_RAILS) {
		return false;
	}
	for (size_t i = 0; i < g->count; i++) {
		const struct pmic_output *r = g->rails[i];

		if (r->step_uv <= 0 || values[i] < r->min_uv || values[i] > r->max_uv ||
		    ((int64_t)values[i] - r->min_uv) % r->step_uv) {
			return false;
		}
	}
	for (size_t i = 0; i < g->constraint_count; i++) {
		const struct regulator_coupling *c = &g->constraints[i];

		if (c->a >= g->count || c->b >= g->count || c->min_delta_uv > c->max_delta_uv) {
			return false;
		}
		int64_t delta = (int64_t)values[c->a] - values[c->b];

		if (delta < c->min_delta_uv || delta > c->max_delta_uv) {
			return false;
		}
	}
	return true;
}

static int snapshot(struct regulator_coupled_group *g, int32_t *values)
{
	if (!g->count || g->count > AON_COUPLED_MAX_RAILS) {
		return -EINVAL;
	}
	for (size_t i = 0; i < g->count; i++) {
		const struct pmic_output *r = g->rails[i];
		int ret = r->ops->get_level(r->context, &values[i]);

		if (ret) {
			return ret;
		}
	}
	return valid(g, values) ? 0 : -ERANGE;
}

static int get_level(void *context, int32_t *uv)
{
	struct regulator_coupled_group *g = context;
	int32_t values[AON_COUPLED_MAX_RAILS];
	int ret = snapshot(g, values);

	if (ret) {
		return ret;
	}
	for (size_t i = 1; i < g->count; i++) {
		if (values[i] != values[0]) {
			return -EAGAIN;
		}
	}
	*uv = values[0];
	return 0;
}

static int get_enabled(void *context, bool *enabled)
{
	struct regulator_coupled_group *g = context;
	bool all = true;

	if (!g->count || g->count > AON_COUPLED_MAX_RAILS) {
		return -EINVAL;
	}
	for (size_t i = 0; i < g->count; i++) {
		bool state;
		int ret = g->rails[i]->ops->get_enabled(g->rails[i]->context, &state);

		if (ret) {
			return ret;
		}
		all &= state;
	}
	*enabled = all;
	return 0;
}

static int set_level(void *context, int32_t uv)
{
	struct regulator_coupled_group *g = context;
	struct {
		uint8_t rail;
		int32_t uv;
	} plan[64];
	int32_t values[AON_COUPLED_MAX_RAILS] = {0}, expected[AON_COUPLED_MAX_RAILS];
	int32_t target[AON_COUPLED_MAX_RAILS] = {uv, uv, uv};
	size_t steps = 0;
	bool enabled;
	int ret;

	if (g->faulted) {
		return -EIO;
	}
	if (!valid(g, target)) {
		return -ERANGE;
	}
	ret = get_enabled(g, &enabled);
	if (ret || !enabled) {
		return ret ? ret : -EACCES;
	}
	ret = snapshot(g, values);
	if (ret) {
		return ret;
	}
	memcpy(expected, values, sizeof(values));
	/* Plan every intermediate state before the first hardware write. */
	for (;;) {
		bool done = true, progress = false;

		for (size_t i = 0; i < g->count; i++) {
			const struct pmic_output *r = g->rails[i];
			int32_t trial[AON_COUPLED_MAX_RAILS];

			if (values[i] == uv) {
				continue;
			}
			done = false;
			memcpy(trial, values, sizeof(trial));
			int64_t lower = r->min_uv, upper = r->max_uv;

			for (size_t j = 0; j < g->constraint_count; j++) {
				const struct regulator_coupling *c = &g->constraints[j];
				int64_t low = lower, high = upper;

				if (c->a == i) {
					low = (int64_t)values[c->b] + c->min_delta_uv;
					high = (int64_t)values[c->b] + c->max_delta_uv;
				} else if (c->b == i) {
					low = (int64_t)values[c->a] - c->max_delta_uv;
					high = (int64_t)values[c->a] - c->min_delta_uv;
				}
				if (low > lower) {
					lower = low;
				}
				if (high < upper) {
					upper = high;
				}
			}
			int64_t next;

			if (values[i] < uv) {
				next = uv < upper ? uv : upper;
				next = r->min_uv + (next - r->min_uv) / r->step_uv * r->step_uv;
			} else {
				next = uv > lower ? uv : lower;
				next = r->min_uv + (next - r->min_uv + r->step_uv - 1) /
							   r->step_uv * r->step_uv;
			}
			if (next < INT32_MIN || next > INT32_MAX) {
				return -ERANGE;
			}
			if (next == values[i]) {
				continue;
			}
			trial[i] = (int32_t)next;
			if (!valid(g, trial)) {
				continue;
			}
			if (steps == ARRAY_SIZE(plan)) {
				return -E2BIG;
			}
			plan[steps].rail = (uint8_t)i;
			plan[steps++].uv = trial[i];
			values[i] = trial[i];
			progress = true;
		}
		if (done) {
			break;
		}
		if (!progress) {
			return -ERANGE;
		}
	}
	for (size_t i = 0; i < steps; i++) {
		unsigned int id = plan[i].rail;
		const struct pmic_output *r = g->rails[id];

		ret = r->ops->set_level(r->context, plan[i].uv);
		if (!ret) {
			ret = snapshot(g, values);
		}
		expected[id] = plan[i].uv;
		if (!ret && memcmp(values, expected, g->count * sizeof(values[0]))) {
			ret = -EIO;
		}
		if (ret) {
			g->faulted = true; /* no blind rollback/replay after partial rail changes */
			return ret;
		}
	}
	return 0;
}

static int set_enabled(void *context, bool enabled)
{
	bool current;
	int ret;

	if (!enabled) {
		return -EACCES;
	}
	ret = get_enabled(context, &current);
	return ret ? ret : (current ? 0 : -EACCES);
}

const struct pmic_output_api regulator_coupled_ops = {
	.get_level = get_level,
	.set_level = set_level,
	.get_enabled = get_enabled,
	.set_enabled = set_enabled,
};
