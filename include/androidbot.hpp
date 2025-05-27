/*
	EVE Echoes bot for Linux.
	Author: Igor Polev.

	AndroidBot class definition.
*/

#pragma once
#include <cstddef>
#include <nlohmann/json.hpp>
#include <opencv2/core/types.hpp>
#include <opencv2/core/mat.hpp>
#include <sys/types.h>
#include "../libv4l2cpp/inc/V4l2Capture.h"

using namespace std;
using json = nlohmann::json;

class AndroidBot {
public:
	enum states {
		uninitialized,
		initialized,
		running,
		stopped
	};
  	AndroidBot() = delete;
    AndroidBot(const json &settings);
    virtual ~AndroidBot();
	virtual void run();
	const states state() const noexcept { return m_bot_state; }
protected:
	constexpr static const int  V4L2_FPS_DEFAULT {30};
	constexpr static const long ADB_WAIT_DEFAULT {5};

	vector<cv::Mat> m_src_images; // library of images to search for
	virtual void process_frame(); // convert data from v4l2 buffer into usable image
private:
	states       m_bot_state     {uninitialized};
	size_t       m_v4l2_buf_size {0};
	V4l2Capture *mp_v4l2_device  {nullptr};
	char        *mp_v4l2_buffer  {nullptr};
	cv::Mat     *mp_frame_yuv    {nullptr};
	cv::Mat     *mp_frame_rgb    {nullptr};
	// user defined settings
	string       m_adb_name;       // user friendly name of Android device
	string       m_adb_serial;     // serial number of Android device
	string       m_v4l2_dev_name;  // v4l2 video device path, for ex.: /dev/video7
	cv::Size     m_resolution;     // user-defined resolution of video stream
	double       m_scale_factor;   // scaling factor form search images to stream resolution 
	int          m_adb_fps;        // user-defined FPS of video stream
	long         m_check_interval; // bot screen check interval in milliseconds
	long         m_adb_wait_for;   // time to wait for ADB to init in seconds
	string       m_adb_log;        // filename for ADB log
	// threads
	void adb_process();
	void getframes_process();
	void console_process();
};