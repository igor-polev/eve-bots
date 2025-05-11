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

AndroidBot::AndroidBot(const json &settings) {
    m_bot_state = states::uninitialized;

    // mandatory JSON settings
    try {
        m_adb_serial     = settings.at("adb_serial");
        m_v4l2_dev_name  = settings.at("v4l2_device");
        m_resolution_x   = settings.at("resolution_x");
        m_resolution_y   = settings.at("resolution_y");
        m_check_interval = settings.at("check_interval");
    }
    catch(const json::out_of_range& e) {
		cerr << "--- ERROR: required parameter is not found in config file:\n";
        cerr << e.what() << endl;
        return;
    }
    m_adb_log = m_adb_serial + "_log.txt";
    
    // optional JSON settings
    auto i_eof = settings.end();
    auto i_key = settings.find("adb_name");
    if (i_key != i_eof) { m_adb_name = *i_key; }
    else                { m_adb_name = m_adb_serial; }
    i_key = settings.find("adb_fps");
    if (i_key != i_eof) { m_adb_fps = *i_key; }
    else                { m_adb_fps = v4l2_default_fps; }
    i_key = settings.find("adb_wait_for");
    if (i_key != i_eof) { m_adb_wait_for = *i_key; }
    else                { m_adb_wait_for = adb_wait_default; }

    // V4L2 device setup
    ConsoleCmd cmd {"sudo -n modprobe v4l2loopback"};
    if (0 != cmd.execute()) {
        cerr << "--- ERROR: failed to init v4l2loopback kernel module\n";
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
        cerr << "--- ERROR: failed to add device " << m_v4l2_dev_name << endl;
        return;
    }
    cmd = string("sudo v4l2-ctl --set-ctrl sustain_framerate=1")
        + " --device " + m_v4l2_dev_name;
        //+ " &> /dev/null";
    if (0 != cmd.execute()) {
        cerr << "--- ERROR: failed to set attribute of device " << m_v4l2_dev_name << endl;
        return;
    }
	m_v4l2_device = NULL;
    m_v4l2_buffer = NULL;
    m_v4l2_buffer_size = 0;

    m_bot_state = states::initialized;
}

AndroidBot::~AndroidBot() {
    if (m_v4l2_device) { delete m_v4l2_device; }
    if (m_v4l2_buffer) { delete[] m_v4l2_buffer; }
}

int AndroidBot::run() {
    if (m_bot_state != states::initialized) {
        cerr << "--- ERROR: can't start uninitialized bot\n";
        return -1;
    }
    cout << " - starting ADB interface on '" << m_adb_name << "'\n";
    thread adb_thread(&AndroidBot::adb_process, this);
    adb_thread.detach();
    cout << " - waiting ADB interface to init for " << m_adb_wait_for << " seconds\n";
    this_thread::sleep_for(chrono::seconds(m_adb_wait_for));
    cout << " - starting V4L2 capture from " << m_v4l2_dev_name << "\n";
    thread frames_thread(&AndroidBot::getframes_process, this);
    m_bot_state = states::running;
    cout << "Bot is running...\n";
    frames_thread.join();
    m_bot_state = states::stopped;
    cout << "Bot stopped.\n";
    return m_gf_proc_retcode;
}

void AndroidBot::adb_process() {
    try {
        ConsoleCmd scrcpy_cmd {
            string(
                "scrcpy"
                " --no-video-playback"
                " --no-audio"
                " --start-app=com.netease.eve.en"
            )
            #ifdef NDEBUG
            + " --no-window"
            + " --turn-screen-off"
            #endif
            + " --serial="    + m_adb_serial
            + " --v4l2-sink=" + m_v4l2_dev_name
            + " --max-fps="   + to_string(m_adb_fps)
            + " --max-size="  + to_string(max(m_resolution_x, m_resolution_y))
            + " &> "          + m_adb_log
        };
        scrcpy_cmd.execute();
    }
    catch (const exception& e) {
		cerr << "--- ERROR (adb_process): unhandled exception:\n";
        cerr << e.what() << endl;
	}
	catch(...) {
		cerr << "--- ERROR (adb_process): unknown excepition type, terminating.\n";
	}
}

void AndroidBot::getframes_process() {
    m_gf_proc_retcode = -1;
    try {
        V4L2DeviceParameters v4l2_params {
            m_v4l2_dev_name.c_str(),
            V4L2_PIX_FMT_YUV420,
            m_resolution_x, // seems like device consider portrait orientation
            m_resolution_y, // while bot uses landscape, further investigation needed
            0,
            IOTYPE_MMAP
        };
        m_v4l2_device = V4l2Capture::create(v4l2_params);
        if (!m_v4l2_device)  {
            cerr << "--- ERROR (getframes_process): failed to open " << m_v4l2_dev_name << endl;
            return;
        }
        m_v4l2_buffer_size = m_v4l2_device->getBufferSize();
        m_v4l2_buffer = new char[m_v4l2_buffer_size];

        size_t bytes_read {0};
        chrono::milliseconds sleep_time {m_check_interval};
        timeval timeout {max(1l, m_check_interval / 500), 0};
        while (m_v4l2_device->isReadable(&timeout))
        {
            bytes_read = m_v4l2_device->read(m_v4l2_buffer, m_v4l2_buffer_size);
            if (bytes_read != m_v4l2_buffer_size) {
                cerr << "--- ERROR (getframes_process): failed to read from " << m_v4l2_dev_name << endl;
                return;
            }

            #ifndef NDEBUG
            ofstream frame_file;
            frame_file.open("frame.raw", ios::out | ios::binary | ios::trunc);
            frame_file.write(m_v4l2_buffer, m_v4l2_buffer_size);
            frame_file.close();
            #endif

            this_thread::sleep_for(sleep_time);
        }
        m_gf_proc_retcode = 0;
    }
    catch (const exception& e) {
		cerr << "--- ERROR (getframes_process): unhandled exception:\n";
        cerr << e.what() << endl;
	}
	catch(...) {
		cerr << "--- ERROR (getframes_process): unknown excepition type, terminating.\n";
	}
}