/*
	EVE Echoes bot for Linux.
	Author: Igor Polev.

	AndroidBot class implementation.
*/

#include "androidbot.hpp"
#include "consolecmd.hpp"
#include "opencv2/core.hpp"
#include "opencv2/core/mat.hpp"
#include "opencv2/core/types.hpp"
#include "opencv2/imgproc.hpp"
#include "opencv2/imgcodecs.hpp"

#include <fcntl.h>
#include <iostream>
#include <mutex>
#include <string>
#include <chrono>
#include <thread>
#include <algorithm>

AndroidBot::AndroidBot(const json &settings)
{
    // mandatory JSON settings
    try {
        m_adb_serial        = settings.at("adb_serial");
        m_v4l2_dev_name     = settings.at("v4l2_device");
        m_check_interval    = settings.at("check_interval");
        m_resolution.width  = settings.at("res_width");
        m_resolution.height = settings.at("res_height");

        // scale factor calculation
        cv::Mat image, image_scaled;
        image = cv::imread(settings.at("res_reference_img"));
        if (image.empty()) {
            cerr << "--- ERROR: failed to read reference image "
                 << settings.at("res_reference_img") << endl;
            return;
        }
        m_scale_factor = static_cast<double>(image.cols);
        image = cv::imread(settings.at("lib_reference_img"));
        if (image.empty()) {
            cerr << "--- ERROR: failed to read reference image "
                 << settings.at("lib_reference_img") << endl;
            return;
        }
        m_scale_factor /= static_cast<double>(image.cols);
    }
    catch(const json::out_of_range& e) {
		cerr << "--- ERROR: required parameter is not found in config file:\n";
        cerr << e.what() << endl;
        return;
    }
    
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
    i_key = settings.find("dump_frames");
    if (i_key != i_eof) m_dump_frames = *i_key;
    else                m_dump_frames = false;
    i_key = settings.find("force_greyscale");
    if (i_key != i_eof) m_force_greyscale = *i_key;
    else                m_force_greyscale = false;
    i_key = settings.find("detect_threshold");
    if (i_key != i_eof) m_threshold = *i_key;
    else                m_threshold = DEF_THRESHOLD;

    // search images library
    bool img_collected {false};
    i_key = settings.find("lib_images");
    if (i_key != i_eof) {
        auto img_count = i_key->size();
        if (img_count > 0) {
            m_lib_images.reserve(img_count);
            m_lib_masks .reserve(img_count);
            img_collected = collect_images(
                *i_key,
                m_force_greyscale
            );
        }
    }
    if (!img_collected) {
        cerr << "--- ERROR: no images to search found in config file" << endl;
        return;
    }

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

    // bot state init
    if (new_states()) // ancestor states registration
    {
        mp_state = m_bot_states.find(DEF_INITIAL_STATE);
        m_bot_status = initialized;
    }
}

AndroidBot::~AndroidBot()
{
    if (mp_v4l2_buffer) delete[] mp_v4l2_buffer;
    if (mp_v4l2_device) delete   mp_v4l2_device;
    if (mp_frame_yuv)   delete   mp_frame_yuv;
    if (mp_frame_rgb)   delete   mp_frame_rgb;
    if (mp_frame_fp)    delete   mp_frame_fp;
}

bool AndroidBot::collect_images(const json& filenames, bool force_gs)
{
    using namespace cv;
    string img_filename;
    Mat image, image_scaled;
    auto img_count = filenames.size();
    for (idx_type i = 0; i < img_count; i++)
    {
        img_filename = filenames[i];
        image = imread(img_filename, IMREAD_UNCHANGED);
        if (image.empty()) {
            cerr << "--- ERROR: failed to read image "
                 << img_filename << endl;
            return false;
        }
        resize(
            image,
            image_scaled,
            Size(),
            m_scale_factor,
            m_scale_factor,
            INTER_AREA // normally scale down expected
        );
    
        // split alpha channel
        auto channel_cnt = image_scaled.channels();
        if (channel_cnt == 2 || 4 == channel_cnt) {
            Mat channels[channel_cnt];
            split(image_scaled, channels);
            channel_cnt--;
            double alphaMinValue;
            minMaxLoc(channels[channel_cnt],&alphaMinValue);
            if (alphaMinValue > 0.0) // alpha channel is irrelevant
                image.release(); // 'image' variable now store alpha channel
            else
                channels[channel_cnt].convertTo(image, CV_8UC1);
            merge(channels, channel_cnt, image_scaled);
        }
        else
            image.release();
        m_lib_masks.push_back(image);

        if (force_gs && image_scaled.channels() > 1)
            cvtColor(
                image_scaled,
                image, // 'image' variable again stores image itself
                COLOR_RGB2GRAY
            );
        else
            image = image_scaled;
        image.convertTo(
            image,
            CV_32FC(image.channels()),
            UC_TO_FP_SCALE
        );
        m_lib_images.push_back(image);
        m_lib_img_map[img_filename] = i;
    }
    return true;
}

