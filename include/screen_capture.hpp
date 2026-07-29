/*
	EVE bots for Windows.
	Author: Igor Polev.

	ScreenCapture - Windows Graphics Capture of a single window.

	Capture runs on a dedicated thread which grabs one frame per tick and
	sleeps for the rest of the interval, so the CPU/GPU cost is set by the
	configured frame rate rather than by the window's redraw rate.

	Graphics Capture itself cannot be throttled: the session keeps filling
	the frame pool at the display rate. So each tick discards whatever
	queued up while we slept - those frames are stale - and then waits for
	a freshly captured one. Only that frame is copied back to the CPU.

	The newest frame is kept in a buffer guarded by a mutex; latest_frame()
	hands out a copy, so it is safe to call from any thread.
*/

#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include <windows.h>
#include <d3d11.h>

#include <winrt/base.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#include "frame.hpp"

class ScreenCapture {
public:
	ScreenCapture() = default;
	~ScreenCapture();
	ScreenCapture(const ScreenCapture&)            = delete;
	ScreenCapture& operator=(const ScreenCapture&) = delete;

	// True if this build of Windows supports Graphics Capture.
	static bool supported();

	// Begins capturing hwnd at frame_rate frames per second.
	// On failure returns false and fills error.
	bool start(HWND hwnd, unsigned frame_rate, std::string& error);
	void stop();

	bool     running()     const noexcept { return m_running.load(); }
	HWND     target()      const noexcept { return m_target; }
	// frames kept, i.e. copied back to the CPU at the configured rate
	uint64_t frame_count() const noexcept { return m_frame_count.load(); }
	// frames Windows delivered, i.e. how often the window actually redrew
	uint64_t arrived_count() const noexcept { return m_arrived_count.load(); }
	unsigned frame_rate()  const noexcept { return m_frame_rate; }

	// Copy of the most recent frame; empty if none has arrived yet.
	Frame latest_frame() const;

private:
	void capture_loop();
	void on_frame_arrived(
		const winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool& sender,
		const winrt::Windows::Foundation::IInspectable& args
	);

	// Throws away queued frames so the next one delivered is current.
	void discard_queued_frames();
	// Blocks until a frame is delivered or the timeout expires.
	bool wait_for_frame(std::chrono::milliseconds timeout);
	// Sleeps until deadline, or returns early once stop() is called.
	// timer may be null, in which case a plain sleep is used.
	void sleep_until_deadline(
		void* timer, std::chrono::steady_clock::time_point deadline
	);
	// Grabs one queued frame, if any, into m_frame. True if one was stored.
	bool grab_frame();
	// Copies a captured texture into m_frame. Caller holds m_device_mutex.
	void store_frame(ID3D11Texture2D* texture, uint32_t width, uint32_t height);
	void release_resources();

	HWND     m_target     {nullptr};
	unsigned m_frame_rate {0};

	std::atomic<bool>     m_running       {false};
	std::atomic<uint64_t> m_frame_count   {0};
	std::atomic<uint64_t> m_arrived_count {0};
	std::thread           m_thread;
	// signalled by stop() so a sleeping capture thread wakes at once
	HANDLE                m_stop_event  {nullptr};

	// signals the capture thread that the pool has a new frame
	std::mutex              m_signal_mutex;
	std::condition_variable m_signal;
	bool                    m_frame_ready {false};

	// D3D11 device and its immediate context are not thread safe.
	mutable std::mutex m_device_mutex;
	winrt::com_ptr<ID3D11Device>        m_d3d_device;
	winrt::com_ptr<ID3D11DeviceContext> m_d3d_context;
	winrt::com_ptr<ID3D11Texture2D>     m_staging;
	uint32_t m_staging_width  {0};
	uint32_t m_staging_height {0};

	mutable std::mutex m_frame_mutex;
	Frame m_frame;

	winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice m_device {nullptr};
	winrt::Windows::Graphics::Capture::GraphicsCaptureItem         m_item   {nullptr};
	winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool  m_pool   {nullptr};
	winrt::Windows::Graphics::Capture::GraphicsCaptureSession      m_session{nullptr};
	winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool::FrameArrived_revoker m_frame_arrived;
};
