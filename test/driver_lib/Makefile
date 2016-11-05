include ../../config.mk

CFLAGS = -Os -Iinclude -mlongcalls -I.. -Wl,-EL -I${SDK_INCLUDE} -DICACHE_FLASH -mtext-section-literals -ffunction-sections -fdata-sections 
LDLIBS = -nostdlib -Wl,--start-group -lmain -lnet80211 -lwpa -llwip -lpp -lphy -ldriver -Wl,--end-group -lgcc
LDFLAGS = -T${SDK_ROOT}/sdk/ld/eagle.app.v6.ld
#STRIP = true

SOURCES = $(wildcard driver/*.c)
OBJECTS = $(patsubst %.c,%.o,$(SOURCES))

libdriver.a: $(OBJECTS)
	$(AR) ru $@ $^
	$(STRIP) --strip-unneeded $@

clean:
	rm -f libdriver.a $(OBJECTS)
