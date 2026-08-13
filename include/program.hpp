/*
	EVE bots for Windows.
	Author: Igor Polev.

	Program - one automated job, and the runner that hosts it.

	A program runs on its own thread and reports back an outcome and a line
	of text. Stopping is cooperative: abort() raises a flag the program
	notices at its next action, since a thread torn down mid-click would
	leave the mouse button held.

	Everything here exists so that a program body can be a sequence of
	actions and nothing else. The actions throw ProgramError instead of
	returning anything to test, exec() checks what has to be true before the
	first one, and declared parameters read themselves out of
	prog_params.json.
*/

#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "common_defs.hpp"
#include "image_detector.hpp"
#include "image_library.hpp"
#include "mouse_input.hpp"
#include "program_defaults.hpp"
#include "program_params.hpp"
#include "screen_capture.hpp"

// Everything a program is allowed to touch. The three big ones are held by
// reference, so the objects behind them must outlive any run.
struct ProgramContext {
	ImageLibrary&        images;
	ImageDetector&       detector;
	const ScreenCapture& capture;
	ProgramDefaults      defaults;
	InputPriority        priority;
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

// How a program gives up. Thrown by the actions and turned back into a
// ProgramResult by exec(), so it never leaves the program thread.
class ProgramError : public std::runtime_error {
public:
	ProgramError(ProgramExit exit, const std::string& why)
		: std::runtime_error {why}, m_exit {exit} {}

	ProgramExit exit() const noexcept { return m_exit; }

private:
	ProgramExit m_exit;
};

// Declares one tunable of a program: its kind, its name, and what it is
// without prog_params.json. The name is written out as the key as well, so
// the two cannot come apart.
#define PROG_PARAM(kind, NAME, fallback) kind NAME {*this, #NAME, fallback}

class Program {
public:
	explicit Program(std::string name) : m_name {std::move(name)} {}
	virtual ~Program() = default;
	Program(const Program&)            = delete;
	Program& operator=(const Program&) = delete;

	const std::string& name() const noexcept { return m_name; }

	// One line for the 'programs' listing.
	virtual std::string purpose() const = 0;
	// Every declared parameter with its value, the ones prog_params.json set
	// marked with a star.
	std::string settings_text() const;
	// Takes whatever this program understands out of prog_params.json. A
	// value that is there but unusable is an error: a parameter that
	// silently fails to apply is worse than a refusal to start. One program
	// built out of another overrides this to pass the file on as well.
	virtual bool configure(const ProgramParams& params, std::string& error);

	// Does the job. Called on the program thread by ProgramRunner. Checks
	// the capture is running and resolves every declared pattern before
	// run() starts.
	ProgramResult exec(ProgramContext& context);

	// Asks a running exec() to give up at its next action. Virtual because a
	// program built out of others has to pass this on to them.
	virtual void request_stop() noexcept { m_stop.store(true); }
	bool stopping() const noexcept { return m_stop.load(); }

	// Whether the pop-up menu offers this one.
	bool in_menu() const noexcept { return SHOW_IN_MENU.value(); }

protected:
	/*
		A pattern the program works with, declared as a member and named the
		way eve_images.json names it:

		    Pattern  UNDOCK   {*this, "undock"};
		    Patterns STATIONS {*this, {"station_route", "station_home_route"}};

		Each registers itself as it is built and exec() turns every name into
		a library index, so a misspelt image is one message before the first
		action rather than a surprise halfway through a route.
	*/
	class PatternDecl {
	public:
		virtual ~PatternDecl() = default;
		PatternDecl(const PatternDecl&)            = delete;
		PatternDecl& operator=(const PatternDecl&) = delete;

	protected:
		explicit PatternDecl(Program& owner);

	private:
		friend class Program;
		// Fills in the index, or adds what the library does not have to
		// missing.
		virtual void resolve(
			ImageLibrary& images, std::vector<std::string>& missing) = 0;
	};

