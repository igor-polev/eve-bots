/*
	EVE bots for Windows.
	Author: Igor Polev.

	ScreenCapture implementation.
*/

#include <algorithm>

#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.h>

#include "screen_capture.hpp"

bool ScreenCapture::new_frame(Frame& into)
{
	std::lock_guard<std::mutex> lock {m_mutex};
	if (m_running.load() && (m_frame.empty() || eb::since(m_checked) >= frame_life())) {
		try {
			// No new frame means the window did not redraw, so the frame
			// in hand is still what is on screen. Only the time of asking
			// moves on. That time is kept apart from the frame's own
			// timestamp, because the pixels are the same pixels and
			// everything already worked out about them still holds.
			capture_frame();
		}
		catch (...) {
			// A frame that cannot be decoded is not fatal. The caller gets
			// the frame captured last.
		}
		m_checked = eb::Clock::now();
	}
	if (into.taken == m_frame.taken)
		return false;
	into = m_frame;
	return true;
}

bool ScreenCapture::start(HWND hwnd, std::string& error)
{
	if (m_running.load()) {
		error = "capture is already running";
		return false;
	}
	if (!IsWindow(hwnd)) {
		error = "target window no longer exists";
		return false;
	}
	if (!supported()) {
		error = "Windows Graphics Capture is not supported on this system";
		return false;
	}

	try {
		// D3D11 device. The capture API needs BGRA support.
		winrt::com_ptr<ID3D11Device>        device;
		winrt::com_ptr<ID3D11DeviceContext> context;
		winrt::check_hresult(D3D11CreateDevice(
			nullptr,
			D3D_DRIVER_TYPE_HARDWARE,
			nullptr,
			D3D11_CREATE_DEVICE_BGRA_SUPPORT,
			nullptr, 0,
			D3D11_SDK_VERSION,
			device.put(),
			nullptr,
			context.put()
		));

		auto dxgi_device = device.as<IDXGIDevice>();
		winrt::com_ptr<::IInspectable> inspectable;
		winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(
			dxgi_device.get(), inspectable.put()
		));
		m_device = inspectable.as<DXDevice>();

		auto interop = winrt::get_activation_factory<
			DXItem, ::IGraphicsCaptureItemInterop>();
		DXItem item {nullptr};
		winrt::check_hresult(interop->CreateForWindow(
			hwnd,
			winrt::guid_of<DXItem>(),
			winrt::put_abi(item)
		));
		m_item = item;

		m_pool = DXFramePool::CreateFreeThreaded(
			m_device, CAPTURE_FORMAT, FRAME_POOL_BUFFERS, m_item.Size()
		);
		m_session = m_pool.CreateCaptureSession(m_item);

		// Cosmetic options, both unsupported on older Windows 10 builds.
		try { m_session.IsCursorCaptureEnabled(false); } catch (...) {}
		try { m_session.IsBorderRequired(false);       } catch (...) {}

		std::unique_lock<std::mutex> lock {m_mutex};
		m_d3d_device     = device;
		m_d3d_context    = context;
		m_staging        = nullptr;
		m_staging_width  = 0;
		m_staging_height = 0;
		m_frame          = Frame {};
		m_checked        = eb::TimePoint {};
		lock.unlock();

		std::unique_lock<std::mutex> signal_lock {m_signal_mutex};
		m_frame_ready = false;
		signal_lock.unlock();

		m_target = hwnd;

		m_frame_arrived = m_pool.FrameArrived(
			winrt::auto_revoke, {this, &ScreenCapture::on_frame_arrived}
		);
		m_session.StartCapture();
		m_running.store(true);
		return true;
	}
	catch (const winrt::hresult_error& e) {
		error = to_string(e.message());
		stop();
		return false;
	}
	catch (const std::exception& e) {
		error = e.what();
		stop();
		return false;
	}
}

void ScreenCapture::stop()
{
	// before the lock, so a request waiting for a frame gives up at once
	m_running.store(false);
	m_signal.notify_all();

	std::lock_guard<std::mutex> lock {m_mutex};
	m_frame_arrived.revoke();
	if (m_session) { m_session.Close(); m_session = nullptr; }
	if (m_pool)    { m_pool   .Close(); m_pool    = nullptr; }
	m_item           = nullptr;
	m_device         = nullptr;
	m_staging        = nullptr;
	m_d3d_context    = nullptr;
	m_d3d_device     = nullptr;
	m_staging_width  = 0;
	m_staging_height = 0;
	m_frame          = Frame {};
	m_checked        = eb::TimePoint {};
	m_target         = nullptr;
}

