/*
	EVE Echoes bot for Linux.
	Author: Igor Polev.

	AndroidBot class implementation.
*/

#include "androidbot.hpp"
#include "consolecmd.hpp"

#include <fcntl.h>
#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <algorithm>
#include <opencv2/opencv.hpp>
#include <opencv2/imgcodecs.hpp>

AndroidBot::AndroidBot(const json &settings)
{
    // mandatory JSON settings
    try {
        m_adb_serial        = settings.at("adb_serial");
        m_v4l2_dev_name     = settings.at("v4l2_device");
        m_check_interval    = settings.at("check_interval");
        m_resolution.width  = settings.at("width");
        m_resolution.height = settings.at("height");

        // scale factor calculation
        cv::Mat image, image_scaled;
        image = cv::imread(settings.at("res_reference_img"));
        if (image.empty()) {
            cerr << "--- ERROR: failed to read reference image "
                 << settings.at("res_reference_img") << endl;
            return;
        }
        m_scale_factor = static_cast<double>(image.cols);
        image = cv::imread(settings.at("src_reference_img"));
        if (image.empty()) {
            cerr << "--- ERROR: failed to read reference image "
                 << settings.at("src_reference_img") << endl;
            return;
        }
        m_scale_factor /= static_cast<double>(image.cols);

        // set of search images
        json src_images = settings.at("src_images");
        auto img_count = src_images.size();
        if (img_count < 1) {
            cerr << "--- ERROR: no images to search found in config file" << endl;
            return;
        }
        m_src_images.reserve(img_count);
        for (string img_file: src_images) {
            image = cv::imread(img_file);
            if (image.empty()) {
                cerr << "--- ERROR: failed to read image "
                     << img_file << endl;
                return;
            }
            cv::resize(
                image,
                image_scaled,
                cv::Size(),
                m_scale_factor,
                m_scale_factor,
                cv::INTER_AREA // normally scale down expected
            );
            m_src_images.push_back(image_scaled);
        }
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
    if (i_key != i_eof) m_adb_name = *i_key;
    else                m_adb_name = m_adb_serial;
    i_key = settings.find("adb_fps");
    if (i_key != i_eof) m_adb_fps = *i_key;
    else                m_adb_fps = V4L2_FPS_DEFAULT;
    i_key = settings.find("adb_wait_for");
    if (i_key != i_eof) m_adb_wait_for = *i_key;
    else                m_adb_wait_for = ADB_WAIT_DEFAULT;

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
        cerr << "--- ERROR: failed to add device "
             << m_v4l2_dev_name << endl;
        return;
    }
    cmd = string("sudo v4l2-ctl --set-ctrl sustain_framerate=1")
        + " --device " + m_v4l2_dev_name;
    if (0 != cmd.execute()) {
        cerr << "--- ERROR: failed to set attribute of device "
             << m_v4l2_dev_name << endl;
        return;
    }

    m_bot_state = initialized;
}

AndroidBot::~AndroidBot()
{
    if (mp_v4l2_device) delete   mp_v4l2_device;
    if (mp_v4l2_buffer) delete[] mp_v4l2_buffer;
    if (mp_frame_yuv)   delete   mp_frame_yuv;
    if (mp_frame_rgb)   delete   mp_frame_rgb;
}

void AndroidBot::run()
{
    if (m_bot_state != initialized) {
        cerr << "--- ERROR: can't start uninitialized bot\n";
        return;
    }

    cout << " - starting ADB interface on '"
         << m_adb_name << "'\n";
    thread adb_thread(&AndroidBot::adb_process, this);
    cout << " - waiting ADB interface to init for "
         << m_adb_wait_for << " seconds\n";
    this_thread::sleep_for(chrono::seconds(m_adb_wait_for));
    adb_thread.detach(); // WARNING! Thread will loose access to this object after detach  

    m_bot_state = running;
    cout << " - starting V4L2 capture from "
         << m_v4l2_dev_name << "\n";
    thread gf_thread(&AndroidBot::getframes_process, this);
    cout << "Bot is running. Enter 'STOP' to terminate.\n";
    thread ui_thread(&AndroidBot::console_process, this);

    gf_thread.join();
    ui_thread.join();
    cout << "Bot stopped.\n";
}

void AndroidBot::adb_process()
{
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
            + " --max-size="  + to_string(max(
                m_resolution.width,
                m_resolution.height
            ))
            + " &> " + m_adb_log
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

void AndroidBot::getframes_process()
{
    try { // each thread requires its own exception handling
        V4L2DeviceParameters v4l2_params {
            m_v4l2_dev_name.c_str(),
            V4L2_PIX_FMT_YUV420,
            // seems like device consider portrait orientation
            // while bot uses landscape, further investigation needed
            static_cast<unsigned>(m_resolution.width),
            static_cast<unsigned>(m_resolution.height),
            0,
            IOTYPE_MMAP
        };
        mp_v4l2_device = V4l2Capture::create(v4l2_params);
        if (!mp_v4l2_device)  {
            cerr << "--- ERROR (getframes_process): failed to open "
                 << m_v4l2_dev_name << endl;
            return;
        }
        m_v4l2_buf_size = mp_v4l2_device->getBufferSize();
        mp_v4l2_buffer  = new char[m_v4l2_buf_size];
        mp_frame_yuv    = new cv::Mat {
            m_resolution.height + m_resolution.height / 2,
            m_resolution.width,
            CV_8UC1,
            static_cast<void*>(mp_v4l2_buffer)
        };
        mp_frame_rgb = new cv::Mat {m_resolution, CV_8UC3};
    
        size_t bytes_read {0};
        chrono::milliseconds sleep_time {m_check_interval};
        timeval timeout {
            max(1l, m_check_interval / 500),
            0
        };
        while (m_bot_state == running)
        {
            if (!mp_v4l2_device->isReadable(&timeout)) {
                cerr << "--- ERROR (getframes_process): device "
                     << m_v4l2_dev_name << " timeout\n";
                return;
            }
            bytes_read = mp_v4l2_device->read(
                mp_v4l2_buffer,
                m_v4l2_buf_size
            );
            if (bytes_read != m_v4l2_buf_size) {
                cerr << "--- ERROR (getframes_process): failed to read from "
                     << m_v4l2_dev_name << endl;
                return;
            }
            process_frame();
            this_thread::sleep_for(sleep_time);
        }
    }
    catch (const exception& e) {
		cerr << "--- ERROR (getframes_process): unhandled exception:\n"
             << e.what() << endl;
	}
	catch (...) {
		cerr << "--- ERROR (getframes_process): unknown excepition type.\n";
	}
}

void AndroidBot::console_process()
{
    try { // each thread requires its own exception handling
        std::string user_cmd;
        while (user_cmd != "STOP")
        {
            std::cout << "BOT>";
            std::cin  >> user_cmd;
        }
        m_bot_state = stopped;
    }
    catch (const exception& e) {
		cerr << "--- ERROR (console_process): unhandled exception:\n"
             << e.what() << endl;
	}
	catch (...) {
		cerr << "--- ERROR (console_process): unknown excepition type.\n";
	}
}

void AndroidBot::process_frame()
{
    // TEST ONLY
    cv::cvtColor(
        *mp_frame_yuv,
        *mp_frame_rgb,
        cv::COLOR_YUV2RGB_YV12,
        3
    );
    if (!cv::imwrite("frame.png", *mp_frame_rgb))
        cerr << "--- ERROR (process_frame): failed to save frame\n";
    // TEST ONLY
}