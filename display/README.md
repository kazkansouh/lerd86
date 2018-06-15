# PureGym activity tracker

Project that uses an ESP12-F connected to shift registers via SPI to
display number of active people in members gym. Contains support for:

* Starting in hotspot mode if can not connect to wifi to accept configuration.
* HTTP server configuration portal (supports `GET`). I.e. for PureGym member name/pin that are stored in flash.
* Basic LED controller that displays 8 LEDs at a given brightness that can either flash, pulse, left/right shift.
* HTTP client (supports `GET`, `POST`, `Transfer-Encoding: Chunked`, partial cookie jar).
