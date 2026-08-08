/*
	EVE bots for Windows.
	Author: Igor Polev.

	Program and ProgramRunner implementation.
*/

#include <algorithm>

#include "program.hpp"

namespace {

// What looking for one pattern ended in.
enum class Look {
	FOUND,
	MISSING,   // not on screen within the budget - an answer, not a fault
	STOPPED,
	TROUBLE    // the search itself could not run
};

// Whether whoever asked for something wants it given up on.
using StopCheck = std::function<bool()>;

// What a click has to produce before it counts, and how hard to insist.
//
// A click that lands is not a click that worked. The interface may be
// busy, the button may still be drawing itself, the game may drop a
// press that falls between two rendered frames - and none of that looks
// any different from success at the moment the button goes down. The
// only way to know is to look for whatever the click was supposed to
// bring about, so that is what this names.
struct ClickConfirm {
	// Any one of these appearing is proof enough: they are alternatives,
	// the way a search for several patterns always is. Empty asks for no
	// confirmation, which leaves a plain click.
	std::vector<size_t>       images;
	// How long to keep looking after each click. Each attempt gets this
	// in full, so the whole thing can take (retries + 1) times as long.
	std::chrono::milliseconds timeout {0};
	// Further clicks to make when one goes unconfirmed. 0 means click
	// once and report whether it showed.
	int                       retries {0};

	bool wanted() const noexcept { return !images.empty(); }
};

enum class Click {
	CONFIRMED,     // clicked, and the game showed it had been taken
	DONE,          // clicked, and no confirmation was asked for
	UNCONFIRMED,   // every allowed click was made and none of them showed
	STOPPED,
	TROUBLE        // the click, or the search after it, could not be done
};

// What a click came to, whether or not it was confirmed.
struct ClickReport {
	ClickResult click;            // the last click that was made
	int         clicks {0};       // how many were made in all
	cv::Point   at;               // where the confirmation was seen
	size_t      image {0};        // which pattern it turned out to be
};

// A program sleeping between attempts checks for a stop this often.
constexpr std::chrono::milliseconds WAIT_STEP {50};

// Pause between two attempts at the same pattern. A full frame search
// already costs seconds, so this only matters once a quick box search is
// possible - and then it keeps a program from spinning on the CPU.
constexpr std::chrono::milliseconds RETRY_PAUSE {250};

// Nothing anybody would write in prog_params.json, so asking for a value
// with this as the fallback answers "was there a number there at all?"
// as well as "what was it?".
constexpr double NOT_A_NUMBER = -1e18;

bool asked_to_stop(const StopCheck& stopping)
{
	return stopping && stopping();
}

// The three below are what Program's own waiting and looking are made
// of, written without the program so that confirmed_click() - which
// anybody may call, program or console - can be made of them too.

bool rest(std::chrono::milliseconds duration, const StopCheck& stopping)
{
	for (std::chrono::milliseconds left = duration;
	     left > std::chrono::milliseconds::zero();
	     left -= WAIT_STEP)
	{
		if (asked_to_stop(stopping)) return false;
		std::this_thread::sleep_for(std::min(WAIT_STEP, left));
	}
	return !asked_to_stop(stopping);
}

Look one_look(
	ProgramContext&            context,
	const std::vector<size_t>& images,
	std::chrono::milliseconds  budget,
	const StopCheck&           stopping,
	cv::Point&                 corner,
	size_t&                    found,
	std::string&               trouble)
{
	if (asked_to_stop(stopping)) return Look::STOPPED;
	if (budget <= std::chrono::milliseconds::zero()) return Look::MISSING;

	// The search gets what is left of the budget and no more, so one slow
	// full frame pass cannot overrun the whole timeout.
	const ProgramClock::time_point began = ProgramClock::now();
	Detection seen;
	if (!context.detector.detect(images, 1, false, seen, trouble, budget)) {
		// Running out of the time it was given is not a fault, it is the
		// answer: the pattern was not there within the budget.
		return since(began) >= budget ? Look::MISSING : Look::TROUBLE;
	}
	if (seen.hits.empty()) return Look::MISSING;

	corner = seen.hits.front().at;
	found  = seen.hits.front().image;
	return Look::FOUND;
}

Look keep_looking(
	ProgramContext&            context,
	const std::vector<size_t>& images,
	std::chrono::milliseconds  budget,
	const StopCheck&           stopping,
	cv::Point&                 corner,
	size_t&                    found,
	std::string&               trouble)
{
	const ProgramClock::time_point deadline = ProgramClock::now() + budget;
	while (true) {
		const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
			deadline - ProgramClock::now()
		);
		if (left <= std::chrono::milliseconds::zero()) return Look::MISSING;

		const Look seen =
			one_look(context, images, left, stopping, corner, found, trouble);
		// Only "not on screen this time" is worth another attempt.
		if (Look::MISSING != seen) return seen;

		if (!rest(RETRY_PAUSE, stopping)) return Look::STOPPED;
	}
}

