# - Makefile — 
# vim: ts=2 noet sw=0
#
# Usage:
#   make                  Build static and shared libraries
#   make clean            Remove build artefacts
#
# Variables you can override on the command line:
#   CC          C compiler              (default: gcc)
#   AR          Archiver                (default: ar)
#   PREFIX      Installation prefix     (default: /usr/local)
#   CROSS       Cross-compile prefix    (e.g. arm-linux-gnueabihf-)
#   DEBUG       Set to 1 for -O0 -g     (default: 0)
# =============================================================================


CC      ?= gcc
AR      ?= ar
PREFIX  ?= /usr/local
CROSS   ?=
DEBUG   ?= 0

ifneq ($(CROSS),)
  CC  := $(CROSS)$(CC)
  AR  := $(CROSS)$(AR)
endif

LIBNAME    := rtl836x

BUILD_DIR   := build

ifeq ($(DEBUG),1)
  CFLAGS_EXTRA := -O0 -g
else
  CFLAGS_EXTRA := -O2
endif

# TODO:
#  * remove RTK_DEV & RTK_ADDR -> CLI
RTK_ADDR   ?= 0x58
RTK_DEV    ?= /dev/i2c-0
CFLAGS := $(CFLAGS_EXTRA) -Wall -pedantic -Wextra -ffunction-sections -fdata-sections -DRTK_I2C_ADDR=$(RTK_ADDR) -DRTK_I2C_DEV=\"$(RTK_DEV)\"

ifeq ($(SHARED),1)
  CFLAGS += $(CFLAGS) -fPIC
  LIB  := lib$(LIBNAME).so
else
  LIB  := lib$(LIBNAME).a
endif

SRCS := $(shell ls *.c)
OBJS := $(addprefix $(BUILD_DIR)/, $(SRCS:.c=.o))

$(BUILD_DIR)/%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

all: $(OBJS)

$(BUILD_DIR)/c.d: $(SRCS)
	mkdir -p "$(BUILD_DIR)"
	$(CC) $(CFLAGS) -MM $(SRCS) > $@

DEPS := $(BUILD_DIR)/c.d
dep:
	rm -f $(BUILD_DIR)
	$(MAKE) $(DEPS)

-include $(DEPS)

.PHONY: all clean dep

clean:
	rm -rf $(BUILD_DIR)

