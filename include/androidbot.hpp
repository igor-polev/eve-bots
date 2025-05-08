/*
	EVE Echoes bot for Linux.
	Author: Igor Polev.

	AndroidBot class definition.
*/

#pragma once
#include <nlohmann/json.hpp>
#include "../libv4l2cpp/inc/V4l2Capture.h"

using json = nlohmann::json;
using namespace std;

#define V4L2_DEFAULT_FPS 30

class AndroidBot {
public:
	enum states {
		uninitialized,
		initialized,
		running,
		stopped
	};
  	AndroidBot() noexcept:
		m_adb_serial(),
		m_v4l2_dev_name(),
		m_v4l2_device(NULL),
		m_fullres{0,0},
		m_v4l2_buffer_size(0),
		m_v4l2_buffer(NULL),
		m_check_interval(0),
		m_bot_state(states::uninitialized)
		{};
    AndroidBot(const json &settings) {
		init(settings);
	};
	const bool init(const json &settings);
    virtual ~AndroidBot();
	virtual int run();
	const states state() const noexcept {
		return _bot_state;
	};
private:
	struct scr_resolution {
		unsigned x;
		unsigned y;
	};
	string         m_adb_serial;
	string         m_v4l2_dev_name;
	V4l2Capture   *m_v4l2_device;
	unsigned       m_v4l2_buffer_size;
	char          *m_v4l2_buffer;
	scr_resolution m_fullres;
	unsigned       m_check_interval;
	states         m_bot_state;
protected:
	void adb_process();
	void getframes_process();
};