#include <fstream>
#include <iostream>
#include "libv4l2cpp/inc/V4l2Capture.h"

#define TEST_DEV "/dev/video10"

int main(int argc, char* argv[]) 
{
	static const V4L2DeviceParameters v4l2_params {
		TEST_DEV,
		V4L2_PIX_FMT_YUV420,
		592,
		1280,
		30,
		IOTYPE_MMAP
	};

	V4l2Capture* scrcpy_dev = V4l2Capture::create(v4l2_params);
	if (scrcpy_dev == NULL) {
		std::cout << "--- Failed to open device " << TEST_DEV << std::endl;
		return -1;
	}
	std::cout << "Device " << TEST_DEV << " opened" << std::endl;
	static unsigned int buff_size = scrcpy_dev->getBufferSize();
	std::cout << "Buffer size = " << buff_size << " bytes" << std::endl;
	char *buffer = new char[buff_size];
	static std::ofstream frame_file;
	auto file_mode = std::ios::out | std::ios::binary | std::ios::trunc;
	auto file_ext = '.' + V4l2Device::fourcc(scrcpy_dev->getFormat());

	int i_frame = 0;
	timeval def_timeout {1, 0};
	while (scrcpy_dev->isReadable(&def_timeout) && i_frame < 30)
	{
		auto byte_count = scrcpy_dev->read(buffer, buff_size);
		if (byte_count == -1) {
			std::cout << "-- Error reading from " << TEST_DEV << std::endl;
			break;
		}
		try {
			frame_file.open("frame_" + std::to_string(i_frame) + file_ext, file_mode);
			frame_file.write(buffer, buff_size);
			frame_file.close();
		}
		catch (...) {
			std::cout << "-- Error while saving frames to file" << std::endl;
			break;
		}
		i_frame++;
	}

	delete[] buffer;
	delete scrcpy_dev;
	return 0;
}