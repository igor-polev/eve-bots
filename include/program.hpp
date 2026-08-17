/*
	EVE bots for Windows.
	Author: Igor Polev.

	Program - one automated job, and the runner that hosts it.

	A program runs on its own thread and reports back an outcome and a line
	of text. Stopping needs the program's help: abort() only raises a flag,
	and the program sees it at its next action. Killing the thread in the
	middle of a click would leave the mouse button pressed.

	Everything here is here so that the body of a program can be a list of
	actions and nothing else. The actions throw ProgramError instead of
	returning something to test, exec() checks what must be true before the
	first action, and declared parameters read their own values out of
	prg_params.json.
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

// Everything a program may touch. The first three are references, so the
// objects behind them must live longer than any run.
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

// How a program gives up. The actions throw it and exec() turns it back
// into a ProgramResult, so it never leaves the program thread.
class ProgramError : public std::runtime_error {
public:
	ProgramError(ProgramExit exit, const std::string& why)
		: std::runtime_error {why}, m_exit {exit} {}

	ProgramExit exit() const noexcept { return m_exit; }

private:
	ProgramExit m_exit;
};

// Declares one setting of a program: its kind, its name, and its value
// without prg_params.json. The macro also writes the name out as the file
// key, so the two cannot drift apart.
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
	// Every declared parameter with its value. The ones prg_params.json set
	// are marked with a star.
	std::string settings_text() const;
	// Reads what this program understands out of prg_params.json. A value
	// that is there but cannot be used is an error, because a parameter that
	// quietly fails to apply is worse than a refusal to start. A program
	// built out of another one overrides this to pass the file on as well.
	virtual bool configure(const ProgramParams& params, std::string& error);

	// Does the job. ProgramRunner calls it on the program thread. It checks
	// that capture is running and looks up every declared pattern before
	// run() starts.
	ProgramResult exec(ProgramContext& context);

	// Asks a running exec() to give up at its next action. Virtual because a
	// program built out of others must pass this on to them.
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

		Each one adds itself to the program as it is built, and exec() turns
		every name into a library index. So a misspelt image name is one
		message before the first action, not a surprise in the middle of a
		route.
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
		// Fills in the index, or adds the name to missing when the library
		// does not have it.
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

		const eb::Images& indexes() const noexcept { return m_indexes; }
		operator const eb::Images&() const noexcept { return m_indexes; }

	private:
		void resolve(
			ImageLibrary& images, std::vector<std::string>& missing) override;

		std::vector<const char*> m_names;
		eb::Images               m_indexes;
	};

	/*
		---- what the person running the bot can change ----------------------

		A tunable, declared as a member and used by its own name:

		    PROG_PARAM(Timeout,  DESTINATION_TIMEOUT,  10000);
		    PROG_PARAM(Interval, WARP_RECHECK_INTERVAL, 2000);

		The member name is also the key in prg_params.json, and also the name
		a failure prints, so the three cannot drift apart.

		The kind says what the number costs to run:

		    Pause     time really spent, every time, whether or not
		              anything was going to happen
		    Interval  the gap between two looks at the same thing
		    Timeout   an upper limit, only ever paid when something is wrong
		    Count     how many times to try something

		The kind and the name must agree: a Pause must be called
		something_PAUSE. configure() refuses a program whose names say
		something other than what its code does.
	*/
	class ParamDecl {
	public:
		virtual ~ParamDecl() = default;
		ParamDecl(const ParamDecl&)            = delete;
		ParamDecl& operator=(const ParamDecl&) = delete;

		const char* key()   const noexcept { return m_key; }
		// Whether prg_params.json had anything to say about it.
		bool        tuned() const noexcept { return m_tuned; }

	protected:
		ParamDecl(Program& owner, const char* key);

		bool m_tuned {false};

	private:
		friend class Program;
		// Reads this program's value from the file, or keeps the built in
		// one. Returns false and fills error when the file value is unusable.
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

	// The numbers that are the same for every program. They are read as
	// common().CONFIRM_TIMEOUT, so a value from eve_config.json never looks
	// like one of this program's own.
	const ProgramDefaults& common() const noexcept
		{ return context().defaults; }

	// Anything a program must set up after the patterns are looked up and
	// before the first action. Most programs have nothing to do here.
	virtual bool prepare(std::string& error);

	// Does the job. The context is already in place.
	virtual ProgramResult run() = 0;

	// What the program may touch during the current run. Valid only inside
	// exec().
	ProgramContext& context() const noexcept { return *m_context; }

	/*
		A length of time shared by more than one wait. Each wait takes what
		is left of it, not the whole of it, so two waits that belong to the
		same job cannot together last longer than the job.

		That is the only difference between the two kinds of time in this
		API, and the names keep them apart. A parameter called timeout is an
		eb::Millis and starts now. A parameter called budget is a Budget and
		started when it was made.

		A Budget converts to what is left of it, so it can be given to any
		action that wants a timeout. The conversion happens at the call, and
		that is what makes it safe: the action gets what was left at the
		moment it was called, not what was left when the budget was made.
		vanish() takes the Budget itself, because it looks again and again
		and must watch the budget run down while it waits.
	*/
	class Budget {
	public:
		explicit Budget(eb::Millis total)
			: m_total {total}, m_began {eb::Clock::now()} {}

		eb::Millis total() const noexcept { return m_total; }
		// Zero when it has run out, never below zero.
		eb::Millis left()  const;
		operator eb::Millis() const { return left(); }

	private:
		eb::Millis    m_total;
		eb::TimePoint m_began;
	};

	/*
		Names the piece of work in progress, so that every failure thrown
		below it says where it happened:

		    Doing note {*this, hop_text(hop)};   // "hop 3: ..."

		It lasts as long as the object lives, and such objects can nest.
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

	// Where something was seen, and which pattern it was.
	struct Sighting {
		cv::Point at;
		size_t    image {0};
	};

	// ---- giving up ------------------------------------------------------
	// Neither of these returns: both leave run() through exec().
	[[noreturn]] void fail(const std::string& why) const;
	// what follows "stopped while": "waiting for the gate to load".
	[[noreturn]] void stopped_while(const std::string& what) const;
	// The normal way out, put here next to the other two.
	static ProgramResult done(std::string description);

	/*
		---- actions --------------------------------------------------------

		What a program is made of. Every action insists: if the game does not
		do what it was asked, the program is over.

		A failure names the pattern it was looking for, as eve_images.json
		names it: "'undock' did not appear", "'station_route' or
		'station_home_route' did not appear". The call site writes none of
		that, so the message cannot drift away from the image it describes.
		What the program was doing at the time comes from the open Doing
		scopes - "hop 3: " - because that belongs to the step, not to the
		picture.

		Several images mean "any of these will do". That is one search which
		looks at the quick boxes of all of them before it searches the whole
		frame for any of them, and it is not the same as looking for each
		one in turn.

		pause() is the exception: it watches nothing, so it has no pattern
		to name and says in words what it is waiting for.
	*/

	// Time spent on purpose, watching nothing. Only a Pause or an Interval
	// is allowed here: time a program spends doing nothing is the one thing
	// it must never do by accident. what is only ever printed when a stop
	// arrives in the middle of the wait.
	void pause(const Pause& duration, const std::string& what) const;
	void pause(const Interval& duration, const std::string& what) const;

	/**
	 * Waits for one of the patterns to appear, and gives up if none does.
	 * Between two looks it waits one frame, because nothing can change
	 * before capture takes the next one.
	 *
	 * @param images  the patterns to look for; any one of them answers.
	 * @param timeout how long to keep looking. A Budget may be given
	 *                instead, and counts as what is left of it.
	 * @return where it was seen, and which pattern it was.
	 * @throws ProgramError FAILURE if none appeared, or if the search
	 *         itself could not run; STOPPED if a stop arrived first.
	 */
	Sighting appear(
		const eb::Images& images,
		eb::Millis timeout,
		eb::Scope scope = eb::Scope::BOX
	) const;
	/// One pattern, the common case.
	/// @return the top left corner of the match, in capture frame pixels.
	cv::Point appear(size_t image, eb::Millis timeout, eb::Scope scope = eb::Scope::BOX) const
		{ return appear(eb::Images {image}, timeout, scope).at; }

	// Waits for something to go away, looking once per recheck. This is for
	// waits of several minutes, where searching again and again would keep a
	// core busy for the whole time.
	void vanish(
		size_t          image,
		const Budget&   budget,
		const Interval& recheck,
		eb::Scope scope = eb::Scope::BOX
	) const;

	/**
	 * Looks once, for when not finding something is an answer and not a
	 * failure. Unlike appear(), a pattern that is not there yet is reported
	 * as not there instead of being waited for.
	 *
	 * There is no timeout: one search takes as long as it takes, and it
	 * cannot be given up half way.
	 *
	 * @param images the patterns to look for; any one of them answers.
	 * @param seen   where it was seen and which pattern it was, left alone
	 *               when none of them was there.
	 * @return whether one of them was on screen.
	 * @throws ProgramError FAILURE if the search itself could not run;
	 *         STOPPED if a stop arrived first.
	 */
	bool visible(
		const eb::Images& images,
		Sighting& seen,
		eb::Scope scope = eb::Scope::BOX
	) const;
	/// @param at where it was seen, left alone when it was not.
	bool visible(size_t image, cv::Point& at, eb::Scope scope = eb::Scope::BOX) const
	{
		Sighting seen;
		if (!visible(eb::Images {image}, seen, scope)) return false;
		at = seen.at;
		return true;
	}
	/// For when only the yes or no matters.
	bool visible(size_t image, eb::Scope scope = eb::Scope::BOX) const
	{
		cv::Point anywhere;
		return visible(image, anywhere, scope);
	}
	bool visible(const eb::Images& images, eb::Scope scope = eb::Scope::BOX) const
	{
		Sighting anywhere;
		return visible(images, anywhere, scope);
	}

	/**
	 * Finds a pattern, clicks the middle of it, and makes the game show
	 * that the click was taken: any one of confirm appearing is that proof.
	 *
	 * The position is looked up here and not passed in. A caller that found
	 * the pattern a moment ago holds where it was, and if the panel has
	 * scrolled since, the click would press whatever moved into that place.
	 *
	 * @param image   the pattern to find and press.
	 * @param src_to  how long the look for it may take, and nothing more.
	 *                It belongs to the caller, because only the caller
	 *                knows what the click is part of. Given a Budget, a
	 *                click that ends a longer piece of work takes what is
	 *                left of that work instead of a fresh timeout of its
	 *                own, so the work cannot grow one action at a time.
	 * @param wait    how long to leave the interface alone after the press,
	 *                so the proof is not looked for in the frame that the
	 *                click was supposed to change.
	 * @param confirm patterns, any one of which proves the click was taken.
	 *                Empty means a plain click.
	 * @param conf_to how long to wait for that proof, for each attempt.
	 * @param retries how many more clicks to make when the proof does not
	 *                come. ACTION_RETRIES, or any negative number, asks for
	 *                the value in eve_config.json. A default argument
	 *                cannot read it, because common() is a member.
	 * @throws ProgramError FAILURE if the pattern was never found, if the
	 *         click could not be made, or if every allowed click went
	 *         unconfirmed; STOPPED if a stop arrived first.
	 */
	static constexpr int ACTION_RETRIES = -1;

	void click(
		size_t            image,
		eb::Millis        src_to,
		eb::Millis        wait,
		const eb::Images& confirm,
		eb::Millis        conf_to,
		int               retries = ACTION_RETRIES
	) const;
	/// The usual click: WAIT_CLICK to let the press settle, and
	/// CONFIRM_TIMEOUT to wait for the proof. Give those two by hand only
	/// where the click belongs to a job with a budget of its own.
	void click(
		size_t image, eb::Millis src_to, const eb::Images& confirm,
		int retries = ACTION_RETRIES
	) const
	{
		click(
			image, src_to, common().WAIT_CLICK, confirm,
			common().CONFIRM_TIMEOUT, retries
		);
	}
	/// Nothing to confirm, so nothing to wait for either. A button pressed
	/// and never checked is the usual reason a program keeps running and
	/// does nothing, so leave the proof out only where there is really
	/// nothing to look for.
	void click(size_t image, eb::Millis src_to) const
	{
		click(
			image, src_to, eb::Millis::zero(), eb::Images {},
			eb::Millis::zero()
		);
	}

	/**
	 * Runs another program as a step of this one, with this program's
	 * context. Anything but success ends this run as well, and carries the
	 * step's own words about what went wrong.
	 *
	 * @param step the program to run. It has its own stop flag, so a
	 *             program built this way passes request_stop() on to it.
	 * @throws ProgramError whatever the step ended with, named so that the
	 *         message says which step it was.
	 */
	void sub_program(Program& step) const
	{
		const ProgramResult result = step.exec(context());
		if (ProgramExit::SUCCESS != result.exit) {
			throw ProgramError {
				result.exit,
				"the " + step.name() + " step " + result.description
			};
		}
	}

private:
	friend class Doing;

	// The open Doing scopes, ready to go in front of a message: "hop 3: ".
	std::string note_text() const;

	// How a message names what was looked for, as eve_images.json names it:
	// "'undock'", "'jump' or 'dock'".
	std::string named(const eb::Images& images) const;

	// What appear() is built from, and what a click waits on for its proof.
	// It looks until one of the patterns is there or the timeout runs out,
	// and reports a miss instead of failing on it. What appear() adds is
	// that a miss ends the program.
	bool watch(
		const eb::Images& images,
		eb::Millis        timeout,
		Sighting&         seen,
		eb::Scope             scope
	) const;

	// Turns every declared pattern into a library index. Returns the names
	// the library did not have, and an empty string when all were found.
	std::string resolve_patterns(ImageLibrary& images);

	// The names of all declared parameters, for saying what the file could
	// have meant.
	std::string param_list() const;

	// Sleeping with no Pause to name, for the actions that wait by
	// themselves.
	void rest_or_stop(eb::Millis duration, const std::string& what) const;

	std::string               m_name;
	std::atomic<bool>         m_stop {false};
	std::vector<PatternDecl*> m_patterns;
	std::vector<ParamDecl*>   m_params;
	std::vector<std::string>  m_notes;      // the open Doing scopes
	ProgramContext*           m_context {nullptr};

	// Every program has this one. The menu has room for only a few entries,
	// and most programs are steps of other programs, so a program stays out
	// of the menu until prg_params.json says otherwise. Declared after the
	// list it adds itself to, which must exist by then.
	PROG_PARAM(Flag, SHOW_IN_MENU, false);
};

// Holds the known programs and runs one at a time. They all drive the same
// game window, so two of them clicking at once would fight each other.
class ProgramRunner {
public:
	static constexpr size_t NOT_FOUND = static_cast<size_t>(-1);

	// Called on the program thread as soon as a run ends, so a finished job
	// is reported without the console asking for it.
	using FinishHandler =
		std::function<void(const std::string&, const ProgramResult&)>;

	ProgramRunner() = default;
	~ProgramRunner();
	ProgramRunner(const ProgramRunner&)            = delete;
	ProgramRunner& operator=(const ProgramRunner&) = delete;

	void add(std::unique_ptr<Program> program);
	void on_finish(FinishHandler handler) { m_on_finish = std::move(handler); }
	// Gives every registered program its parameters. Stops at the first
	// program that refuses what it was given.
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
	// Name of the running program, empty when none is running.
	std::string current() const;

	// Starts a program on its own thread. False when one is already running.
	// start(), abort() and wait() are called from both the console and the
	// pop-up menu, so they share a lock of their own. A running program
	// never takes that lock, so joining the thread under it cannot
	// deadlock.
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
