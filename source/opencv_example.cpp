
#include <opencv2/opencv.hpp>

bool detectImage(const cv::Mat& bigImage, const cv::Mat& smallImage) {
  cv::Mat result;
  cv::matchTemplate(bigImage, smallImage, result, cv::TM_CCOEFF_NORMED);
  cv::threshold(result, result, 0.8, 1, cv::THRESH_BINARY);

  cv::Mat locations;
  cv::findNonZero(result, locations);

  return !locations.empty();
}

cv::Mat bigImage = cv::imread("big_image.yuv", cv::IMREAD_GRAYSCALE);
cv::Mat smallImage = cv::imread("small_image.yuv", cv::IMREAD_GRAYSCALE);

if (detectImage(bigImage, smallImage)) {
  std::cout << "Small image found!" << std::endl;
} else {
  std::cout << "Small image not found." << std::endl;
}