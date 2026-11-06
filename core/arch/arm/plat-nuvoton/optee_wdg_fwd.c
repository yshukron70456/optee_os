// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (C) 2026, Nuvoton
 */

/*
 * Hand the BL31 watchdog interrupts back to BL31.
 *
 * BL31 owns the watchdog pre-timeout interrupts and the stop SGI used by its
 * reset code (plat/nuvoton/npcm845x/npcm845x_psci.c). On the GICv2 they are
 * Group 0 like OP-TEE's own interrupts, signalled as FIQ. BL31 takes them at
 * EL3 only while the non-secure world runs; while OP-TEE runs they are taken
 * natively at S-EL1, here. OP-TEE knows nothing about them: without a handler
 * the default is to acknowledge the interrupt and mask it in the GIC, which
 * would silently disable the watchdog pre-timeout and leave a CPU that never
 * parks for the reset code to wait for.
 *
 * So register a handler that calls BL31 to run the same code as for the
 * interrupt taken at EL3. The call does not return: BL31 parks this CPU until
 * the watchdog resets the system. The interrupts are configured by BL31
 * (Group 0, priority 0x20) and must not be reconfigured here: OP-TEE would
 * give them priority 0x1, which is the one BL31 forwards to OP-TEE.
 */

#include <initcall.h>
#include <kernel/interrupt.h>
#include <kernel/thread.h>
#include <trace.h>

/* Keep in sync with include/plat/nuvoton/npcm845x/platform_def.h in BL31 */
#define NPCM_WDG_INT0			79
#define NPCM_WDG_INT1			80
#define NPCM_WDG_INT2			81
#define NPCM_STOP_SGI			10

/* BL31 SiP service, see plat/nuvoton/npcm845x/npcm845x_optee_fwd.c */
#define NPCM_FID_WDG_PRETIMEOUT		0x82000112U
#define NPCM_FID_WDG_STOP_CPU		0x82000113U

static enum itr_return npcm_wdg_itr_cb(struct itr_handler *h)
{
	struct thread_smc_args args = {
		.a0 = h->it == NPCM_STOP_SGI ? NPCM_FID_WDG_STOP_CPU :
					       NPCM_FID_WDG_PRETIMEOUT,
	};

	thread_smccc(&args);

	/*
	 * Only reached if BL31 refused the call (old BL31 without the
	 * service). Not handled: the framework masks a level interrupt, which
	 * would otherwise fire again at once.
	 */
	EMSG("BL31 did not take interrupt %zu", h->it);

	return h->it == NPCM_STOP_SGI ? ITRR_HANDLED : ITRR_NONE;
}

static TEE_Result npcm_wdg_fwd_init(void)
{
	static const size_t ids[] = {
		NPCM_WDG_INT0, NPCM_WDG_INT1, NPCM_WDG_INT2, NPCM_STOP_SGI,
	};
	TEE_Result res = TEE_SUCCESS;
	size_t n = 0;

	for (n = 0; n < ARRAY_SIZE(ids); n++) {
		res = interrupt_create_handler(interrupt_get_main_chip(),
					       ids[n], npcm_wdg_itr_cb, NULL,
					       0, NULL);
		if (res) {
			EMSG("Cannot register interrupt %zu: %#"PRIx32,
			     ids[n], res);
			return res;
		}
	}

	return TEE_SUCCESS;
}
driver_init(npcm_wdg_fwd_init);
