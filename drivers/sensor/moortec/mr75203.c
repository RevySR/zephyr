/* SPDX-License-Identifier: Apache-2.0 */
#define DT_DRV_COMPAT moortec_mr75203

#include <zephyr/drivers/counter.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor/mr75203.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>

#define PVT_CONFIG     0x04
#define SYNTH          0x00
#define DISABLE        0x04
#define STATUS         0x08
#define SERIAL_WRITE   0x0c
#define HALT           0x10
#define SAMPLE_CONTROL 0x20
#define SERIAL_COMMAND (BIT(31) | BIT(27))

struct pvt_config {
	uintptr_t common, temperature, process, voltage;
	size_t temperature_size, voltage_size;
	const struct device *timer;
	uint32_t input_hz, h, g, cal5;
	int32_t j;
};

struct pvt_data {
	uint8_t temperatures, processes, monitors, channels;
	uint32_t converter_hz;
	int32_t sample[2];
};

static int poll(const struct pvt_config *c, uintptr_t reg, uint32_t mask, uint32_t expected)
{
	uint32_t start, now;

	counter_get_value(c->timer, &start);
	do {
		if ((sys_read32(reg) & mask) == expected) {
			return 0;
		}
		counter_get_value(c->timer, &now);
	} while ((uint32_t)(now - start) < counter_get_frequency(c->timer) / 50);
	return -ETIMEDOUT;
}

static void write_reg(uintptr_t reg, uint32_t value)
{
	sys_write32(value, reg);
	__asm__ volatile("fence iorw, iorw" ::: "memory");
}

static int serial_write(const struct pvt_config *c, uintptr_t base, uint8_t address, uint32_t value)
{
	int ret = poll(c, base + STATUS, BIT(0), 0);

	if (ret) {
		return ret;
	}
	write_reg(base + SERIAL_WRITE, SERIAL_COMMAND | (uint32_t)address << 24 | value);
	return poll(c, base + STATUS, BIT(0), 0);
}

int mr75203_temperature_count(const struct device *dev)
{
	struct pvt_data *d = dev->data;

	return d->temperatures;
}

int mr75203_channel_count(const struct device *dev)
{
	struct pvt_data *d = dev->data;

	return d->temperatures + d->monitors * d->channels;
}

int mr75203_read_channel(const struct device *dev, unsigned int id, int32_t *milli)
{
	const struct pvt_config *c = dev->config;
	struct pvt_data *d = dev->data;
	uint32_t raw;
	int ret;

	if (id >= mr75203_channel_count(dev)) {
		return -EINVAL;
	}
	if (id < d->temperatures) {
		ret = poll(c, c->temperature + 0x54 + 0x40 * id, 1, 1);
		if (ret) {
			return ret;
		}
		raw = sys_read32(c->temperature + 0x58 + 0x40 * id) & 0xffff;
		*milli = CLAMP((int64_t)c->g + (int64_t)c->h * raw / c->cal5 - c->h / 2 +
				       (int64_t)c->j * d->converter_hz / 1000000,
			       -40000, 125000);
	} else {
		unsigned int monitor = (id - d->temperatures) / d->channels;
		unsigned int channel = (id - d->temperatures) % d->channels;

		ret = poll(c, c->voltage + 0x234 + 0x200 * monitor, 1, 1);
		if (ret) {
			return ret;
		}
		raw = sys_read32(c->voltage + 0x240 + 0x200 * monitor + 4 * channel) & 0xffff;
		*milli = ((int32_t)raw * 90 - 245805) / 1024;
	}
	return 0;
}

static int fetch(const struct device *dev, enum sensor_channel channel)
{
	struct pvt_data *d = dev->data;
	int ret = 0;

	if (channel == SENSOR_CHAN_ALL || channel == SENSOR_CHAN_DIE_TEMP) {
		ret = mr75203_read_channel(dev, 0, &d->sample[0]);
	}
	if (!ret && (channel == SENSOR_CHAN_ALL || channel == SENSOR_CHAN_VOLTAGE)) {
		ret = mr75203_read_channel(dev, d->temperatures, &d->sample[1]);
	}
	return channel == SENSOR_CHAN_ALL || channel == SENSOR_CHAN_DIE_TEMP ||
			       channel == SENSOR_CHAN_VOLTAGE
		       ? ret
		       : -ENOTSUP;
}

static int get(const struct device *dev, enum sensor_channel channel, struct sensor_value *value)
{
	struct pvt_data *d = dev->data;
	int32_t sample;

	if (channel != SENSOR_CHAN_DIE_TEMP && channel != SENSOR_CHAN_VOLTAGE) {
		return -ENOTSUP;
	}
	sample = d->sample[channel == SENSOR_CHAN_VOLTAGE];
	value->val1 = sample / 1000;
	value->val2 = sample % 1000 * 1000;
	return 0;
}

