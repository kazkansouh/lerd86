include ../../config.mk

CFLAGS += -Os -Iinclude -I.. -Wl,-EL -mtext-section-literals -ffunction-sections -fdata-sections 
#STRIP = true

PROJECT_NAME=libdriver.a
SOURCES = $(wildcard driver/*.c)
OBJECTS = $(patsubst %.c,%.o,$(SOURCES))

include $(ROOT_DIR)/rules.mk
