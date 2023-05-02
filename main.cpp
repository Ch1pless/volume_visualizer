#include <GL/glew.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "imgui.h"
#include "imgui_stdlib.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <iostream>
#include <cstdlib>
#include <limits>

#include "Shader.h"
#include "JSONParser.h"
#include "VOLParser.h"

const bool DEBUG = true;

const GLsizei DEFAULT_WIDTH = 800; 
const GLsizei DEFAULT_HEIGHT = 450; 

GLFWwindow* main_window;
ImGuiIO io;

GLsizei window_width, window_height;
GLuint render_quad, raytracing_result;
ComputeProgram compute;
RenderProgram renderer;
std::vector<GLint> work_group_size(3);
glm::vec3 camera_spherical(5.0, glm::pi<float>() / 2.0, 0.0); // radius, theta, phi

const glm::mat4 IDENTITY_MATRIX(1.0);

// spherical = (radius, theta, phi)
glm::vec3 sphericalToCartesian(glm::vec3 spherical) {
	glm::vec3 result;
	/* physically
	 * result.x = cos(phi)*sinTheta;
	 * result.y = sin(phi)*sinTheta;
	 * result.z = cosTheta;
	*/
	// since y is up, and z is forward
	result.x = glm::sin(spherical.z) * glm::sin(spherical.y);
	result.y = glm::cos(spherical.y);
	result.z = glm::cos(spherical.z) * glm::sin(spherical.y);
	result *= spherical.x;
	return result;
}

std::vector<GLint> calculateWorkGroups(std::vector<GLint>& work_group_size) {
	std::vector<GLint> work_groups(3);
	work_groups[0] = (window_width + work_group_size[0] - 1) / work_group_size[0];
	work_groups[1] = (window_height + work_group_size[1] - 1) / work_group_size[1];
	work_groups[2] = 1;
	return work_groups;
}

/// <summary>
/// Sets up a texture for the compute shader to store the result of raytracing into for displaying later.
/// </summary>
/// <returns>The associated texture id.</returns>
GLuint setupRaytracingResultStorage() {
	if (DEBUG)
		std::cout << "Setting the Image for Storage with the compute shader." << std::endl;
	GLuint texture;
	glGenTextures(1, &texture);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, window_width, window_height, 0, GL_RGBA, GL_FLOAT, NULL);
	glBindTexture(GL_TEXTURE_2D, 0);
	return texture;
}

/// <summary>
/// Callback to dynamically update window viewport to match window size.
/// </summary>
/// <param name="window">Window the callback is tied to and receives updates from.</param>
/// <param name="width">Resized width of window.</param>
/// <param name="height">Resized height of window.</param>
void framebuffer_size_callback(GLFWwindow* window, GLsizei width, GLsizei height) {
	window_width = width, window_height = height;

	compute.workgroups = calculateWorkGroups(work_group_size);
	if (glIsTexture(raytracing_result))
		glDeleteTextures(1, &raytracing_result);
	raytracing_result = setupRaytracingResultStorage();

	glViewport(0, 0, width, height);
}

/// <summary>
/// Key callback that acts upon keys pressed upon the focused window it is tied to.
/// </summary>
/// <param name="window">Window the callback is tied to and receives updates from.</param>
/// <param name="key">Key pressed when focused on window.</param>
/// <param name="scancode"></param>
/// <param name="action"></param>
/// <param name="mods"></param>
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
	if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
		glfwSetWindowShouldClose(window, GLFW_TRUE);
}


