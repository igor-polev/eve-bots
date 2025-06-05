/*
	EVE Echoes bot for Linux.
	Author: Igor Polev.

	AndroidBot class definition.
*/

#pragma once
#include <cstddef>
#include <mutex>
#include <string>
#include <vector>
#include <set>
#include <nlohmann/json.hpp>
#include "opencv2/core/types.hpp"
#include "opencv2/core/mat.hpp"
#include "V4l2Capture.h"

using namespace std;
using json = nlohmann::json;

class AndroidBot {
public:
	enum statuses {
		uninitialized,
		initialized,
		running,
		stopped
	};
  	AndroidBot() = delete;
    AndroidBot(const json &settings);
    virtual ~AndroidBot();
	void run();
	inline const statuses status() const noexcept {
		return m_bot_status;
	}
protected:
	// constants & types
	constexpr static const int   V4L2_FPS_DEFAULT  {30};
	constexpr static const long  ADB_WAIT_DEFAULT  {5};
    constexpr static const float UC_TO_FP_SCALE    {1.0 / 255.0};
	constexpr static const char* DEF_INITIAL_STATE {"unknown"};
	constexpr static const char* TERMINATION_STATE {"termination"};
	typedef vector<cv::Mat>::size_type idx_type;
	typedef set<string>::iterator      state_itype;
	// image detection
	double detect_image(idx_type idx);
	inline double detect_image(string image_name) {
		return detect_image(m_lib_img_map[image_name]);
	}
	// bot state manipulation
	inline const string& state() const noexcept {
		return *mp_state;
	}
	inline bool has_state(const string& new_state) const {
		return m_bot_states.find(new_state) != m_bot_states.end();
	}
	inline bool reg_state(const string& new_state) {
		if (has_state(new_state)) return false; 
		return get<bool>(m_bot_states.insert(new_state));
	}
	void set_sate (const string& new_state);
	// ancestor interface - must be implemented in child class
	virtual void register_states() {}; // add implemented states using reg_state()
	virtual void program() {};         // the program of the bot
	
	// global mutex
	mutex m_mutex_all;
	// image processing data
	vector<cv::Mat>       m_lib_images;  // library of images to search for
	vector<cv::Mat>       m_lib_masks;   // masks for each image
	map<string, idx_type> m_lib_img_map; // image library index
	cv::Mat     *mp_frame_fp     {nullptr};
	cv::Mat     *mp_frame_rgb    {nullptr};
	cv::Mat     *mp_frame_yuv    {nullptr};
	char        *mp_v4l2_buffer  {nullptr};
private:
	size_t       m_v4l2_buf_size {0};
	V4l2Capture *mp_v4l2_device  {nullptr};
	statuses     m_bot_status    {uninitialized};
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
	// bot (program) state
	state_itype  mp_state;      // current state iterator
	set<string>  m_bot_states { // set of possible states
		DEF_INITIAL_STATE,
		TERMINATION_STATE
	};
	// internal methods
	void process_frame(); // convert data from v4l2 buffer
	bool collect_images(  // load image library
		const json& filenames,
		bool force_gs
	); 
	// threads
	void adb_process();
	void getframes_process();
	void console_process();
};