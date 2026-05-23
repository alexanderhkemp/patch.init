# cheap-heat-guitar Makefile for Hothouse/Daisy Seed
# Cleveland Music Co Hothouse DSP Platform

TARGET = cheap-heat-guitar

# Use DaisyToolchain GCC
GCC_PATH = /Library/DaisyToolchain/0.2.0/arm/bin

# Sources
CPP_SOURCES = CheapHeatGuitar.cpp hothouse.cpp

# Hothouse HAL header location
C_INCLUDES += -I.

# Library Locations
LIBDAISY_DIR = ../libDaisy
DAISYSP_DIR = ../DaisySP

# Core location, and generic Makefile
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
