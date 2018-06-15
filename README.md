# lerd86

A monolithic repository for projects related to ESP8622 (and some related Arduino code).

## Build System

The build system used for all projects based off [open-esp-sdk](/pfalcon/esp-open-sdk) with some additional makefiles. To prepare the build system, enter the following:

```bash
# checkout esp-open-sdk in externals directory
git submodule update --init

# trigger toolchain build
make
```

Once the toolchain is built, it can be found in `externals/esp-open-sdk/xtensa-lx106-elf/` with the SDK in `externals/esp-open-sdk/sdk/`. All needed paths are saved into `config.mk` when the toolchain is built, which defines the build environment and is included in all of the project specific makefiles.

## Projects

The following are the projects:

* [Display](display/) is a remake of [PiPuGym](/kazkansouh/PiPuGym) on the ESP8266, it contains a simple HTTP server and HTTP client.

* [Portal](portal/) and [Time Print](timeprint/) are two projects, for ESP8266 and Arduino, respectively, that work together over UART. Together they provide an internet connected clock that can be controlled via web browser and displays on an LED matrix.