	// One image, standing in for its library index.
	class Pattern : public PatternDecl {
	public:
		Pattern(Program& owner, const char* name)
			: PatternDecl {owner}, m_name {name} {}

		const char* name()  const noexcept { return m_name; }
		size_t      index() const noexcept { return m_index; }
		operator size_t()   const noexcept { return m_index; }

	private:
		void resolve(
			ImageLibrary& images, std::vector<std::string>& missing) override;

		const char* m_name;
		size_t      m_index {0};
	};

	// Several images that mean the same thing here, searched for as one.
	class Patterns : public PatternDecl {
	public:
		Patterns(Program& owner, std::initializer_list<const char*> names)
			: PatternDecl {owner}, m_names {names} {}

		const std::vector<size_t>& indexes() const noexcept { return m_indexes; }
		operator const std::vector<size_t>&() const noexcept { return m_indexes; }

	private:
		void resolve(
			ImageLibrary& images, std::vector<std::string>& missing) override;

		std::vector<const char*> m_names;
		std::vector<size_t>      m_indexes;
	};

	/*
		---- what the person running the bot can change ----------------------

		A tunable, declared as a member and used by its own name:

		    PROG_PARAM(Timeout,  DESTINATION_TIMEOUT,  10000);
		    PROG_PARAM(Interval, WARP_RECHECK_INTERVAL, 2000);

		The member name is also the key in prog_params.json and also what a
		failure calls it, so the three cannot drift apart.

		Which kind it is says what it costs to run:

		    Pause     time really spent, every time, whether or not anything
		              was going to happen
		    Interval  the gap between two looks at the same thing
		    Timeout   an upper bound, only ever paid when something is wrong
		    Count     how many times to try something

		The kind and the name have to agree - a Pause has to be called
		something_PAUSE - and configure() refuses a program whose names say
		something different from what its code does.
	*/
	class ParamDecl {
	public:
		virtual ~ParamDecl() = default;
		ParamDecl(const ParamDecl&)            = delete;
		ParamDecl& operator=(const ParamDecl&) = delete;

		const char* key()   const noexcept { return m_key; }
		// Whether prog_params.json had anything to say about it.
		bool        tuned() const noexcept { return m_tuned; }

	protected:
		ParamDecl(Program& owner, const char* key);

		bool m_tuned {false};

	private:
		friend class Program;
		// Takes this program's value out of the file, or keeps the built in
		// one. False and error when what the file says cannot be used.
		virtual bool read(
			const ProgramParams& params,
			const std::string&   program,
			std::string&         error
		) = 0;
		// The value as the listing shows it: "10000 ms", "3".
		virtual std::string text() const = 0;
		// What the end of its name has to be.
		virtual const char* suffix() const = 0;

		const char* m_key;
	};

	// The three kinds that are a length of time.
	class Millis : public ParamDecl {
	public:
		eb::Millis value() const noexcept { return m_value; }
		operator eb::Millis() const noexcept { return m_value; }

	protected:
		Millis(Program& owner, const char* key, long long fallback)
			: ParamDecl {owner, key}, m_value {fallback} {}

	private:
		bool read(
			const ProgramParams& params,
			const std::string&   program,
			std::string&         error
		) override;
		std::string text() const override;
		// The smallest value that still means something.
		virtual long long least() const = 0;

		eb::Millis m_value;
	};

	class Timeout : public Millis {
	public:
		Timeout(Program& owner, const char* key, long long fallback)
			: Millis {owner, key, fallback} {}

	private:
		const char* suffix() const override { return "_TIMEOUT"; }
		// Zero would give a search no chance to run even once.
		long long   least()  const override { return 1; }
	};

	class Pause : public Millis {
	public:
		Pause(Program& owner, const char* key, long long fallback)
			: Millis {owner, key, fallback} {}

	private:
		const char* suffix() const override { return "_PAUSE"; }
		// Nothing wrong with asking for no pause at all.
		long long   least()  const override { return 0; }
	};

