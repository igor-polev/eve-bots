/*
	EVE Echoes bot for Linux.
	Author: Igor Polev.

	EveMinerBot class definition.
*/

#pragma once
#include "androidbot.hpp"
#include <chrono>

class EveMinerBot : public AndroidBot {
public:
	EveMinerBot() = delete;
	EveMinerBot(const json &settings);
	bool new_states() override;
	void program()    override;
private:
	// general parameters
	chrono::milliseconds m_wait_some {1000};
	// images positions cache
	cv::Point
		pnt_undock_btn {0, 0};
	// bot states cache
	state_itype
		stUNKNOWN {nullptr},
		stDOCKED  {nullptr};
};