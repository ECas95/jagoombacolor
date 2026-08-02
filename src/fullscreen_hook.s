@ Fullscreen renderer interworking hooks.
@
@ lcd.s is preprocessed with vblankinterrupt and newframe_vblank renamed to
@ private originals. timeout.s still calls the public newframe_vblank wrapper
@ below, giving the fullscreen compositor one deterministic call per emulated
@ Game Boy frame.

	.text
	.align
	.pool

	.global _scanlinehook

	global_func vblankinterrupt
vblankinterrupt:
	@ Preserve the complete interrupted context. Fourteen registers keep the
	@ IRQ stack 8-byte aligned while the wrapper calls Thumb C helpers.
	stmfd sp!,{r0-r12,lr}

	blx_long fullscreen_gate_refresh
	blx_long fullscreen_vblank_pre
	bl fullscreen_original_vblankinterrupt
	blx_long fullscreen_gate_refresh
	blx_long fullscreen_vblank_post

	ldmfd sp!,{r0-r12,pc}

	global_func newframe_vblank
newframe_vblank:
	@ Save two registers, not LR alone. The former one-word push misaligned the
	@ stack for every C call made by the stock newframe routine.
	stmfd sp!,{r12,lr}
	bl fullscreen_original_newframe_vblank

	@ Compose a complete native frame after the stock frame-boundary bookkeeping.
	stmfd sp!,{r0-r12,lr}
	blx_long fullscreen_compose_frame_now
	ldmfd sp!,{r0-r12,lr}

	ldmfd sp!,{r12,pc}

	global_func fullscreen_scanline_hook
fullscreen_scanline_hook:
	@ V5+ does not compose through the per-scanline pointer. Preserve the stock
	@ timing, IRQ and HDMA path unchanged.
	b default_scanlinehook

	.align
	.pool
