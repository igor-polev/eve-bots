/*
	EVE Echoes bot for Linux.
	Author: Igor Polev.

	AndroidBot class implementation.
*/

#include <androidbot.hpp>

#include <iostream>

AndroidBot::AndroidBot(json settings) {
    std::cout << "AndroidBot::AndroidBot(json settings)" << std::endl;
}

AndroidBot::~AndroidBot() {
    std::cout << "AndroidBot::~AndroidBot()" << std::endl;
}

void AndroidBot::run() {
    std::cout << "AndroidBot::run()" << std::endl;
}
