#include <iostream>			
#include <fstream>
#include <sstream>		
#include <climits>	
#include <math.h>	
#include <stdlib.h>	
#include <algorithm>
#include <unordered_map>

#include <opencv2/opencv.hpp>

#include "coord.h"	
#include "centroid.h"
#include "pair_hash.h"
#include "centroid_average.h"
#include <unordered_set>

void debug_log(const std::string& message) {
#ifdef DEBUG
    std::cout << message << std::endl;
#endif
}

enum colour { blue = 0, green = 1, red = 2 };

constexpr int MAX_ITT = 1000;		    // Default iteration limit
constexpr float CONV_THRESH = 0.0001;	// Default convergence threshold

inline bool converged(cv::Vec3f& convergence_errors, std::vector<cv::Vec3f>& centroids, std::vector<cv::Vec3f>& old_centroids) {
	for (int i=0;i<centroids.size();i++) {
		convergence_errors[blue] = abs(centroids[i][blue] - old_centroids[i][blue]);
		convergence_errors[green] = abs(centroids[i][green] - old_centroids[i][green]);
		convergence_errors[red] = abs(centroids[i][red] - old_centroids[i][red]);

		if (convergence_errors[blue] > CONV_THRESH || 
			convergence_errors[green] > CONV_THRESH || 
			convergence_errors[red] > CONV_THRESH) {
				return false;
		}
	}

	return true;
}

void initialize_centroids(std::vector<cv::Vec3f>& centroids, cv::Mat& image) {
	srand(time(NULL));
	uint8_t centroid_count = centroids.size();
	cv::Size s = image.size();

	// Set the centroid values to random pixels in the image (must be unique)
	// Since collisions are unlikely given the centroid count is typically small
	// and the image size is typically large, just keep generating random
	// coordinates until we have n unique values
	std::unordered_set <std::pair<unsigned int, unsigned int>, pair_hash> initial_centroids;

	// Random selection of unique initial centroids
	for (int i=0;i<centroid_count;i++) {
		int x = rand() % s.height;
		int y = rand() % s.width;

		if (initial_centroids.count({ x, y })) {
			i--;
		}
		else {
			centroids[i][blue] = static_cast<float>(image.at<cv::Vec3b>(x, y)[blue]);
			centroids[i][green] = static_cast<float>(image.at<cv::Vec3b>(x, y)[green]);
			centroids[i][red] = static_cast<float>(image.at<cv::Vec3b>(x, y)[red]);

			initial_centroids.insert({ x, y });
		}
	}
}

std::vector<cv::Vec3f> kmeans(cv::Mat& image, uint8_t centroid_count) {
	cv::Size s = image.size();

	std::vector<cv::Vec3f> centroids(centroid_count);

	initialize_centroids(centroids, image);

	std::vector<centroid_average> centroid_averages(centroid_count);
	for (int i=0;i<centroid_count;i++) {
		centroid_averages[i] = { 0, 0, 0 ,0 };
	}

	// Main k-means loop
	int itt = 0;
	do {
		// Save a copy of the centroids before we modify them
		std::vector<cv::Vec3f> old_centroids(centroid_count);
		for (int j=0;j<centroid_averages.size();j++) {
			old_centroids[j][blue] = centroids[j][blue]; 
			old_centroids[j][green] = centroids[j][green]; 
			old_centroids[j][red] = centroids[j][red];
		};

		// Reset averages
		for (int i=0;i<centroids.size();i++) {
			centroid_averages[i] = { 0, 0, 0, 0 };
		}

 		for (int j=0;j<s.width;j++) {
			for (int k=0;k<s.height;k++) {
				float current_min = FLT_MAX;
				cv::Vec3b pixel = image.at<cv::Vec3b>(k, j);

				int closest_centroid_idx = 0;
				// For each pixel, point the corresponding centroid map entry to the nearest centroid
				for (int i=0;i<centroid_count;i++) {
					float dist = abs(static_cast<float>(pixel[blue]) - centroids[i][blue]) +
								 abs(static_cast<float>(pixel[green]) - centroids[i][green]) +
								 abs(static_cast<float>(pixel[red]) - centroids[i][red]);

					if (dist < current_min) {
						current_min = dist;
						closest_centroid_idx = i;
					}
				}

				// Update the centroid averages
				centroid_averages[closest_centroid_idx].blue += pixel[blue];
				centroid_averages[closest_centroid_idx].green += pixel[green];
				centroid_averages[closest_centroid_idx].red += pixel[red];
				centroid_averages[closest_centroid_idx].total++;
			}
		}

		// Update centroid locations
		for (int i=0;i<centroids.size();i++) {
			if (centroid_averages[i].total != 0) {
				centroids[i][blue] = static_cast<float>(centroid_averages[i].blue) / centroid_averages[i].total;
				centroids[i][green] = static_cast<float>(centroid_averages[i].green) / centroid_averages[i].total;
				centroids[i][red] = static_cast<float>(centroid_averages[i].red) / centroid_averages[i].total;
			}
		}

		// Check for convergence
		cv::Vec3f convergence_errors;
		if (converged(convergence_errors, centroids, old_centroids)) {
			break;
		}

#ifdef DEBUG
		std::cout << "=== Iteration: " << itt << " ===\n";
		for (cv::Vec3f c : centroids) {
			std::cout << " (B,G,R)" << c[blue] << ", " << c[green] << ", " << c[red] << "\n";
		}

		std::cout << "\n - CONVERGENCE ERR\n" <<
			" BLUE ERROR:  " + std::to_string(convergence_errors[blue]) + "\n" <<
			" GREEN ERROR: " + std::to_string(convergence_errors[green]) + "\n" <<
			" RED ERROR:   " + std::to_string(convergence_errors[red]) + "\n" <<
			"=======================\n";
#endif

		itt++;
	} while (itt < MAX_ITT);

	return centroids;
}

cv::Mat im_to_mat(std::string image_arg) {
	cv::Mat image;
	image = cv::imread(image_arg, 1);
	if (!image.data)
	{
		throw std::invalid_argument("No image data found for " + image_arg);
	}

	// TODO: Resize the image by scaling it so we handle someone passing in something massive

	return image;
}

uint8_t centroid_count(const char* centroids_arg) {
	int num_of_centroids = atoi(centroids_arg);

	if (num_of_centroids < 1 || num_of_centroids > 255) {
		throw std::invalid_argument("Number of centroids must be in the range [1,255]");
	}

	return num_of_centroids;
}

int main(int argc, char* argv[]) {
	if (argc < 3) {
		std::cout << "Missing required image name and palette count\n";
		std::cout << "eg: ./imgseg apple.png 6\n";
		exit(1);
	}

#ifdef DEBUG
		std::cout << "Loading image into memory: " << argv[1] << "\n";
#endif

	auto im = im_to_mat(argv[1]);
	auto result_set = kmeans(im, centroid_count(argv[2]));

	std::sort(result_set.begin(), result_set.end(), [](const cv::Vec3f& lhs, const cv::Vec3f& rhs) {
		// TODO: Not this - step sorting
		return lhs[red] < rhs[red];
	});

	for (cv::Vec3f v : result_set) {
	//	std::cout << "#" << std::hex << (v[red] << 16 | v[green] << 8 | v[blue]) << "\n";
		std::cout << "#" << v[red] << " " << v[green] <<  " " << v[blue] << "\n";
	}

	return 0;
}
