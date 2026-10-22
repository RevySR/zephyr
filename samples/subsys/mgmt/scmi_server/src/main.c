/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/kernel.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/mgmt/scmi/performance.h>
#include <zephyr/drivers/mbox.h>
#include <zephyr/drivers/mbox/e902.h>
#include <zephyr/mgmt/scmi/regulator.h>
#include <zephyr/sys/sys_io.h>
#include "resources.h"

#define SHMEM   DT_CHOSEN(zephyr_scmi_shmem)
#define MAILBOX DT_CHOSEN(zephyr_scmi_mailbox)
#define USER    DT_PATH(zephyr_user)
BUILD_ASSERT(DT_REG_SIZE(SHMEM) == SCMI_SMT_SIZE);
#if defined(CONFIG_SCMI_SERVER_PERFORMANCE) && DT_HAS_CHOSEN(zephyr_scmi_notification_shmem)
#define NOTIFY_SHMEM DT_CHOSEN(zephyr_scmi_notification_shmem)
BUILD_ASSERT(DT_REG_SIZE(NOTIFY_SHMEM) == SCMI_SMT_SIZE);
#endif

static struct scmi_server server = {
	.identity = {.vendor = "Zephyr", .sub_vendor = CONFIG_SOC, .implementation_version = 5},
};
static const struct device *const mailbox = DEVICE_DT_GET(MAILBOX);
static uint32_t read_memory(void *ctx, size_t offset)
{
	return sys_read32((uintptr_t)ctx + offset);
}
static void write_memory(void *ctx, size_t offset, uint32_t value)
{
	sys_write32(value, (uintptr_t)ctx + offset);
}
static void memory_barrier(void *ctx)
{
	ARG_UNUSED(ctx);
	/* Order device I/O as well as memory; a data-only fence is insufficient. */
	__asm__ volatile("fence iorw, iorw" ::: "memory");
}
static void notify(void *ctx)
{
	ARG_UNUSED(ctx);
	(void)mbox_send(mailbox, 0, NULL);
}
static const struct scmi_smt_io io = {
	.context = (void *)DT_REG_ADDR(SHMEM),
	.read32 = read_memory,
	.write32 = write_memory,
	.barrier = memory_barrier,
	.notify = notify,
};
#if defined(NOTIFY_SHMEM)
static void notify_agent(void *ctx)
{
	ARG_UNUSED(ctx);
	(void)mbox_send(mailbox, 1, NULL);
}
static const struct scmi_smt_io notification_io = {
	.context = (void *)DT_REG_ADDR(NOTIFY_SHMEM),
	.read32 = read_memory,
	.write32 = write_memory,
	.barrier = memory_barrier,
	.notify = notify_agent,
};

/* Build a notification only when it can be sent, honoring later disables. */
static void send_notifications(void)
{
	uint8_t message[20];
	size_t size;

	if (!scmi_smt_free(&notification_io)) {
		/* Recover a message abandoned by an agent restarted with kexec. */
		if (scmi_performance_take_resend(&server)) {
			notify_agent(NULL);
		}
		return;
	}
	(void)scmi_performance_take_resend(&server);
	size = scmi_performance_notification(&server, message, sizeof(message));
	if (size) {
		(void)scmi_smt_send(&notification_io, message, size);
	}
}
#endif
static void receive(const struct device *dev, uint32_t channel, void *ctx, struct mbox_msg *msg)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(channel);
	ARG_UNUSED(ctx);
	ARG_UNUSED(msg);
	(void)scmi_smt_respond(&io, &server);
}

#if defined(CONFIG_SCMI_SERVER_REGULATOR) && DT_NODE_HAS_PROP(USER, voltage_domains)
struct domain_config {
	const struct device *dev;
	const char *name;
	bool always_on;
};
#define DOMAIN(node, prop, idx)                                                                    \
	{DEVICE_DT_GET(DT_PHANDLE_BY_IDX(node, prop, idx)),                                        \
	 DT_PROP(DT_PHANDLE_BY_IDX(node, prop, idx), regulator_name),                              \
	 DT_PROP(DT_PHANDLE_BY_IDX(node, prop, idx), regulator_always_on)},
static const struct domain_config configs[] = {DT_FOREACH_PROP_ELEM(USER, voltage_domains, DOMAIN)};
static struct scmi_voltage_domain domains[ARRAY_SIZE(configs)];
#endif

#if defined(CONFIG_SCMI_SERVER_PERFORMANCE) && DT_NODE_HAS_PROP(USER, performance_domains)
#define PERF_OPP(node)                                                                             \
	{DT_PROP_BY_IDX(node, opp_hz, 1) / 1000, DT_PROP_BY_IDX(node, opp_hz, 1) / 1000, 10000},
