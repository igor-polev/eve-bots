/*
	EVE bots for Windows.
	Author: Igor Polev.

	UndockProgram - leave the station and confirm the ship is in space.

	The undock button is the evidence that the ship is docked, and the ship
	core the evidence that it is not: neither can be seen from the other
	side, so finding one and then the other is a complete undock.

	No undock button is not a failure by itself - far more often it means
	the ship was never docked. So the first pass is one look at each: the
	button, and then the ship core, which answers "are we already out?"
	immediately instead of spending the whole budget retrying a button
	that is never going to appear. That makes running this program a way
	of asking for the ship to be in space rather than for a button to be
	pressed.

	Only when neither shows up on that first pass is retrying worthwhile,
	because then the likely cause is the interface still catching up.
	Retries go back to the button alone, until it appears or the search
	budget is gone. Waiting for space after the click retries too, since
	undocking takes seconds of animation during which nothing
	recognisable is on screen.

	Everything before the click shares one SEARCH_TIMEOUT deadline, so a
	client showing neither pattern still gives up on time.
*/

#pragma once
#include <chrono>

#include "mouse_input.hpp"
#include "program.hpp"

class UndockProgram : public Program {
public:
	// Patterns this program looks for, by their name in eve_images.json.
	static constexpr const char* UNDOCK_IMAGE   = "undock";
	static constexpr const char* SHIPCORE_IMAGE = "shipcore";

	// Parameter names in prog_params.json, and what they mean without it.
	static constexpr const char* KEY_SEARCH_TIMEOUT = "SEARCH_TIMEOUT";
	static constexpr const char* KEY_UNDOCK_TIMEOUT = "UNDOCK_TIMEOUT";
	static constexpr int SEARCH_TIMEOUT_DEFAULT = 15000;   // ms
	static constexpr int UNDOCK_TIMEOUT_DEFAULT = 45000;   // ms

	UndockProgram() : Program("undock") {}

	std::string purpose()       const override;
	std::string settings_text() const override;
	bool        configure(
		const ProgramParams& params, std::string& error) override;

protected:
	ProgramResult run(ProgramContext& context) override;

private:
	std::chrono::milliseconds m_search_timeout {SEARCH_TIMEOUT_DEFAULT};
	std::chrono::milliseconds m_undock_timeout {UNDOCK_TIMEOUT_DEFAULT};
};
