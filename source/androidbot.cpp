/*
	EVE Echoes bot for Linux.
	Author: Igor Polev.

	AndroidBot class implementation.
*/

#include <iostream>
#include <fstream>
#include <stdexcept>
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
        m_check_interval = millis(settings.at("check_interval"));
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
    m_tap_cmd = string("adb shell -s ") + m_adb_serial + " input ";
    
    // optional JSON settings
    bool check_system {true};
    auto i_eof = settings.end();
    auto i_key = settings.find("check_system");
    if (i_key != i_eof) check_system = *i_key;
    i_key = settings.find("adb_name");
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
    if (i_key != i_eof)
        img_collected = collect_images(*i_key, m_force_greyscale);
    if (!img_collected) {
        cerr << "\n--- ERROR (AndroidBot): faild to read image library.\n";
        return;
    }
    cout << "ok\n";

    if (check_system) {

        // system prerequisites
        cout << "Checking required tools...\n";
        ConsoleCmd command;
        string cmd_list[] {
            "v4l2loopback-ctl",
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
        #ifdef NDEBUG
        command = "sudo --validate";
        #else
        command = "sudo --validate --askpass";
        #endif
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
        cout << "ok\n";

    } // check_system
    else
        cout << "System check skipped.\n";

    // bot states init
    mp_state = m_bot_states.find(DEF_INITIAL_STATE);
    m_bot_status = initialized;
    cout << "Bot initialized.\n";

    // telegram bot using curl
    curl_global_init(CURL_GLOBAL_DEFAULT);
    mp_curl = curl_easy_init();
    if (!mp_curl) {
        cerr << "--- WARNING (AndroidBot): failed to init curl - telegram bot disabled\n";
        return;
    }
    curl_easy_setopt(
        mp_curl,
        CURLOPT_URL,
        "https://api.telegram.org/bot"
        "8043028444:AAG2PGAHnw90aQSLOlgeuaIMw1PPLXo85uc"
        "/sendMessage"
    );
    curl_easy_setopt(
        mp_curl,
        CURLOPT_HTTPHEADER,
        (struct curl_slist*)curl_slist_append(
            NULL,
            "Content-Type: application/x-www-form-urlencoded"
        )
    );
}

AndroidBot::~AndroidBot()
{
    if (mp_v4l2_buffer) delete[] mp_v4l2_buffer;
    if (mp_v4l2_device) delete   mp_v4l2_device;
    if (mp_frame_yuv)   delete   mp_frame_yuv;
    if (mp_frame_rgb)   delete   mp_frame_rgb;
    if (mp_frame_fp)    delete   mp_frame_fp;

    if (mp_curl) curl_easy_cleanup(mp_curl);
    curl_global_cleanup();
}

