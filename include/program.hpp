/*
	EVE bots for Windows.
	Author: Igor Polev.

	Program - one automated job, and the runner that hosts it.

	A program is whatever a person would otherwise do by hand: undock,
	mine an asteroid, fly a route. It runs on its own thread so the console
	stays usable while it works, and it reports back an outcome and a line
	of text explaining what happened.

	Stopping is cooperative. A program cannot be killed outright - a thread
	torn down mid-click would leave the mouse button held and the detector
	waiting - so ProgramRunner::abort() only raises a flag, and the program
	notices it at its next check point. Since the longest thing a program
	waits for is one image search, that is the granularity of a stop.
*/

#pragma once
#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "image_detector.hpp"
#include "image_library.hpp"
#include "mouse_input.hpp"
#include "program_params.hpp"
#include "screen_capture.hpp"

using ProgramClock = std::chrono::steady_clock;

// How long ago something happened.
std::chrono::milliseconds since(ProgramClock::time_point start);
// "4.2 s"
std::string seconds_text(std::chrono::milliseconds spent);
// "789,1245"
std::string point_text(const cv::Point& at);

// Everything a program is allowed to touch. Held by reference, so the
// objects behind it must outlive any run.
struct ProgramContext {
	ImageLibrary&        images;
	ImageDetector&       detector;
	const ScreenCapture& capture;
};

enum class ProgramExit {
	SUCCESS,   // the job was done and confirmed
	FAILURE,   // it could not be done, and the description says why
	STOPPED    // abort() was asked for before it finished
};

std::string program_exit_text(ProgramExit exit);

struct ProgramResult {
	ProgramExit exit {ProgramExit::FAILURE};
	std::string description;
};

class Program {
public:
	explicit Program(std::string name) : m_name {std::move(name)} {}
	virtual ~Program() = default;
	Program(const Program&)            = delete;
	Program& operator=(const Program&) = delete;

	const std::string& name() const noexcept { return m_name; }

	// One line for the 'programs' listing.
	virtual std::string purpose() const = 0;
	// What this run will use, after configure() - also for the listing.
	virtual std::string settings_text() const = 0;
	// Takes whatever this program understands out of prog_params.json.
	// Anything the file does not mention keeps its built in default; a
	// value that is there but unusable is an error, because a parameter
	// that silently fails to apply is worse than a refusal to start.
	virtual bool configure(const ProgramParams& params, std::string& error) = 0;

	// Does the job. Called on the program thread by ProgramRunner.
	ProgramResult exec(ProgramContext& context);

	// Asks a running exec() to give up at its next check point. Virtual
	// because a program built out of other programs has to pass this on
	// to whichever one is doing the work for it.
	virtual void request_stop() noexcept { m_stop.store(true); }
	bool stopping() const noexcept { return m_stop.load(); }

protected:
	// What looking for one pattern ended in.
	enum class Look {
		FOUND,
		MISSING,   // not on screen within the budget - an answer, not a fault
		STOPPED,
		TROUBLE    // the search itself could not run
	};

	virtual ProgramResult run(ProgramContext& context) = 0;

	// Sleeps in short steps so a stop request is noticed quickly.
	// False means the program was asked to stop and should return.
	bool wait(std::chrono::milliseconds duration) const;

	// One search and no more. The list form asks for any one of several
	// patterns in a single search - "a gate or a station", say - and says
	// through found which one it turned out to be. That is not the same as
	// looking for each in turn: one search shares the budget honestly
	// between them, and the quick boxes of all of them are looked at before
	// any of them is hunted for across the whole frame.
	Look look_once(
		ProgramContext&            context,
		const std::vector<size_t>& images,
		std::chrono::milliseconds  budget,
		cv::Point&                 corner,
		size_t&                    found,
		std::string&               trouble
	) const;
	Look look_once(
		ProgramContext&           context,
		size_t                    image,
		std::chrono::milliseconds budget,
		cv::Point&                corner,
		std::string&              trouble
	) const;

	// Repeats look_once until one of the patterns turns up or the budget
	// runs out.
	Look look_for(
		ProgramContext&            context,
		const std::vector<size_t>& images,
		std::chrono::milliseconds  budget,
		cv::Point&                 corner,
		size_t&                    found,
		std::string&               trouble
	) const;
	Look look_for(
		ProgramContext&           context,
		size_t                    image,
		std::chrono::milliseconds budget,
		cv::Point&                corner,
		std::string&              trouble
	) const;

	// Clicks the middle of a pattern sitting at corner. The corner is
	// where a match starts; the middle is what a person would aim at.
	bool click_middle(
		ProgramContext&  context,
		size_t           image,
		const cv::Point& corner,
		ClickResult&     click,
		std::string&     trouble
	) const;

private:
	std::string       m_name;
	std::atomic<bool> m_stop {false};
};

/*
	Holds the known programs and runs one at a time. One at a time because
	they all drive the same game window: two programs clicking at once
	would fight each other.
*/
class ProgramRunner {
public:
	static constexpr size_t NOT_FOUND = static_cast<size_t>(-1);

	// Called on the program thread the moment a run ends, so a finished
	// job is reported without the console having to poll for it.
	using FinishHandler =
		std::function<void(const std::string&, const ProgramResult&)>;

	ProgramRunner() = default;
	~ProgramRunner();
	ProgramRunner(const ProgramRunner&)            = delete;
	ProgramRunner& operator=(const ProgramRunner&) = delete;

	void add(std::unique_ptr<Program> program);
	void on_finish(FinishHandler handler) { m_on_finish = std::move(handler); }
	// Hands every registered program its parameters. Setting up, so it
	// belongs here rather than to whoever only reads the list afterwards.
	// Stops at the first program that rejects what it was given.
	bool configure(const ProgramParams& params, std::string& error);

	bool   empty() const noexcept { return m_programs.empty(); }
	size_t size()  const noexcept { return m_programs.size(); }

	// Position of a named program, NOT_FOUND when there is none.
	size_t index(const std::string& name) const;
	// Unchecked, like ImageLibrary::operator().
	const Program& operator()(size_t program) const
		{ return *m_programs[program]; }
	std::string name_list() const;

	bool running() const noexcept { return m_running.load(); }
	// Name of the program in flight, empty when none is.
	std::string current() const;

	// Starts a program on its own thread. False when one is already
	// running. start(), abort() and wait() are asked for from both the
	// console and the pop-up menu, which are different threads, so they
	// share a lock of their own. A run in flight never takes it - it
	// touches only m_running, m_current and the finish handler - so
	// joining underneath it cannot deadlock.
	bool start(
		size_t program, const ProgramContext& context, std::string& error
	);
	// Raises the stop flag on whatever is running. Does not wait.
	void abort();
	// Waits for the current run to end, if there is one.
	void wait();

private:
	void set_current(const std::string& name);

	std::vector<std::unique_ptr<Program>> m_programs;
	std::map<std::string, size_t>         m_index;

	FinishHandler      m_on_finish;
	std::atomic<bool>  m_running {false};
	std::mutex         m_control_mutex;  // serialises start, abort and wait
	std::thread        m_thread;         // only under m_control_mutex
	mutable std::mutex m_current_mutex;  // guards m_current, nothing else
	std::string        m_current;
};
