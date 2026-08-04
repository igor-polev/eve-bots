/*
	EVE bots for Windows.
	Author: Igor Polev.

	UndockProgram - leave the station and confirm the ship is in space.

	The undock button is the evidence that the ship is docked, and the ship
	core the evidence that it is not: neither can be seen from the other
	side, so finding one and then the other is a complete undock.

	Both steps retry, because neither is instant. The button may still be
	drawing when the program starts, and undocking itself takes seconds of
	animation during which nothing recognisable is on screen.
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
	// What looking for one pattern ended in.
	enum class Look { FOUND, MISSING, STOPPED, TROUBLE };

	// Searches for a pattern until it turns up or the budget runs out.
	Look look_for(
		ProgramContext&           context,
		size_t                    image,
		std::chrono::milliseconds budget,
		cv::Point&                corner,
		std::string&              trouble
	) const;

	std::chrono::milliseconds m_search_timeout {SEARCH_TIMEOUT_DEFAULT};
	std::chrono::milliseconds m_undock_timeout {UNDOCK_TIMEOUT_DEFAULT};
};
