/*
	EVE Echoes bot for Linux.
	Author: Igor Polev.

	Console class implementation.
*/

#include "consolecmd.hpp"

#include <cstdio>
#include <stdexcept>

bool ConsoleCmd::has_output() const
{
    runtime_error pipe_error { string("failed to execute command\n") + *this };
    char byte_buffer, *result;
    FILE* pipe = popen(c_str(), "r");
    if (!pipe) { throw pipe_error; }
    result = fgets(&byte_buffer, 1, pipe);
    if (ferror(pipe)) {
        pclose(pipe);
        throw pipe_error;
    }
    pclose(pipe);
    return static_cast<bool>(result);
}