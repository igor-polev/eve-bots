/*
	EVE Echoes bot for Linux.
	Author: Igor Polev.

	EveMinerBot class implementation.
*/

#include <stdexcept>
#include <thread>

#include "eveminerbot.hpp"

//#include <iostream> // DEBUG

EveMinerBot::EveMinerBot(const char* config_file)
	: AndroidBot(config_file)
{
	// image cache
	idx_type im_success {1};

	im_UNDOCK_BTN = image_idx("eve_undock_btn.png");
	im_success &= im_UNDOCK_BTN;
	
	if (!im_success)
		throw out_of_range("some images do not exist in image library");

	// initial state cache
	st_UNKNOWN = has_state(DEF_INITIAL_STATE);

	// specific parameters
	m_wait_some = check_interval() * 2;
}

bool EveMinerBot::new_states()
{
	streg_type reg_result;

	reg_result = reg_state("DOCKED");
	if (!get<bool>(reg_result))
		return false;
	st_DOCKED = get<state_itype>(reg_result);

	return true;
}

void EveMinerBot::program() // one iteration of main cycle
{
	////////////////////////////////////////////////////////
	if (st_UNKNOWN == state_ptr())
	{
		// wait for "docked" signature
		if (detect_image(im_UNDOCK_BTN, &pnt_UNDOCK_BTN))
			set_state(st_DOCKED);
		else
			this_thread::sleep_for(m_wait_some);
	}
	////////////////////////////////////////////////////////
	else if (st_DOCKED == state_ptr())
	{
		this_thread::sleep_for(m_wait_some);
	}
	////////////////////////////////////////////////////////
}