/*
	EVE bots for Windows.
	Author: Igor Polev.

	Program and ProgramRunner implementation.
*/

#include <algorithm>

#include <winrt/base.h>

#include "program.hpp"

namespace {

// A program sleeping checks for a stop this often.
constexpr eb::Millis WAIT_STEP {100};

// Nothing anybody would write in prg_params.json, so asking for a value with
// this as the fallback answers "was there a number there at all?" as well as
// "what was it?".
constexpr double NOT_A_NUMBER = -1e18;

} // namespace

std::string program_exit_text(ProgramExit exit)
{
	switch (exit) {
	case ProgramExit::SUCCESS: return "SUCCESS";
	case ProgramExit::STOPPED: return "STOPPED";
	default:                   return "FAILURE";
	}
}

Program::PatternDecl::PatternDecl(Program& owner)
{
	owner.m_patterns.push_back(this);
}

void Program::Pattern::resolve(
	ImageLibrary& images, std::vector<std::string>& missing)
{
	m_index = images.index(m_name);
	if (ImageLibrary::NOT_FOUND == m_index) missing.push_back(m_name);
}

void Program::Patterns::resolve(
	ImageLibrary& images, std::vector<std::string>& missing)
{
	m_indexes.clear();
	m_indexes.reserve(m_names.size());
	for (const char* name : m_names) {
		const size_t at = images.index(name);
		if (ImageLibrary::NOT_FOUND == at) missing.push_back(name);
		else                               m_indexes.push_back(at);
	}
}

Program::ParamDecl::ParamDecl(Program& owner, const char* key) : m_key {key}
{
	owner.m_params.push_back(this);
}

bool Program::Millis::read(
	const ProgramParams& params,
	const std::string&   program,
	std::string&         error)
{
	m_tuned = params.has(program, key());
	if (!m_tuned) return true;

	// The file says something about it, so anything that is not a number
	// it can use is a refusal rather than a fall back to the built in
	// value: that value is what the file was written to change.
	const double value = params.number(program, key(), NOT_A_NUMBER);
	if (NOT_A_NUMBER == value) {
		error = std::string(key()) + " must be a number of milliseconds";
		return false;
	}
	if (value < static_cast<double>(least())) {
		error = std::string(key()) + " is milliseconds and cannot be less than "
		      + std::to_string(least());
		return false;
	}
	m_value = eb::Millis {static_cast<long long>(value)};
	return true;
}

std::string Program::Millis::text() const
{
	return std::to_string(m_value.count()) + " ms";
}

bool Program::Flag::read(
	const ProgramParams& params,
	const std::string&   program,
	std::string&         error)
{
	m_tuned = params.has(program, key());
	if (!m_tuned) return true;

	const double value = params.number(program, key(), NOT_A_NUMBER);
	if (NOT_A_NUMBER == value) {
		error = std::string(key()) + " must be true or false";
		return false;
	}
	m_value = 0.0 != value;
	return true;
}

bool Program::Count::read(
	const ProgramParams& params,
	const std::string&   program,
	std::string&         error)
{
	m_tuned = params.has(program, key());
	if (!m_tuned) return true;

	const double value = params.number(program, key(), NOT_A_NUMBER);
	if (NOT_A_NUMBER == value) {
		error = std::string(key()) + " must be a number of times";
		return false;
	}
	if (value < 0.0) {
		error = std::string(key()) + " is a number of times and cannot be "
		        "negative";
		return false;
	}
	m_value = static_cast<int>(value);
	return true;
}

std::string Program::Count::text() const
{
	return std::to_string(m_value);
}

bool Program::configure(const ProgramParams& params, std::string& error)
{
	for (ParamDecl* param : m_params) {
		// The name has to say what the number is, because that is all a call
		// site shows of it: GATE_JUMP_PAUSE is time the program will really
		// spend, DOCKING_TIMEOUT is time it hopes not to.
		const std::string key    = param->key();
		// A Flag says what it turns on rather than what it costs, so it is
		// the one kind with no ending to check.
		const std::string wanted = param->suffix();
		if (!wanted.empty()
			&& (key.size() <= wanted.size()
			    || 0 != key.compare(
			           key.size() - wanted.size(), wanted.size(), wanted)))
		{
			error = key + " must be named to end in " + wanted;
			return false;
		}
		if (!param->read(params, m_name, error)) return false;
	}

	// A key nothing answers to is almost always a parameter that has been
	// renamed or misspelt, and the file gives no sign of it: the program
	// simply runs on its built in value. Better to say so and refuse.
	for (const std::string& key : params.keys(m_name)) {
		const auto known = [&key](const ParamDecl* param) {
			return key == param->key();
		};
		if (m_params.end() == std::find_if(m_params.begin(), m_params.end(), known)) {
			error = "'" + key + "' is not one of its parameters ("
			      + param_list() + ")";
			return false;
		}
	}
	return true;
}

