/*
	EVE bots for Windows.
	Author: Igor Polev.

	InputPriority - who the desktop belongs to while a program is running.

	A click takes the mouse and the foreground away from whoever was using
	them; see mouse_input.hpp for why that cannot be avoided. What can be
	chosen is when to take them.
*/

#pragma once

#include "common_defs.hpp"

struct InputPriority {
	// How long the user must be quiet before a click is made. Zero means
	// the bot goes first, and the user is the one interrupted.
	eb::Millis USER_PRIORITY_IDLE    {0};
	// Where the waiting ends: after this the click is made anyway. It also
	// limits the wait for a covered point to become free.
	eb::Millis USER_PRIORITY_TIMEOUT {0};
};

// What a click typed at the console runs under. The person who typed it was
// using the keyboard a moment before, so waiting for them to be quiet would
// mean waiting out the whole timeout on every click.
inline InputPriority asked_for_by_hand(const InputPriority& configured)
{
	return InputPriority {eb::Millis {0}, configured.USER_PRIORITY_TIMEOUT};
}