glm::vec2 last_mpos(std::numeric_limits<float>::min());
double drag_delay = 0.05, drag_accum = 0.0;
void mouse_cursor_callback(GLFWwindow* window, double xpos, double ypos) {
	ImGuiIO& io = ImGui::GetIO();
	// If the mouse is released or interacting IMGui, ignore or end dragging
	if (io.WantCaptureMouse || glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_RELEASE) {
		last_mpos.x = std::numeric_limits<float>::min(), last_mpos.y = std::numeric_limits<float>::min();
		return;
	}

	if (last_mpos.x == std::numeric_limits<float>::min()) {
		last_mpos.x = xpos, last_mpos.y = ypos;
		drag_accum = glfwGetTime();
	}

	// dragging is activateds
	if (glfwGetTime() - drag_accum > drag_delay) {
		camera_spherical.z += glm::pi<float>() * (last_mpos.x - xpos) / (window_width); // phi is horizontal
		camera_spherical.y += glm::pi<float>() * (last_mpos.y - ypos) / (window_height); // theta is vertical
		camera_spherical.y = glm::clamp<float>(camera_spherical.y, 1e-5, glm::pi<float>() - 1e-5);
		last_mpos.x = xpos, last_mpos.y = ypos;
	}
}

/// <summary>
/// Zooms in based on mouse scrolling by a multiplier to avoid zooming past the origin.
/// </summary>
/// <param name="window">The current GLFW window context.</param>
/// <param name="xdelta">The change in x of the mouse wheel, is not used.</param>
/// <param name="ydelta">The change in y of the mouse wheel, used for zoom.</param>
void mouse_scroll_callback(GLFWwindow* window, double xdelta, double ydelta) {
	if (ydelta < 0) {
		camera_spherical.x /= 0.95;
	}
	else {
		camera_spherical.x *= 0.95;
	}
}

/// <summary>
/// An error callback used with glfw and this project for reporting errors automatically.
/// </summary>
/// <param name="error">The error code.</param>
/// <param name="description">The error traceback and description.</param>
void error_callback(int error, const char* description) {
	std::cerr << "[ERROR] " << description << std::endl;
}

/// <summary>
/// Sets up GLFW and GLEW along with the main window, will return True if successful, False otherwise.
/// </summary>
bool programSetup() {
	glfwSetErrorCallback(error_callback);
	if (!glfwInit()) {
		return false;
	}


	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	// glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

	window_width = DEFAULT_WIDTH, window_height = DEFAULT_HEIGHT;
	main_window = glfwCreateWindow(window_width, window_height, "Volume Visualizer", NULL, NULL);
	if (main_window == NULL) {
		glfwTerminate();
		return false;
	}

	glfwMakeContextCurrent(main_window);

	glewExperimental = GL_TRUE;
	GLenum err = glewInit();
	if (err != GLEW_OK) {
		std::cerr << "[ERROR] Failed to initialize GLEW due to " << glewGetErrorString(err) << std::endl;
		glfwDestroyWindow(main_window);
		glfwTerminate();
		return false;
	}

	if (DEBUG)
		std::cout << "Status: Using GLEW " << glewGetString(GLEW_VERSION) << std::endl;

	glfwSwapInterval(1); // Enables VSYNC
	
	glfwSetFramebufferSizeCallback(main_window, framebuffer_size_callback);
	glViewport(0, 0, window_width, window_height);
	glfwSetWindowPos(main_window, 50, 80);

	glfwSetKeyCallback(main_window, key_callback);

	glfwSetCursorPosCallback(main_window, mouse_cursor_callback);

	glfwSetScrollCallback(main_window, mouse_scroll_callback);
	return true;
}

/// <summary>
/// Sets up ImGui for usage with this project.
/// </summary>
/// <param name="io">The reference to ImGui that is linked to for interacting with.</param>
/// <returns>The result of attempting to setup ImGui.</returns>
bool ImGuiSetup(ImGuiIO& io) {
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	io = ImGui::GetIO();
	(void)io;

	ImGui::StyleColorsDark();

	ImGui_ImplGlfw_InitForOpenGL(main_window, true);
	ImGui_ImplOpenGL3_Init("#version 430");

	ImGui::SetNextWindowSizeConstraints(ImVec2(100, 100), ImVec2(500, 500));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

	return true;
}


