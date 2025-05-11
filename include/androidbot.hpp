/*
	EVE Echoes bot for Linux.
	Author: Igor Polev.

	AndroidBot class definition.
*/

#pragma once
#include <nlohmann/json.hpp>
#include "../libv4l2cpp/inc/V4l2Capture.h"

using namespace std;
using json = nlohmann::json;

class AndroidBot {
public:
	static const int  v4l2_default_fps {30};
	static const long adb_wait_default {5};
	enum states {
		uninitialized,
		initialized,
		running,
		stopped
	};
  	AndroidBot() = delete;
    AndroidBot(const json &settings);
    virtual ~AndroidBot();
	virtual int run();
	const states state() const noexcept {
		return m_bot_state;
	};
private:
	string         m_adb_name;         // user friendly name of Android device
	string         m_adb_serial;       // serial number of Android device
	string         m_v4l2_dev_name;    // v4l2 video device path, for ex.: /dev/video7
	V4l2Capture   *m_v4l2_device;
	size_t         m_v4l2_buffer_size;
	char          *m_v4l2_buffer;
	unsigned       m_resolution_x;     // user-defined resolution of video stream
	unsigned       m_resolution_y;
	int            m_adb_fps;          // user-defined FPS of video stream
	long           m_check_interval;   // bot screen check interval in milliseconds
	long           m_adb_wait_for;     // time to wait for ADB to init in seconds
	string         m_adb_log;
	states         m_bot_state;

	void adb_process();
	void getframes_process();
	int m_gf_proc_retcode;
};