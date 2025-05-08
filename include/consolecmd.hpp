/*
	EVE Echoes bot for Linux.
	Author: Igor Polev.

	ConsoleCmd class definition.
*/

#pragma once
#include <string>

using namespace std;

class ConsoleCmd : private string {
private:
public:
    ConsoleCmd() : string() {};
    ConsoleCmd(const string &cmd) : string(cmd) {};
    ConsoleCmd(const char *cmd) : string(cmd) {};
    ConsoleCmd& operator = (const string &cmd) {
        string::operator=(cmd);
        return *this;
    };
    ConsoleCmd& operator = (const char *cmd) {
        string::operator=(cmd);
        return *this;
    };
    const string& to_string() const { return *this; };
    int execute() const {
        return system(c_str());
    };
    bool available() const {
        string exec_cmd { *this + " &> /dev/null" };
        return (0 == system(exec_cmd.c_str()));
    };
    bool has_output() const;
};