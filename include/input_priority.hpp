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
	// How long the user must have been quiet before a click is made. Zero
	// is the bot-first mode, where the interrupted work is the user's.
	eb::Millis USER_PRIORITY_IDLE    {0};
	// Where the politeness ends: past this the click is made regardless,
	// and it bounds the wait for a covered point to clear as well.
	eb::Millis USER_PRIORITY_TIMEOUT {0};
};

// What a click asked for at the console runs under. Whoever typed it was
// using the keyboard a moment ago, so requiring them to be idle would mean
// waiting out the timeout on every single one.
inline InputPriority asked_for_by_hand(const InputPriority& configured)
{
	return InputPriority {eb::Millis {0}, configured.USER_PRIORITY_TIMEOUT};
}
