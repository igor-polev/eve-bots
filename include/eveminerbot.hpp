/*
	EVE Echoes bot for Linux.
	Author: Igor Polev.

	EveMinerBot class definition.
*/

#pragma once
#include "androidbot.hpp"

using json = nlohmann::json;

class EveMinerBot : private AndroidBot {
public:
	EveMinerBot() = delete;
	EveMinerBot(const json &settings) : AndroidBot(settings) {};
	void register_states() override;
	void program() override;
};