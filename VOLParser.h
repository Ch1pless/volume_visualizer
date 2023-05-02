#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <bit>

#include <glm/glm.hpp>

int _endian_check = 1;
#define LITTLE_ENDIAN (*(char *)&_endian_check == 1)


struct VOLData {
	std::string name;
	glm::ivec3 resolution;
	int saved_border;
	glm::vec3 true_size;
	std::vector<unsigned char> values;
};

void printVOLData(const VOLData& data) {
	std::cout << "----- " << data.name << " Volume Data -----" << std::endl;
	std::cout << "Resolution: ";
	for (int i = 0; i < 3; ++i) {
		std::cout << data.resolution[i];
		if (i != 2) std::cout << ", ";
	}
	std::cout << std::endl;
	std::cout << "Saved Border: " << data.saved_border << std::endl;
	std::cout << "True Size: ";
	for (int i = 0; i < 3; ++i) {
		std::cout << data.true_size[i];
		if (i != 2) std::cout << ", ";
	}
	std::cout << std::endl;
	std::cout << "Values: " << data.values.size() << std::endl << std::endl;
	unsigned char min_val = 255, max_val = 0;
	for (unsigned char x : data.values) {
		min_val = std::min(min_val, x);
		max_val = std::max(max_val, x);
	}
	std::cout << "Range of Values: min " << (int)min_val << " max " << (int)max_val << std::endl;
}
// values are big endian so a[0] is largest and a[3] is smallest
int get_int(std::vector<unsigned char>& buffer, int index) {
	if (LITTLE_ENDIAN) {
		return int(
			buffer[index] << 24 |
			buffer[index + 1] << 16 |
			buffer[index + 2] << 8 |
			buffer[index + 3]
			);
	} else {
		return int(
			buffer[index] |
			buffer[index + 1] << 8 |
			buffer[index + 2] << 16 |
			buffer[index + 3] << 24
			);
	}
}

float get_float(std::vector<unsigned char>& buffer, int index) {
	float result;
	int binary = get_int(buffer, index);
	std::memcpy(&result, &binary, sizeof(int));
	return result;
}

void load_template(VOLData& data) {
	data.name = std::string("Placeholder");
	data.name = data.name.substr(0, data.name.find_last_of("."));
	data.resolution.x = 1;
	data.resolution.y = 1;
	data.resolution.z = 1;

	data.saved_border = 0;

	data.true_size.x = 1.0;
	data.true_size.y = 1.0;
	data.true_size.z = 1.0;

	data.values = std::vector<unsigned char>(1);
}

VOLData parseVOLDataFromFile(const char* VOL_filepath) {
	std::filesystem::path file_path(VOL_filepath);

	VOLData data;

	if (!std::filesystem::exists(file_path)) {
		load_template(data);
		return data;
	}

	std::vector<unsigned char> file_data;
	std::ifstream VOL_fstream;
	VOL_fstream.exceptions(std::ifstream::failbit | std::ifstream::badbit);
	try {
		VOL_fstream.open(VOL_filepath, std::ios::binary);

		std::streampos file_size;
		VOL_fstream.seekg(0, std::ios::end);
		file_size = VOL_fstream.tellg();
		VOL_fstream.seekg(0, std::ios::beg);

		file_data.resize(file_size);
		VOL_fstream.read((char*)&file_data[0], file_size);

		VOL_fstream.close();
	}
	catch (std::ifstream::failure e) {
		std::cerr << "[ERROR] Loading VOL File failed. Loading placeholder." << std::endl;

		load_template(data);
		return data;
	}

	data.name = std::string(VOL_filepath);
	data.name = data.name.substr(0, data.name.find_last_of("."));

	data.resolution.z = get_int(file_data, 0); // changes the slowest
	data.resolution.y = get_int(file_data, 4);
	data.resolution.x = get_int(file_data, 8); // changes the fastest

	data.saved_border = get_int(file_data, 12);
	
	data.true_size.z = get_float(file_data, 16);
	data.true_size.y = get_float(file_data, 20);
	data.true_size.x = get_float(file_data, 24);

	data.values = std::vector<unsigned char>(file_data.begin() + 28, file_data.end());

	return data;
}