// Clicks a point of the captured window and, when a confirmation was
// asked for, waits for proof that the game took it - clicking again up
// to retries times when none arrives.
//
// error is filled for TROUBLE and for UNCONFIRMED, since both are things
// the caller will want to say out loud; the other outcomes leave it
// alone. stopping may be empty, and is then never asked.
Click confirmed_click(
	ProgramContext&     context,
	const cv::Point&    target,
	int                 wait_ms,
	const ClickConfirm& confirm,
	const StopCheck&    stopping,
	ClickReport&        report,
	std::string&        error)
{
	report = ClickReport {};

	// The first click, plus however many further ones were allowed. A
	// negative retry count is read as none rather than refused: it can
	// only have come from arithmetic, and one click is what it meant.
	const int attempts = 1 + std::max(0, confirm.retries);
	for (int attempt = 0; attempt < attempts; ++attempt) {
		if (asked_to_stop(stopping)) return Click::STOPPED;

		if (!click_at(
				context.capture.target(), target, wait_ms,
				context.priority, stopping, report.click, error))
		{
			// Clicking failed outright. A desktop busy with somebody else
			// has already been waited out in there, so what is left is the
			// window being gone or the input refused - which another
			// attempt would meet in exactly the same state. This is not
			// what the retries are for.
			return Click::TROUBLE;
		}
		++report.clicks;

		// Nothing to wait for: the click was made, and that was all that
		// was asked of it.
		if (!confirm.wanted()) return Click::DONE;

		const Look seen = keep_looking(
			context, confirm.images, confirm.timeout, stopping,
			report.at, report.image, error
		);
		switch (seen) {
		case Look::FOUND:   return Click::CONFIRMED;
		case Look::STOPPED: return Click::STOPPED;
		case Look::TROUBLE: return Click::TROUBLE;
		default:            break;   // not there yet, so click again
		}
	}

	error = context.detector.names_text(confirm.images)
	      + " did not follow";
	return Click::UNCONFIRMED;
}

} // namespace

std::chrono::milliseconds since(ProgramClock::time_point start)
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(
		ProgramClock::now() - start
	);
}

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
	m_value = std::chrono::milliseconds {static_cast<long long>(value)};
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
		// The name has to say what the number is, because that is all a
		// call site shows of it: GATE_JUMP_PAUSE is time the program will
		// really spend, DOCKING_TIMEOUT is time it hopes not to. A
		// parameter whose name disagrees with its kind would quietly make
		// that unreadable, so it is refused here rather than left to be
		// noticed later.
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

