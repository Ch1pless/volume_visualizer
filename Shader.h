#pragma once

#include <GL/glew.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <cstdlib>





bool checkShaderCompilation(GLuint shader, std::string shader_type) {
	int success;
	char infoLog[512];
	glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
	if (success == 0) {
		glGetShaderInfoLog(shader, 512, NULL, infoLog);
		std::cerr << "[ERROR] Compiling a " << shader_type << " shader failed.\nError Log: " << infoLog << std::endl;
		return false;
	}
	return true;
}

bool generateShader(GLuint& shader, std::string shader_type) {
	if (shader_type == "FRAGMENT") {
		shader = glCreateShader(GL_FRAGMENT_SHADER);
	}
	else if (shader_type == "VERTEX") {
		shader = glCreateShader(GL_VERTEX_SHADER);
	}
	else if (shader_type == "COMPUTE") {
		shader = glCreateShader(GL_COMPUTE_SHADER);
	}
	else {
		std::cerr << "[ERROR] Unknown shader type: \n" << shader_type << std::endl;
		return false;
	}
	return true;
}

GLuint loadFromFile(const char* shader_file_path, const char* shader_type) {
	GLuint shader;
	std::string stype(shader_type);
	if (!generateShader(shader, stype))
		return 0;

	std::ifstream shader_fstream;
	shader_fstream.exceptions(std::ifstream::failbit | std::ifstream::badbit);
	std::string shader_code;
	try {
		shader_fstream.open(shader_file_path);
		std::stringstream shader_sstream;
		shader_sstream << shader_fstream.rdbuf();
		shader_fstream.close();
		shader_code = shader_sstream.str();
	}
	catch (std::ifstream::failure e) {
		std::cerr << "[ERROR] Could not read shader file." << std::endl;
		return 0;
	}

	const char* shader_cstr = shader_code.c_str();
	// lets source the shader
	glShaderSource(shader, 1, &shader_cstr, NULL);
	glCompileShader(shader);
	// a sanity check for unsuccessful compilations
	if (!checkShaderCompilation(shader, stype)) return 0;
	return shader;
}

GLuint loadFromText(std::string shader_code, const char* shader_type) {
	GLuint shader;
	std::string stype(shader_type);
	if (!generateShader(shader, stype))
		return 0;

	const char* shader_cstr = shader_code.c_str();
	// lets source the shader
	glShaderSource(shader, 1, &shader_cstr, NULL);
	glCompileShader(shader);
	// a sanity check for unsuccessful compilations
	if (!checkShaderCompilation(shader, stype)) return 0;
	return shader;
}

class Program {
	public:
		GLuint program = 0;

		Program() {

		}

		bool checkShaderProgram() {
			int success;
			char infoLog[512];
			glGetProgramiv(program, GL_LINK_STATUS, &success);

			if (success == 0) {
				glGetProgramInfoLog(program, 512, NULL, infoLog);
				std::cerr << "[ERROR] Linking the shader program failed.\nError Log: " << infoLog << std::endl;
				return false;
			}
			return true;
		}


};

class ComputeProgram : public Program {
	public:
		std::vector<GLint> workgroups = std::vector<GLint>(3);
	
		ComputeProgram() {}

		bool generateProgramFromFile(const char* compute_shader_path) {
			std::cout << "Setting up the Compute Program" << std::endl;
			program = glCreateProgram();
			GLuint shader = loadFromFile(compute_shader_path, "COMPUTE");
			glAttachShader(program, shader);
			glLinkProgram(program);
			if (!checkShaderProgram())
				return false;
			glDeleteShader(shader);
			std::cout << "Completed setting up the Compute Program" << std::endl;
			return true;
		}

		bool generateProgramFromText(std::string compute_shader) {
			std::cout << "Setting up the Compute Program" << std::endl;
			program = glCreateProgram();
			GLuint shader = loadFromText(compute_shader, "COMPUTE");
			glAttachShader(program, shader);
			glLinkProgram(program);
			if (!checkShaderProgram())
				return false;
			glDeleteShader(shader);
			std::cout << "Completed setting up the Compute Program" << std::endl;
			return true;
		}
};

class RenderProgram : public Program {
	public:
		RenderProgram() {}

		bool generateProgramFromFile(const char* vert_shader_path, const char* frag_shader_path) {
			std::cout << "Setting up the Render Program" << std::endl;
			program = glCreateProgram();
			GLuint vshader = loadFromFile(vert_shader_path, "VERTEX");
			GLuint fshader = loadFromFile(frag_shader_path, "FRAGMENT");
			glAttachShader(program, vshader);
			glAttachShader(program, fshader);
			glLinkProgram(program);
			if (!checkShaderProgram())
				return false;
			glDeleteShader(vshader);
			glDeleteShader(fshader);
			std::cout << "Completed setting up the Render Program" << std::endl;
			return true;
		}

		bool generateProgramFromText(std::string vertex_shader, const char* fragment_shader) {
			std::cout << "Setting up the Render Program" << std::endl;
			program = glCreateProgram();
			GLuint vshader = loadFromText(vertex_shader, "VERTEX");
			GLuint fshader = loadFromText(fragment_shader, "FRAGMENT");
			glAttachShader(program, vshader);
			glAttachShader(program, fshader);
			glLinkProgram(program);
			if (!checkShaderProgram())
				return false;
			glDeleteShader(vshader);
			glDeleteShader(fshader);
			std::cout << "Completed setting up the Render Program" << std::endl;
			return true;
		}
};



