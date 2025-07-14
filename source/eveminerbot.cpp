/*
	EVE Echoes bot for Linux.
	Author: Igor Polev.

	EveMinerBot class implementation.
*/

#include <stdexcept>
#include <thread>

#include "eveminerbot.hpp"
#include "androidbot.hpp"

EveMinerBot::EveMinerBot(const char* config_file)
	: AndroidBot(config_file)
{
	// image cache
	idx_type im_success {1};

	im_UNDOCK_BTN = image_idx("eve_undock_btn.png");
	im_success &= im_UNDOCK_BTN;
	im_SHIP_CORE = image_idx("eve_ship_core.png");
	im_success &= im_SHIP_CORE;
	im_OV_FILTER = image_idx("eve_ov_filter.png");
	im_success &= im_OV_FILTER;
	im_OV_BUTTON = image_idx("eve_ov_button.png");
	im_success &= im_OV_BUTTON;
	
	if (!im_success)
		throw out_of_range("some images do not exist in image library");

	// initial state cache
	st_UNKNOWN     = has_state(DEF_INITIAL_STATE);
	st_TERMINATION = has_state(TERMINATION_STATE);
}

bool EveMinerBot::new_states()
{
	streg_type reg_result;

	reg_result = reg_state("DOCKED");
	if (!get<bool>(reg_result)) return false;
	st_DOCKED = get<state_itype>(reg_result);

	reg_result = reg_state("UNDOCKING");
	if (!get<bool>(reg_result)) return false;
	st_UNDOCKING = get<state_itype>(reg_result);

	reg_result = reg_state("CANCEL");
	if (!get<bool>(reg_result)) return false;
	st_CANCEL = get<state_itype>(reg_result);

	return true;
}

void EveMinerBot::program() // one iteration of main cycle
{
	constexpr static const millis  WAIT_SOME_TIME      {1500}; 
	constexpr static const seconds MAX_UNKNOWN_TIME    {300}; 
	constexpr static const seconds MAX_DOCKED_TIME     {120}; 
	constexpr static const seconds MAX_UNDOCKING_TIME  {40}; 

	////////////////////////////////////////////////////////
	if (st_UNKNOWN == state_ptr())
	{
		if (state_time_out(MAX_UNKNOWN_TIME, TERMINATION_STATE))
			return;
		if (!detect(im_UNDOCK_BTN, nullptr, &pnt_UNDOCK_BTN)) {
			this_thread::sleep_for(WAIT_SOME_TIME);
			return;
		}
		set_state(st_DOCKED);
	}
	////////////////////////////////////////////////////////
	else if (st_DOCKED == state_ptr())
	{
		if (state_time_out(MAX_DOCKED_TIME, TERMINATION_STATE))
			return;
		if (!tap(pnt_UNDOCK_BTN))
			throw runtime_error("failed to tap undock button");
		set_state(st_UNDOCKING);
	}
	////////////////////////////////////////////////////////
	else if (st_UNDOCKING == state_ptr())
	{
		if (state_time_out(MAX_UNDOCKING_TIME, "CANCEL"))
			return;
		if (!detect(im_SHIP_CORE, nullptr, &pnt_SHIP_CORE)) {
			this_thread::sleep_for(WAIT_SOME_TIME);
			return;
		}
		if (!detect(im_OV_FILTER)) {
			if (!tap(im_OV_BUTTON))
				throw runtime_error("failed to tap overview button");
		}
	}
	////////////////////////////////////////////////////////
}