/// <summary>
/// A diagnostic function that outputs the hardware components of the system.
/// </summary>
void hardwareDiagnostic() {
	std::vector<GLint> work_group_count(3), work_group_size(3);
	for (int i = 0; i < 3; ++i) {
		glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, i, &work_group_count[i]);
		glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, i, &work_group_size[i]);
	}
	GLint work_group_invocations;
	glGetIntegerv(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS, &work_group_invocations);

	std::cout << "----- Device Hardware Statistics -----" << std::endl;
	std::cout << "  Max Work Group Count: ";
	std::cout << work_group_count[0] << ", " << work_group_count[1] << ", " << work_group_count[2] << std::endl;
	std::cout << "  Max Work Group Size: ";
	std::cout << work_group_size[0] << ", " << work_group_size[1] << ", " << work_group_size[2] << std::endl;
	std::cout << "  Max Work Group Invocations: " << work_group_invocations << std::endl;
	std::cout << "--------------------------------------" << std::endl;
}


/// <summary>
/// Sets up a rendering quad to display the result of raytracing the volume data.
/// </summary>
/// <returns>The vertex array buffer that is associated with the rendering quad.</returns>
GLuint setupRenderingQuad() {
	if (DEBUG)
		std::cout << "Setting Vertices of the rendering quad." << std::endl;
	// set vertices to vao and vbo for rendering quad
	float vertices[] = {
		// 5 float: 3 for viewport position followed by 2 for texture coords 
		-1.0f, -1.0f, 0.0f, 0.0f, 0.0f, // bottom left
		-1.0f, 1.0f,  0.0f, 0.0f, 1.0f, // bottom right
		1.0f,  -1.0f, 0.0f, 1.0f, 0.0f, // top left
		1.0f,  1.0f,  0.0f, 1.0f, 1.0f  // top right
	};
	GLuint vao, vbo;
	glGenVertexArrays(1, &vao);
	glGenBuffers(1, &vbo);

	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), &vertices, GL_STATIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
	glBindVertexArray(0);
	glDeleteBuffers(1, &vbo);
	return vao;
}



/// <summary>
/// Sets up the 3D texture for sampling within the compute shader.
/// </summary>
/// <param name="volume_data">The volume data to store into the texture.</param>
/// <returns>The associated texture id.</returns>
GLuint storeVolumeData(VOLData& volume_data) {
	std::cout << "Setting the Volume for Storage with the compute shader." << std::endl;
	GLuint texture;
	glGenTextures(1, &texture);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_3D, texture);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage3D(
		GL_TEXTURE_3D,
		0,
		GL_R16F,
		volume_data.resolution.x,
		volume_data.resolution.y,
		volume_data.resolution.z,
		0,
		GL_RED,
		GL_UNSIGNED_BYTE,
		&(volume_data.values[0])
	);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

	if (DEBUG) {
		int _x, _y, _z;
		glGetTexLevelParameteriv(GL_TEXTURE_3D, 0, GL_TEXTURE_WIDTH, &_x);
		glGetTexLevelParameteriv(GL_TEXTURE_3D, 0, GL_TEXTURE_HEIGHT, &_y);
		glGetTexLevelParameteriv(GL_TEXTURE_3D, 0, GL_TEXTURE_DEPTH, &_z);
	
		std::cout << "Texture Generated has dimensions: " << _x << " " << _y << " " << _z << std::endl;
	}

	glBindTexture(GL_TEXTURE_3D, 0);
	return texture;
}

int volume_id = 3;