std::chrono::milliseconds Program::Budget::left() const
{
	const std::chrono::milliseconds spent = since(m_began);
	return spent >= m_total ? std::chrono::milliseconds::zero() : m_total - spent;
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

void Program::rest_or_stop(
	std::chrono::milliseconds duration, const std::string& what) const
{
	if (!rest(duration, [this] { return stopping(); }))
		stopped_while("waiting for " + what);
}

void Program::pause(const Pause& duration, const std::string& what) const
{
	rest_or_stop(duration.value(), what);
}

void Program::pause(const Interval& duration, const std::string& what) const
{
	rest_or_stop(duration.value(), what);
}

Program::Sighting Program::watch_for(
	const std::string&         what,
	const std::vector<size_t>& images,
	std::chrono::milliseconds  budget) const
{
	Sighting    seen;
	std::string trouble;
	const Look  looked = keep_looking(
		context(), images, budget, [this] { return stopping(); },
		seen.at, seen.image, trouble
	);
	if (Look::FOUND == looked)   return seen;
	if (Look::STOPPED == looked) stopped_while("waiting for " + what);
	if (Look::TROUBLE == looked) fail("cannot look for " + what + ": " + trouble);
	fail(what + " did not appear");
}

cv::Point Program::appear(
	const std::string&        what,
	size_t                    image,
	std::chrono::milliseconds budget) const
{
	return watch_for(what, std::vector<size_t> {image}, budget).at;
}

cv::Point Program::appear(
	const std::string& what, size_t image, const Budget& budget) const
{
	return watch_for(what, std::vector<size_t> {image}, budget.left()).at;
}

Program::Sighting Program::appear(
	const std::string&         what,
	const std::vector<size_t>& images,
	std::chrono::milliseconds  budget) const
{
	return watch_for(what, images, budget);
}

void Program::vanish(
	const std::string& what,
	size_t             image,
	const Budget&      budget,
	const Interval&    recheck) const
{
	while (true) {
		rest_or_stop(recheck.value(), what + " to go");

		const std::chrono::milliseconds left = budget.left();
		if (left <= std::chrono::milliseconds::zero())
			fail(what + " never went away");
		cv::Point at;
		if (!sighted(what, image, left, at)) return;
	}
}

bool Program::sighted(
	const std::string&        what,
	size_t                    image,
	std::chrono::milliseconds budget,
	cv::Point&                at) const
{
	Sighting seen;
	if (!sighted(what, std::vector<size_t> {image}, budget, seen)) return false;
	at = seen.at;
	return true;
}

bool Program::sighted(
	const std::string&         what,
	const std::vector<size_t>& images,
	std::chrono::milliseconds  budget,
	Sighting&                  seen) const
{
	std::string trouble;
	const Look  looked = one_look(
		context(), images, budget, [this] { return stopping(); },
		seen.at, seen.image, trouble
	);
	if (Look::FOUND == looked)   return true;
	if (Look::MISSING == looked) return false;
	if (Look::STOPPED == looked) stopped_while("looking for " + what);
	fail("cannot look for " + what + ": " + trouble);
}

cv::Point Program::click(
	const std::string&         what,
	size_t                     image,
	const cv::Point&           corner,
	const std::vector<size_t>& confirm) const
{
	// The usual confirmation: any one of these patterns, waited for and
	// insisted on exactly as eve_config.json says. Spelling those two out
	// at every call site would only invite one of them to drift.
	ClickConfirm wanted;
	wanted.images  = confirm;
	wanted.timeout = common().CONFIRM_TIMEOUT;
	wanted.retries = common().ACTION_RETRIES;

	const ImagePattern& pattern = context().images(image);
	const cv::Point target =
		corner + cv::Point {pattern.width() / 2, pattern.height() / 2};

	ClickReport report;
	std::string trouble;
	const Click clicked = confirmed_click(
		context(), target, UI_WAIT_DEFAULT, wanted,
		[this] { return stopping(); }, report, trouble
	);
	if (Click::CONFIRMED == clicked) return report.at;
	if (Click::DONE == clicked)      return report.click.frame;
	if (Click::STOPPED == clicked)     stopped_while("clicking " + what);
	if (Click::UNCONFIRMED == clicked) fail(what + " did not take: " + trouble);
	fail("cannot click " + what + ": " + trouble);
}

cv::Point Program::click(
	const std::string& what, size_t image, const cv::Point& corner) const
{
	return click(what, image, corner, std::vector<size_t> {});
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
