/*
	EVE Echoes bot for Linux.
	Author: Igor Polev.

	AndroidBot class definition.
*/

#pragma once
#include <cstddef>
#include <initializer_list>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <chrono>
#include <stdexcept>
#include <mutex>
#include <condition_variable>
#include <nlohmann/json.hpp>

#include "consolecmd.hpp"
#include "opencv2/core/types.hpp"
#include "opencv2/core/mat.hpp"
#include "V4l2Capture.h"

using namespace std;
using json     = nlohmann::json;
using seconds  = chrono::seconds;
using millis = chrono::milliseconds;

class AndroidBot {
public:
	enum statuses {
		uninitialized,
		initialized,
		running,
		stopped
	};
  	AndroidBot() = delete;
    AndroidBot(const char* config_file); // this is the default constructor
    virtual ~AndroidBot();
	statuses status() const noexcept;
	int run();
protected:
	// constants & types
	constexpr static const int     V4L2_FPS_DEFAULT  {30};
    constexpr static const double  UC_TO_FP_SCALE    {1.0 / 255.0},
                                   DEF_THRESHOLD     {0.8};
	constexpr static const seconds ADB_WAIT_DEFAULT  {5};
	constexpr static const char   *DEF_INITIAL_STATE {"UNKNOWN"},
	                              *TERMINATION_STATE {"TERMINATION"};
	typedef vector<cv::Mat>::size_type idx_type;
	typedef set<string>::iterator      state_itype;
	typedef pair<state_itype, bool>    streg_type;

	// image operations
	idx_type image_idx(const string& image_name) const;
	int detect_image(
		const string& image_name,
		cv::Point *p_location  = nullptr,
		double    *p_certainty = nullptr,
		int        max_locs    = 1,
		condition_variable *p_notify = nullptr);
	int detect_image(
		idx_type   idx,
		cv::Point *p_location  = nullptr,
		double    *p_certainty = nullptr,
		int        max_locs    = 1,
		condition_variable *p_notify = nullptr);
	int detect_image(
		initializer_list<idx_type> &idx_list,
		cv::Point *p_location  = nullptr,
		double    *p_certainty = nullptr);
	
	// screen tapping
	int tap(cv::Point loc) const;
		
	// member access
	millis check_interval() const noexcept;

	// bot state manipulation
	const set<string>& all_states() const noexcept;
	const string&      state()      const noexcept;
	state_itype        state_ptr()  const noexcept;
	state_itype has_state(const string& state) const;
	state_itype has_state(const char*   state) const;
	streg_type  reg_state(const string& new_state);
	streg_type  reg_state(const char*   new_state);
	bool set_state(const string& new_state);
	bool set_state(const char*   new_state);
	void set_state(state_itype   new_state_ptr) noexcept; // unsafe pointer operation

	// ancestor interface - must be implemented in child class
	virtual bool new_states() = 0; // register new states
	virtual void program()    = 0; // define bot program
	
	// multi-threading
	mutex m_mutex_all,
		  m_mutex_detect;
	condition_variable m_notify;
private:
	int      m_ret_code   {0};
	statuses m_bot_status {uninitialized};
	// image processing data
	vector<cv::Mat> m_lib_images,  // library of images to search for
	                m_lib_masks;   // masks for each image
	map<string, idx_type> m_lib_img_map; // image library index
	cv::Mat     *mp_frame_fp     {nullptr},
	            *mp_frame_rgb    {nullptr},
	            *mp_frame_yuv    {nullptr};
	// V4L2 data
	char        *mp_v4l2_buffer  {nullptr};
	size_t       m_v4l2_buf_size {0};
	V4l2Capture *mp_v4l2_device  {nullptr};
	// user defined settings
	string       m_img_lib_dir,     // path to image library, / at the end is required
	             m_adb_name,        // user friendly name of Android device
	             m_adb_serial,      // serial number of Android device
	             m_v4l2_dev_name;   // v4l2 video device name, for ex.: /dev/video7
	unsigned     m_v4l2_dev_num;    // v4l2 video device number, for ex.: 7 for /dev/video7
	cv::Size     m_resolution;      // user-defined resolution of video stream
	double       m_scale_factor,    // scaling factor form search images to stream resolution
	             m_threshold;       // detection threshold 
	int          m_adb_fps;         // user-defined FPS of video stream
	millis       m_check_interval;  // bot screen check interval in milliseconds
	seconds      m_adb_wait_for;    // time to wait for ADB to init in seconds
	bool         m_dump_frames,     // dump each frame to file
	             m_force_greyscale; // force image detection in greayscale mode
	// bot (program) state
	state_itype  mp_state;      // current state iterator
	set<string>  m_bot_states { // set of possible states
		DEF_INITIAL_STATE,
		TERMINATION_STATE
	};
	// other stuff
	string m_tap_cmd;
	// internal methods
	void console_ui();        // console user interface
	void program_loop();      // bot program loop
	void getframes_process(); // v4l2 data reading thread
	void process_frame();     // convert data from v4l2 buffer
	bool collect_images(      // load image library
		const json& filenames,
		bool force_gs
	);
	void detect_image_any(
		initializer_list<idx_type> &idx_list,
		cv::Point &location,
		double    &certainty);
};

///////////////////////////////////////////////////////////////
// inline methods implementation

inline AndroidBot::statuses AndroidBot::status() const noexcept
{
	return m_bot_status;
};

inline AndroidBot::idx_type AndroidBot::image_idx(const string& image_name) const
{
	auto idx = m_lib_img_map.find(image_name);
	return idx != m_lib_img_map.end() ? idx->second : 0;
}

inline int AndroidBot::detect_image(
	const string& image_name,
	cv::Point *p_location,
	double    *p_certainty,
	int        max_locs,
	condition_variable *p_notify)
{
	auto idx = image_idx(image_name);
	if (!idx) 
		throw out_of_range(image_name + " does not exist in image library");
	return detect_image(idx, p_location, p_certainty, max_locs, p_notify);
}

inline const set<string>& AndroidBot::all_states() const noexcept
{
	return m_bot_states;
}

inline const string& AndroidBot::state() const noexcept
{
	return *mp_state;
}
inline AndroidBot::state_itype AndroidBot::state_ptr() const noexcept
{
	return mp_state;
}

inline AndroidBot::state_itype AndroidBot::has_state(const char* state) const
{
	state_itype it = m_bot_states.find(state);
	if (it == m_bot_states.end())
		return static_cast<state_itype>(nullptr);
	else
		return it;
}
inline AndroidBot::state_itype AndroidBot::has_state(const string& state) const
{
	return has_state(state.c_str());
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

inline millis AndroidBot::check_interval() const noexcept
{
	return m_check_interval;
}

inline int AndroidBot::tap(cv::Point loc) const
{
    ConsoleCmd cmd {m_tap_cmd
		+ to_string(loc.x)
        + " "
		+ to_string(loc.y)
        + " &> /dev/null"
	};
	return cmd.execute();
}
