/*
	EVE bots for Windows.
	Author: Igor Polev.

	PNG output implementation.
*/

#include <vector>

#include <wincodec.h>
#include <winrt/base.h>

#include "png_writer.hpp"

bool write_png(const Frame& frame, const std::wstring& path, std::string& error)
{
	if (frame.empty()) {
		error = "no frame to write";
		return false;
	}

	try {
		auto factory = winrt::create_instance<IWICImagingFactory>(
			CLSID_WICImagingFactory
		);

		winrt::com_ptr<IWICStream> stream;
		winrt::check_hresult(factory->CreateStream(stream.put()));
		winrt::check_hresult(stream->InitializeFromFilename(
			path.c_str(), GENERIC_WRITE
		));

		winrt::com_ptr<IWICBitmapEncoder> encoder;
		winrt::check_hresult(factory->CreateEncoder(
			GUID_ContainerFormatPng, nullptr, encoder.put()
		));
		winrt::check_hresult(encoder->Initialize(
			stream.get(), WICBitmapEncoderNoCache
		));

		winrt::com_ptr<IWICBitmapFrameEncode> wic_frame;
		winrt::check_hresult(encoder->CreateNewFrame(
			wic_frame.put(), nullptr
		));
		winrt::check_hresult(wic_frame->Initialize(nullptr));
		winrt::check_hresult(wic_frame->SetSize(frame.width, frame.height));

		// The encoder may not honour the requested format; it reports back
		// what it will actually use, which we must then match.
		WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
		winrt::check_hresult(wic_frame->SetPixelFormat(&format));
		if (!IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA)) {
			error = "PNG encoder rejected 32bpp BGRA pixel format";
			return false;
		}

		// WritePixels takes a non-const buffer, so hand it a copy.
		std::vector<uint8_t> pixels {frame.pixels};
		winrt::check_hresult(wic_frame->WritePixels(
			frame.height,
			frame.stride(),
			static_cast<UINT>(pixels.size()),
			pixels.data()
		));

		winrt::check_hresult(wic_frame->Commit());
		winrt::check_hresult(encoder->Commit());
		return true;
	}
	catch (const winrt::hresult_error& e) {
		error = to_string(e.message());
		return false;
	}
	catch (const std::exception& e) {
		error = e.what();
		return false;
	}
}
