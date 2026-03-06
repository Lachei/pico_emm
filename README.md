# Pico Iot

This repository contains a base system for iot applications with the raspberry pi pico based on [the pico-sdk](https://github.com/raspberrypi/pico-sdk).

The basis for this project further are the [wlan freertos examples from the pico-example repository](https://github.com/raspberrypi/pico-examples/tree/master/pico_w/wifi/freertos).

[TOC]

## Intro Scope

The goal of this project is to have a simple, quickly-buildabla and easily extensible base pico system that supports the following base services:
- Easy setup of access point
- Dns server for access point for easy webserver access
- Dhcp client for hostname propagation
- FreeRTOS integration for multi-process support
- Easy permanent storage for config settings
- Modern tcp server which is easily extensible and can be reached via wifi (both via hotspot and normal wifi connection)
- Easy setup for adding pages to the web view
- Pre-implemented base authentication for the webserver
- Log system already in place
- Usb interface for easy debugging
- Wifi configuration via webserver and usb interface
- Access point activation via webserver and usb interface
- Hostname setting via webserver interface

## Capability details

This section gives a functional overview as well as tips for adopting the framework to your own needs.

### Webserver

As the webserver will be the main access point for people, this is where we shall begin.

By default the webserver will always be running and be listening on port 80 on the pico, thus will always accept incoming http requests
wich are sent against the ip of the pico with any modern browser.

Whether the client is connected to the pico via access point directly with the pico or the pico is configured to connect to a router
and the requests are sent to the pico like so does not matter.

By default the access point will always be set up if no wifi setup to connect to an external router was done before (should be the case on first flash).
The default ssid and password for the access point are `pico_iot` and `12345678` respectively and can be adopted in the `include/access_point.h` header.

## Build instructions

This project does require to have the pico_sdk installed, as well as the [Free-RTOS Kernel](https://github.com/FreeRTOS/FreeRTOS-Kernel/tree/main) downloaded
in some folder on the machine.

To build then simply run the following commands:
```bash
git clone https://github.com/Lachei/pico_iot.git
cd pico_iot
mkdir build
cd build
cmake .. -DPICO_SDK_PATH=<path> -DFREERTOS_KERNEL_PATH=<path> -DLIBMODBUS_STATIC_PATH=<path> -DLIBPIMORONI_PICO_PATH=<path> -DPRESTO_PATH=<path> --fresh -DPICO_BOARD=pimoroni_pico_plus2_w_rp2350
make -j12
```

To rebuild the project and upload to the pico without having to replug the pico run (requires the picotool to be installed):
```bash
make -j12 && picotool load -f dcdc-converter.uf2
```