void prepareVolumeData(VOLData& volume_data, glm::mat4& volume_inverse_matrix, GLuint& texture, GLuint compute_program) {
	if (DEBUG)
		printVOLData(volume_data);

	glUseProgram(compute_program);

	// will scale box to shape of true size, but remain in -0.5 to 0.5 range
	glm::mat4 identity(1.0);
	volume_inverse_matrix = glm::mat4(1.0);
	if (volume_data.name == "Foot" || volume_data.name == "Skull") {
		volume_inverse_matrix = glm::rotate(volume_inverse_matrix, glm::radians(-90.0f), glm::vec3(1.0, 0.0, 0.0));
	}
	
	if (volume_data.name == "Frog") {
		volume_inverse_matrix = glm::rotate(volume_inverse_matrix, glm::radians(-90.0f), glm::vec3(0.0, 1.0, 0.0));
		volume_inverse_matrix = glm::rotate(volume_inverse_matrix, glm::radians(90.0f), glm::vec3(1.0, 0.0, 0.0));
	}
	
	volume_inverse_matrix = glm::scale(volume_inverse_matrix, volume_data.true_size);

	volume_inverse_matrix = glm::inverse(volume_inverse_matrix);

	if (glIsTexture(texture))
		glDeleteTextures(1, &texture);
	texture = storeVolumeData(volume_data);
}

void generateVolumeMatrix(glm::mat4& volume_inv_matrix, glm::vec3& rotation, glm::vec3& scaling) {
	volume_inv_matrix = IDENTITY_MATRIX;
	volume_inv_matrix = glm::rotate(volume_inv_matrix, glm::radians(rotation.z), glm::vec3(0.0, 0.0, 1.0));
	volume_inv_matrix = glm::rotate(volume_inv_matrix, glm::radians(rotation.y), glm::vec3(0.0, 1.0, 0.0));
	volume_inv_matrix = glm::rotate(volume_inv_matrix, glm::radians(rotation.x), glm::vec3(1.0, 0.0, 0.0));

	volume_inv_matrix = glm::scale(volume_inv_matrix, scaling);

	volume_inv_matrix = glm::inverse(volume_inv_matrix);
}

/// <summary>
/// Interpolates colors from a range of 0 - 255 mapped values.
/// WARNING: if 0 or 255 are uninitialized, will initialize them to 0 in color_map
/// </summary>
/// <param name="color_map">The map which will have its entries interpolated.</param>
/// <param name="colors">A reference to the array of colors that will store the interpolated colors.</param>
void interpolate_color(std::map<int, glm::vec3>& color_map, std::vector<glm::vec4>& colors) {
	colors.resize(256);

	if (!color_map.count(0)) {
		color_map[0] = glm::vec3(0.0);
	}
	if (!color_map.count(255)) {
		color_map[255] = glm::vec3(0.0);
	}

	int tstart = 0, tdiff = 0;
	glm::vec3 cstart(0.0), cdiff(0.0);

	for (auto &[tend, cend] : color_map) {
		// Skip any entries in the map that are out of bounds.
		if (tend < 0 || tend > 255) continue;

		tdiff = tend - tstart;
		cdiff = cend - cstart;
		
		// interpolate all values between tstart and tend, exclusively
		for (int i = 1; i < tdiff; ++i) {
			float interp = i / (float)tdiff;
			colors[tstart + i].r = (cstart + cdiff * interp).r;
			colors[tstart + i].g = (cstart + cdiff * interp).g;
			colors[tstart + i].b = (cstart + cdiff * interp).b;
		}

		colors[tend].r = cend.r;
		colors[tend].g = cend.g;
		colors[tend].b = cend.b;

		tstart = tend;
		cstart = cend;
	}
}

/// <summary>
/// Interpolates opacity from a range of 0 - 255 mapped values.
/// WARNING: if 0 or 255 are uninitialized, will initialize them to 0 in opacity_map
/// </summary>
/// <param name="opacity_map">The map which will have its entries interpolated.</param>
/// <param name="opacities">A reference to the array of opacities that will store the interpolated opacities.</param>
void interpolate_opacity(std::map<int, float>& opacity_map, std::vector<glm::vec4>& opacities) {
	opacities.resize(256);

	if (!opacity_map.count(0)) {
		opacity_map[0] = 0.0;
	}
	if (!opacity_map.count(255)) {
		opacity_map[255] = 0.0;
	}

	int tstart = 0, tdiff = 0;
	float ostart = 0.0, odiff = 0.0;

	for (auto& [tend, oend] : opacity_map) {
		// Skip any entries in the map that are out of bounds.
		if (tend < 0 || tend > 255) continue;

		tdiff = tend - tstart;
		odiff = oend - ostart;

		// interpolate all values between tstart and tend, exclusively
		for (int i = 1; i < tdiff; ++i) {
			float interp = i / (float)tdiff;
			opacities[tstart + i].a = (ostart + odiff * interp);
		}

		opacities[tend].a = oend;

		tstart = tend;
		ostart = oend;
	}
}

