#version 430 core
in vec2 texCoord;

out vec4 fragColor;

uniform sampler2D u_texture;

void main() { 
	fragColor = vec4(texture(u_texture, texCoord)); 
}
