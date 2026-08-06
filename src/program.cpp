/*
	EVE bots for Windows.
	Author: Igor Polev.

	Program and ProgramRunner implementation.
*/

#include <algorithm>
#include <iomanip>
#include <sstream>

#include "program.hpp"

namespace {

// A program sleeping between attempts checks for a stop this often.
constexpr std::chrono::milliseconds WAIT_STEP {50};

// Pause between two attempts at the same pattern. A full frame search
// already costs seconds, so this only matters once a quick box search is
// possible - and then it keeps a program from spinning on the CPU.
constexpr std::chrono::milliseconds RETRY_PAUSE {250};

} // namespace

std::chrono::milliseconds since(ProgramClock::time_point start)
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(
		ProgramClock::now() - start
	);
}

std::string seconds_text(std::chrono::milliseconds spent)
{
	std::ostringstream text;
	text << std::fixed << std::setprecision(1)
	     << (spent.count() / 1000.0) << " s";
	return text.str();
}

std::string point_text(const cv::Point& at)
{
	return std::to_string(at.x) + "," + std::to_string(at.y);
}

std::string program_exit_text(ProgramExit exit)
{
	switch (exit) {
	case ProgramExit::SUCCESS: return "SUCCESS";
	case ProgramExit::STOPPED: return "STOPPED";
	default:                   return "FAILURE";
	}
}

ProgramResult Program::exec(ProgramContext& context)
{
	// A previous run may have been aborted; this one starts clean.
	m_stop.store(false);
	try {
		return run(context);
	}
	catch (const std::exception& e) {
		return ProgramResult {
			ProgramExit::FAILURE,
			std::string("stopped by an unexpected error: ") + e.what()
		};
	}
	catch (...) {
		return ProgramResult {
			ProgramExit::FAILURE, "stopped by an unknown error"
		};
	}
}

bool Program::wait(std::chrono::milliseconds duration) const
{
	for (std::chrono::milliseconds left = duration;
	     left > std::chrono::milliseconds::zero();
	     left -= WAIT_STEP)
	{
		if (stopping()) return false;
		std::this_thread::sleep_for(std::min(WAIT_STEP, left));
	}
	return !stopping();
}

Program::Look Program::look_once(
	ProgramContext&           context,
	size_t                    image,
	std::chrono::milliseconds budget,
	cv::Point&                corner,
	std::string&              trouble) const
{
	if (stopping()) return Look::STOPPED;
	if (budget <= std::chrono::milliseconds::zero()) return Look::MISSING;

	// The search gets what is left of the budget and no more, so one slow
	// full frame pass cannot overrun the whole timeout.
	const ProgramClock::time_point began = ProgramClock::now();
	Detection found;
	if (!context.detector.detect(image, 1, false, found, trouble, budget)) {
		// Running out of the time it was given is not a fault, it is the
		// answer: the pattern was not there within the budget.
		return since(began) >= budget ? Look::MISSING : Look::TROUBLE;
	}
	if (found.hits.empty()) return Look::MISSING;

	corner = found.hits.front().at;
	return Look::FOUND;
}

Program::Look Program::look_for(
	ProgramContext&           context,
	size_t                    image,
	std::chrono::milliseconds budget,
	cv::Point&                corner,
	std::string&              trouble) const
{
	const ProgramClock::time_point deadline = ProgramClock::now() + budget;
	while (true) {
		const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
			deadline - ProgramClock::now()
		);
		if (left <= std::chrono::milliseconds::zero()) return Look::MISSING;

		const Look seen = look_once(context, image, left, corner, trouble);
		// Only "not on screen this time" is worth another attempt.
		if (Look::MISSING != seen) return seen;

		if (!wait(RETRY_PAUSE)) return Look::STOPPED;
	}
}

bool Program::click_middle(
	ProgramContext&  context,
	size_t           image,
	const cv::Point& corner,
	ClickResult&     click,
	std::string&     trouble) const
{
	const ImagePattern& pattern = context.images(image);
	const cv::Point target =
		corner + cv::Point {pattern.width() / 2, pattern.height() / 2};
	return click_at(
		context.capture.target(), target, UI_WAIT_DEFAULT, click, trouble
	);
}

ProgramRunner::~ProgramRunner()
{
	abort();
	wait();
}

void ProgramRunner::add(std::unique_ptr<Program> program)
{
	if (!program) return;
	m_index[program->name()] = m_programs.size();
	m_programs.push_back(std::move(program));
}

bool ProgramRunner::configure(const ProgramParams& params, std::string& error)
{
	for (const std::unique_ptr<Program>& program : m_programs) {
		if (!program->configure(params, error)) {
			error = "program '" + program->name() + "': " + error;
			return false;
		}
	}
	return true;
}

size_t ProgramRunner::index(const std::string& name) const
{
	const auto found = m_index.find(name);
	return m_index.end() == found ? NOT_FOUND : found->second;
}

std::string ProgramRunner::name_list() const
{
	std::string list;
	for (const std::unique_ptr<Program>& program : m_programs) {
		if (!list.empty()) list += ", ";
		list += program->name();
	}
	return list;
}

std::string ProgramRunner::current() const
{
	std::lock_guard<std::mutex> lock {m_current_mutex};
	return m_current;
}

void ProgramRunner::set_current(const std::string& name)
{
	std::lock_guard<std::mutex> lock {m_current_mutex};
	m_current = name;
}

bool ProgramRunner::start(
	size_t program, const ProgramContext& context, std::string& error)
{
	if (m_running.load()) {
		error = "'" + current() + "' is already running; abort it first";
		return false;
	}
	// The previous run has ended - m_running said so - but nobody has
	// joined its thread yet.
	if (m_thread.joinable()) m_thread.join();

	Program& job = *m_programs[program];
	set_current(job.name());
	m_running.store(true);
	try {
		m_thread = std::thread {[this, &job, context] {
			// context holds three references and is copied into the thread,
			// so it stays valid as long as the objects behind it do.
			ProgramContext local = context;
			const ProgramResult result = job.exec(local);
			set_current(std::string {});
			m_running.store(false);
			if (m_on_finish) m_on_finish(job.name(), result);
		}};
	}
	catch (const std::exception& e) {
		set_current(std::string {});
		m_running.store(false);
		error = std::string("cannot start the program thread: ") + e.what();
		return false;
	}
	return true;
}

void ProgramRunner::abort()
{
	// Every program, not just the running one: the runner does not track
	// which is in flight, and exec() clears the flag as a run begins, so a
	// flag left raised here cannot affect a later start().
	for (const std::unique_ptr<Program>& program : m_programs)
		program->request_stop();
}

void ProgramRunner::wait()
{
	if (m_thread.joinable()) m_thread.join();
}
