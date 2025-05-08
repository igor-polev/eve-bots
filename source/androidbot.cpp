/*
	EVE Echoes bot for Linux.
	Author: Igor Polev.

	AndroidBot class implementation.
*/

#include "androidbot.hpp"

#include <thread>
#include <algorithm>
#include <iostream>

const bool AndroidBot::init(const json &settings) {
    m_bot_state = states::uninitialized;

    try {
        m_adb_serial     = settings.at("adb_serial");
        m_v4l2_dev_name  = settings.at("v4l2_device");
        m_fullres.x      = settings.at("fullres_x");
        m_fullres.y      = settings.at("fullres_y");
        m_check_interval = settings.at("check_interval");
    }
    catch(const json::out_of_range& e) {
		cout << "--- ERROR: required parameter is not found in config file:\n";
        cout << e.what() << endl;
        return false;
    }
    iterator 
    try {
        m_adb_name  = settings.find("adb_name");
    }
    catch(const json::out_of_range& e) {
        m_adb_name  = m_adb_serial;
    }

	V4L2DeviceParameters v4l2_params {
		m_v4l2_dev_name.c_str(),
		V4L2_PIX_FMT_YUV420,
		m_scr_res.y,  // seems like device consider portrait orientation
		m_scr_res.x,  // while bot uses landscape
		V4L2_DEFAULT_FPS,
		IOTYPE_MMAP
	};
	m_v4l2_device = V4l2Capture::create(v4l2_params);
	if (m_v4l2_device == NULL) {
		cout << "--- Failed to open device " << m_v4l2_dev_name << endl;
		return false;
	}
    m_v4l2_buffer_size = m_v4l2_device->getBufferSize();
    m_v4l2_buffer = new char[m_v4l2_buffer_size];

    m_bot_state = states::initialized;
    return true;
}

AndroidBot::~AndroidBot() {
    if (m_v4l2_device != NULL) {
        delete m_v4l2_device;
    }
    if (m_v4l2_buffer != NULL) {
        delete[] m_v4l2_buffer;
    }
}

int AndroidBot::run() {
    if (m_bot_state != states::initialized) {
        cout << "--- ERROR: can't start uninitialized bot\n";
        return -1;
    }
    cout << " - starting ADB interface on '" << m_adb_serial << "'\n";
    thread adb_thread(adb_process);
    cout << " - starting V4L2 capture from " << m_v4l2_dev_name << "\n";
    thread frames_thread(getframes_process);
    m_bot_state = states::running;
    cout << "Bot is running...\n";
    frames_thread.join();
    adb_thread.join();
    m_bot_state = states::stopped;
    cout << "Bot stopped.\n";
    return 0;
}

void AndroidBot::adb_process() {
    const char *scrcpy_cmd =
        "scrcpy" +
        " --serial=" + m_adb_serial.c_str() +
        " --max-size=" + to_string(max(_scr_res.x, _scr_res.y)).c_str() +
        " --start-app=com.netease.eve.en";

}

void AndroidBot::getframes_process() {

}
