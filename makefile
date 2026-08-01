# Compatibility wrapper for case-sensitive filesystems and current devkitARM.
# The upstream project names its file Makefile but recursively requests makefile.
ROOT_MAKEFILE_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
include $(ROOT_MAKEFILE_DIR)Makefile

# GCC 14 promotes this legacy C conversion to an error.  The original code
# intentionally overlays stateheader/configdata storage, so preserve that build
# behaviour while the relevant assignment remains unchanged.
CFLAGS += -Wno-error=incompatible-pointer-types
