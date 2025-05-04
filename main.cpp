/*
	EVE Echoes bot for Linux.
	Author: Igor Polev.

	See usage string below.
*/

#include <exception>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

#include "androidbot.hpp"

using json = nlohmann::json;

static const char *_usage_str = R"(
Run a bot for EVE Echoes game.
Requirees scrcpy (https://github.com/Genymobile/scrcpy) to be installed.
Usage:
    eve_bot <config.json>
)";

int main(int argc, char* argv[]) 
{
	std::cout << "EVE Echoes bot for Linux." << std::endl;

	if (argc < 2) {
		std::cout << "Bad arguments. See usage below.\n";
		std::cout << _usage_str << std::endl;
		return -1;
	}
	std::cout << "Checking prerequisites...\n";
	if (0 != std::system("scrcpy --help &> /dev/null")) {
		std::cout << "scrcpy utility is not found. See usage below.\n";
		std::cout << _usage_str << std::endl;
		return -1;
	}
	std::cout << " - scrcpy utility detected\n";

	// start bot
	try {
		std::cout << "Configuring bot...\n";
		std::ifstream config_file(argv[1]); // first argument must be JSON config file
		if (!config_file.is_open()) {
			std::cout << "--- ERROR: faild to open config file '" << argv[1] << "'\n";
			return -1;
		}
		json bot_settings = json::parse(config_file);
		std::cout << " - config file '" << argv[1] << "' parsed\n";
		AndroidBot bot(bot_settings);
		if (bot.state() != AndroidBot::states::initialized) {
			std::cout << "--- ERROR: failed to initialize bot\n";
			return -1;
		}
		std::cout << " - bot initialized\n";
		std::cout << "Starting bot...\n";
		bot.run();
	}
    catch (const json::parse_error& e) {
		std::cout << "--- ERROR: failed to parse config file '" << argv[1] << "':\n";
        std::cout << e.what() << std::endl;
		return -1;
    }
    catch (const std::exception& e) {
		std::cout << "--- ERROR: unhandled exception:\n";
        std::cout << e.what() << std::endl;
		return -1;
	}
	catch(...) {
		std::cout << "--- ERROR: unknown excepition type, terminating" << std::endl;
		return -1;
	}
	return 0;
}