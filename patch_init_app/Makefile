# Project Name
TARGET = patch_init_app

# Toolchain
GCC_PATH ?= /Library/DaisyToolchain/0.2.0/arm/bin

# Sources
CPP_SOURCES = PatchInitApp.cpp

# Library Locations
LIBDAISY_DIR = ../libDaisy
DAISYSP_DIR = ../DaisySP

# Core location, and generic Makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

 libs:
	$(MAKE) -C $(LIBDAISY_DIR) GCC_PATH=$(GCC_PATH)
	$(MAKE) -C $(DAISYSP_DIR) GCC_PATH=$(GCC_PATH)

 all: libs
