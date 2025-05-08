/*
	EVE Echoes bot for Linux.
	Author: Igor Polev.

	AndroidBot class implementation.
*/

#include "androidbot.hpp"
#include "consolecmd.hpp"

#ifndef NDEBUG
#include <fstream>
#endif

#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <algorithm>
#include <stdexcept>

AndroidBot::AndroidBot(const json &settings) {
    m_bot_state = states::uninitialized;

    // mandatory JSON settings
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
        return;
    }

    // optional JSON settings
    auto i_eof = settings.end();
    auto i_key = settings.find("adb_name");
    if (i_key != i_eof) { m_adb_name = *i_key; }
    else                { m_adb_name = m_adb_serial; }
    i_key = settings.find("adb_fps");
    if (i_key != i_eof) { m_adb_fps = *i_key; }
    else                { m_adb_fps = v4l2_default_fps; }
    i_key = settings.find("adb_log");
    if (i_key != i_eof) { m_adb_log = *i_key; }
    else                { m_adb_log = string("/dev/null"); }
    i_key = settings.find("res_divide_factor");
    if (i_key != i_eof) {
        int r_factor = *i_key;
        m_v4l2res.x = m_fullres.x / r_factor;
        m_v4l2res.y = m_fullres.y / r_factor;
    } else {
        m_v4l2res.x = m_fullres.x;
        m_v4l2res.y = m_fullres.y;
    }

    // V4L2 device initialization
    ConsoleCmd cmd {"sudo -n modprobe v4l2loopback"};
    if (0 != cmd.execute()) {
        cout << "--- Failed to init v4l2loopback kernel module\n";
        return;
    }
    cmd = string("sudo -n v4l2loopback-ctl delete ")
        + m_v4l2_dev_name
        + " &> /dev/null";
    cmd.execute();
    cmd = string("sudo -n v4l2loopback-ctl add ")
        + " --name " + m_adb_name
        + " " + m_v4l2_dev_name
        + " &> /dev/null";
    if (0 != cmd.execute()) {
        cout << "--- Failed to add device " << m_v4l2_dev_name << endl;
        return;
    }
    V4L2DeviceParameters v4l2_params {
		m_v4l2_dev_name.c_str(),
		V4L2_PIX_FMT_YUV420,
		m_v4l2res.y,  // seems like device consider portrait orientation
		m_v4l2res.x, // while bot uses landscape, further investigation needed
		m_adb_fps,      // looks like FPS values less then 30 is ignored, further investigation needed
		IOTYPE_MMAP
	};
	m_v4l2_device = V4l2Capture::create(v4l2_params);
	if (!m_v4l2_device) {
		cout << "--- Failed to open device " << m_v4l2_dev_name << endl;
		return;
	}
    m_v4l2_buffer_size = m_v4l2_device->getBufferSize();
    m_v4l2_buffer = new char[m_v4l2_buffer_size];

    m_bot_state = states::initialized;
}

AndroidBot::~AndroidBot() {
    if (m_v4l2_device) { delete m_v4l2_device; }
    if (m_v4l2_buffer) { delete[] m_v4l2_buffer; }
}

int AndroidBot::run() {
    if (m_bot_state != states::initialized) {
        cout << "--- ERROR: can't start uninitialized bot\n";
        return -1;
    }
    cout << " - starting ADB interface on '" << m_adb_name << "'\n";
    thread adb_thread(&AndroidBot::adb_process, this);
    cout << " - starting V4L2 capture from " << m_v4l2_dev_name << "\n";
    thread frames_thread(&AndroidBot::getframes_process, this);
    m_bot_state = states::running;
    cout << "Bot is running...\n";
    frames_thread.join();
    adb_thread.join();
    m_bot_state = states::stopped;
    cout << "Bot stopped.\n";
    return 0;
}

void AndroidBot::adb_process() {
    ConsoleCmd scrcpy_cmd {
        string(
            "scrcpy"
            " --no-window"
            " --no-video-playback"
            " --no-audio"
            " --start-app=com.netease.eve.en"
        )
        #ifdef NDEBUG
        + " --turn-screen-off"
        #endif
        + " --serial="    + m_adb_serial
        + " --v4l2-sink=" + m_adb_serial
        + " --max-fps="   + to_string(m_adb_fps)
        + " --max-size="  + to_string(max(m_v4l2res.x, m_v4l2res.y))
        + " &> "          + m_adb_log
    };
    if (0 != scrcpy_cmd.execute()) {
        throw runtime_error(string("failed to run command:\n") + scrcpy_cmd.to_string()); 
    }
}

void AndroidBot::getframes_process() {
    runtime_error v4l2_error {string("failed to read form ") + m_v4l2_dev_name};
    size_t bytes_read {0};
    chrono::milliseconds sleep_time {m_check_interval};
	timeval timeout {max(1u, static_cast<unsigned>(m_check_interval) / 500), 0};
	while (m_v4l2_device->isReadable(&timeout))
	{
		bytes_read = m_v4l2_device->read(m_v4l2_buffer, m_v4l2_buffer_size);
		if (bytes_read != m_v4l2_buffer_size) { throw v4l2_error; }

        #ifndef NDEBUG
        ofstream frame_file;
        frame_file.open("frame.raw", ios::out | ios::binary | ios::trunc);
        frame_file.write(m_v4l2_buffer, m_v4l2_buffer_size);
        frame_file.close();
        #endif

        this_thread::sleep_for(sleep_time);
    }
    throw v4l2_error;
}