# EVE Echoes mining bot

Run a bot for EVE Echoes game.

**Usage**:
    eve_bot <config.json>

## Runtime requirements
[`dkms`](https://wiki.archlinux.org/title/Dynamic_Kernel_Module_Support) package;
[`v4l2loopback-dkms`](https://github.com/v4l2loopback/v4l2loopback) kernel module;
[`v4l2loopback-utils`](https://github.com/v4l2loopback/v4l2loopback) package;
[`scrcpy`](https://github.com/Genymobile/scrcpy) utility;
[`sudo`] command (required to setup v4l2loopback devices).

See [scrcpy:Video4Linux](https://github.com/Genymobile/scrcpy/blob/master/doc/v4l2.md) for more details.

## Build dependencies

[`openCV`](https://github.com/opencv/opencv)
[`nlohmann_json`](https://github.com/nlohmann/json)

## Borrowed code

`V4L2 C++ Wrapper` by Michel Promonet is included in source code of this project. Full code taken from [github](https://github.com/mpromonet/libv4l2cpp) placed inside libv4l2cpp folder.