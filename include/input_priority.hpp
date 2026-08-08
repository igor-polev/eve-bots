/*
	EVE bots for Windows.
	Author: Igor Polev.

	InputPriority - who the desktop belongs to while a program is running.

	A click is not addressed to a window. It goes into the one input queue
	the whole desktop shares, lands on whatever happens to be under the
	cursor, and is delivered to whatever holds the focus. Making one
	therefore means taking the mouse and the foreground away from whoever
	was using them, and while the game reads its mouse through Raw Input
	there is no way around that - see mouse_input.hpp for why the polite
	alternatives were tried and removed.

	What can be chosen is *when* to take them, and that is this pair.

	USER_PRIORITY_IDLE is how long the user must have been quiet before a
	click is made. The disruption that actually matters is a click landing
	in the middle of something - a drag, a sentence, a selection - and a
	second or two of required quiet is enough to put every click into the
	gaps instead. Set it to zero and the bot clicks the moment it wants to,
	and the interrupted work is the user's. Those are the two priority
	modes, and they are the same number.

	USER_PRIORITY_TIMEOUT is where the politeness ends. Somebody working
	steadily for minutes would otherwise stall a program indefinitely, so
	once this long has passed the click is made regardless. It bounds the
	other waiting for the desktop too: a window of the user's sitting over
	the point to be clicked is now retried rather than reported as a
	failure, until this runs out. That one does end in a refusal, because
	clicking into somebody else's window is never the right answer.
*/

#pragma once
#include <chrono>

struct InputPriority {
	std::chrono::milliseconds USER_PRIORITY_IDLE    {0};
	std::chrono::milliseconds USER_PRIORITY_TIMEOUT {0};
};

// What a click asked for at the console runs under. Whoever typed it was
// using the keyboard a moment ago, so requiring them to be idle would
// mean waiting out the timeout on every single one. Waiting for a covered
// point to clear is still worth having, so the timeout is kept.
inline InputPriority asked_for_by_hand(const InputPriority& configured)
{
	return InputPriority {
		std::chrono::milliseconds {0}, configured.USER_PRIORITY_TIMEOUT
	};
}
