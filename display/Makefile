include ../config.mk

LDLIBS = -nostdlib -Wl,--start-group -lmain -lnet80211 -lwpa -llwip -lpp -lphy -ldriver -Wl,--end-group -lc -lgcc

PROJECT_NAME=display
ADDITIONAL_MODULES=
OBJECTS=$(PROJECT_NAME).o \
	$(patsubst %,%.o,$(ADDITIONAL_MODULES))

REC_TARGETS=driver_lib/libdriver.a

include $(ROOT_DIR)/rules.mk
