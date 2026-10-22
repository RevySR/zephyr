/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/kernel.h>
#define DT_DRV_COMPAT thead_th1520_mbox
#include <zephyr/drivers/mbox.h>
#include <zephyr/drivers/mbox/e902.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/barrier.h>

/* Shared XuanTie AON mailbox register interface used by TH1520 and A210. */
#define E902_MBOX_STATUS       0x00
#define E902_MBOX_CLEAR        0x04
#define E902_MBOX_MASK         0x0c
#define E902_MBOX_GENERATE     0x10
#define E902_MBOX_DATA_PENDING BIT(6)

static void io_fence(void)
{
#if defined(CONFIG_RISCV)
	__asm__ volatile("fence iorw, iorw" ::: "memory");
#else
	/* Native simulation uses ordinary host memory for the register bank. */
	barrier_dmem_fence_full();
#endif
}

struct mbox_config {
	uintptr_t rx, tx;
	uint32_t rx_mask;
};
struct mbox_data {
	mbox_callback_t callback;
	void *user_data;
	bool enabled;
};
static int send(const struct device *dev, uint32_t channel, const struct mbox_msg *msg)
{
	const struct mbox_config *c = dev->config;

	if (channel || msg) {
		return -EINVAL;
	}
	/* Order device I/O as well as memory; a data-only fence is insufficient. */
	io_fence();
	sys_write32(E902_MBOX_DATA_PENDING, c->tx + E902_MBOX_GENERATE);
	return 0;
}
static int register_callback(const struct device *dev, uint32_t channel, mbox_callback_t callback,
			     void *user_data)
{
	struct mbox_data *d = dev->data;

	if (channel) {
		return -EINVAL;
	}
	if (!callback && d->enabled) {
		return -EBUSY;
	}
	d->callback = callback;
	d->user_data = user_data;
	return 0;
}
static int set_enabled(const struct device *dev, uint32_t channel, bool enabled)
{
	const struct mbox_config *c = dev->config;
	struct mbox_data *d = dev->data;

	if (channel) {
		return -EINVAL;
	}
	if (d->enabled == enabled) {
		return -EALREADY;
	}
	if (enabled && !d->callback) {
		return -EINVAL;
	}
	sys_write32((sys_read32(c->rx + E902_MBOX_MASK) & ~c->rx_mask) | (enabled ? c->rx_mask : 0),
		    c->rx + E902_MBOX_MASK);
	io_fence();
	d->enabled = enabled;
	return 0;
}
static int mtu(const struct device *dev)
{
	ARG_UNUSED(dev);
	return 0;
}
static uint32_t channels(const struct device *dev)
{
	ARG_UNUSED(dev);
	return 1;
}
int mbox_e902_poll(const struct device *dev)
{
	const struct mbox_config *c = dev->config;
	struct mbox_data *d = dev->data;

	if (d->enabled && (sys_read32(c->rx + E902_MBOX_STATUS) & c->rx_mask)) {
		sys_write32(c->rx_mask, c->rx + E902_MBOX_CLEAR);
		io_fence();
		d->callback(dev, 0, d->user_data, NULL);
		return 1;
	}
	return 0;
}
static int mbox_init(const struct device *dev)
{
	const struct mbox_config *c = dev->config;
	struct mbox_data *d = dev->data;

	/* Clear stale state only at probe, before publishing the SCMI channel. */
	sys_write32(sys_read32(c->rx + E902_MBOX_MASK) & ~c->rx_mask, c->rx + E902_MBOX_MASK);
	sys_write32(c->rx_mask, c->rx + E902_MBOX_CLEAR);
	io_fence();
	d->enabled = false;
	return 0;
}
static DEVICE_API(mbox, api) = {
	.send = send,
	.register_callback = register_callback,
	.set_enabled = set_enabled,
	.mtu_get = mtu,
	.max_channels_get = channels,
};
#define DEFINE_MBOX(inst)                                                                          \
	static struct mbox_data data_##inst;                                                       \
	static const struct mbox_config config_##inst = {                                          \
		.rx_mask = BIT(DT_INST_PROP(inst, rx_channel)),                                    \
		.rx = DT_INST_REG_ADDR_BY_NAME(inst, rx),                                          \
		.tx = DT_INST_REG_ADDR_BY_NAME(inst, tx),                                          \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(inst, mbox_init, NULL, &data_##inst, &config_##inst, POST_KERNEL,    \
			      CONFIG_MBOX_INIT_PRIORITY, &api);
DT_INST_FOREACH_STATUS_OKAY(DEFINE_MBOX)