bool AndroidBot::collect_images(const json& filenames, bool force_gs)
{
    using namespace cv;
    string img_filename;
    Mat image, image_scaled;
    auto img_count = filenames.size();

    if (img_count < 1) return false;

    // index 0 in image library is reserved for error signal
    img_count++;
    m_lib_images.reserve(img_count);
    m_lib_masks .reserve(img_count);
    m_lib_images.push_back(image);
    m_lib_masks .push_back(image);

    for (idx_type i = 1; i < img_count; i++)
    {
        img_filename = filenames[i - 1];
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
            "scrcpy",
            "--start-app=com.netease.eve.en",
            "--no-audio",
            #ifdef NDEBUG
            "--no-video-playback",
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
    cout << " - initializing bot states...\n";
    if (!new_states()) { // ancestor states registration
        cerr << "\n--- ERROR (run): failed to register bot states.\n";
        return -1;
    }
    cout << " - starting bot program loop\n";
    thread program_thread(&AndroidBot::program_loop, this);

    cout << "Bot is running.\n";
    telegram_message("Bot started on " + m_adb_name);
    console_ui();
    cout << "Terminating bot...\n";

    // normal termination
    cout << " - waiting for program to terminate...\n";
    program_thread  .join();
    cout << " - waiting for V4l2 capture to terminate...\n";
    getframes_thread.join();
    cout << " - terminating ADB interface...\n";
    if (0 != kill(adb_pid, SIGTERM))
        cout << "--- WARNING (run): failed to kill ADB process\n";
    cout << "Bot stopped.\n";
    telegram_message("Bot terminated on " + m_adb_name);
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
            m_notify.notify_one();
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
        m_notify.notify_one();

        // main loop
        while (m_bot_status == running)
        {
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
    try { // each thread requires its own exception handling
        state_itype TERM_STATE  {m_bot_states.find(TERMINATION_STATE)},
                    UNDEF_STATE {m_bot_states.end()};
        set_state(mp_state); // start time counter
        while (m_bot_status == running) {
            if (TERM_STATE == mp_state || mp_state == UNDEF_STATE) {
                if (UNDEF_STATE == mp_state) {
                    cerr << "--- ERROR (program): undefined bot program state.\n";
                    retcode = -1;
                }
                break;
            }
            m_state_time = chrono::duration_cast<seconds>(
                chrono::steady_clock::now() - m_state_start
            );
            program(); // ancestor implemented state switch behavior
        }
    }
    catch (const runtime_error& e) {
        cerr << "--- ERROR (program): " << e.what() << endl;
        telegram_message("Runtime error on " + m_adb_name + ": " + e.what());
        retcode = -1;
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
    lock_guard<mutex> data_lock {m_mutex_all};
    m_bot_status = stopped;
    m_ret_code  |= retcode;
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

        // switch dump mode
        else if ("DUMP" == user_cmd) {
            lock_guard<mutex> data_lock {m_mutex_all};
            if (m_dump_frames) {
                m_dump_frames = false;
                cout << "Frames dump disabled.\n";
            } else {
                m_dump_frames = true;
                cout << "Frames dump enabled.\n";
            }
        }

        // TEST
        else if ("D" == user_cmd) {
            int found;
            double certainty;
            cv::Point location;
            cv::TickMeter tickMeter;
            cout << "Image detection... ";
            tickMeter.start();
            found = detect_image(
                "eve_undock_btn.png",
                &location,
                &certainty
            );
            tickMeter.stop();
            if (found) {
                cout << "succedded!\n"
                     << "imgs found: " << found << endl
                     << "certainty:  " << certainty << endl
                     << "location:   " << location << endl
                     << "time spent: " << tickMeter.getTimeSec() << "s\n";
            } else {
                cout << "failed (" << tickMeter.getTimeSec() << "s)\n";
            }
        }
        // TEST

        else
            cout << "Unknown command: " << user_cmd << endl;
    }
    lock_guard<mutex> data_lock {m_mutex_all};
    m_bot_status = stopped;
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
        cerr << "--- ERROR (process_frame): failed to dump frame\n";
   }

int AndroidBot::detect_image(
    idx_type   idx,
    cv::Point *p_location,
    double    *p_certainty,
    int        max_locs,
	condition_variable *p_notify)
{
    using namespace cv;
    // this function does not change object data
    // but we lock while copying frame into local variable
    // to prevent image distortion from getframes thread
    unique_lock<mutex> detect_lock {m_mutex_detect, defer_lock},
                       data_lock   {m_mutex_all};
    if (idx < 1 || idx >= m_lib_images.size()) {
        data_lock.unlock();
        cerr << "--- ERROR (detect_image): invalid image index\n";
        if (p_notify) p_notify->notify_one();
        return 0;
    }
    Mat *p_image = &m_lib_images[idx],
        *p_mask  = &m_lib_masks [idx],
         frame;
    if (!m_force_greyscale && 1 == p_image->channels())
        cvtColor(
            *mp_frame_fp,
            frame,
            COLOR_RGB2GRAY
        );
    else
        frame = mp_frame_fp->clone();
    // coping data from non-local object fields
    // in case parent thread is detached
    double threshold       {m_threshold};
    bool   dump_frames     {m_dump_frames};
    bool   force_greyscale {m_force_greyscale};
    string adb_serial      {m_adb_serial};
    data_lock.unlock();

    // dump images
    static const char* dump_err = "--- ERROR (detect_image): failed to dump images\n";
    double fp_to_uc_scale;
    short  dump_channels;
    Mat dump_img;
    if (dump_frames) {
        dump_channels = force_greyscale ? 1 : 3;
        fp_to_uc_scale = 1.0 / UC_TO_FP_SCALE;

        if (p_notify) detect_lock.lock();
        frame.convertTo(dump_img, CV_8UC(dump_channels), fp_to_uc_scale);
        if (!imwrite(adb_serial + "_det_frame.png", dump_img))
            cerr << dump_err;
        p_image->convertTo(dump_img, CV_8UC(dump_channels), fp_to_uc_scale);
        if (!imwrite(adb_serial + "_det_image.png", dump_img))
            cerr << dump_err;
        if (!p_mask->empty()) {
            p_mask->convertTo(dump_img, CV_8UC1, fp_to_uc_scale);
            if (!imwrite(adb_serial + "_det_mask.png", dump_img))
                cerr << dump_err;
        }
        if (detect_lock.owns_lock()) detect_lock.unlock();
    }

    Mat match_result;
    matchTemplate(
        frame,
        *p_image,
        match_result,
        TM_CCOEFF_NORMED, // if changed to TM_SQDIFF_NORMED methods,
                                 // don't forget to change code below 
        p_mask->empty() ? noArray() : *p_mask
    );

    // dump result
    if (dump_frames) {
        match_result.convertTo(dump_img, CV_8UC1, fp_to_uc_scale);
        if (p_notify) detect_lock.lock();
        if (!imwrite(adb_serial + "_det_result.png", dump_img))
            cerr << dump_err;
        if (detect_lock.owns_lock()) detect_lock.unlock();
    }

    // select best matches
    double match_certainty;
    Point  match_location,
           image_center {p_image->cols / 2, p_image->rows / 2};
    Rect erase_area;
    int x_offset = p_image->cols / 3,
        y_offset = p_image->rows / 3,
        v, // temporary calcuations storage
        det_count {0};
    for (int i = 0, steps = max_locs - 1; i <= steps; i++) {
        minMaxLoc(  // change args order if TM_SQDIFF_NORMED
            match_result,
            nullptr,
            &match_certainty,
            nullptr,
            &match_location
        );
        // "inverse" match_certainty if TM_SQDIFF_NORMED:
        // match_certainty = 1.0 - match_certainty;
        if (match_certainty < threshold)
            break;

        // save result
        det_count++;
        if (p_notify)    detect_lock.lock();
        if (p_location)  p_location [i] = match_location + image_center;
        if (p_certainty) p_certainty[i] = match_certainty;
        if (p_notify)    p_notify->notify_one();
        if (detect_lock.owns_lock()) detect_lock.unlock();

        // erase current match area - not required at last iteration
        if (i < steps) {
            v = match_location.x - x_offset;
            erase_area.x = v < 0 ? 0 : v;
            v = match_location.y - y_offset;
            erase_area.y = v < 0 ? 0 : v;
            v = match_location.x + p_image->cols + x_offset;
            erase_area.width = (v < match_result.cols ?
                v : match_result.cols - 1) - erase_area.x + 1;
            v = match_location.y + p_image->rows + y_offset;
            erase_area.height = (v < match_result.rows ?
                v : match_result.rows - 1) - erase_area.y + 1;
            match_result(erase_area) = 0.0; // 1.0 if TM_SQDIFF_NORMED
        }
    } // for (maxLocations)
    if (p_notify && !det_count) p_notify->notify_one();
    return det_count;
}

int AndroidBot::detect_image(
	initializer_list<idx_type> &idx_list,
	cv::Point *pLocation,
	double    *pCertainty)
{
	cv::Point location;
	double    found {0.0};
	unique_lock<mutex> det_lock(m_mutex_detect);
	thread det_thread([&]() {
		detect_image_any(idx_list, location, found);
	});
	m_notify.wait(det_lock);
	det_lock.unlock();
	det_thread.detach();
	if (!found)     return 0;
    if (pLocation)  *pLocation = location;
    if (pCertainty) *pCertainty = found;
	return 1;
}

void AndroidBot::detect_image_any(
    initializer_list<idx_type> &idx_list,
    cv::Point &location,
    double    &certainty)
{
    cv::Point th_location;
    double    th_certainty {0.0};
    auto img_cnt = idx_list.size();
    thread  **p_threads = new thread*[img_cnt];
    condition_variable det_notify;
    unique_lock<mutex> det_lock {m_mutex_detect};

    auto *idx = idx_list.begin(); 
    for (auto i = 0; i < img_cnt; i++, idx++)
        p_threads[i] = new thread([&]() {
            detect_image(
                *idx,
                &th_location,
                &th_certainty,
                1,
                &det_notify);
        });
    for (auto w = img_cnt; w; w--) {
        det_notify.wait(det_lock);
        if (th_certainty) {
            location  = th_location;
            certainty = th_certainty;
            // thread will be detached after notifying:
            // don't use non-local variables any further
            m_notify.notify_one();
            break;
        }
    }
    if (!th_certainty)
        m_notify.notify_one();
    det_lock.unlock();
    
    for (auto i = 0; i < img_cnt; i++) {
        p_threads[i]->join();
        delete p_threads[i];
    }
    delete[] p_threads;
}