std::string Program::param_list() const
{
	std::string list;
	for (const ParamDecl* param : m_params) {
		if (!list.empty()) list += ", ";
		list += param->key();
	}
	return list.empty() ? "it has none" : list;
}

std::string Program::settings_text() const
{
	if (m_params.empty()) return "nothing to tune";

	std::string text;
	for (size_t i = 0; i < m_params.size(); ++i) {
		// Three to a line, the way the listing indents them.
		if (0 != i) text += (0 == i % 3) ? ",\n      " : ", ";
		text += std::string(m_params[i]->key()) + " " + m_params[i]->text();
		if (m_params[i]->tuned()) text += "*";
	}
	return text;
}

std::string Program::resolve_patterns(ImageLibrary& images)
{
	std::vector<std::string> missing;
	for (PatternDecl* pattern : m_patterns) pattern->resolve(images, missing);
	if (missing.empty()) return std::string {};

	std::string names;
	for (const std::string& name : missing) {
		if (!names.empty()) names += ", ";
		names += "'" + name + "'";
	}
	return (1 == missing.size() ? "the image library has no pattern called "
	                            : "the image library has no patterns called ")
	     + names;
}

bool Program::prepare(std::string&)
{
	return true;
}

ProgramResult Program::exec(ProgramContext& context)
{
	// A previous run may have been aborted; this one starts clean.
	m_stop.store(false);
	m_notes.clear();
	m_context = &context;
	try {
		if (!context.capture.running()) {
			return {ProgramExit::FAILURE,
			        "capture is not running; use 'start' first"};
		}
		// Every pattern up front, so a missing one is reported before the
		// ship is committed to anything.
		const std::string missing = resolve_patterns(context.images);
		if (!missing.empty()) return {ProgramExit::FAILURE, missing};

		std::string trouble;
		if (!prepare(trouble)) return {ProgramExit::FAILURE, trouble};

		return run();
	}
	catch (const ProgramError& e) {
		// The ordinary way a run ends short: an action the game did not
		// answer, or a stop that was asked for. The message was written
		// where it happened and needs nothing added to it.
		return ProgramResult {e.exit(), e.what()};
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

Program::Doing::Doing(Program& program, std::string note)
	: m_program {program}
{
	m_program.m_notes.push_back(std::move(note));
}

Program::Doing::~Doing()
{
	if (!m_program.m_notes.empty()) m_program.m_notes.pop_back();
}

eb::Millis Program::Budget::left() const
{
	const eb::Millis spent = eb::since(m_began);
	return spent >= m_total ? eb::Millis::zero() : m_total - spent;
}

std::string Program::note_text() const
{
	std::string text;
	for (const std::string& note : m_notes) text += note + ": ";
	return text;
}

void Program::fail(const std::string& why) const
{
	throw ProgramError {ProgramExit::FAILURE, note_text() + why};
}

void Program::stopped_while(const std::string& what) const
{
	throw ProgramError {
		ProgramExit::STOPPED, note_text() + "stopped while " + what
	};
}

ProgramResult Program::done(std::string description)
{
	return ProgramResult {ProgramExit::SUCCESS, std::move(description)};
}

void Program::rest_or_stop(eb::Millis duration, const std::string& what) const
{
	// In steps rather than in one sleep, so a stop asked for in the middle
	// of a long pause is noticed while it is still worth noticing.
	for (eb::Millis left = duration;
	     left > eb::Millis::zero();
	     left -= WAIT_STEP)
	{
		if (stopping()) stopped_while("waiting for " + what);
		std::this_thread::sleep_for(std::min(WAIT_STEP, left));
	}
	if (stopping()) stopped_while("waiting for " + what);
}

void Program::pause(const Pause& duration, const std::string& what) const
{
	rest_or_stop(duration.value(), what);
}

void Program::pause(const Interval& duration, const std::string& what) const
{
	rest_or_stop(duration.value(), what);
}

std::string Program::named(const eb::Images& images) const
{
	return context().images.names_text(images);
}

bool Program::visible(const eb::Images& images, Sighting& seen, Scope scope) const
{
	if (stopping()) stopped_while("looking for " + named(images));

	ImageDetector::Detection found;
	std::string              trouble;
	if (!context().detector.detect(images, found, trouble, scope))
		fail("cannot look for " + named(images) + ": " + trouble);
	if (found.hits.empty()) return false;

	seen.at    = found.hits.front().at;
	seen.image = found.hits.front().image;
	return true;
}

bool Program::watch(
	const eb::Images& images,
	eb::Millis        timeout,
	Sighting&         seen,
	Scope             scope) const
{
	const eb::TimePoint deadline = eb::Clock::now() + timeout;
	while (true) {
		if (visible(images, seen, scope)) return true;
		if (eb::Clock::now() >= deadline) return false;

		// One frame's worth, because nothing on screen can have changed
		// until capture has taken another: searching the same frame again
		// would hand back the same answer out of the detector's memory.
		rest_or_stop(
			context().capture.frame_life(), named(images) + " to appear"
		);
	}
}

Program::Sighting Program::appear(
	const eb::Images& images, eb::Millis timeout, Scope scope) const
{
	Sighting seen;
	if (!watch(images, timeout, seen, scope))
		fail(named(images) + " did not appear");
	return seen;
}

void Program::vanish(
	size_t          image,
	const Budget&   budget,
	const Interval& recheck,
	Scope           scope) const
{
	const std::string what = named(eb::Images {image});
	while (true) {
		rest_or_stop(recheck.value(), what + " to go");

		if (budget.left() <= eb::Millis::zero())
			fail(what + " never went away");
		if (!visible(image, scope)) return;
	}
}

void Program::click(
	size_t            image,
	eb::Millis        src_to,
	eb::Millis        wait,
	const eb::Images& confirm,
	eb::Millis        conf_to,
	int               retries) const
{
	// Where it is now, which is not always where whoever asked for the click
	// last saw it. Failing to find it here is the same failure as failing to
	// find it anywhere else, so appear() reports it.
	const cv::Point corner = appear(image, src_to);

	const ImagePattern& pattern = context().images(image);
	const cv::Point     target =
		corner + cv::Point {pattern.width() / 2, pattern.height() / 2};

	const std::string what = named(eb::Images {image});
	// The first click, plus however many further ones were allowed. A
	// negative count asks for what eve_config.json says.
	const int attempts =
		1 + (retries < 0 ? common().ACTION_RETRIES : retries);
	for (int attempt = 0; attempt < attempts; ++attempt) {
		if (stopping()) stopped_while("clicking " + what);

		ClickResult made;
		std::string trouble;
		if (!click_at(
				context().capture.target(), target, wait,
				context().priority, made, trouble))
		{
			// A desktop busy with somebody else has already been waited out
			// in there, so what is left is the window being gone or the
			// input refused - which another attempt would meet in exactly
			// the same state. This is not what the retries are for.
			fail("cannot click " + what + ": " + trouble);
		}
		// Nothing to wait for: the click was made, and that was all that was
		// asked of it.
		if (confirm.empty()) return;

		Sighting seen;
		if (watch(confirm, conf_to, seen, Scope::BOX)) return;
	}
	fail(what + " did not take: " + named(confirm) + " did not follow");
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
	std::lock_guard<std::mutex> lock {m_control_mutex};
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
			// A search pulls a frame, and capture is WinRT, so this thread
			// needs an apartment of its own.
			winrt::init_apartment(winrt::apartment_type::multi_threaded);
			// context holds three references and is copied into the thread,
			// so it stays valid as long as the objects behind it do.
			ProgramContext local = context;
			const ProgramResult result = job.exec(local);
			set_current(std::string {});
			m_running.store(false);
			if (m_on_finish) m_on_finish(job.name(), result);
			winrt::uninit_apartment();
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
	std::lock_guard<std::mutex> lock {m_control_mutex};
	// Every program, not just the running one: the runner does not track
	// which is in flight, and exec() clears the flag as a run begins, so a
	// flag left raised here cannot affect a later start().
	for (const std::unique_ptr<Program>& program : m_programs)
		program->request_stop();
}

void ProgramRunner::wait()
{
	std::lock_guard<std::mutex> lock {m_control_mutex};
	if (m_thread.joinable()) m_thread.join();
}
