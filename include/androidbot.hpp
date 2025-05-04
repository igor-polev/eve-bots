/*
	EVE Echoes bot for Linux.
	Author: Igor Polev.

	AndroidBot class definition.
*/

#include <nlohmann/json.hpp>

using json = nlohmann::json;

class AndroidBot {
public:
    AndroidBot();
    AndroidBot(json settings);
    virtual ~AndroidBot();
	void virtual run();
};