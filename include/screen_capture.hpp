/*
	EVE bots for Windows.
	Author: Igor Polev.

	ScreenCapture - Windows Graphics Capture of a single window.

	Frames are taken on request, not on a timer. frame() hands back the frame
	it holds while that frame is younger than frame_life(). Only after that
	does it capture a new one.

	Graphics Capture itself cannot be slowed down: the session keeps filling
	the frame pool at the display rate. But nothing is copied back to the CPU
	until somebody asks for a frame.
*/

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>

#include <windows.h>
#include <d3d11.h>

#include <winrt/base.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#include "common_defs.hpp"
#include "frame.hpp"

class ScreenCapture {
public:
	explicit ScreenCapture(unsigned frame_rate) : m_frame_rate {frame_rate} {}
	~ScreenCapture();
	ScreenCapture(const ScreenCapture&)            = delete;
	ScreenCapture& operator=(const ScreenCapture&) = delete;

	// True when this build of Windows supports Graphics Capture.
	static bool supported();

	// Starts capturing hwnd. Returns false and fills error on failure.
	bool start(HWND hwnd, std::string& error);
	void stop();

	bool     running()    const noexcept { return m_running.load(); }
	HWND     target()     const noexcept { return m_target; }
	unsigned frame_rate() const noexcept { return m_frame_rate; }
	// frames copied back to the CPU
	uint64_t frame_count()   const noexcept { return m_frame_count.load(); }
	// frames Windows delivered, that is, how often the window redrew
	uint64_t arrived_count() const noexcept { return m_arrived_count.load(); }

	// How long a captured frame counts as the current one.
	eb::Millis frame_life() const noexcept;

	// Puts the current frame into `into`. Captures a new one first when the
	// frame in hand is older than frame_life(). Returns true when `into` was
	// replaced. False means `into` already held the current frame and was
	// left alone, which saves copying every pixel. `into` stays empty while
	// nothing has been captured at all.
	bool new_frame(Frame& into);

private:
	// Two buffers are enough: only the newest frame is ever used.
	static constexpr int FRAME_POOL_BUFFERS = 2;

	static constexpr winrt::Windows::Graphics::DirectX::DirectXPixelFormat
		CAPTURE_FORMAT =
			winrt::Windows::Graphics::DirectX::DirectXPixelFormat::
				B8G8R8A8UIntNormalized;

	// Longest wait for a frame taken after the request. A window that does
	// not redraw sends no frames at all, so a timeout here is normal.
	static constexpr eb::Millis FRAME_WAIT {250};

	void on_frame_arrived(
		const winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool& sender,
		const winrt::Windows::Foundation::IInspectable& args
	);

	// Drops the frames that queued up while nobody was asking, waits for a
	// frame taken after that, and copies it into m_frame. Caller holds
	// m_mutex.
	bool capture_frame();
	// Copies a captured texture into m_frame. Caller holds m_mutex.
	void store_frame(ID3D11Texture2D* texture, uint32_t width, uint32_t height);
	// Caller holds m_mutex.
	void release_resources();

	HWND     m_target     {nullptr};
	unsigned m_frame_rate {0};

	std::atomic<bool>     m_running       {false};
	std::atomic<uint64_t> m_frame_count   {0};
	std::atomic<uint64_t> m_arrived_count {0};

	// one request at a time, and guards the frame, the D3D device and the pool
	std::mutex m_mutex;

	// tells the waiting request that the pool has a new frame
	std::mutex              m_signal_mutex;
	std::condition_variable m_signal;
	bool                    m_frame_ready {false};

	winrt::com_ptr<ID3D11Device>        m_d3d_device;
	winrt::com_ptr<ID3D11DeviceContext> m_d3d_context;
	winrt::com_ptr<ID3D11Texture2D>     m_staging;
	uint32_t m_staging_width  {0};
	uint32_t m_staging_height {0};

	Frame m_frame;
	// when the pool was last asked, which is not when m_frame was taken. A
	// window that did not redraw keeps the same frame, and its pixels are
	// still the current ones
	eb::TimePoint m_checked {};

	winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice m_device {nullptr};
	winrt::Windows::Graphics::Capture::GraphicsCaptureItem         m_item   {nullptr};
	winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool  m_pool   {nullptr};
	winrt::Windows::Graphics::Capture::GraphicsCaptureSession      m_session{nullptr};
	winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool::FrameArrived_revoker m_frame_arrived;
};
