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
using namespace std;

static const char *_usage_str = R"(
Run a bot for EVE Echoes game.
Requirees scrcpy (https://github.com/Genymobile/scrcpy) to be installed.
Usage:
    eve_bot <config.json>
)";

int main(int argc, char* argv[]) 
{
	cout << "EVE Echoes bot for Linux." << endl;

	if (argc < 2) {
		cout << "Bad arguments. See usage below.\n";
		cout << _usage_str << endl;
		return -1;
	}
	cout << "Checking prerequisites...\n";
	if (0 != system("scrcpy --help &> /dev/null")) {
		cout << "scrcpy utility is not found. See usage below.\n";
		cout << _usage_str << endl;
		return -1;
	}
	cout << " - scrcpy utility detected\n";

	// start bot
	try {
		cout << "Configuring bot...\n";
		ifstream config_file(argv[1]); // first argument must be JSON config file
		if (!config_file.is_open()) {
			cout << "--- ERROR: faild to open config file '" << argv[1] << "'\n";
			return -1;
		}
		json bot_settings = json::parse(config_file);
		cout << " - config file '" << argv[1] << "' parsed\n";
		AndroidBot bot(bot_settings);
		if (bot.state() != AndroidBot::states::initialized) {
			cout << "--- ERROR: failed to initialize bot.\n";
			return -1;
		}
		cout << " - bot initialized\n";
		cout << "Starting bot...\n";
		return bot.run();
	}
    catch (const json::parse_error& e) {
		cout << "--- ERROR: failed to parse config file '" << argv[1] << "':\n";
        cout << e.what() << endl;
		return -1;
    }
    catch (const exception& e) {
		cout << "--- ERROR: unhandled exception:\n";
        cout << e.what() << endl;
		return -1;
	}
	catch(...) {
		cout << "--- ERROR: unknown excepition type, terminating." << endl;
		return -1;
	}
}