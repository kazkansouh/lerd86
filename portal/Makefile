include ../config.mk

CFLAGS = -I. -mlongcalls -Idriver_lib/include/ -I${SDK_INCLUDE} -DICACHE_FLASH
LDLIBS = -nostdlib -Ldriver_lib -L${SDK_LIBS} -Wl,--start-group -lmain -lnet80211 -lwpa -llwip -lpp -lphy -ldriver -Wl,--end-group -lc -lgcc
LDFLAGS = -T${SDK_ROOT}/sdk/ld/eagle.app.v6.ld

PROJECT_NAME=portal


${PROJECT_NAME}-0x00000.bin: ${PROJECT_NAME}
	PATH=${TOOLCHAIN_ROOT}/bin/:$${PATH} ${ESPTOOL} elf2image $^

${PROJECT_NAME}: ${PROJECT_NAME}.o

${PROJECT_NAME}.o: ${PROJECT_NAME}.c

.PHONEY: clean test
flash: ${PROJECT_NAME}-0x00000.bin
	PATH=${TOOLCHAIN_ROOT}/bin/:$${PATH} ${ESPTOOL} write_flash 0 ${PROJECT_NAME}-0x00000.bin 0x40000 ${PROJECT_NAME}-0x40000.bin
	touch $@

clean:
	rm -f ${PROJECT_NAME} ${PROJECT_NAME}.o ${PROJECT_NAME}-0x00000.bin ${PROJECT_NAME}-0x40000.bin flash

test: flash
	gtkterm --speed 9600 --port /dev/ttyUSB0
