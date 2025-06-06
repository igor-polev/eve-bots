/*
	EVE Echoes bot for Linux.
	Author: Igor Polev.

	See usage string below.
*/

#include <csignal>
#include <exception>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

#include "eveminerbot.hpp"

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
	try {
		cout << "EVE Echoes bot for Linux." << endl;

		if (argc < 2) {
			cout << "Bad arguments. See usage below.\n"
				 << _usage_str << endl;
			return -1;
		}
		if (string(argv[1]) == "--help") {
			cout << _usage_str << endl;
			return 0;
		}

		// start bot
		cout << "Reading config file...\n";
		ifstream config_file(argv[1]); // first argument must be JSON config file
		if (!config_file.is_open()) {
			cerr << "--- ERROR: faild to open config file '"
			     << argv[1] << "'\n";
			return -1;
		}
		json bot_settings = json::parse(config_file);
		cout << " - config file '" << argv[1] << "' parsed\n";
		EveMinerBot bot(bot_settings);
		if (bot.status() != AndroidBot::statuses::initialized) {
			cerr << "--- ERROR: failed to initialize bot.\n";
			return -1;
		}
		cout << " - bot initialized\n";
		cout << "Starting bot...\n";
		bot.run();
	}
    catch (const json::parse_error& e) {
		cerr << "--- ERROR (main): failed to parse config file '"
		     << argv[1] << "':\n"
             << e.what() << endl;
		return -1;
    }
    catch (const exception& e) {
		cerr << "--- ERROR (main): unhandled exception:\n"
             << e.what() << endl;
		return -1;
	}
	catch(...) {
		cerr << "--- ERROR (main): unknown excepition type, terminating.\n";
		return -1;
	}
	kill(0, SIGTERM); // kill is needed to terminate detached threads
	return 0; // never reached
}