void storeTransferFunction(std::map<int, glm::vec3>& tfunc_color, std::map<int, float>& tfunc_opacity, GLuint& texture, GLuint compute_program) {
	glUseProgram(compute_program);

	if (DEBUG)
		std::cout << "Setting the Transfer Function Storage with the compute shader." << std::endl;
	if (glIsTexture(texture))
		glDeleteTextures(1, &texture);

	glGenTextures(1, &texture);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_1D, texture);
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	std::vector<glm::vec4> full_tfunc(256);

	for (auto [key, value] : tfunc_color) {
		full_tfunc[key] = glm::vec4(value, 0.0);
	}
	for (auto [key, value] : tfunc_opacity) {
		full_tfunc[key].a = value;
	}

	interpolate_color(tfunc_color, full_tfunc);
	interpolate_opacity(tfunc_opacity, full_tfunc);

	glTexImage1D(
		GL_TEXTURE_1D,
		0,
		GL_RGBA16F,
		full_tfunc.size(),
		0,
		GL_RGBA,
		GL_FLOAT,
		&(full_tfunc[0])
	);

	glBindTexture(GL_TEXTURE_1D, 0);
}


int main() {
	if (!programSetup())
		return EXIT_FAILURE;

	if (!ImGuiSetup(io))
		return EXIT_FAILURE;

	if (DEBUG)
		hardwareDiagnostic();

	if (!compute.generateProgramFromFile("compute.comp"))
		return EXIT_FAILURE;

	if (!renderer.generateProgramFromFile("compute.vert", "compute.frag"))
		return EXIT_FAILURE;

	std::vector<const char*> volume_names = { "LargeBuckyball.vol", "Frog.vol", "Foot.vol", "Skull.vol" };

	if (DEBUG)
		std::cout << "Reading in volume data" << std::endl;
	VOLData volume_data = parseVOLDataFromFile(volume_names[volume_id]);

	glGetProgramiv(compute.program, GL_COMPUTE_WORK_GROUP_SIZE, work_group_size.data());

	if (DEBUG) {
		std::cout << "Local Work Group Size: ";
		std::cout << work_group_size[0] << ", " << work_group_size[1] << ", " << work_group_size[2];
		std::cout << std::endl;
	}

	compute.workgroups = calculateWorkGroups(work_group_size);

	if (DEBUG) {
		std::cout << "Compute Work Group Amount: ";
		std::cout << compute.workgroups[0] << ", " << compute.workgroups[1] << ", " << compute.workgroups[2];
		std::cout << std::endl;
	}

	render_quad = setupRenderingQuad();
	raytracing_result = setupRaytracingResultStorage();

	glm::vec3 cam_eye = sphericalToCartesian(camera_spherical),
		cam_target(0),
		cam_up(0, 1, 0);
	GLfloat fov = 70.0;
	GLfloat aspect = window_width / (GLfloat)window_height;

	glm::vec3 w = glm::normalize(cam_eye - cam_target),
		u = glm::normalize(glm::cross(cam_up, w)),
		v = glm::normalize(glm::cross(w, u));

	glm::vec2 canvas;
	canvas.y = 2.0 * tan(fov / 2.0);
	canvas.x = canvas.y * aspect;

	glUseProgram(compute.program);

	glUniform2fv(glGetUniformLocation(compute.program, "u_canvas"), 1, glm::value_ptr(canvas));

	glm::vec2 xslice(0.0, 1.0), yslice(0.0, 1.0), zslice(0.0, 1.0);

	glm::mat4 volume_inv_matrix;
	GLuint volume_texture = 0;
	prepareVolumeData(volume_data, volume_inv_matrix, volume_texture, compute.program);

	std::map<int, glm::vec3> tfunc_color;
	std::map<int, float> tfunc_opacity;

	std::vector<glm::vec4> gui_tfunc(256);

	// Defaults for color and opacity
	tfunc_color[0] = glm::vec3(0.0, 0.0, 0.51);
	tfunc_color[51] = glm::vec3(0.0, 0.24, 0.67);
	tfunc_color[102] = glm::vec3(0.02, 1.0, 1.0);
	tfunc_color[153] = glm::vec3(1.0, 1.0, 0.0);
	tfunc_color[204] = glm::vec3(0.98, 0.0, 0.0);
	tfunc_color[255] = glm::vec3(0.50, 0.0, 0.0);

	tfunc_opacity[75] = 0.0;
	tfunc_opacity[80] = 0.2;
	tfunc_opacity[85] = 0.0;

	tfunc_opacity[125] = 0.0;
	tfunc_opacity[130] = 0.8;
	tfunc_opacity[135] = 0.0;

	// interpolate then set to 5s
	interpolate_color(tfunc_color, gui_tfunc);
	interpolate_opacity(tfunc_opacity, gui_tfunc);

	tfunc_color.clear();
	tfunc_opacity.clear();

	for (int i = 0; i < 256; i += 15) {
		tfunc_color[i].r = gui_tfunc[i].r;
		tfunc_color[i].g = gui_tfunc[i].g;
		tfunc_color[i].b = gui_tfunc[i].b;

	}

	for (int i = 0; i < 256; i += 5) {
		tfunc_opacity[i] = gui_tfunc[i].a;
	}

	auto GLMWrapperColorPicker = [](glm::vec3& color) {
		float color_arr[] = { color.r, color.g, color.b };

		if (!ImGui::ColorEdit3("##v", color_arr, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel))
			return false;


		color.r = color_arr[0];
		color.g = color_arr[1];
		color.b = color_arr[2];

		return true;
	};

	GLuint tfunc_texture = 0;
	storeTransferFunction(tfunc_color, tfunc_opacity, tfunc_texture, compute.program);

	std::string volume_file_path = "File.vol";
	glm::vec3 volume_rotations(0);

	glClearColor(0.0, 0.0, 0.0, 1.0);
	while (!glfwWindowShouldClose(main_window)) {

		// polling
		glfwPollEvents();

		// ImGui rendering
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		ImGui::Begin("Volume Control Panel");

		ImGui::Text("Insert the absolute file path of the volume data you would like to render.");
		if (ImGui::InputText("Volume File", &volume_file_path)) {
			volume_data = parseVOLDataFromFile(volume_file_path.c_str());
			prepareVolumeData(volume_data, volume_inv_matrix, volume_texture, compute.program);
		}

		if (ImGui::SliderFloat3("Rotation xyz", glm::value_ptr(volume_rotations), -180.0, 180.0)) {
			generateVolumeMatrix(volume_inv_matrix, volume_rotations, volume_data.true_size);
		}


		ImGui::Text("Select from a list of template volumes.");
		if (ImGui::Combo("##combo", &volume_id, volume_names.data(), volume_names.size())) {
			volume_data = parseVOLDataFromFile(volume_names[volume_id]);
			prepareVolumeData(volume_data, volume_inv_matrix, volume_texture, compute.program);
		}

		ImGui::Text("X - Slice");

		if (ImGui::DragFloatRange2("##xslice", &xslice.x, &xslice.y, 0.05, 0.0, 1.0, "%.2f \%")) {
			// do nothing
		}

		ImGui::Text("Y - Slice");

		if (ImGui::DragFloatRange2("##yslice", &yslice.x, &yslice.y, 0.05, 0.0, 1.0, "%.2f \%")) {
			// do nothing
		}

		ImGui::Text("Z - Slice");

		if (ImGui::DragFloatRange2("##zslice", &zslice.x, &zslice.y, 0.05, 0.0, 1.0, "%.2f \%")) {
			// do nothing
		}


		ImGui::Text("Color Pickers");
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(25.75, 4));
		ImGui::PushID("color_pickers");
		for (int i = 0; i < 256; i += 15) {
			if (i > 0) ImGui::SameLine();
			ImGui::PushID(i);
			if (GLMWrapperColorPicker(tfunc_color[i])) {
				storeTransferFunction(tfunc_color, tfunc_opacity, tfunc_texture, compute.program);
			}
			ImGui::PopID();
		}
		ImGui::PopID();
		ImGui::PopStyleVar();

		ImGui::Text("Opacity Sliders");
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 4));
		ImGui::PushID("opacity_sliders");
		for (int i = 0; i < 256; i += 5) {
			if (i > 0) ImGui::SameLine();
			ImGui::PushID(i);
			if (ImGui::VSliderFloat("##v", ImVec2(15, 100), &tfunc_opacity[i], 0.0, 1.0, "")) {
				storeTransferFunction(tfunc_color, tfunc_opacity, tfunc_texture, compute.program);
			}
			ImGui::PopID();
		}
		ImGui::PopID();
		ImGui::PopStyleVar();

		ImGui::End();

		ImGui::Render();


		glClear(GL_COLOR_BUFFER_BIT);

		// compute
		glUseProgram(compute.program);

		glBindImageTexture(0, raytracing_result, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);

		cam_eye = sphericalToCartesian(camera_spherical);
		w = glm::normalize(cam_eye - cam_target);
		u = glm::normalize(glm::cross(cam_up, w));
		v = glm::normalize(glm::cross(w, u));

		glUniform3fv(glGetUniformLocation(compute.program, "u_cam_eye"), 1, glm::value_ptr(cam_eye));
		glUniform3fv(glGetUniformLocation(compute.program, "u_cam_w"), 1, glm::value_ptr(w));
		glUniform3fv(glGetUniformLocation(compute.program, "u_cam_u"), 1, glm::value_ptr(u));
		glUniform3fv(glGetUniformLocation(compute.program, "u_cam_v"), 1, glm::value_ptr(v));

		glUniform2fv(glGetUniformLocation(compute.program, "u_xslice"), 1, glm::value_ptr(xslice));
		glUniform2fv(glGetUniformLocation(compute.program, "u_yslice"), 1, glm::value_ptr(yslice));
		glUniform2fv(glGetUniformLocation(compute.program, "u_zslice"), 1, glm::value_ptr(zslice));

		glUniform3fv(glGetUniformLocation(compute.program, "u_volume_true_size"), 1, glm::value_ptr(volume_data.true_size));

		glUniformMatrix4fv(glGetUniformLocation(compute.program, "u_volume_inv_matrix"), 1, GL_FALSE, glm::value_ptr(volume_inv_matrix));

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_3D, volume_texture);

		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_1D, tfunc_texture);

		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

		glDispatchCompute(compute.workgroups[0], compute.workgroups[1], compute.workgroups[2]);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

		// rendering
		glClear(GL_COLOR_BUFFER_BIT);
		glUseProgram(renderer.program);

		glBindVertexArray(render_quad);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, raytracing_result);

		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

		glBindVertexArray(0);
		glBindTexture(GL_TEXTURE_2D, 0);

		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		glfwSwapBuffers(main_window);


	}

	glDeleteTextures(1, &raytracing_result);
	glDeleteProgram(renderer.program);
	glDeleteProgram(compute.program);

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	glfwDestroyWindow(main_window);
	glfwTerminate();
	return EXIT_SUCCESS;
}