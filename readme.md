# EVE Echoes mining bot

Run a bot for EVE Echoes game.

**Usage**:
    eve_bot <config.json>

## System requirements

[`scrcpy`](https://github.com/Genymobile/scrcpy) utility must be installed.
The module `v4l2loopback` must be installed and v4l2loopback device configured properly. See [scrcpy:Video4Linux](https://github.com/Genymobile/scrcpy/blob/master/doc/v4l2.md) for detailes.

## Build dependencies

[`openCV`](https://github.com/opencv/opencv)
[`nlohmann_json`](https://github.com/nlohmann/json)

## Borrowed code

`V4L2 C++ Wrapper` by Michel Promonet is included in source code of this project. Full code taken from [github](https://github.com/mpromonet/libv4l2cpp) placed inside libv4l2cpp folder.