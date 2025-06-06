/*
	EVE Echoes bot for Linux.
	Author: Igor Polev.

	EveMinerBot class implementation.
*/

#include "eveminerbot.hpp"
#include <chrono>
#include <thread>

#ifndef NDEBUG
#include <iostream>
#endif

EveMinerBot::EveMinerBot(const json &settings) : AndroidBot(settings)
{
	stUNKNOWN = get<state_itype>(reg_state(DEF_INITIAL_STATE));
	m_wait_some = chrono::milliseconds(2 * check_interval());
};

bool EveMinerBot::new_states()
{
	streg_type reg_result;

	reg_result = reg_state("DOCKED");
	if (!get<bool>(reg_result)) return false;
	stDOCKED = get<state_itype>(reg_result);

	return true;
}

void EveMinerBot::program() // one iteration of main cycle
{
	#ifndef NDEBUG
	cout << "EveMinerBot state = " << state() << endl;
	#endif
	////////////////////////////////////////////////////////
	if (stUNKNOWN == state_ptr())
	{
		// wait for docked signature
		if (detect_image("eve_undock_btn.png", &pnt_undock_btn)) {
			set_state(stDOCKED);
		}
		else
			this_thread::sleep_for(m_wait_some);
	}
	////////////////////////////////////////////////////////
	else if (stDOCKED == state_ptr())
	{
		this_thread::sleep_for(m_wait_some);
	}
	////////////////////////////////////////////////////////
}