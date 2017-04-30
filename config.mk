# TODO: improve handling of below variable
ROOT_DIR=/home/kazza/lerd86

SDK_ROOT=${ROOT_DIR}/externals/esp-open-sdk
TOOLCHAIN_ROOT=${SDK_ROOT}/xtensa-lx106-elf
SDK_INCLUDE = ${SDK_ROOT}/sdk/include
SDK_LIBS = ${SDK_ROOT}/sdk/lib
CC = ${TOOLCHAIN_ROOT}/bin/xtensa-lx106-elf-gcc
AR = ${TOOLCHAIN_ROOT}/bin/xtensa-lx106-elf-ar
STRIP = ${TOOLCHAIN_ROOT}/bin/xtensa-lx106-elf-strip
ESPTOOL = ${TOOLCHAIN_ROOT}/bin/esptool.py
