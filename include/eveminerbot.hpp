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
	// image library cache
	idx_type
		im_UNDOCK_BTN,
		im_SHIP_CORE,
		im_OV_FILTER,
		im_OV_BUTTON;
	// images positions cache
	cv::Point
		pnt_UNDOCK_BTN {-1, -1},
		pnt_SHIP_CORE  {-1, -1};
	// bot states cache
	state_itype
		st_TERMINATION {nullptr},
		st_UNKNOWN     {nullptr},
		st_CANCEL      {nullptr},
		st_DOCKED      {nullptr},
		st_UNDOCKING   {nullptr};
};