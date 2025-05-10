/*
	EVE Echoes bot for Linux.
	Author: Igor Polev.

	See usage string below.
*/

#include <exception>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

#include "consolecmd.hpp"
#include "androidbot.hpp"

using json = nlohmann::json;
using namespace std;

static const char *_usage_str = R"(
Run a bot for EVE Echoes game.
Requirements:
    dkms package;
    v4l2loopback-dkms kernel module;
    v4l2loopback-utils package;
    scrcpy utility.
    See https://github.com/Genymobile/scrcpy for more details.
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
	ConsoleCmd command;
	string cmd_list[] {
		"dkms",
		"v4l2loopback-ctl",
		"scrcpy",
		"sudo"
	};
	for (string cmd : cmd_list) {
		command = cmd + " --help";
		if (!command.available()) {
			cout << cmd << " is not available. See usage below.\n";
			cout << _usage_str << endl;
			return -1;
		}
		cout << " - " << cmd << " present\n";
	}
	command = "dkms status | grep v4l2loopback";
	if (!command.has_output()) {
		cout << " - v4l2loopback kernel module not found. See usage below.\n";
		cout << _usage_str << endl;
		return -1;
	}
	cout << " - v4l2loopback kernel module detected\n";
	cout << "Validating sudo command...\n";
	command = "sudo --validate";
	if (0 != command.execute()) {
		cerr << "--- ERROR: failed to validate sudo command.\n";
		return -1;
	}

	// start bot
	try {
		cout << "Configuring bot...\n";
		ifstream config_file(argv[1]); // first argument must be JSON config file
		if (!config_file.is_open()) {
			cerr << "--- ERROR: faild to open config file '" << argv[1] << "'\n";
			return -1;
		}
		json bot_settings = json::parse(config_file);
		cout << " - config file '" << argv[1] << "' parsed\n";
		AndroidBot bot(bot_settings);
		if (bot.state() != AndroidBot::states::initialized) {
			cerr << "--- ERROR: failed to initialize bot.\n";
			return -1;
		}
		cout << " - bot initialized\n";
		cout << "Starting bot...\n";
		return bot.run();
	}
    catch (const json::parse_error& e) {
		cerr << "--- ERROR (main): failed to parse config file '" << argv[1] << "':\n";
        cerr << e.what() << endl;
		return -1;
    }
    catch (const exception& e) {
		cerr << "--- ERROR (main): unhandled exception:\n";
        cerr << e.what() << endl;
		return -1;
	}
	catch(...) {
		cerr << "--- ERROR (main): unknown excepition type, terminating.\n";
		return -1;
	}
}