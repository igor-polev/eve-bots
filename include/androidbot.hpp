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
	constexpr static const int   V4L2_FPS_DEFAULT {30};
	constexpr static const long  ADB_WAIT_DEFAULT {5};
    constexpr static const float UC_TO_FP_SCALE   {1.0 / 255.0};
	typedef vector<cv::Mat>::size_type idx_type;

	virtual void process_frame();  // convert data from v4l2 buffer
	virtual double detect_image(idx_type idx);
	inline double detect_image(string image_name) {
		return detect_image(m_src_img_map[image_name]);
	}
	
	vector<cv::Mat>       m_src_images;  // library of images to search for
	vector<cv::Mat>       m_src_masks;   // masks for each image
	map<string, idx_type> m_src_img_map; // image library index
	cv::Mat     *mp_frame_fp     {nullptr};
	cv::Mat     *mp_frame_rgb    {nullptr};
	cv::Mat     *mp_frame_yuv    {nullptr};
	char        *mp_v4l2_buffer  {nullptr};
private:
	size_t       m_v4l2_buf_size {0};
	V4l2Capture *mp_v4l2_device  {nullptr};
	states       m_bot_state     {uninitialized};
	// user defined settings
	string       m_adb_name;        // user friendly name of Android device
	string       m_adb_serial;      // serial number of Android device
	string       m_v4l2_dev_name;   // v4l2 video device path, for ex.: /dev/video7
	cv::Size     m_resolution;      // user-defined resolution of video stream
	double       m_scale_factor;    // scaling factor form search images to stream resolution 
	int          m_adb_fps;         // user-defined FPS of video stream
	long         m_check_interval;  // bot screen check interval in milliseconds
	long         m_adb_wait_for;    // time to wait for ADB to init in seconds
	bool         m_dump_frames;     // dump each frame to file
	bool         m_force_greyscale; // force image detection in greayscale mode
	// internal methods
	bool collect_images(const json& filenames, bool force_gs); // load library of images
	// threads
	void adb_process();
	void getframes_process();
	void console_process();
};