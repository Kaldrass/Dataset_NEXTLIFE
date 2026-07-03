#include <opencv2/opencv.hpp>
#include <iostream>
#include <cmath>

cv::Mat createGaussianKernel(int ksize, double sigma) {
    int half = ksize / 2;
    cv::Mat kernel(ksize, ksize, CV_32F);

    float sum = 0.0f;
    for (int y = -half; y <= half; ++y) {
        for (int x = -half; x <= half; ++x) {
            float val = std::exp(-(x * x + y * y) / (2 * sigma * sigma));
            kernel.at<float>(y + half, x + half) = val;
            sum += val;
        }
    }
    kernel /= sum;

    return kernel;
}

cv::Mat strictMaskedGaussianBlur(const cv::Mat& image, const cv::Mat& mask, int ksize, double sigma) {
    CV_Assert(image.type() == CV_8UC3 && mask.type() == CV_8UC1);

    cv::Mat kernel = createGaussianKernel(ksize, sigma);
    int half = ksize / 2;

    cv::Mat result = image.clone();

    for (int y = 0; y < image.rows; ++y) {
        for (int x = 0; x < image.cols; ++x) {
            if (mask.at<uchar>(y, x) == 0) continue;

            cv::Vec3f sum = {0, 0, 0};
            float weightSum = 0.0f;

            for (int dy = -half; dy <= half; ++dy) {
                for (int dx = -half; dx <= half; ++dx) {
                    int ny = y + dy;
                    int nx = x + dx;
                    if (nx < 0 || ny < 0 || nx >= image.cols || ny >= image.rows) continue;
                    if (mask.at<uchar>(ny, nx) == 0) continue;

                    float weight = kernel.at<float>(dy + half, dx + half);
                    cv::Vec3b color = image.at<cv::Vec3b>(ny, nx);

                    sum += weight * cv::Vec3f(color);
                    weightSum += weight;
                }
            }

            if (weightSum > 0) {
                cv::Vec3b blurredPixel = cv::Vec3b(sum / weightSum);
                result.at<cv::Vec3b>(y, x) = blurredPixel;
            }
        }
    }

    return result;
}

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cout << "Usage: ./masked_blur <image> <mask> <output> [ksize] [sigma]\n";
        return -1;
    }

    std::string imgPath = argv[1];
    std::string maskPath = argv[2];
    std::string outputPath = argv[3];
    int ksize = (argc > 4) ? std::stoi(argv[4]) : 15;
    double sigma = (argc > 5) ? std::stod(argv[5]) : 5.0;

    cv::Mat image = cv::imread(imgPath);
    cv::Mat mask = cv::imread(maskPath, cv::IMREAD_GRAYSCALE);

    if (image.empty() || mask.empty()) {
        std::cerr << "❌ Failed to load image or mask.\n";
        return -1;
    }

    if (image.size() != mask.size()) {
        std::cout << "⚠️ Resizing mask to match image...\n";
        cv::resize(mask, mask, image.size(), 0, 0, cv::INTER_NEAREST);
    }

    // Optional resizing for speed (keep or remove as needed)
    cv::resize(mask, mask, image.size(), 0, 0, cv::INTER_NEAREST);
    cv::resize(image, image, image.size(), 0, 0, cv::INTER_NEAREST);

    cv::Mat result = strictMaskedGaussianBlur(image, mask, ksize, sigma);

    if (!cv::imwrite(outputPath, result)) {
        std::cerr << "❌ Failed to write to " << outputPath << "\n";
        return -1;
    }

    std::cout << "✅ Masked blur saved to: " << outputPath << "\n";
    return 0;
}
