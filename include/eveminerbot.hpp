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
	// general parameters
	mseconds m_wait_some {1000};
	// images positions cache
	cv::Point
		pnt_undock_btn {0, 0};
	// bot states cache
	state_itype
		stUNKNOWN {nullptr},
		stDOCKED  {nullptr};
};

///////////////////////////////////////////////////////////////
// inline methods implementation

inline EveMinerBot::EveMinerBot(const char* config_file)
	: AndroidBot(config_file)
{
	// DEF_INITIAL_STATE is allready registered,
	// but we get its state_itype pointer 
	stUNKNOWN = get<state_itype>(reg_state(DEF_INITIAL_STATE));
	m_wait_some = mseconds(2 * check_interval());
};