/*
	EVE Echoes bot for Linux.
	Author: Igor Polev.

	AndroidBot class definition.
*/

#pragma once
#include <nlohmann/json.hpp>
#include "../libv4l2cpp/inc/V4l2Capture.h"

using json = nlohmann::json;

#define V4L2_DEFAULT_FPS 30

class AndroidBot {
public:
	enum states {
		uninitialized,
		initialized
	};
  	AndroidBot() noexcept:
		_adb_serial(),
		_v4l2_dev_name(),
		_v4l2_device(NULL),
		_scr_res{0,0},
		_v4l2_buffer_size(0),
		_v4l2_buffer(NULL),
		_check_interval(0),
		_bot_state(states::uninitialized)
		{};
    AndroidBot(const json &settings) {
		init(settings);
	};
	const bool init(const json &settings);
    virtual ~AndroidBot();
	virtual void run();
	const states state() const noexcept {
		return _bot_state;
	};
private:
	struct scr_resolution {
		unsigned x;
		unsigned y;
	};
	std::string    _adb_serial;
	std::string    _v4l2_dev_name;
	V4l2Capture   *_v4l2_device;
	unsigned       _v4l2_buffer_size;
	char          *_v4l2_buffer;
	scr_resolution _scr_res;
	unsigned       _check_interval;
	states         _bot_state;
protected:
	void adb_process();
	void getframes_process();
};