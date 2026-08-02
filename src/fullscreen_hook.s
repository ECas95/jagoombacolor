@ Fullscreen renderer interworking hooks.
@
@ lcd.s is preprocessed with vblankinterrupt and newframe_vblank renamed to
@ private originals.  timeout.s still calls the public newframe_vblank wrapper
@ below, giving the fullscreen compositor one deterministic call per emulated
@ Game Boy frame.

	.text
	.align
	.pool

	.global _scanlinehook

	global_func vblankinterrupt
vblankinterrupt:
	@ Preserve the complete interrupted context. The original handler uses the
	@ emulator's register conventions and the fullscreen helpers are Thumb C.
	stmfd sp!,{r0-r12,lr}

	blx_long fullscreen_gate_refresh
	blx_long fullscreen_vblank_pre
	bl fullscreen_original_vblankinterrupt
	blx_long fullscreen_gate_refresh
	blx_long fullscreen_vblank_post

	ldmfd sp!,{r0-r12,pc}

	global_func newframe_vblank
newframe_vblank:
	@ Preserve the return address expected by timeout.s, run the stock emulated
	@ frame-boundary work, then compose a complete 160x144 frame in one pass.
	stmfd sp!,{lr}
	bl fullscreen_original_newframe_vblank
	stmfd sp!,{r0-r12,lr}
	blx_long fullscreen_compose_frame_now
	ldmfd sp!,{r0-r12,lr}
	ldmfd sp!,{pc}

	global_func fullscreen_scanline_hook
fullscreen_scanline_hook:
	@ V5 no longer composes through the scanline pointer. Keep this hook timing-
	@ transparent because fullscreen_vblank_post may still install it while the
	@ backend is active.
	b default_scanlinehook

	.align
	.pool
