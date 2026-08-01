#ifndef __INCLUDES_H__
#define __INCLUDES_H__

#ifdef __cplusplus
	extern "C" {
#endif

#ifndef ARRSIZE
#define ARRSIZE(xxxx) (sizeof((xxxx))/sizeof((xxxx)[0]))
#endif

#include "config.h"

//#if !RESIZABLE
//#define XGB_sram XGB_SRAM
//#define END_of_exram END_OF_EXRAM
//#endif

#include <stdio.h>
#include <string.h>
#include "gba.h"

/*
 * The legacy local gba.h defines bool itself and cannot be combined with the
 * current libgba gba_dma.h.  Keep the fullscreen copy self-contained and use
 * DMA3 directly.  byteCount must be word-aligned, which the 240x160 Mode 4
 * framebuffer is.
 */
static __inline void dmaCopy(const void *source, void *destination, u32 byteCount)
{
	volatile u32 *dma3Source = (volatile u32 *)0x040000D4;
	volatile u32 *dma3Destination = (volatile u32 *)0x040000D8;
	volatile u32 *dma3Control = (volatile u32 *)0x040000DC;
	volatile u16 *dma3ControlHigh = (volatile u16 *)0x040000DE;

	*dma3ControlHigh = 0;
	*dma3Source = (u32)source;
	*dma3Destination = (u32)destination;
	*dma3Control = (byteCount >> 2) | (1u << 26) | (1u << 31);
}

#include "asmcalls.h"
//#include "fs.h"
#include "minilzo.107/minilzo.h"
#include "main.h"
#include "ui.h"
#include "sram.h"
#include "rumble.h"
#include "mbclient.h"
#include "cache.h"
#include "dma.h"
#include "pocketnes_text.h"

#if MOVIEPLAYER
#include "filemenu.h"
#endif

#ifdef __cplusplus
	}
#endif

#endif
