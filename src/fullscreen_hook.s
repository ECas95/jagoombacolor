@ Fullscreen renderer interworking hooks.
@
@ This file is included by all.s after lcd.s.  lcd.s is preprocessed with
@ vblankinterrupt renamed to fullscreen_original_vblankinterrupt, leaving the
@ public vblankinterrupt symbol available for the wrapper below.

	.text
	.align
	.pool

	.global _scanlinehook

	global_func vblankinterrupt
vblankinterrupt:
	@ Preserve the complete interrupted context.  The original handler uses the
	@ emulator's register conventions and the fullscreen helpers are Thumb C.
	stmfd sp!,{r0-r12,lr}
	blx_long fullscreen_vblank_pre
	bl fullscreen_original_vblankinterrupt
	blx_long fullscreen_vblank_post
	ldmfd sp!,{r0-r12,pc}

	global_func fullscreen_scanline_hook
fullscreen_scanline_hook:
	@ The GB CPU core keeps emulated state in ARM registers, so preserve every
	@ register around the C line compositor, then continue through the stock
	@ timing/IRQ/HDMA scanline handler.
	stmfd sp!,{r0-r12,lr}
	blx_long fullscreen_scanline_render
	ldmfd sp!,{r0-r12,lr}
	b default_scanlinehook

	.align
	.pool
