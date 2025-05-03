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
	if (scrcpy_dev == NULL)
	{
		std::cout << "Failed to open device " << TEST_DEV << std::endl;
		return -1;
	}
	std::cout << "Device " << TEST_DEV << " opened" << std::endl;

	timeval def_timeout {1, 0};
	while (scrcpy_dev->isReadable(&def_timeout))
	{

	}

	delete scrcpy_dev;
	return 0;
}