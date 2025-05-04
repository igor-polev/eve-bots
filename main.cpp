/*
	EVE Echoes bot for Linux.
	Author: Igor Polev.

	See usage string below.
*/

#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

#include "androidbot.hpp"

using json = nlohmann::json;

static const char *_usage_str = R"usage(
Run a bot for EVE Echoes game.
Requirees scrcpy (https://github.com/Genymobile/scrcpy) to be installed.
Usage:
    eve_bot config.json
)usage";

int main(int argc, char* argv[]) 
{
	std::cout << "EVE Echoes bot for Linux." << std::endl;

	// prereqs check
	auto scrcpy_pipe = popen("iscrcpy --help &> /dev/null", "r");
	if (0 != std::system("scrcpy --help &> /dev/null")) {
		std::cout << "scrcpy utility is not found. See usage below." << std::endl;
		std::cout << _usage_str << std::endl;
		return -1;
	}
	if (argc < 2) {
		std::cout << "Bad arguments. See usage below." << std::endl;
		std::cout << _usage_str << std::endl;
		return -1;
	}

	// start bot
	try {
		std::ifstream config_file(argv[1]); // first argument must be JSON config file
		if (!config_file.is_open()) {
			std::cout << "--- ERROR: faild to open config file '" << argv[1] << "'\n";
			return -1;
		}
		json bot_config = json::parse(config_file);
		AndroidBot bot(bot_config);
		bot.run();
	}
    catch (const json::parse_error& e) {
		std::cout << "--- ERROR: failed to parse config file '" << argv[1] << "'\n";
        std::cout << e.what() << std::endl;
		return -1;
    }
	catch(...) {
		std::cout << "--- ERROR: unhandles excepition, terminating" << std::endl;
		return -1;
	}
	return 0;
}