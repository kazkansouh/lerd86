include ../config.mk

LDLIBS = -nostdlib -Wl,--start-group -lmain -lnet80211 -lwpa -llwip -lpp -lphy -Wl,--end-group -lc -lgcc

PROJECT_NAME=portal
ADDITIONAL_MODULES=sntp
OBJECTS=$(PROJECT_NAME).o \
	$(patsubst %,%.o,$(ADDITIONAL_MODULES))

REC_TARGETS=driver_lib/libdriver.a

BAUD_RATE=57600

include $(ROOT_DIR)/rules.mk
