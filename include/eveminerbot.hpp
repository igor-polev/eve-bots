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
	virtual void program();
private:
	enum miner_states {
		unknown,
		termination
	};
	// unknown state is a default begining of the program
	miner_states m_state {unknown};
};