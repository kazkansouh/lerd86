SDK_ROOT=externals/esp-open-sdk
TOOLCHAIN_ROOT=${SDK_ROOT}/xtensa-lx106-elf

.PHONY: toolchain clean-toolchain
toolchain: config.mk ${TOOLCHAIN_ROOT}/bin/xtensa-lx106-elf-gcc

config.mk:
	echo "ROOT_DIR=`pwd`" > $@
	echo "SDK_ROOT=\$${ROOT_DIR}/${SDK_ROOT}" >> $@
	echo "TOOLCHAIN_ROOT=\$${SDK_ROOT}/xtensa-lx106-elf" >> $@
	echo "SDK_INCLUDE=\$${SDK_ROOT}/sdk/include" >> $@
	echo "SDK_LIBS=\$${SDK_ROOT}/sdk/lib" >> $@
	echo "CC=\$${TOOLCHAIN_ROOT}/bin/xtensa-lx106-elf-gcc" >> $@
	echo "AR=\$${TOOLCHAIN_ROOT}/bin/xtensa-lx106-elf-ar" >> $@
	echo "STRIP=\$${TOOLCHAIN_ROOT}/bin/xtensa-lx106-elf-strip" >> $@
	echo "ESPTOOL=\$${TOOLCHAIN_ROOT}/bin/esptool.py" >> $@
	echo "LDFLAGS=-L\$${SDK_ROOT}/sdk/ld/ -Teagle.app.v6.ld -L\$${SDK_LIBS}" >> $@
	echo "CFLAGS=-I. -mlongcalls -I\$${SDK_INCLUDE} -DICACHE_FLASH -Idriver_lib/include/" >> $@
	echo "ESPTOOL_FULL=PATH=\$${TOOLCHAIN_ROOT}/bin/:\$${PATH} \$${ESPTOOL}" >> $@
	echo "BAUD_RATE=115200" >> $@

${TOOLCHAIN_ROOT}/bin/xtensa-lx106-elf-gcc:
	cd ${SDK_ROOT} && make STANDALONE=n

clean-toolchain:
	-cd ${SDK_ROOT} && make clean
	-rm config.mk
