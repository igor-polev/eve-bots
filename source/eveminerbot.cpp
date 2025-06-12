/*
	EVE Echoes bot for Linux.
	Author: Igor Polev.

	EveMinerBot class implementation.
*/

#include "eveminerbot.hpp"
//#include <fcntl.h>
#include <stdexcept>
#include <thread>

#include <iostream> // DEBUG

EveMinerBot::EveMinerBot(const char* config_file)
	: AndroidBot(config_file)
{
	// image cache
	idx_type im_success {static_cast<idx_type>(1)};

	im_UNDOCK_BTN = image_idx("eve_undock_btn.png");
	im_success &= im_UNDOCK_BTN;
	
	if (!im_success)
		throw out_of_range("some images do not exist in image library");

	// initial state cache
	st_UNKNOWN = has_state(DEF_INITIAL_STATE);

	// specific parameters
	m_wait_some = mseconds(2 * check_interval());
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
	cout << "EB program state: " << state() << endl; // DEBUG
	////////////////////////////////////////////////////////
	if (st_UNKNOWN == state_ptr())
	{
		// wait for "docked" signature
		if (detect_image(im_UNDOCK_BTN, &pnt_UNDOCK_BTN)) {
			set_state(st_DOCKED);
		}
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