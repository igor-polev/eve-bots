/*
	EVE Echoes bot for Linux.
	Author: Igor Polev.

	AndroidBot class definition.
*/

#pragma once
#include <cstddef>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <chrono>
#include <mutex>
#include <nlohmann/json.hpp>

#include "opencv2/core/types.hpp"
#include "opencv2/core/mat.hpp"
#include "V4l2Capture.h"

using namespace std;
using json     = nlohmann::json;
using seconds  = chrono::seconds;
using mseconds = chrono::milliseconds;

class AndroidBot {
public:
	enum statuses {
		uninitialized,
		initialized,
		running,
		stopped
	};
  	AndroidBot() = delete;
    AndroidBot(const char*   config_file);
    AndroidBot(const string& config_file);
    virtual ~AndroidBot();
	statuses status() const noexcept;
	int run();
protected:
	// constants & types
	constexpr static const int     V4L2_FPS_DEFAULT  {30};
    constexpr static const double  UC_TO_FP_SCALE    {1.0 / 255.0};
    constexpr static const double  DEF_THRESHOLD     {0.8};
	constexpr static const seconds ADB_WAIT_DEFAULT  {5};
	constexpr static const char*   DEF_INITIAL_STATE {"UNKNOWN"};
	constexpr static const char*   TERMINATION_STATE {"TERMINATION"};
	typedef vector<cv::Mat>::size_type idx_type;
	typedef set<string>::iterator      state_itype;
	typedef pair<state_itype, bool>    streg_type;

	// image detection
	bool detect_image(
		const string& image_name,
		cv::Point *pLocation  = nullptr,
		double    *pCertainty = nullptr);
	bool detect_image(
		idx_type   idx,
		cv::Point *pLocation  = nullptr,
		double    *pCertainty = nullptr);
	
	// member access
	mseconds check_interval() const noexcept;

	// bot state manipulation
	const string& state()     const noexcept;
	state_itype   state_ptr() const noexcept;
	state_itype has_state(const string& new_state) const;
	state_itype has_state(const char*   new_state) const;
	streg_type  reg_state(const string& new_state);
	streg_type  reg_state(const char*   new_state);
	bool set_state(const string& new_state);
	bool set_state(const char*   new_state);
	void set_state(state_itype   new_state_ptr) noexcept; // unsafe pointer operation

	// ancestor interface - must be implemented in child class
	virtual bool new_states() {return true;} // register new states
	virtual void program()    {}             // define bot program
	
	// multi-threading
	mutex m_mutex_all;     // all data write access
	mutex m_mutex_threads; // threads execution ordering
    unique_lock<mutex> m_run_lock {m_mutex_all, defer_lock};
private:
	int      m_ret_code   {0};
	statuses m_bot_status {uninitialized};
	// image processing data
	vector<cv::Mat>       m_lib_images;  // library of images to search for
	vector<cv::Mat>       m_lib_masks;   // masks for each image
	map<string, idx_type> m_lib_img_map; // image library index
	cv::Mat     *mp_frame_fp     {nullptr};
	cv::Mat     *mp_frame_rgb    {nullptr};
	cv::Mat     *mp_frame_yuv    {nullptr};
	// V4L2 data
	char        *mp_v4l2_buffer  {nullptr};
	size_t       m_v4l2_buf_size {0};
	V4l2Capture *mp_v4l2_device  {nullptr};
	// user defined settings
	string       m_img_lib_dir;     // path to image library, / at the end is required
	string       m_adb_name;        // user friendly name of Android device
	string       m_adb_serial;      // serial number of Android device
	string       m_v4l2_dev_name;   // v4l2 video device path, for ex.: /dev/video7
	cv::Size     m_resolution;      // user-defined resolution of video stream
	double       m_scale_factor;    // scaling factor form search images to stream resolution
	double       m_threshold;       // detection threshold 
	int          m_adb_fps;         // user-defined FPS of video stream
	mseconds     m_check_interval;  // bot screen check interval in milliseconds
	seconds      m_adb_wait_for;    // time to wait for ADB to init in seconds
	bool         m_dump_frames;     // dump each frame to file
	bool         m_force_greyscale; // force image detection in greayscale mode
	// bot (program) state
	state_itype  mp_state;      // current state iterator
	set<string>  m_bot_states { // set of possible states
		DEF_INITIAL_STATE,
		TERMINATION_STATE
	};
	// internal methods
	void console_ui();        // console user interface
	void program_loop();      // bot program loop
	void getframes_process(); // v4l2 data reading thread
	void process_frame();     // convert data from v4l2 buffer
	bool collect_images(      // load image library
		const json& filenames,
		bool force_gs
	);
};

///////////////////////////////////////////////////////////////
// inline methods implementation

inline AndroidBot::AndroidBot(const string& config_file)
{
	AndroidBot(config_file.c_str());
}

inline AndroidBot::statuses AndroidBot::status() const noexcept
{
	return m_bot_status;
};

inline bool AndroidBot::detect_image(
	const string& image_name,
	cv::Point *pLocation,
	double    *pCertainty)
{
	return detect_image(
		m_lib_img_map[image_name],
		pLocation,
		pCertainty
	);
}

inline const string& AndroidBot::state() const noexcept
{
	return *mp_state;
}
inline AndroidBot::state_itype AndroidBot::state_ptr() const noexcept
{
	return mp_state;
}

inline AndroidBot::state_itype AndroidBot::has_state(const char* new_state) const
{
	state_itype it = m_bot_states.find(new_state);
	if (it == m_bot_states.end())
		return static_cast<state_itype>(nullptr);
	else
		return it;
}
inline AndroidBot::state_itype AndroidBot::has_state(const string& new_state) const
{
	return has_state(new_state.c_str());
}

inline AndroidBot::streg_type AndroidBot::reg_state(const char* new_state)
{
    auto it = m_bot_states.find(new_state);
    if (it != m_bot_states.end()) // already registered
        return streg_type(it, true); 
    return m_bot_states.insert(new_state);
}
inline AndroidBot::streg_type AndroidBot::reg_state(const string& new_state)
{
	return reg_state(new_state.c_str());
}

inline void AndroidBot::set_state(state_itype new_state_ptr) noexcept
{  
	// warning: unsafe pointer operation
	// new_state_ptr may point outside of m_bot_states
	mp_state = new_state_ptr;
}
inline bool AndroidBot::set_state(const char* new_state)
{
    mp_state = m_bot_states.find(new_state);
    if (mp_state != m_bot_states.end())
		return true;
    else
		return false;
}
inline bool AndroidBot::set_state(const string& new_state)
{
	return set_state(new_state.c_str());
}

inline mseconds AndroidBot::check_interval() const noexcept
{
	return m_check_interval;
}