	class Interval : public Millis {
	public:
		Interval(Program& owner, const char* key, long long fallback)
			: Millis {owner, key, fallback} {}

	private:
		const char* suffix() const override { return "_INTERVAL"; }
		// Zero would be a program looking as fast as it can.
		long long   least()  const override { return 1; }
	};

	// A yes or no. Its name says what it turns on rather than what it costs
	// to run, so there is no ending for it to have.
	class Flag : public ParamDecl {
	public:
		Flag(Program& owner, const char* key, bool fallback)
			: ParamDecl {owner, key}, m_value {fallback} {}

		bool value() const noexcept { return m_value; }
		operator bool() const noexcept { return m_value; }

	private:
		bool read(
			const ProgramParams& params,
			const std::string&   program,
			std::string&         error
		) override;
		std::string text()   const override { return m_value ? "yes" : "no"; }
		const char* suffix() const override { return ""; }

		bool m_value;
	};

	// A number of times, not a length of time.
	class Count : public ParamDecl {
	public:
		Count(Program& owner, const char* key, int fallback)
			: ParamDecl {owner, key}, m_value {fallback} {}

		int value() const noexcept { return m_value; }
		operator int() const noexcept { return m_value; }

	private:
		bool read(
			const ProgramParams& params,
			const std::string&   program,
			std::string&         error
		) override;
		std::string text()   const override;
		const char* suffix() const override { return "_RETRIES"; }

		int m_value;
	};

	// The numbers that are the same everywhere, read as
	// common().CONFIRM_TIMEOUT so a value from eve_config.json never looks
	// like one of this program's own.
	const ProgramDefaults& common() const noexcept
		{ return context().defaults; }

	// Anything a program has to settle after the patterns are resolved and
	// before the first action. Most have nothing to do here.
	virtual bool prepare(std::string& error);

	// Does the job. The context is already in place.
	virtual ProgramResult run() = 0;

	// What the program is allowed to touch, for the run in flight. Only
	// valid inside exec().
	ProgramContext& context() const noexcept { return *m_context; }

	// A stretch of time shared by more than one wait. Each of them takes
	// what is left rather than the whole of it, so two waits that belong to
	// the same thing cannot together outlast it.
	class Budget {
	public:
		explicit Budget(eb::Millis total)
			: m_total {total}, m_began {eb::Clock::now()} {}

		eb::Millis total() const noexcept { return m_total; }
		eb::Millis left()  const;

	private:
		eb::Millis    m_total;
		eb::TimePoint m_began;
	};

	/*
		Names the stretch of work in flight, so that every failure thrown
		underneath it says where it happened:

		    Doing note {*this, hop_text(hop)};   // "hop 3: ..."

		It lasts as long as the object does, and nests.
	*/
	class Doing {
	public:
		Doing(Program& program, std::string note);
		~Doing();
		Doing(const Doing&)            = delete;
		Doing& operator=(const Doing&) = delete;

	private:
		Program& m_program;
	};

	// Where something was seen, and which pattern it turned out to be.
	struct Sighting {
		cv::Point at;
		size_t    image {0};
	};

	// ---- giving up ------------------------------------------------------
	// Neither of these returns: both leave run() through exec().
	[[noreturn]] void fail(const std::string& why) const;
	// what reads on from "stopped while": "waiting for the gate to load".
	[[noreturn]] void stopped_while(const std::string& what) const;
	// The one ordinary way out, for symmetry with the two above.
	static ProgramResult done(std::string description);

	/*
		---- actions --------------------------------------------------------

		What a program is made of. Every one of them insists: if the game
		does not do what it was asked, the program is over.

		Each takes a phrase naming what it is working on, written the way a
		person would say it - "the undock button", "the warp message" - and
		that is what a failure is reported in terms of. It is spelt out at
		the call site rather than derived from a pattern name because what
		the program was trying to do is not the same as which image it
		happened to be looking at.

		Several images mean "any of these will do": one search that looks at
		the quick boxes of all of them before hunting any across the whole
		frame, which is not what looking for each in turn would do.
	*/

