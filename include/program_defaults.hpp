/*
	EVE bots for Windows.
	Author: Igor Polev.

	ProgramDefaults - the numbers every program starts from.

	Waiting for the interface to catch up is the same job wherever it
	happens, and how long it takes is a property of the client and the
	machine it runs on rather than of any one program. So these live in
	eve_config.json beside the frame rate and the detection threshold,
	and a program only names a timeout of its own when it wants something
	other than the usual - a whole warp, say, which is nothing like the
	wait for a button to light up.
*/

#pragma once
#include <chrono>

// Named the way a program's own parameters are, and read through
// common(), so that a call site says which of the two a number came from
// without anybody having to go and look: MAX_JUMP_TIMEOUT is this
// program's, common().CONFIRM_TIMEOUT is everybody's. The keys in
// eve_config.json carry a _DEFAULT on the end to say the same thing from
// the file's side.
struct ProgramDefaults {
	// How long a program gives the interface to show the result of
	// something it did, when it has no reason to want longer.
	std::chrono::milliseconds ACTION_TIMEOUT  {2000};
	// Confirming a click: how long the proof of it is waited for, per
	// attempt.
	std::chrono::milliseconds CONFIRM_TIMEOUT {3000};
	// Further clicks to make when that proof does not come.
	int                       ACTION_RETRIES  {3};
};