static int init(const struct device *dev)
{
	const struct pvt_config *c = dev->config;
	struct pvt_data *d = dev->data;
	uint32_t cfg, divisor, synth;
	int ret;

	if (!device_is_ready(c->timer) || !counter_get_frequency(c->timer) || !c->input_hz ||
	    !c->cal5) {
		return -ENODEV;
	}
	cfg = sys_read32(c->common + PVT_CONFIG);
	d->temperatures = cfg & 0x1f;
	d->processes = (cfg >> 8) & 0x1f;
	d->monitors = (cfg >> 16) & 0x1f;
	d->channels = cfg >> 24;
	if (!d->temperatures || !d->monitors || !d->channels || d->channels > 16) {
		return -ENOTSUP;
	}
	if (0x5c + 0x40 * (d->temperatures - 1) > c->temperature_size ||
	    0x240 + 0x200 * (d->monitors - 1) + 4 * d->channels > c->voltage_size) {
		return -EINVAL;
	}
	divisor = CLAMP(DIV_ROUND_CLOSEST(c->input_hz, 6000000), 2, 514);
	d->converter_hz = c->input_hz / divisor;
	synth = ((divisor - 1) / 2) | (((divisor - 2) / 2) << 8) | (((divisor - 2) / 2) << 16) |
		BIT(24);
	write_reg(c->temperature + SAMPLE_CONTROL, 0);
	write_reg(c->temperature + HALT, 0);
	write_reg(c->temperature + SYNTH, synth);
	write_reg(c->temperature + DISABLE, 0);
	ret = serial_write(c, c->temperature, 1, 1);
	if (!ret) {
		ret = serial_write(c, c->temperature, 5, 0x100);
	}
	if (!ret) {
		ret = serial_write(c, c->temperature, 0, BIT(1) | BIT(3) | BIT(8));
	}
	if (ret) {
		return ret;
	}
	write_reg(c->process + HALT, 0);
	write_reg(c->process + DISABLE, BIT(d->processes) - 1);
	write_reg(c->process + SYNTH, synth);
	write_reg(c->voltage + SAMPLE_CONTROL, 0);
	write_reg(c->voltage + HALT, 0);
	write_reg(c->voltage + SYNTH, synth);
	write_reg(c->voltage + DISABLE, 0);
	ret = serial_write(c, c->voltage, 4, (BIT(d->channels) - 1) | BIT(20));
	if (!ret) {
		ret = serial_write(c, c->voltage, 1, 0);
	}
	if (!ret) {
		ret = serial_write(c, c->voltage, 5, 0x40);
	}
	if (!ret) {
		ret = serial_write(c, c->voltage, 0, BIT(1) | BIT(3) | BIT(8) | BIT(10));
	}
	return ret;
}

static DEVICE_API(sensor, api) = {.sample_fetch = fetch, .channel_get = get};

#define DEFINE_PVT(n)                                                                              \
	static const struct pvt_config config_##n = {                                              \
		.common = DT_INST_REG_ADDR_BY_NAME(n, common),                                     \
		.temperature = DT_INST_REG_ADDR_BY_NAME(n, ts),                                    \
		.process = DT_INST_REG_ADDR_BY_NAME(n, pd),                                        \
		.voltage = DT_INST_REG_ADDR_BY_NAME(n, vm),                                        \
		.temperature_size = DT_INST_REG_SIZE_BY_NAME(n, ts),                               \
		.voltage_size = DT_INST_REG_SIZE_BY_NAME(n, vm),                                   \
		.timer = DEVICE_DT_GET(DT_INST_PHANDLE(n, timeout_counter)),                       \
		.input_hz = DT_INST_PROP(n, clock_frequency),                                      \
		.h = DT_INST_PROP(n, moortec_ts_coeff_h),                                          \
		.g = DT_INST_PROP(n, moortec_ts_coeff_g),                                          \
		.j = DT_INST_PROP(n, moortec_ts_coeff_j),                                          \
		.cal5 = DT_INST_PROP(n, moortec_ts_coeff_cal5),                                    \
	};                                                                                         \
	static struct pvt_data data_##n;                                                           \
	DEVICE_DT_INST_DEFINE(n, init, NULL, &data_##n, &config_##n, POST_KERNEL,                  \
			      CONFIG_SENSOR_INIT_PRIORITY, &api);
DT_INST_FOREACH_STATUS_OKAY(DEFINE_PVT)
