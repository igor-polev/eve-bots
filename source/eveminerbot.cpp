/*
	EVE Echoes bot for Linux.
	Author: Igor Polev.

	EveMinerBot class implementation.
*/

#include "eveminerbot.hpp"

void EveMinerBot::register_states()
{
	reg_state("DOCKED");
}

void EveMinerBot::program()
{
	// UNKNOWN - initial state
	if (DEF_INITIAL_STATE == state()) {
		// wait for docked signature
		if (detect_image("eve_undock_btn.png"))
			set_state("DOCKED");
	}
}