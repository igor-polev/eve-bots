/*
	EVE bots for Windows.
	Author: Igor Polev.

	ScreenCapture - Windows Graphics Capture of a single window.

	Frames are taken on request rather than on a timer: frame() hands back
	the frame it holds while that one is younger than frame_life(), and only
	then goes and captures a new one.

	Graphics Capture itself cannot be throttled - the session keeps filling
	the frame pool at the display rate - but nothing is copied back to the
	CPU until somebody asks for a frame.
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

	// True if this build of Windows supports Graphics Capture.
	static bool supported();

	// Begins capturing hwnd. On failure returns false and fills error.
	bool start(HWND hwnd, std::string& error);
	void stop();

	bool     running()    const noexcept { return m_running.load(); }
	HWND     target()     const noexcept { return m_target; }
	unsigned frame_rate() const noexcept { return m_frame_rate; }
	// frames copied back to the CPU
	uint64_t frame_count()   const noexcept { return m_frame_count.load(); }
	// frames Windows delivered, i.e. how often the window redrew
	uint64_t arrived_count() const noexcept { return m_arrived_count.load(); }

	// How long a captured frame stands as the current one.
	eb::Millis frame_life() const noexcept;

	// Puts the current frame into `into`, capturing a new one first when the
	// held one has outlived frame_life(). True when `into` was replaced;
	// false means it already held the current frame and was left alone,
	// which is what spares the copy of every pixel. `into` is left empty
	// while nothing has been captured at all.
	bool frame(Frame& into);

private:
	// Two buffers is enough: only the newest frame is ever consumed.
	static constexpr int FRAME_POOL_BUFFERS = 2;

	static constexpr winrt::Windows::Graphics::DirectX::DirectXPixelFormat
		CAPTURE_FORMAT =
			winrt::Windows::Graphics::DirectX::DirectXPixelFormat::
				B8G8R8A8UIntNormalized;

	// Upper bound on the wait for a frame taken after the request. A window
	// that is not redrawing produces none at all, so timing out is normal.
	static constexpr eb::Millis FRAME_WAIT {250};

	void on_frame_arrived(
		const winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool& sender,
		const winrt::Windows::Foundation::IInspectable& args
	);

	// Discards what queued up while nobody was asking, waits for a frame
	// taken since, and copies it into m_frame. Caller holds m_mutex.
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

	// serialises pulls, and guards the frame, the D3D device and the pool
	std::mutex m_mutex;

	// signals that the pool holds a frame taken since the request
	std::mutex              m_signal_mutex;
	std::condition_variable m_signal;
	bool                    m_frame_ready {false};

	winrt::com_ptr<ID3D11Device>        m_d3d_device;
	winrt::com_ptr<ID3D11DeviceContext> m_d3d_context;
	winrt::com_ptr<ID3D11Texture2D>     m_staging;
	uint32_t m_staging_width  {0};
	uint32_t m_staging_height {0};

	Frame m_frame;
	// when the pool was last asked, which is not when m_frame was taken: a
	// window that has not redrawn leaves the frame it was, and its pixels
	// stay the current ones
	eb::TimePoint m_checked {};

	winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice m_device {nullptr};
	winrt::Windows::Graphics::Capture::GraphicsCaptureItem         m_item   {nullptr};
	winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool  m_pool   {nullptr};
	winrt::Windows::Graphics::Capture::GraphicsCaptureSession      m_session{nullptr};
	winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool::FrameArrived_revoker m_frame_arrived;
};
