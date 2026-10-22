/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/toolchain.h>
#include <zephyr/linker/sections.h>

GTEXT(soc_reset_hook)

/* Called before a stack exists. Do not touch clocks, rails or DDR here. */
SECTION_FUNC(TEXT, soc_reset_hook)
	csrc mstatus, 8
	ret

/* CLIC acknowledges vectored interrupts on entry, unlike CLINT's mip. */
GTEXT(__soc_handle_irq)
SECTION_FUNC(TEXT, __soc_handle_irq)
	ret