void AndroidBot::run()
{
    if (m_bot_status != initialized) {
        cerr << "--- ERROR: can't start uninitialized bot\n";
        return;
    }
    unique_lock<mutex> data_lock {m_mutex_all, defer_lock};

    // start ADB interface
    cout << " - starting ADB interface on '"
         << m_adb_name << "'\n";
    thread adb_thread(&AndroidBot::adb_process, this);
    // need to give adb_thread some time to init
    // otherwise gf_thread will fail to access V4L2 device
    cout << " - waiting ADB interface to init for "
         << m_adb_wait_for << " seconds\n";
    this_thread::sleep_for(chrono::seconds(m_adb_wait_for));
    // WARNING! adb_thread will loose access to this object after detach
    data_lock.lock(); // to be sure that adb_thread allready got data
    adb_thread.detach();
    data_lock.unlock();
    if (m_bot_status == stopped) return;

    // start V4L2 capture and console UI
    m_bot_status = running;
    cout << " - starting V4L2 capture from "
         << m_v4l2_dev_name << "\n";
    thread gf_thread(&AndroidBot::getframes_process, this);
    cout << "Bot is running. Enter 'stop' to terminate.\n";
    thread ui_thread(&AndroidBot::console_process, this);

    // start bot program
    state_itype term_state  {m_bot_states.find(TERMINATION_STATE)},
                undef_state {m_bot_states.end()};
	while (mp_state != term_state && m_bot_status == running) {
        if (mp_state == undef_state) {
            cerr << "--- ERROR: undefined bot program state\n";
            break;
        }
        program(); // ancestor implemented state switch behavior
    }

    // normal termination
    data_lock.lock();
    m_bot_status = stopped;
    data_lock.unlock();
    gf_thread.join();
    ui_thread.detach(); // may stack on user input
    cout << "Bot stopped.\n";
}

void AndroidBot::adb_process()
{
    // this thread will be detached and will lose access to this object
    // so we lock data - detach will happen after we unlock it
    unique_lock<mutex> data_lock {m_mutex_all};
    try { // each thread requires its own exception handling
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
            + " &> " + m_adb_serial + "_log.txt"
        };
        data_lock.unlock();
        scrcpy_cmd.execute();
    }
    catch (const exception& e) {
		cerr << "--- ERROR (adb_process): unhandled exception:\n";
        cerr << e.what() << endl;
	}
	catch(...) {
		cerr << "--- ERROR (adb_process): unknown excepition type, terminating.\n";
	}
    // can't stop the bot after detach but can until we hold data lock
    if (data_lock) {
        m_bot_status = stopped;
        data_lock.unlock();
    }
}

