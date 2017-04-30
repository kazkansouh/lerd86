include ../config.mk

CFLAGS = -I. -mlongcalls -Idriver_lib/include/ -I${SDK_INCLUDE} -DICACHE_FLASH
LDLIBS = -nostdlib -Ldriver_lib -L${SDK_LIBS} -Wl,--start-group -lmain -lnet80211 -lwpa -llwip -lpp -lphy -ldriver -Wl,--end-group -lc -lgcc
LDFLAGS = -T${SDK_ROOT}/sdk/ld/eagle.app.v6.ld

PROJECT_NAME=portal
ADDITIONAL_MODULES=sntp
OBJECTS=$(PROJECT_NAME).o $(patsubst %,%.o,$(ADDITIONAL_MODULES))

${PROJECT_NAME}-0x00000.bin: ${PROJECT_NAME}
	PATH=${TOOLCHAIN_ROOT}/bin/:$${PATH} ${ESPTOOL} elf2image $^

driver_lib/libdriver.a:
	make -C driver_lib

${PROJECT_NAME}: driver_lib/libdriver.a ${OBJECTS}

.PHONEY: clean test
flash: ${PROJECT_NAME}-0x00000.bin
	PATH=${TOOLCHAIN_ROOT}/bin/:$${PATH} ${ESPTOOL} write_flash 0 ${PROJECT_NAME}-0x00000.bin 0x10000 ${PROJECT_NAME}-0x10000.bin
	touch $@

clean:
	rm -f ${PROJECT_NAME} ${OBJECTS} ${PROJECT_NAME}-0x00000.bin ${PROJECT_NAME}-0x10000.bin flash
	make -C driver_lib clean

test: flash
	gtkterm --speed 9600 --port /dev/ttyUSB0
