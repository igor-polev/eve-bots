/*
	EVE Echoes bot for Linux.
	Author: Igor Polev.

	See usage string below.
*/

#include <iostream>
#include <exception>

#include "eveminerbot.hpp"

constexpr static const char* _USAGE_STR = R"(
Run a bot for EVE Echoes game.
Requirements:
    dkms package;
    v4l2loopback-dkms kernel module;
    v4l2loopback-utils package;
    scrcpy utility;
	sudo command.
    See https://github.com/Genymobile/scrcpy for more details.
Usage:
    eve_bot <config.json>
)";

using namespace std;

int main(int argc, char* argv[]) 
{
	try {
		cout << "EVE Echoes bot for Linux." << endl;
		if (argc < 2) {
			cout << "Bad arguments. See usage below.\n"
				 << _USAGE_STR << endl;
			return -1;
		}
		if (string(argv[1]) == "--help") {
			cout << _USAGE_STR << endl;
			return 0;
		}
		EveMinerBot bot(argv[1]);
		if (bot.status() != AndroidBot::statuses::initialized) {
			cerr << "   [ERROR] Failed to initialize bot.\n";
			return -1;
		}
		return bot.run();
	}
    catch (const exception& e) {
		cerr << "   [ERROR] Unhandled exception in main():\n"
             << e.what() << endl;
	}
	catch(...) {
		cerr << "   [ERROR] Unknown excepition in main().\n";
	}
	return -1; // exception was thrown
}