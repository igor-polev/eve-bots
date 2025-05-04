/*
	EVE Echoes bot for Linux.
	Author: Igor Polev.

	AndroidBot class implementation.
*/

#include "androidbot.hpp"

#include <iostream>

const bool AndroidBot::init(const json &settings) {
    _bot_state = states::uninitialized;

    try {
        _adb_serial     = settings.at("adb_serial");
        _v4l2_dev_name  = settings.at("v4l2_device");
        _scr_res.x      = settings.at("resolution_x");
        _scr_res.y      = settings.at("resolution_y");
        _check_interval = settings.at("check_interval");
    }
    catch(const json::out_of_range& e) {
		std::cout << "--- ERROR: required parameter is not found in config file:\n";
        std::cout << e.what() << std::endl;
        return false;
    }

	V4L2DeviceParameters v4l2_params {
		_v4l2_dev_name.c_str(),
		V4L2_PIX_FMT_YUV420,
		_scr_res.y,  // seems like device consider portrait orientation
		_scr_res.x,  // while bot uses landscape
		V4L2_DEFAULT_FPS,
		IOTYPE_MMAP
	};
	_v4l2_device = V4l2Capture::create(v4l2_params);
	if (_v4l2_device == NULL) {
		std::cout << "--- Failed to open device " << _v4l2_dev_name << std::endl;
		return false;
	}
    _v4l2_buffer_size = _v4l2_device->getBufferSize();
    _v4l2_buffer = new char[_v4l2_buffer_size];

    _bot_state = states::initialized;
    return true;
}

AndroidBot::~AndroidBot() {
    if (_v4l2_device != NULL) {
        delete _v4l2_device;
    }
    if (_v4l2_buffer != NULL) {
        delete[] _v4l2_buffer;
    }
}

void AndroidBot::run() {
    std::cout << " - starting ADB screen capture on '" << _adb_serial << "'\n";
}
