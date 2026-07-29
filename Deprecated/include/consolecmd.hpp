/*
	EVE Echoes bot for Linux.
	Author: Igor Polev.

	ConsoleCmd class definition.
*/

#pragma once
#include <iostream>
#include <ostream>
#include <string>

using namespace std;

class ConsoleCmd : private string {
public:
    ConsoleCmd() : string() {};
    ConsoleCmd(const string &cmd) : string(cmd) {};
    ConsoleCmd(const char   *cmd) : string(cmd) {};
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
        cout << flush; // required befor 'system' call
        return system(c_str());
    };
    bool available() const {
        string exec_cmd { *this + " &> /dev/null" };
        cout << flush; // required befor 'system' call
        return (0 == system(exec_cmd.c_str()));
    };
    bool has_output() const
    {
        char byte_buffer, *result;
        FILE* pipe = popen(c_str(), "r");
        if (!pipe) {
            throw runtime_error { string("failed to run command\n") + *this };
        }
        result = fgets(&byte_buffer, 1, pipe);
        if (ferror(pipe)) {
            pclose(pipe);
            throw runtime_error { string("some error while running command\n") + *this };
        }
        pclose(pipe);
        return static_cast<bool>(result);
    };
};