void ScreenCapture::on_frame_arrived(const DXFramePool&, const DXInspectable&)
{
	// Does no work on purpose. It only notes that a frame is waiting, and
	// whoever asked for a frame decides whether to spend time on it.
	std::unique_lock<std::mutex> lock {m_signal_mutex};
	m_frame_ready = true;
	lock.unlock();
	m_signal.notify_one();
}

bool ScreenCapture::capture_frame()
{
	if (!m_pool) return false;

	while (m_pool.TryGetNextFrame()) {} // Drop frames nobody asked for.

	// One frame life is the longest wait that can bring anything: a window
	// that does not redraw sends no frame at all, and waiting longer than the
	// frame rate asks for only delays the answer.
	std::unique_lock<std::mutex> lock {m_signal_mutex};
	m_frame_ready = false;
	m_signal.wait_for(lock, frame_life(), [this] {
		return m_frame_ready || !m_running.load();
	});
	lock.unlock();
	// Asked for however the wait ended: a frame may have arrived between
	// dropping the old ones and clearing the flag.
	auto frame = m_pool.TryGetNextFrame();
	if (!frame) return false;

	const wrtg::SizeInt32 content {frame.ContentSize()};
	if (content.Width <= 0 || content.Height <= 0) return false;

	winrt::com_ptr<ID3D11Texture2D> texture;
	{
		// the interop interface lives in the ABI namespace, which is not the
		// projected winrt one the aliases name
		auto access = frame.Surface().as<
			::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess
		>();
		winrt::check_hresult(access->GetInterface(
			winrt::guid_of<ID3D11Texture2D>(), texture.put_void()
		));
	}

	D3D11_TEXTURE2D_DESC desc {};
	texture->GetDesc(&desc);

	// The pool texture can be larger than the window content, so crop it.
	const uint32_t width  {(std::min)(static_cast<uint32_t>(content.Width),  desc.Width)};
	const uint32_t height {(std::min)(static_cast<uint32_t>(content.Height), desc.Height)};

	if (!m_d3d_device || !m_d3d_context) return false;
	store_frame(texture.get(), width, height);

	// Follow window resizes, so the pool keeps matching the window.
	const auto size {m_item.Size()};
	if (content.Width != size.Width || content.Height != size.Height)
		m_pool.Recreate(m_device, CAPTURE_FORMAT, FRAME_POOL_BUFFERS, content);

	return true;
}

void ScreenCapture::store_frame(
	ID3D11Texture2D* texture,
	uint32_t         width,
	uint32_t         height)
{
	// staging texture, because the CPU cannot read a GPU texture
	if (!m_staging || m_staging_width != width || m_staging_height != height) {
		D3D11_TEXTURE2D_DESC staging_desc {};
		staging_desc.Width            = width;
		staging_desc.Height           = height;
		staging_desc.MipLevels        = 1;
		staging_desc.ArraySize        = 1;
		staging_desc.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
		staging_desc.SampleDesc.Count = 1;
		staging_desc.Usage            = D3D11_USAGE_STAGING;
		staging_desc.BindFlags        = 0;
		staging_desc.CPUAccessFlags   = D3D11_CPU_ACCESS_READ;
		staging_desc.MiscFlags        = 0;

		winrt::com_ptr<ID3D11Texture2D> staging;
		winrt::check_hresult(m_d3d_device->CreateTexture2D(
			&staging_desc, nullptr, staging.put()
		));
		m_staging        = staging;
		m_staging_width  = width;
		m_staging_height = height;
	}

	D3D11_BOX box {};
	box.left   = 0;
	box.top    = 0;
	box.front  = 0;
	box.right  = width;
	box.bottom = height;
	box.back   = 1;
	m_d3d_context->CopySubresourceRegion(
		m_staging.get(), 0, 0, 0, 0, texture, 0, &box
	);

	D3D11_MAPPED_SUBRESOURCE mapped {};
	winrt::check_hresult(m_d3d_context->Map(
		m_staging.get(), 0, D3D11_MAP_READ, 0, &mapped
	));

	m_frame.pixels.resize(static_cast<size_t>(width) * height * 4);
	m_frame.width  = width;
	m_frame.height = height;
	m_frame.taken  = eb::Clock::now();

	const auto*  src = static_cast<const uint8_t*>(mapped.pData);
	auto*        dst = m_frame.pixels.data();
	const size_t row_bytes = static_cast<size_t>(width) * 4;
	for (uint32_t y = 0; y < height; ++y) {
		memcpy(dst + y * row_bytes, src + y * mapped.RowPitch, row_bytes);
	}
	m_d3d_context->Unmap(m_staging.get(), 0);
}
