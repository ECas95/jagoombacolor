@I need to do this to get around limitations of gnu assembler
	#include "config.h"
	#include "equates.h"
	#include "gbz80mac.h"
	
	.global MULTIBOOT_LIMIT
	
	#include "gbz80.s"
	#include "timeout.s"
	#include "gamespecific.s"
	#include "memory.s"
	#include "cart.s"
	#include "mappers.s"

@ Keep the original renderer available behind a wrapper.  C code and the IRQ
@ table continue to reference vblankinterrupt, which is implemented by
@ fullscreen_hook.s below.
	#define vblankinterrupt fullscreen_original_vblankinterrupt
	#include "lcd.s"
	#undef vblankinterrupt
	#include "fullscreen_hook.s"

	#include "io.s"
	#include "sound.s"
	#include "sgb.s"
	#include "gbpalettes.s"
	#include "state.s"

	.end