	// Time spent on purpose, watching nothing. Only a Pause or an Interval
	// will do: time a program spends doing nothing is the one thing it
	// should never be able to do by accident. what is only ever heard when a
	// stop arrives in the middle of it.
	void pause(const Pause& duration, const std::string& what) const;
	void pause(const Interval& duration, const std::string& what) const;

	// Waits for something to turn up, and gives up if it does not.
	Sighting appear(
		const std::string&         what,
		const std::vector<size_t>& images,
		eb::Millis                 budget
	) const;

	// Waits for something to go away again, looking once per recheck: this
	// is for waits measured in minutes, where searching on repeat would keep
	// a core busy for the whole of it.
	void vanish(
		const std::string& what,
		size_t             image,
		const Budget&      budget,
		const Interval&    recheck
	) const;

	// Asks rather than insists, for when not finding something is an answer
	// rather than a failure. Still gives up if the search itself cannot run,
	// or if the program was stopped. seen may be null.
	bool sighted(
		const std::string&         what,
		const std::vector<size_t>& images,
		eb::Millis                 budget,
		Sighting*                  seen = nullptr
	) const;

	// Finds a pattern and clicks the middle of it, then insists the game
	// show the click was taken: any one of confirm appearing is that proof.
	// An empty confirm leaves a plain click, for the few places where there
	// is genuinely nothing to look for.
	//
	// Where it is, is looked up here rather than passed in: a caller holding
	// a position found a moment ago is holding where the thing was, and a
	// panel that has scrolled since would have the click press whatever
	// moved into that spot.
	void click(
		const std::string&         what,
		size_t                     image,
		const std::vector<size_t>& confirm
	) const;

private:
	friend class Doing;

	// Whatever Doing scopes are open, ready to be put in front of a message:
	// "hop 3: ".
	std::string note_text() const;

	// Turns every declared pattern into a library index. Returns the names
	// the library did not have, empty when all were found.
	std::string resolve_patterns(ImageLibrary& images);

	// Every declared parameter by name, for saying what the file could have
	// meant.
	std::string param_list() const;

	// Sleeping without a Pause to point at, for the actions that wait on
	// their own account.
	void rest_or_stop(eb::Millis duration, const std::string& what) const;

	std::string               m_name;
	std::atomic<bool>         m_stop {false};
	std::vector<PatternDecl*> m_patterns;
	std::vector<ParamDecl*>   m_params;
	std::vector<std::string>  m_notes;      // the open Doing scopes
	ProgramContext*           m_context {nullptr};

	// Every program has this one: the menu has room for a handful of entries
	// and most programs are steps others are built out of, so a program is
	// kept out of it until prog_params.json says otherwise. Declared after
	// the list it registers itself with, which has to exist by then.
	PROG_PARAM(Flag, SHOW_IN_MENU, false);
};

// Holds the known programs and runs one at a time: they all drive the same
// game window, and two clicking at once would fight each other.
class ProgramRunner {
public:
	static constexpr size_t NOT_FOUND = static_cast<size_t>(-1);

	// Called on the program thread the moment a run ends, so a finished job
	// is reported without the console having to poll for it.
	using FinishHandler =
		std::function<void(const std::string&, const ProgramResult&)>;

	ProgramRunner() = default;
	~ProgramRunner();
	ProgramRunner(const ProgramRunner&)            = delete;
	ProgramRunner& operator=(const ProgramRunner&) = delete;

	void add(std::unique_ptr<Program> program);
	void on_finish(FinishHandler handler) { m_on_finish = std::move(handler); }
	// Hands every registered program its parameters, stopping at the first
	// one that rejects what it was given.
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

	// Starts a program on its own thread. False when one is already running.
	// start(), abort() and wait() are asked for from both the console and
	// the pop-up menu, so they share a lock of their own; a run in flight
	// never takes it, so joining underneath it cannot deadlock.
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
