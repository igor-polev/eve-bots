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

using namespace winrt::Windows::Graphics;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;

ScreenCapture::~ScreenCapture()
{
	stop();
}

bool ScreenCapture::supported()
{
	try {
		return GraphicsCaptureSession::IsSupported();
	}
	catch (...) {
		return false;
	}
}

eb::Millis ScreenCapture::frame_life() const noexcept
{
	return eb::Millis {1000 / std::max(1u, m_frame_rate)};
}

bool ScreenCapture::frame(Frame& into)
{
	std::lock_guard<std::mutex> lock {m_mutex};
	if (m_running.load()
		&& (m_frame.empty() || eb::since(m_checked) >= frame_life()))
	{
		try {
			// Nothing newly delivered means the window has not redrawn, so
			// what is held is still what is on screen. Only the moment of
			// asking moves on, which is why it is kept apart from the frame's
			// own timestamp: those pixels are the same pixels, and everything
			// already worked out about them still holds.
			capture_frame();
		}
		catch (...) {
			// A frame that cannot be decoded is not fatal: the caller gets
			// whatever was captured last.
		}
		m_checked = eb::Clock::now();
	}
	if (into.taken == m_frame.taken) return false;
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
		// D3D11 device - BGRA support is required by the capture API
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
		m_device = inspectable.as<IDirect3DDevice>();

		auto interop = winrt::get_activation_factory<
			GraphicsCaptureItem, ::IGraphicsCaptureItemInterop>();
		GraphicsCaptureItem item {nullptr};
		winrt::check_hresult(interop->CreateForWindow(
			hwnd,
			winrt::guid_of<GraphicsCaptureItem>(),
			winrt::put_abi(item)
		));
		m_item = item;

		m_pool = Direct3D11CaptureFramePool::CreateFreeThreaded(
			m_device, CAPTURE_FORMAT, FRAME_POOL_BUFFERS, m_item.Size()
		);
		m_session = m_pool.CreateCaptureSession(m_item);

		// Cosmetic options, both unsupported on older Windows 10 builds.
		try { m_session.IsCursorCaptureEnabled(false); } catch (...) {}
		try { m_session.IsBorderRequired(false);        } catch (...) {}

		{
			std::lock_guard<std::mutex> lock {m_mutex};
			m_d3d_device     = device;
			m_d3d_context    = context;
			m_staging        = nullptr;
			m_staging_width  = 0;
			m_staging_height = 0;
			m_frame          = Frame {};
			m_checked        = eb::TimePoint {};
		}
		{
			std::lock_guard<std::mutex> lock {m_signal_mutex};
			m_frame_ready = false;
		}
		m_frame_count.store(0);
		m_arrived_count.store(0);
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
	// before the lock, so a pull waiting for a frame gives it up at once
	m_running.store(false);
	m_signal.notify_all();

	std::lock_guard<std::mutex> lock {m_mutex};
	m_frame_arrived.revoke();
	release_resources();
	m_target = nullptr;
}

void ScreenCapture::release_resources()
{
	if (m_session) { m_session.Close(); m_session = nullptr; }
	if (m_pool)    { m_pool.Close();    m_pool    = nullptr; }
	m_item   = nullptr;
	m_device = nullptr;

	m_staging        = nullptr;
	m_d3d_context    = nullptr;
	m_d3d_device     = nullptr;
	m_staging_width  = 0;
	m_staging_height = 0;
	m_frame          = Frame {};
	m_checked        = eb::TimePoint {};
}

void ScreenCapture::on_frame_arrived(
	const Direct3D11CaptureFramePool&,
	const winrt::Windows::Foundation::IInspectable&)
{
	// Deliberately does no work: it only records that a frame is waiting,
	// and whoever asked for one decides whether to spend time on it.
	m_arrived_count.fetch_add(1);
	{
		std::lock_guard<std::mutex> lock {m_signal_mutex};
		m_frame_ready = true;
	}
	m_signal.notify_one();
}

bool ScreenCapture::capture_frame()
{
	if (!m_pool) return false;

	// Frames that queued while nobody was asking show the window as it was;
	// each of them returns its buffer to the pool as it goes out of scope.
	while (m_pool.TryGetNextFrame()) {}
	{
		std::unique_lock<std::mutex> lock {m_signal_mutex};
		m_frame_ready = false;
		m_signal.wait_for(lock, FRAME_WAIT, [this] {
			return m_frame_ready || !m_running.load();
		});
	}
	// Asked for regardless of how the wait ended: a frame may have been
	// delivered in the gap between discarding and clearing the flag.
	auto frame = m_pool.TryGetNextFrame();
	if (!frame) return false;

	const SizeInt32 content = frame.ContentSize();
	if (content.Width <= 0 || content.Height <= 0) return false;

	winrt::com_ptr<ID3D11Texture2D> texture;
	{
		// interop interface lives in the ABI namespace, not the projected
		// winrt one pulled in by the using-directives above
		auto access = frame.Surface().as<
			::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
		winrt::check_hresult(access->GetInterface(
			winrt::guid_of<ID3D11Texture2D>(), texture.put_void()
		));
	}

	D3D11_TEXTURE2D_DESC desc {};
	texture->GetDesc(&desc);

	// The pool texture can be larger than the window content; crop.
	const uint32_t width  = (std::min)(
		static_cast<uint32_t>(content.Width),  desc.Width);
	const uint32_t height = (std::min)(
		static_cast<uint32_t>(content.Height), desc.Height);

	if (!m_d3d_device || !m_d3d_context) return false;
	store_frame(texture.get(), width, height);
	m_frame_count.fetch_add(1);

	// Track window resizes so the pool keeps matching the window.
	if (content.Width  != m_item.Size().Width ||
		content.Height != m_item.Size().Height)
	{
		m_pool.Recreate(m_device, CAPTURE_FORMAT, FRAME_POOL_BUFFERS, content);
	}
	return true;
}

void ScreenCapture::store_frame(
	ID3D11Texture2D* texture, uint32_t width, uint32_t height)
{
	// staging texture: a GPU texture is not CPU readable
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

	Frame frame;
	frame.width  = width;
	frame.height = height;
	frame.taken  = eb::Clock::now();
	frame.pixels.resize(static_cast<size_t>(width) * height * 4);

	const auto*  src = static_cast<const uint8_t*>(mapped.pData);
	auto*        dst = frame.pixels.data();
	const size_t row_bytes = static_cast<size_t>(width) * 4;
	for (uint32_t y = 0; y < height; ++y) {
		memcpy(dst + y * row_bytes, src + y * mapped.RowPitch, row_bytes);
	}
	m_d3d_context->Unmap(m_staging.get(), 0);

	m_frame = std::move(frame);
}