#define PERF_LEVELS(node, prop, i)                                                                 \
	static const struct scmi_performance_level perf_levels_##i[] = {                           \
		DT_FOREACH_CHILD_STATUS_OKAY(                                                      \
			DT_PHANDLE(DT_PHANDLE_BY_IDX(node, prop, i), operating_points_v2),         \
			PERF_OPP)}
DT_FOREACH_PROP_ELEM_SEP(USER, performance_domains, PERF_LEVELS, (;));

static int perf_get(void *context, uint32_t *khz)
{
	uint32_t hz;
	int ret = clock_control_get_rate(context, NULL, &hz);

	if (!ret) {
		*khz = hz / 1000;
	}
	return ret;
}

static int perf_set(void *context, uint32_t khz)
{
	if (khz > UINT32_MAX / 1000) {
		return -ERANGE;
	}
	return clock_control_set_rate(context, NULL, (void *)(uintptr_t)(khz * 1000));
}

static struct scmi_performance_state performance_states[DT_PROP_LEN(USER, performance_domains)];
#define PERF_DOMAIN(node, prop, i)                                                                 \
	{                                                                                          \
		.name = DT_PROP(DT_PHANDLE_BY_IDX(node, prop, i), domain_name),                    \
		.levels = perf_levels_##i,                                                         \
		.count = ARRAY_SIZE(perf_levels_##i),                                              \
		.sustained_khz =                                                                   \
			DT_PROP(DT_PHANDLE_BY_IDX(node, prop, i), sustained_frequency_khz),        \
		.rate_limit_us = DT_PROP(DT_PHANDLE_BY_IDX(node, prop, i), transition_latency_us), \
		.get_level = perf_get,                                                             \
		.set_level = perf_set,                                                             \
		.context = (void *)DEVICE_DT_GET(DT_PHANDLE_BY_IDX(node, prop, i)),                \
		.state = &performance_states[i],                                                   \
	},
static const struct scmi_performance_domain performance_domains[] = {
	DT_FOREACH_PROP_ELEM(USER, performance_domains, PERF_DOMAIN)};
#endif

#if DT_NODE_HAS_PROP(USER, read_only_voltage_domains)
#define RO_DOMAIN(node, prop, i) DEVICE_DT_GET(DT_PHANDLE_BY_IDX(node, prop, i)),
static const struct device *const read_only_supplies[] = {
	DT_FOREACH_PROP_ELEM(USER, read_only_voltage_domains, RO_DOMAIN)};
#endif

int main(void)
{
	int ret = scmi_resources_init(&server);

	if (ret) {
		return ret;
	}
	if (!device_is_ready(mailbox)) {
		return -ENODEV;
	}
	(void)irq_lock();
#if defined(CONFIG_SCMI_SERVER_REGULATOR) && DT_NODE_HAS_PROP(USER, voltage_domains)
	for (size_t i = 0; i < ARRAY_SIZE(configs); i++) {
		(void)scmi_regulator_domain_init(&domains[i], configs[i].dev, configs[i].name,
						 configs[i].always_on);
	}
#if DT_NODE_HAS_PROP(USER, read_only_voltage_domains)
	for (size_t i = 0; i < ARRAY_SIZE(configs); i++) {
		for (size_t j = 0; j < ARRAY_SIZE(read_only_supplies); j++) {
			if (configs[i].dev == read_only_supplies[j]) {
				domains[i].read_only = true;
			}
		}
	}
#endif
	server.voltage_domains = domains;
	server.voltage_count = ARRAY_SIZE(domains);
#endif
#if defined(CONFIG_SCMI_SERVER_PERFORMANCE) && DT_NODE_HAS_PROP(USER, performance_domains)
	for (size_t i = 0; i < ARRAY_SIZE(performance_domains); i++) {
		if (!device_is_ready(performance_domains[i].context)) {
			return -ENODEV;
		}
	}
	server.performance_domains = performance_domains;
	server.performance_count = ARRAY_SIZE(performance_domains);
#endif
	scmi_smt_init(&io);
#if defined(NOTIFY_SHMEM)
	scmi_smt_init(&notification_io);
	server.notifications = mbox_max_channels_get(mailbox) > 1;
#endif
	mbox_register_callback(mailbox, 0, receive, NULL);
	if (mbox_set_enabled(mailbox, 0, true)) {
		return -EIO;
	}
	for (;;) {
		(void)mbox_e902_poll(mailbox);
		scmi_resources_poll();
#if defined(NOTIFY_SHMEM)
		if (server.notifications) {
			send_notifications();
		}
#endif
	}
	return 0;
}
