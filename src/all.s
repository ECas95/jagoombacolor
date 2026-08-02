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

@ Keep the original hardware VBlank and emulated-frame renderer available
@ behind wrappers. timeout.s was included before these aliases, so its call to
@ newframe_vblank resolves to the public wrapper in fullscreen_hook.s.
	#define vblankinterrupt fullscreen_original_vblankinterrupt
	#define newframe_vblank fullscreen_original_newframe_vblank
	#include "lcd.s"
	#undef newframe_vblank
	#undef vblankinterrupt
	#include "fullscreen_hook.s"

	#include "io.s"
	#include "sound.s"
	#include "sgb.s"
	#include "gbpalettes.s"
	#include "state.s"

	.end
