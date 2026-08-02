# Compatibility wrapper for case-sensitive filesystems and current devkitARM.
# The upstream project names its file Makefile but recursively requests makefile.
ROOT_MAKEFILE_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
include $(ROOT_MAKEFILE_DIR)Makefile

# GCC 14 promotes this legacy C conversion to an error.  The original code
# intentionally overlays stateheader/configdata storage, so preserve that build
# behaviour while the relevant assignment remains unchanged.
CFLAGS += -Wno-error=incompatible-pointer-types

# Route standalone frontend and in-game menu transitions through the fullscreen
# gate. Calls from main.c are wrapped at link time; the ui() wrapper covers the
# internal L+R menu whose visibility helpers are in the same translation unit.
LDFLAGS += -Wl,--wrap=make_ui_visible \
           -Wl,--wrap=make_ui_invisible \
           -Wl,--wrap=ui
