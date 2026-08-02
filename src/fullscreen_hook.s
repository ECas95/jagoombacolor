@ Fullscreen renderer interworking hooks.
@
@ lcd.s is preprocessed with vblankinterrupt and newframe_vblank renamed to
@ private originals. timeout.s still calls the public newframe_vblank wrapper.

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

	@ Once fullscreen is active, do not run the complete Mode 0 renderer a second
	@ time.  The emulator only needs the VBlank completion flag here; the bitmap
	@ compositor and presentation run through the fullscreen hooks below.
	ldr r0,=fullscreen_active
	ldrb r0,[r0]
	cmp r0,#0
	beq 1f
	mov r0,#1
	strb_ r0,vblank_happened
	b 2f
1:
	bl fullscreen_original_vblankinterrupt
2:
	blx_long fullscreen_gate_refresh
	blx_long fullscreen_vblank_post

	ldmfd sp!,{r0-r12,pc}

	global_func newframe_vblank
newframe_vblank:
	@ Save two registers, not LR alone. This keeps the System stack aligned for
	@ the original frame routine and the Thumb compositor call.
	stmfd sp!,{r12,lr}
	bl fullscreen_original_newframe_vblank

	@ Compose one complete native frame after stock frame-boundary bookkeeping.
	stmfd sp!,{r0-r12,lr}
	blx_long fullscreen_compose_frame_now
	ldmfd sp!,{r0-r12,lr}

	ldmfd sp!,{r12,pc}

	global_func fullscreen_scanline_hook
fullscreen_scanline_hook:
	@ Full-frame composition no longer depends on the per-scanline hook. Preserve
	@ the stock timing, IRQ and HDMA path when the stock renderer is active.
	b default_scanlinehook

	.align
	.pool
