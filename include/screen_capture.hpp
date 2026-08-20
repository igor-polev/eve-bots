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

namespace wrt  = winrt::Windows;
namespace wrtg = wrt::Graphics;

class ScreenCapture {
public:
	explicit ScreenCapture(unsigned frame_rate) : m_frame_rate {frame_rate} {}
	~ScreenCapture() { stop(); }

	// Main interface -- frame delivery
	bool new_frame(Frame& into);

	// Service interface
	inline static bool supported();
	bool start(HWND hwnd, std::string& error);
	void stop();

	// Accessors
	bool        running()       const noexcept { return m_running.load();       }
	HWND        target()        const noexcept { return m_target;               }
	unsigned    frame_rate()    const noexcept { return m_frame_rate;           }
	eb::Millis  frame_life()    const noexcept {
		return eb::Millis {1000 / std::max(1u, m_frame_rate)};
	}

private:
	using DXPixelFormat = wrtg::DirectX::DirectXPixelFormat;
	using DXItem        = wrtg::Capture::GraphicsCaptureItem;
	using DXSession     = wrtg::Capture::GraphicsCaptureSession;
	using DXDevice      = wrtg::DirectX::Direct3D11::IDirect3DDevice;
	using DXFramePool   = wrtg::Capture::Direct3D11CaptureFramePool;
	using DXRevoker     = wrtg::Capture::Direct3D11CaptureFramePool::FrameArrived_revoker;
	using DXInspectable = wrt::Foundation::IInspectable;

	static constexpr DXPixelFormat CAPTURE_FORMAT {
		DXPixelFormat::B8G8R8A8UIntNormalized
	};
	static constexpr int        FRAME_POOL_BUFFERS {2};
	static constexpr eb::Millis FRAME_WAIT_TIMEOUT {250};

	void on_frame_arrived(const DXFramePool& sender, const DXInspectable& args);
	void store_frame(ID3D11Texture2D* texture, uint32_t width, uint32_t height);
	bool capture_frame();

	HWND          m_target      {nullptr};
	unsigned      m_frame_rate  {0};
	// when the pool was last asked, which is not when m_frame was taken. A
	// window that did not redraw keeps the same frame, and its pixels are
	// still the current ones.
	eb::TimePoint m_checked     {};
	Frame         m_frame;

	winrt::com_ptr<ID3D11Device>        m_d3d_device;
	winrt::com_ptr<ID3D11DeviceContext> m_d3d_context;
	winrt::com_ptr<ID3D11Texture2D>     m_staging;
	uint32_t                            m_staging_width  {0};
	uint32_t                            m_staging_height {0};

	DXDevice                m_device         {nullptr};
	DXItem                  m_item           {nullptr};
	DXFramePool             m_pool           {nullptr};
	DXSession               m_session        {nullptr};
	DXRevoker               m_frame_arrived;

	/*
		Two threads meet here. Requests come in on the thread that asked --
		new_frame(), start(), stop() -- and Windows calls on_frame_arrived()
		on a thread of its own, because the pool is created free threaded.

		m_mutex serialises the requests and guards everything they work on:
		the frame, the D3D objects, the pool and the staging texture. The
		capture thread never takes it, and must not: capture_frame() waits
		for a frame while holding m_mutex, so a callback that wanted the same
		mutex would wait for the very request that is waiting for it.

		That is why the signal has a mutex of its own. m_signal_mutex guards
		one bool and is held for a few instructions at a time. The callback
		sets m_frame_ready and notifies; capture_frame() clears it first and
		then waits on m_signal for it to come back, or for the timeout.

		m_running is atomic, so both threads read it without any lock. stop()
		clears it and notifies before taking m_mutex, so a request waiting
		for a frame gives up at once instead of making stop() wait out the
		whole timeout.
	*/
	std::atomic<bool>       m_running     {false};
	bool                    m_frame_ready {false};
	std::condition_variable m_signal;
	std::mutex              m_mutex,
	                        m_signal_mutex;
};

inline bool ScreenCapture::supported()
{
	try {
		return DXSession::IsSupported();
	}
	catch (...) {
		return false;
	}
}
