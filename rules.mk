.PHONEY: clean test clean-all default fake_phony

ifeq ($(findstring .a,$(PROJECT_NAME)),.a)
default: $(PROJECT_NAME)

$(PROJECT_NAME): $(REC_TARGETS) $(OBJECTS)
	$(AR) ru $@ $^
	$(STRIP) --strip-unneeded $@
else
EXE_EXTENSION=.elf

default: $(PROJECT_NAME)$(EXE_EXTENSION)-0x00000.bin $(PROJECT_NAME)$(EXE_EXTENSION)-0x10000.bin

$(PROJECT_NAME)$(EXE_EXTENSION): $(OBJECTS) $(REC_TARGETS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)
endif

fake_phony:

$(REC_TARGETS): %: fake_phony
	make -C $$(dirname $@) $$(basename $@)

$(PROJECT_NAME)$(EXE_EXTENSION)-0x00000.bin $(PROJECT_NAME)$(EXE_EXTENSION)-0x10000.bin: $(PROJECT_NAME)$(EXE_EXTENSION)
	$(ESPTOOL_FULL) elf2image $^

# note, the value used here is for a 32mbit flash.
# see here for more info: https://github.com/nodemcu/nodemcu-firmware/blob/master/docs/en/flash.md#sdk-init-data
init-flash:
	$(ESPTOOL_FULL) erase_flash
	$(ESPTOOL_FULL) write_flash 0x3fc000 $(SDK_ROOT)/sdk/bin/esp_init_data_default.bin
	touch $@

flash: init-flash $(PROJECT_NAME)$(EXE_EXTENSION)-0x00000.bin $(PROJECT_NAME)$(EXE_EXTENSION)-0x10000.bin
	$(ESPTOOL_FULL) write_flash 0 $(PROJECT_NAME)$(EXE_EXTENSION)-0x00000.bin 0x10000 $(PROJECT_NAME)$(EXE_EXTENSION)-0x10000.bin
	touch $@

clean-all: clean
	for f in $(REC_TARGETS) ; do make -C $$(dirname $$f) clean-all ; done

clean:
	rm -f $(PROJECT_NAME)$(EXE_EXTENSION) $(OBJECTS) $(PROJECT_NAME)$(EXE_EXTENSION)-0x00000.bin $(PROJECT_NAME)$(EXE_EXTENSION)-0x10000.bin flash init-flash $(ADDITIONAL_CLEAN)

test: flash
	gtkterm --speed $(BAUD_RATE) --port /dev/ttyUSB0
