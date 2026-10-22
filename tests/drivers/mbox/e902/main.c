/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/ztest.h>
#include <string.h>
/* Test the driver against host register banks, without DT hardware instances. */
#include "mbox_e902.c"

static uint32_t rx[5], tx[5];
static unsigned int received;
static struct mbox_data data;
static const struct mbox_config config = {
	.rx = (uintptr_t)rx,
	.tx = (uintptr_t)tx,
	.rx_mask = BIT(0),
};
static const struct device mailbox = {.config = &config, .data = &data, .api = &api};

static void callback(const struct device *dev, uint32_t channel, void *user, struct mbox_msg *msg)
{
	zassert_equal(dev, &mailbox);
	zassert_equal(channel, 0);
	zassert_is_null(msg);
	zassert_equal(rx[E902_MBOX_CLEAR / 4], BIT(0));
	(*(unsigned int *)user)++;
}
static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	memset(rx, 0, sizeof(rx));
	memset(tx, 0, sizeof(tx));
	memset(&data, 0, sizeof(data));
	received = 0;
	zassert_ok(mbox_register_callback(&mailbox, 0, callback, &received));
}
ZTEST(e902, test_pending_message_survives_enable)
{
	rx[E902_MBOX_STATUS / 4] = BIT(0);
	rx[E902_MBOX_MASK / 4] = BIT(3);
	zassert_ok(mbox_set_enabled(&mailbox, 0, true));
	zassert_equal(rx[E902_MBOX_CLEAR / 4], 0);
	zassert_equal(rx[E902_MBOX_MASK / 4], BIT(3) | BIT(0));
	zassert_equal(mbox_e902_poll(&mailbox), 1);
	zassert_equal(received, 1);
}
ZTEST(e902, test_disable_preserves_pending_message)
{
	zassert_ok(mbox_set_enabled(&mailbox, 0, true));
	rx[E902_MBOX_CLEAR / 4] = 0;
	rx[E902_MBOX_STATUS / 4] = BIT(0);
	zassert_ok(mbox_set_enabled(&mailbox, 0, false));
	zassert_equal(rx[E902_MBOX_CLEAR / 4], 0);
	zassert_equal(mbox_e902_poll(&mailbox), 0);
}
ZTEST(e902, test_no_null_callback_while_enabled)
{
	zassert_ok(mbox_set_enabled(&mailbox, 0, true));
	zassert_equal(mbox_register_callback(&mailbox, 0, NULL, NULL), -EBUSY);
	zassert_ok(mbox_set_enabled(&mailbox, 0, false));
	zassert_ok(mbox_register_callback(&mailbox, 0, NULL, NULL));
	zassert_equal(mbox_set_enabled(&mailbox, 0, true), -EINVAL);
}
ZTEST(e902, test_repeated_state_and_send)
{
	zassert_equal(mbox_set_enabled(&mailbox, 0, false), -EALREADY);
	zassert_ok(mbox_set_enabled(&mailbox, 0, true));
	zassert_equal(mbox_set_enabled(&mailbox, 0, true), -EALREADY);
	zassert_ok(mbox_send(&mailbox, 0, NULL));
	zassert_equal(tx[E902_MBOX_GENERATE / 4], BIT(6));
	zassert_equal(mbox_send(&mailbox, 1, NULL), -EINVAL);
}
ZTEST(e902, test_probe_masks_only_owned_channel)
{
	rx[E902_MBOX_MASK / 4] = BIT(0) | BIT(3);
	zassert_ok(mbox_init(&mailbox));
	zassert_equal(rx[E902_MBOX_MASK / 4], BIT(3));
	zassert_equal(rx[E902_MBOX_CLEAR / 4], BIT(0));
	zassert_false(data.enabled);
}
ZTEST_SUITE(e902, NULL, NULL, before, NULL, NULL);
