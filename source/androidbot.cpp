/*
	EVE Echoes bot for Linux.
	Author: Igor Polev.

	AndroidBot class implementation.
*/

#include <iostream>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <csignal>
#include <unistd.h>

#include "opencv2/core.hpp"
#include "opencv2/core/types.hpp"
#include "opencv2/imgproc.hpp"
#include "opencv2/imgcodecs.hpp"

#include "consolecmd.hpp"
#include "androidbot.hpp"

AndroidBot::AndroidBot(const char* config_file)
{
    static const char* help_text = "Run with --help option for detailes.\n";

    m_bot_status = uninitialized;

    // system prerequisites
    cout << "Checking required tools...\n";
    ConsoleCmd command;
    string cmd_list[] {
        "v4l2loopback-ctl",
        "v4l2-ctl",
        "scrcpy",
        "sudo"
    };
    for (string cmd : cmd_list) {
        command = cmd + " --help";
        if (!command.available()) {
            cout << "--- ERROR (AndroidBot): required command " << cmd
                 << " is not available.\n" << help_text;
            return;
        }
        cout << " - " << cmd << " present\n";
    }
    cout << "Validating sudo command...\n";
    command = "sudo --validate";
    if (0 != command.execute()) {
        cerr << "--- ERROR (AndroidBot): failed to validate sudo command.\n";
        return;
    }
    cout << "Checking dkms status......";
    command = "sudo -n dkms status | grep v4l2loopback";
    if (!command.has_output()) {
        cout << "\n - v4l2loopback kernel module not found.\n" << help_text;
        return;
    }
    cout << "ok\n";

    // settings from JSON
    json settings;
    try {
        cout << "Parsing config file.......";
        ifstream json_file(config_file);
        if (!json_file.is_open()) {
            cerr << "\n--- ERROR (AndroidBot): faild to open config file '"
                 << config_file << "'\n";
            return;
        }
        settings = json::parse(json_file);
        json_file.close();

        // mandatory JSON settings
        m_adb_serial        = settings.at("adb_serial");
        m_v4l2_dev_num      = settings.at("v4l2_device_num");
        m_resolution.width  = settings.at("res_width");
        m_resolution.height = settings.at("res_height");
        m_img_lib_dir       = settings.at("img_lib_dir");
        string res_ref_img  = settings.at("res_reference_img"),
               lib_ref_img  = settings.at("lib_reference_img");
        m_check_interval = seconds(settings.at("check_interval"));
        m_v4l2_dev_name = "/dev/video" + to_string(m_v4l2_dev_num);

        // scale factor calculation
        cv::Mat image;
        image = cv::imread(m_img_lib_dir + res_ref_img);
        if (image.empty()) {
            cerr << "\n--- ERROR (AndroidBot): failed to read reference image "
                 << res_ref_img << endl;
            return;
        }
        m_scale_factor = static_cast<double>(image.cols);
        image = cv::imread(m_img_lib_dir + lib_ref_img);
        if (image.empty()) {
            cerr << "\n--- ERROR (AndroidBot): failed to read reference image "
                 << lib_ref_img << endl;
            return;
        }
        m_scale_factor /= static_cast<double>(image.cols);
    }
    catch (const json::parse_error& e) {
		cerr << "\n--- ERROR (AndroidBot): failed to parse config file '"
		     << config_file << "':\n"
             << e.what() << endl;
        return;
    }
    catch(const json::out_of_range& e) {
		cerr << "\n--- ERROR (AndroidBot): required parameter is not found in config file:\n"
             << e.what() << endl;
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
    if (i_key != i_eof) m_adb_wait_for = seconds(*i_key);
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
    cout << "ok\n";

    // search images library
    cout << "Loading images library....";
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
        cerr << "\n--- ERROR (AndroidBot): faild to read image library.\n";
        return;
    }
    cout << "ok\n";

    // V4L2 device setup
    cout << "Preparing V4L2 device.....";
    ConsoleCmd cmd {"sudo -n modprobe v4l2loopback"};
    if (0 != cmd.execute()) {
        cerr << "\n--- ERROR (AndroidBot): failed to init v4l2loopback kernel module\n";
        return;
    }
    cmd = string("sudo -n v4l2loopback-ctl delete ")
        + to_string(m_v4l2_dev_num)
        + " &> /dev/null";
    cmd.execute();
    cmd = string("sudo -n v4l2loopback-ctl add ")
        + to_string(m_v4l2_dev_num)
        + " --name " + m_adb_name
        + " &> /dev/null";
    if (0 != cmd.execute()) {
        cerr << "\n--- ERROR (AndroidBot): failed to add V4L2 device "
             << m_v4l2_dev_name << endl;
        return;
    }
    cmd = string("sudo v4l2-ctl --set-ctrl sustain_framerate=1")
        + " --device " + m_v4l2_dev_name;
    if (0 != cmd.execute()) {
        cerr << "\n--- ERROR (AndroidBot): failed to set attribute of V4L2 device "
             << m_v4l2_dev_name << endl;
        return;
    }
    cout << "ok\n";

    // bot states init
    cout << "Initializing bot states...";
    if (!new_states()) { // ancestor states registration
        cerr << "\n--- ERROR (AndroidBot): failed to register bot states.\n";
        return;
    }
    cout << "ok\n";
    mp_state = m_bot_states.find(DEF_INITIAL_STATE);
    m_bot_status = initialized;
    cout << "Bot initialized.\n";
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
        image = imread(
            m_img_lib_dir + img_filename,
            IMREAD_UNCHANGED
        );
        if (image.empty()) {
            cerr << "--- ERROR (AndroidBot): failed to read image "
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

int AndroidBot::run()
{
    cout << "Starting bot...\n";
    if (m_bot_status != initialized) {
        cerr << "--- ERROR (run): can't start uninitialized bot\n";
        return -1;
    }

    // start ADB interface
    cout << " - starting ADB interface on '"
         << m_adb_name << "'\n";
    string adb_log {m_adb_serial + "_log.txt"};
    int adb_output = open(
        adb_log.c_str(),
        O_CREAT|O_TRUNC|O_WRONLY,
        S_IRWXU|S_IRWXG|S_IRWXO
    );
    if (!adb_output) {
        cerr << "--- ERROR (run): failed to create ADB log file\n";
        return -1;
    }
    pid_t adb_pid = fork();
    if (adb_pid < 0) {
        cerr << "--- ERROR (run): failed to fork ADB process\n";
        return -1;
    }
    if (adb_pid == 0) {
        // child ADB process
        string serial_opt {"--serial=" + m_adb_serial},
               sink_opt   {"--v4l2-sink=" + m_v4l2_dev_name},
               fps_opt    {"--max-fps=" + to_string(m_adb_fps)},
               size_opt   {"--max-size=" + to_string(
                   m_resolution.width > m_resolution.height ?
                   m_resolution.width : m_resolution.height
               )};
        dup2 (adb_output, STDOUT_FILENO);
        dup2 (adb_output, STDERR_FILENO);
        close(adb_output);
        execlp(
            "scrcpy",
            "--start-app=com.netease.eve.en",
            "--no-video-playback",
            "--no-audio",
            #ifdef NDEBUG
            "--no-window",
            "--turn-screen-off",
            #endif
            serial_opt.c_str(),
            sink_opt  .c_str(),
            fps_opt   .c_str(),
            size_opt  .c_str(),
            nullptr
        );
        return -1; // if execlp failed
    }
    cout << " - waiting for ADB interface to init: "
         << m_adb_wait_for.count() << " seconds\n";
    this_thread::sleep_for(m_adb_wait_for);

    // start threads
    unique_lock<mutex> wait_lock {m_mutex_all};
    m_bot_status = running;
    cout << " - starting V4L2 capture from "
         << m_v4l2_dev_name << "\n";
    thread getframes_thread(&AndroidBot::getframes_process, this);
    m_notify.wait(wait_lock);
    wait_lock.unlock();
    if (m_bot_status != running) return -1;
    cout << " - starting bot program loop\n";
    thread program_thread(&AndroidBot::program_loop, this);

    cout << "Bot is running.\n";
    console_ui();
    cout << "Terminating bot...\n";

    // normal termination
    cout << "(run) status = " << m_bot_status << endl;
    cout << " - waiting for program to terminate...\n";
    program_thread  .join();
    cout << " - waiting for V4l2 capture to terminate...\n";
    getframes_thread.join();
    cout << " - terminating ADB interface...\n";
    if (0 != kill(adb_pid, SIGTERM))
        cout << "--- WARNING (run): failed to kill ADB process\n";
    cout << "Bot stopped.\n";
    return m_ret_code;
}

void AndroidBot::getframes_process()
{
    int retcode {0};
    unique_lock<mutex> data_lock {m_mutex_all}; // lock while init then notify
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
            cerr << "--- ERROR (getframes): failed to open "
                 << m_v4l2_dev_name << endl;
            m_bot_status = stopped;
            data_lock.unlock();
            m_notify.notify_all();
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

        // loop init
        size_t bytes_read {0};
        timeval timeout {
            2 * chrono::ceil<seconds>(m_check_interval).count(),
            0
        };
        data_lock.unlock();
        m_notify.notify_all();

        // main loop
        while (m_bot_status == running)
        {
            cout << "(frames) status = " << m_bot_status << endl;
            if (!mp_v4l2_device->isReadable(&timeout)) {
                cerr << "--- ERROR (getframes): device "
                     << m_v4l2_dev_name << " timeout\n";
                retcode = -1;
                break;
            }
            data_lock.lock();
            bytes_read = mp_v4l2_device->read(
                mp_v4l2_buffer,
                m_v4l2_buf_size
            );
            if (bytes_read != m_v4l2_buf_size) {
                cerr << "--- ERROR (getframes): failed to read from "
                     << m_v4l2_dev_name << endl;
                retcode = -1;
                break;
            }
            process_frame();
            data_lock.unlock();
            this_thread::sleep_for(m_check_interval);
        }
    }
    catch (const exception& e) {
		cerr << "--- ERROR (getframes): unhandled exception:\n"
             << e.what() << endl;
        retcode = -1;
	}
	catch (...) {
		cerr << "--- ERROR (getframes): unknown excepition type.\n";
        retcode = -1;
	}
    if (!data_lock.owns_lock())
        data_lock.lock();
    m_bot_status = stopped;
    m_ret_code  |= retcode;
    data_lock.unlock();
}

void AndroidBot::program_loop()
{
    int retcode {0};
    unique_lock<mutex> data_lock {m_mutex_all, defer_lock};
    try { // each thread requires its own exception handling
        state_itype TERM_STATE  {m_bot_states.find(TERMINATION_STATE)},
                    UNDEF_STATE {m_bot_states.end()};
        while (m_bot_status == running) {
            cout << "(program) status = " << m_bot_status << endl;
            if (TERM_STATE == mp_state || mp_state == UNDEF_STATE) {
                if (UNDEF_STATE == mp_state) {
                    cerr << "--- ERROR (program): undefined bot program state.\n";
                    retcode = -1;
                }
                break;
            }
            program(); // ancestor implemented state switch behavior
        }
    }
    catch (const exception& e) {
		cerr << "--- ERROR (program): unhandled exception:\n"
             << e.what() << endl;
        retcode = -1;
	}
	catch (...) {
		cerr << "--- ERROR (program): unknown excepition type.\n";
        retcode = -1;
	}
    data_lock.lock();
    m_bot_status = stopped;
    m_ret_code  |= retcode;
    data_lock.unlock();
}

void AndroidBot::console_ui()
{
    string user_cmd;
    cout << "To stop the bot, type 'stop'.\n";
    while (m_bot_status == running)
    {
        cout << "BOT<" << m_adb_name << ">: ";
        getline(cin, user_cmd);
        transform(
            user_cmd.begin(),
            user_cmd.end(),
            user_cmd.begin(),
            ::toupper
        );
        // terminate bot
        if ("STOP" == user_cmd) break;
        // print bot state
        else if ("STATE" == user_cmd)
            cout << "Current state: " << state() << endl;

        // TEST
        else if ("D" == user_cmd) {
            bool success;
            double certainty;
            cv::Point location;
            cv::TickMeter tickMeter;
            cout << "Image detection... ";
            tickMeter.start();
            success = detect_image(0, &location, &certainty);
            tickMeter.stop();
            if (success) {
                cout << "succedded (" << tickMeter.getTimeSec() << "s)\n"
                     << "certainty: " << certainty << endl
                     << "location:  " << location << endl;
            } else {
                cout << "failed (" << tickMeter.getTimeSec() << "s)\n";
            }
        }
        // TEST

        else
            cout << "Unknown command: " << user_cmd << endl;
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

int AndroidBot::detect_image(
    idx_type   idx,
    cv::Point *pLocation,
    double    *pCertainty,
    int        maxLocations)
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

    Mat match_result;
    matchTemplate(
        frame,
        m_lib_images[idx],
        match_result,
        TM_CCORR_NORMED, // if changed to TM_SQDIFF_NORMED methods,
                                // don't forget to change code below 
        m_lib_masks[idx].empty() ? noArray() : m_lib_masks[idx]
    );

    // select best matches
    Point  match_location;
    double match_certainty;
    Point image_center {
        m_lib_images[idx].cols / 2,
        m_lib_images[idx].rows / 2
    };
    Rect erase_area;
    int x_offset = m_lib_images[idx].cols / 3,
        y_offset = m_lib_images[idx].rows / 3,
        v, // temporary calcuations storage
        det_count {0},
        steps {maxLocations - 1};
    for (int i = 0; i <= steps; i++) {
        minMaxLoc(  // change args order if TM_SQDIFF_NORMED
            match_result,
            nullptr,
            &match_certainty,
            nullptr,
            &match_location
        );
        // "inverse" match_certainty if TM_SQDIFF_NORMED:
        // match_certainty = 1.0 - match_certainty;
        if (match_certainty < m_threshold)
            break;
        det_count++;
        if (pLocation) {
            pLocation[i] = match_location + image_center;
        };
        if (pCertainty)
            pCertainty[i] = match_certainty;
        // erase current match area - not required at last iteration
        if (i < steps) {
            v = match_location.x - x_offset;
            erase_area.x = v < 0 ? 0 : v;
            v = match_location.y - y_offset;
            erase_area.y = v < 0 ? 0 : v;
            v = match_location.x + m_lib_images[idx].cols + x_offset;
            erase_area.width = (v < match_result.cols ?
                v : match_result.cols - 1) - erase_area.x + 1;
            v = match_location.y + m_lib_images[idx].rows + y_offset;
            erase_area.height = (v < match_result.rows ?
                v : match_result.rows - 1) - erase_area.y + 1;
            match_result(erase_area) = 0.0; // 1.0 if TM_SQDIFF_NORMED
        }
    }
    return det_count;
}