void AndroidBot::getframes_process()
{
    unique_lock<mutex> data_lock {m_mutex_all};
    try { // each thread requires its own exception handling
        
        // init section
        V4L2DeviceParameters v4l2_params {
            m_v4l2_dev_name.c_str(),
            V4L2_PIX_FMT_YUV420,
            0, 0, 0,
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
        short c = m_force_greyscale ? 1 : 3;
        mp_frame_rgb = new cv::Mat {m_resolution, CV_8UC(c)};
        mp_frame_fp  = new cv::Mat {m_resolution, CV_32FC(c)};
        data_lock.unlock();
    
        // main loop
        size_t bytes_read {0};
        chrono::milliseconds sleep_time {m_check_interval};
        timeval timeout {
            max(1l, m_check_interval / 500),
            0
        };
        while (m_bot_status == running)
        {
            if (!mp_v4l2_device->isReadable(&timeout)) {
                cerr << "--- ERROR (getframes_process): device "
                     << m_v4l2_dev_name << " timeout\n";
                break;
            }
            data_lock.lock();
            bytes_read = mp_v4l2_device->read(
                mp_v4l2_buffer,
                m_v4l2_buf_size
            );
            if (bytes_read != m_v4l2_buf_size) {
                data_lock.unlock();
                cerr << "--- ERROR (getframes_process): failed to read from "
                     << m_v4l2_dev_name << endl;
                break;
            }
            process_frame();
            data_lock.unlock();
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
    data_lock.lock();
    m_bot_status = stopped;
    data_lock.unlock();
}

void AndroidBot::console_process()
{
    // this thread is not supposed to change object data directly
    // so it doesn't lock except in case of exit or exception
    try { // each thread requires its own exception handling
        string user_cmd;
        while (m_bot_status == running)
        {
            cout << "BOT>";
            cin  >> user_cmd;
            if ("stop" == user_cmd) break;

            // TEST
            if ("d" == user_cmd) {
                cv::TickMeter tickMeter;
                tickMeter.start();
                double prob = detect_image(0);
                tickMeter.stop();
                cout << "detection probability: " << prob << endl
                     << "time spend: " << tickMeter.getTimeSec() << "s" << endl;
            }
            // TEST

            else
                cout << "Unknown command: " << user_cmd << endl;
        }
    }
    catch (const exception& e) {
		cerr << "--- ERROR (console_process): unhandled exception:\n"
             << e.what() << endl;
    }
	catch (...) {
		cerr << "--- ERROR (console_process): unknown excepition type.\n";
	}
    unique_lock<mutex> data_lock {m_mutex_all};
    m_bot_status = stopped;
    data_lock.unlock();
}

void AndroidBot::process_frame()
{
    // data is not locked in this function
    // it must be locked by caller
    int channels;
    cv::ColorConversionCodes convert_type;
    if (m_force_greyscale) {
        convert_type = cv::COLOR_YUV2GRAY_YV12;
        channels = 1;
    }
    else {
        convert_type = cv::COLOR_YUV2RGB_YV12;
        channels = 3;
    }
    cv::cvtColor(
        *mp_frame_yuv,
        *mp_frame_rgb,
        convert_type,
        channels
    );
    mp_frame_rgb->convertTo(
        *mp_frame_fp,
        CV_32FC(channels),
        UC_TO_FP_SCALE
    );
    if (!m_dump_frames) return;
    if (!cv::imwrite(m_adb_serial + ".png", *mp_frame_rgb))
        cerr << "--- ERROR (process_frame): failed to save frame\n";
}

bool AndroidBot::detect_image(
    idx_type   idx,
    cv::Point *pLocation,
    double    *pCertainty)
{
    using namespace cv;
    // this function does not change object data
    // but we lock while copying frame into local variable
    // to prevent image distortion from getframes thread
    Mat frame;
    unique_lock<mutex> data_lock {m_mutex_all};
    if (!m_force_greyscale && 1 == m_lib_images[idx].channels())
        cvtColor(
            *mp_frame_fp,
            frame,
            COLOR_RGB2GRAY
        );
    else
        frame = mp_frame_fp->clone();
    data_lock.unlock();

    double match_certainty;
    Point  match_location;
    Mat    match_result;
    matchTemplate(
        frame,
        m_lib_images[idx],
        match_result,
        TM_CCORR_NORMED,
        m_lib_masks[idx].empty() ? noArray() : m_lib_masks[idx]
    );
    minMaxLoc(
        match_result,
        nullptr,
        &match_certainty,
        nullptr,
        &match_location
    );
    if (match_certainty < m_threshold)
        return false;
    if (pLocation) {
        match_location.x += m_lib_images[idx].cols / 2;
        match_location.y += m_lib_images[idx].rows / 2;
        *pLocation  = match_location;
    };
    if (pCertainty)
        *pCertainty = match_certainty;
    return true;
}