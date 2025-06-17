/*
	EVE Echoes bot for Linux.
	Author: Igor Polev.

	EveMinerBot class definition.
*/

#pragma once
#include "androidbot.hpp"

class EveMinerBot : public AndroidBot {
public:
	EveMinerBot(const char* config_file);
	bool new_states() override;
	void program()    override;
private:
	// specific parameters
	millis m_wait_some;
	// image library cache
	idx_type
		im_UNDOCK_BTN;
	// images positions cache
	cv::Point
		pnt_UNDOCK_BTN {-1, -1};
	// bot states cache
	state_itype
		st_UNKNOWN {nullptr},
		st_DOCKED  {nullptr};
};