/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/drivers/interrupt_controller/riscv_clic.h>
#include <errno.h>

#define CLIC_BASE           DT_REG_ADDR(DT_INST(0, thead_e902_clic))
#define CLIC_CONFIG         (CLIC_BASE + 0x00U)
#define CLIC_INFO           (CLIC_BASE + 0x04U)
#define CLIC_THRESHOLD      (CLIC_BASE + 0x08U)
#define CLIC_PENDING(irq)   (CLIC_BASE + 0x1000U + 4U * (irq))
#define CLIC_ENABLE(irq)    (CLIC_PENDING(irq) + 1U)
#define CLIC_ATTRIBUTE(irq) (CLIC_PENDING(irq) + 2U)
#define CLIC_CONTROL(irq)   (CLIC_PENDING(irq) + 3U)
#define CLIC_VECTOR         1U

BUILD_ASSERT(CONFIG_RISCV_VECTORED_MODE);
BUILD_ASSERT(!IS_ENABLED(CONFIG_CLIC));
static uint8_t control_bits;

void riscv_clic_irq_enable(uint32_t irq)
{
	if (irq < CONFIG_NUM_IRQS) {
		sys_write8(1, CLIC_ENABLE(irq));
	}
}

void riscv_clic_irq_disable(uint32_t irq)
{
	if (irq < CONFIG_NUM_IRQS) {
		sys_write8(0, CLIC_ENABLE(irq));
	}
}

int riscv_clic_irq_is_enabled(uint32_t irq)
{
	return irq < CONFIG_NUM_IRQS && (sys_read8(CLIC_ENABLE(irq)) & 1);
}

void riscv_clic_irq_vector_set(uint32_t irq)
{
	if (irq < CONFIG_NUM_IRQS) {
		sys_write8(sys_read8(CLIC_ATTRIBUTE(irq)) | CLIC_VECTOR, CLIC_ATTRIBUTE(irq));
	}
}

void riscv_clic_irq_priority_set(uint32_t irq, uint32_t priority, uint32_t flags)
{
	uint32_t max_level = (1U << control_bits) - 1U;
	uint32_t shift = 8U - control_bits;

	if (irq >= CONFIG_NUM_IRQS) {
		return;
	}
	/* All implemented control bits encode level, matching the vendor setup. */
	priority = MIN(priority, max_level);
	sys_write8((uint8_t)((priority << shift) | ((1U << shift) - 1)), CLIC_CONTROL(irq));
	/* Keep SHV set: the SoC uses Zephyr's normal vectored ISR dispatch. */
	sys_write8((uint8_t)(CLIC_VECTOR | ((flags & 3U) << 1)), CLIC_ATTRIBUTE(irq));
}

static int e902_interrupt_init(void)
{
	uint32_t bits = (sys_read32(CLIC_INFO) >> 21) & 0xfU;

	for (unsigned int irq = 0; irq < CONFIG_NUM_IRQS; irq++) {
		sys_write8(0, CLIC_ENABLE(irq));
		sys_write8(0, CLIC_PENDING(irq));
		sys_write8(CLIC_VECTOR, CLIC_ATTRIBUTE(irq));
	}
	if (bits > 8) {
		return -EINVAL;
	}
	control_bits = (uint8_t)bits;
	sys_write32(0, CLIC_THRESHOLD);
	sys_write8((uint8_t)((sys_read8(CLIC_CONFIG) & ~0x1eU) | (bits << 1)), CLIC_CONFIG);
	return 0;
}

SYS_INIT(e902_interrupt_init, PRE_KERNEL_1, CONFIG_INTC_INIT_PRIORITY);
