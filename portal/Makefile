include ../config.mk

LDLIBS = -nostdlib -Wl,--start-group -lmain -lnet80211 -lwpa -llwip -lpp -lphy -Wl,--end-group -lc -lgcc

PROJECT_NAME=portal
ADDITIONAL_MODULES=sntp
OBJECTS=$(PROJECT_NAME).o \
	$(patsubst %,%.o,$(ADDITIONAL_MODULES))

REC_TARGETS=driver_lib/libdriver.a

BAUD_RATE=57600

OPENSCAD=openscad
OPENSCADFLAGS=--camera=0,0,0,20,20,90,0 --viewall --autocenter

ADDITIONAL_CLEAN=\
	models/lid.png\
	models/case.png\
	models/lid.stl\
	models/case.stl

include $(ROOT_DIR)/rules.mk

.PHONY: models png stl

models: png stl

png: models/lid.png models/case.png

stl: models/lid.stl models/case.stl

models/lid.stl models/lid.png: models/led-case.scad
	$(OPENSCAD) $(OPENSCADFLAGS) -D makelid=true -o $@ $<

models/case.stl models/case.png: models/led-case.scad
	$(OPENSCAD) $(OPENSCADFLAGS) -D makelid=false -o $@ $<
