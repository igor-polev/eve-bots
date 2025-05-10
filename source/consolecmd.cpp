/*
	EVE Echoes bot for Linux.
	Author: Igor Polev.

	Console class implementation.
*/

#include "consolecmd.hpp"

#include <stdexcept>
#include <string>

bool ConsoleCmd::has_output() const
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
}

/*
bool ConsoleCmd::execute_to(const string& output, long wait_time)
// Failed to implement: fgets returned child proc output only partially.
// Looks like child process (scrspt) does to flush its cout - full
// output recieved only after child process is terminated.
{
    runtime_error cmd_error { string("failed to run command\n") + *this };
    if (m_pipe) { throw cmd_error; }
    m_pipe = popen(c_str(), "r");
    if (!m_pipe) { throw cmd_error; }

    int pipe_fn {fileno(m_pipe)};
    fd_set read_fds;
    timeval timeout {
        wait_time / 1000,
        (wait_time % 1000) * 1000
    };
    auto buffer_size = output.length();
    char buffer[buffer_size], *read_result;
    string full_output {};
    size_t find_result {string::npos};
    while (true) {
        // check timeout
        FD_ZERO(&read_fds);
        FD_SET (pipe_fn, &read_fds);
        int check = select(
            pipe_fn + 1,
            &read_fds,
            NULL,
            NULL,
            &timeout
        );
        if (check == -1) { throw cmd_error; }
        if (check == 0)  { return false; }
        // read data and check result
        read_result = fgets(buffer, buffer_size, m_pipe);
        if (ferror(m_pipe)) {
            pclose(m_pipe);
            m_pipe = NULL;
            throw runtime_error { string("some error while running command\n") + *this };
        }
        if (read_result) {
            full_output += buffer;
            //DEBUG
            std::cout << "full_output:\n" << full_output << std::endl;
            //DEBUG
            find_result = full_output.find(output);
        }
        if (feof(m_pipe)) {
            return (find_result != string::npos);
        }
        if (find_result != string::npos) {
            return true;
        }
    }
}
*/