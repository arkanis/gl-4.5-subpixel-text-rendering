#include <stdbool.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>

#define GLAD_GL_IMPLEMENTATION
#include <gl45.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include <SDL/SDL.h>


// 
// Some utilities and OpenGL helper functions I cooked up over the years
// 

typedef struct {
	const char* buffer;
	const char* end;
	uint32_t    codepoint;
} utf8_iterator_t;

utf8_iterator_t utf8_next(utf8_iterator_t it) {
	it.codepoint = 0;
	// We're at the end of string, return 0 as code point, without dereferencing the buffer
	if (it.buffer == it.end)
		return it;
	
	uint8_t byte = *it.buffer;
	// We're at the zero terminator, return 0 as code point
	if (byte == 0)
		return it;
	it.buffer++;
	
	// __builtin_clz() counts the leading zeros but we want the leading ones. Therefore flip
	// all bits (~).
	// __builtin_clz() works on 32 bit ints, but we only want the leading one bits of our
	// 8 bit byte. Therefore put the byte at the highest order bits of the int (<< 24).
	int leading_ones = __builtin_clz(~byte << 24);
	
	if (leading_ones != 1) {
		// Store the data bits of the first byte in the code point
		int data_bits_in_first_byte = 8 - 1 - leading_ones;
		it.codepoint = byte & ~(0xFFFFFFFF << data_bits_in_first_byte);
		
		ssize_t additional_bytes = leading_ones - 1;
		// additional_bytes is -1 when we have no further bytes for this code point (got a one byte
		// code point). This value is actually wrong (should be 0) but we don't need any special
		// handling for that case. The compare and loop both use signed compares so we're fine.
		// The for loop is completely skipped in that case, too.
		
		if (it.buffer + additional_bytes <= it.end) {
			for(ssize_t i = 0; i < additional_bytes; i++) {
				byte = *it.buffer;
				
				if ( (byte & 0xC0) == 0x80 ) {
					// Make room in it.codepoint for 6 more bits and OR the current bytes data
					// bits in there.
					it.codepoint <<= 6;
					it.codepoint |= byte & 0x3F;
				} else {
					// Error, this isn't an itermediate byte! It's either the zero terminator or
					// the start of a new code point. In both cases we'll return the replacement
					// character to signal the current broken code point. Leave the buffer at the
					// current position so the next call sees either the new code point or the
					// zero terminator.
					it.codepoint = 0xFFFD;
					break;
				}
				
				it.buffer++;
			}
		} else {
			// Error, buffer doesn't contain all the bytes of this code point. Return the replacement
			// character and set the buffer to the end.
			it.codepoint = 0xFFFD;
			it.buffer = it.end;
		}
	} else {
		// Error, we're at an intermediate byte.
		// Skip all intermediate bytes (or to the end of the buffer) and return the replacement
		// character.
		while ( (*(it.buffer) & 0xC0) == 0x80 && it.buffer < it.end )
			it.buffer++;
		it.codepoint = 0xFFFD;
	}
	
	return it;
}

utf8_iterator_t utf8_first(const char* buffer) {
	// Use the highest possible memory addess as end (more or less UINTPTR_MAX)
	// so the size checks don't hit.
	return utf8_next((utf8_iterator_t){
		.buffer = buffer,
		.end    = (const char*)UINTPTR_MAX,
		.codepoint = 0
	});
}

/**
 * Returns a pointer to the zero terminated `malloc()`ed contents of the file. If size is
 * not `NULL` it's target is set to the size of the file not including the zero terminator
 * at the end of the memory block.
 * 
 * On error `NULL` is returned and `errno` is set accordingly.
 */
void* fload(const char* filename, size_t* size) {
	long filesize = 0;
	char* data = NULL;
	int error = -1;
	
	FILE* f = fopen(filename, "rb");
	if (f == NULL)
		return NULL;
	
	if ( fseek(f, 0, SEEK_END)              == -1       ) goto fail;
	if ( (filesize = ftell(f))              == -1       ) goto fail;
	if ( fseek(f, 0, SEEK_SET)              == -1       ) goto fail;
	if ( (data = malloc(filesize + 1))      == NULL     ) goto fail;
	// TODO: proper error detection for fread and get proper error code with ferror
	if ( (long)fread(data, 1, filesize, f)  != filesize ) goto free_and_fail;
	fclose(f);
	
	data[filesize] = '\0';
	if (size)
		*size = filesize;
	return (void*)data;
	
	free_and_fail:
		error = errno;
		free(data);
	
	fail:
		if (error == -1)
			error = errno;
		fclose(f);
	
	errno = error;
	return NULL;
}

void gl_debug_callback(GLenum src, GLenum type, GLuint id, GLenum severity, GLsizei length, GLchar const* msg, void const* user_param) {
	const char *src_str = NULL, *type_str = NULL, *severity_str = NULL;
	
	switch (src) {
		case GL_DEBUG_SOURCE_API:             src_str = "API";             break;
		case GL_DEBUG_SOURCE_WINDOW_SYSTEM:   src_str = "WINDOW SYSTEM";   break;
		case GL_DEBUG_SOURCE_SHADER_COMPILER: src_str = "SHADER COMPILER"; break;
		case GL_DEBUG_SOURCE_THIRD_PARTY:     src_str = "THIRD PARTY";     break;
		case GL_DEBUG_SOURCE_APPLICATION:     src_str = "APPLICATION";     break;
		case GL_DEBUG_SOURCE_OTHER:           src_str = "OTHER";           break;
	}
	switch (type) {
		case GL_DEBUG_TYPE_ERROR:               type_str = "ERROR";               break;
		case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: type_str = "DEPRECATED_BEHAVIOR"; break;
		case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:  type_str = "UNDEFINED_BEHAVIOR";  break;
		case GL_DEBUG_TYPE_PORTABILITY:         type_str = "PORTABILITY";         break;
		case GL_DEBUG_TYPE_PERFORMANCE:         type_str = "PERFORMANCE";         break;
		case GL_DEBUG_TYPE_MARKER:              type_str = "MARKER";              break;
		case GL_DEBUG_TYPE_OTHER:               type_str = "OTHER";               break;
	}
	switch (severity) {
		case GL_DEBUG_SEVERITY_NOTIFICATION: severity_str = "NOTIFICATION"; break;
		case GL_DEBUG_SEVERITY_LOW:          severity_str = "LOW";          break;
		case GL_DEBUG_SEVERITY_MEDIUM:       severity_str = "MEDIUM";       break;
		case GL_DEBUG_SEVERITY_HIGH:         severity_str = "HIGH";         break;
	}
	
	fprintf(stderr, "[GL %s %s %s] %u: %s\n", src_str, type_str, severity_str, id, msg);
}

void gl_init_debug_log() {
	glEnable(GL_DEBUG_OUTPUT);
	// Uncomment this if you want to debug into your OpenGL driver by setting a breakpoint into the message callback below
	//glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
	glDebugMessageCallback(gl_debug_callback, NULL);
	glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, NULL, GL_TRUE);
}

GLuint gl_load_shader_program(const char* vertex_shader_code, const char* fragment_shader_code) {
	void fprint_shader_source_with_line_numbers(FILE* f, const char* source, int error_line_number) {
		int line_number = 1;
		const char *line_start = source;
		while (*line_start != '\0') {
			const char* line_end = line_start;
			while ( !(*line_end == '\n' || *line_end == '\0') )
				line_end++;
			
			// Print the line if no error line number was given (aka print all lines), or if the line number is close
			// to the given error line number.
			if ( error_line_number == -1 || abs(line_number - error_line_number) < 5 )
				fprintf(f, "%3d: %.*s\n", line_number, (int)(line_end - line_start), line_start);
			line_number++;
			
			line_start = (*line_end == '\n') ? line_end + 1 : line_end;
		}
	}
	
	int compile_and_attach_shader(GLenum gl_shader_type, const char* code, GLuint program, const char* shader_type_name) {
		GLuint shader = glCreateShader(gl_shader_type);
		glShaderSource(shader, 1, (const char*[]){ code }, NULL);
		glCompileShader(shader);
		
		GLint is_compiled = GL_FALSE;
		glGetShaderiv(shader, GL_COMPILE_STATUS, &is_compiled);
		if (is_compiled) {
			glAttachShader(program, shader);
			glDeleteShader(shader);
			return GL_TRUE;
		} else {
			GLint log_size = 0;
			glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_size);
			char* log_buffer = malloc(log_size);
				glGetShaderInfoLog(shader, log_size, NULL, log_buffer);
				fprintf(stderr, "ERROR on compiling %s:\n%s\n", shader_type_name, log_buffer);
				
				// Try to extract the line number from the first error.
				// Example error from Linux AMD driver: "0:136(45): error: no function with name 'color_srgb_to_linear'".
				// Not sure what the first "0" is supposed to mean. On nVidia it seems to be the source string index in case that glShaderSource()
				// is passed multiple strings. But on AMD this seems to stay 0 in that case. So in case of multiple source strings we would have
				// to concat everything together into one string.
				// If sscanf() fails it just leaves -1 in line_number and fprint_shader_source_with_line_numbers() then ignores that argument.
				int line_number = -1;
				sscanf(log_buffer, "%*u:%u", &line_number);
				
				fprintf(stderr, "Shader source:\n");
				fprint_shader_source_with_line_numbers(stderr, code, line_number);
			free(log_buffer);
			
			glDeleteShader(shader);
			return GL_FALSE;
		}
	}
	
	GLuint program = glCreateProgram();
	if ( ! compile_and_attach_shader(GL_VERTEX_SHADER, vertex_shader_code, program, "vertex shader") )
		goto fail;
	if ( ! compile_and_attach_shader(GL_FRAGMENT_SHADER, fragment_shader_code, program, "fragment shader") )
		goto fail;
	
	// Note: Error reporting needed since linker errors (like missing local group size) are not reported as OpenGL errors
	glLinkProgram(program);
	GLint is_linked = GL_FALSE;
	glGetProgramiv(program, GL_LINK_STATUS, &is_linked);
	if (is_linked) {
		return program;
	} else {
		GLint log_size = GL_FALSE;
		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_size);
		char* log_buffer = malloc(log_size);
			glGetProgramInfoLog(program, log_size, NULL, log_buffer);
			fprintf(stderr, "ERROR on linking shader:\n%s\n", log_buffer);
		free(log_buffer);
		
		fprintf(stderr, "Vertex source code:\n");
		fprint_shader_source_with_line_numbers(stderr, vertex_shader_code, -1);
		fprintf(stderr, "Fragment shader code:\n");
		fprint_shader_source_with_line_numbers(stderr, fragment_shader_code, -1);
		
		goto fail;
	}
	
	fail:
		glDeleteProgram(program);
		return 0;
}


//
// Main program. Only renders one string.
//

int main(int, char**) {
	SDL_Init(SDL_INIT_VIDEO);
	atexit(SDL_Quit);
	
	
	// Init window and OpenGL context
	int window_width = 400, window_height = 100;
	SDL_Window* window = SDL_CreateWindow("Minimal subpixel font rendering", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, window_width, window_height, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
	
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GLContext gl_ctx = SDL_GL_CreateContext(window);
	SDL_GL_SetSwapInterval(1);
	
	gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress); // Expects a function that returns a function pointer, but SDL_GL_GetProcAddress() just returns a void pointer. Hence the cast.
	gl_init_debug_log();
	
	
	// Setup stuff to render rectangles with OpenGL.
	// Use instancing to render the rects. One VBO that contains the data for a single rectangle instance, and another
	// one with all the per-instance data (stuff that is unique for each rect).
	// First setup the vertex and fragment shader, then setup the buffers and their formats and finally the vertex array
	// object that reads the buffers and feeds that data into the vertex shader.
	GLuint shader_program = gl_load_shader_program(
		// Vertex shader
			"#version 450 core\n"
			"\n"
			"layout(location = 0) uniform vec2 half_viewport_size;\n"
			"\n"
			"layout(location = 0) in uvec2 ltrb_index;\n"
			"layout(location = 1) in vec4  rect_ltrb;\n"
			"layout(location = 2) in vec4  rect_tex_ltrb;\n"
			"layout(location = 3) in vec4  rect_color;\n"
			"layout(location = 4) in float rect_subpixel_shift;\n"
			"\n"
			"out vec2  tex_coords;\n"
			"out vec4  color;\n"
			"out float subpixel_shift;\n"
			"\n"
			"void main() {\n"
			"	// Convert color to pre-multiplied alpha\n"
			"	color = vec4(rect_color.rgb * rect_color.a, rect_color.a);\n"
			"	\n"
			"	vec2 pos   = vec2(rect_ltrb[ltrb_index.x],     rect_ltrb[ltrb_index.y]);\n"
			"	tex_coords = vec2(rect_tex_ltrb[ltrb_index.x], rect_tex_ltrb[ltrb_index.y]);\n"
			"	subpixel_shift = rect_subpixel_shift;"
			"	\n"
			"	vec2 axes_flip  = vec2(1, -1);  // to flip y axis from bottom-up (OpenGL standard) to top-down (normal for UIs)\n"
			"	vec2 pos_in_ndc = (pos / half_viewport_size - 1.0) * axes_flip;\n"
			"	gl_Position = vec4(pos_in_ndc, 0, 1);\n"
			"}\n"
		,
		// Fragment shader
			"#version 450 core\n"
			"\n"
			"layout(location = 1) uniform float coverage_adjustment;\n"
			"\n"
			"// Note: binding is the number of the texture unit, not the uniform location. We don't care about the uniform location\n"
			"// since we already set the texture unit via the binding here and don't have to set it via OpenGL as a uniform.\n"
			"layout(binding = 0) uniform sampler2DRect glyph_atlas;\n"
			"\n"
			"in      vec2  tex_coords;\n"
			"in flat vec4  color;\n"
			"in flat float subpixel_shift;\n"
			"\n"
			"// Use dual-source blending to blend individual color components with different weights instead of just one weight (alpha) for the entire pixel\n"
			"layout(location = 0, index = 0) out vec4 fragment_color;\n"
			"layout(location = 0, index = 1) out vec4 blend_weights;\n"
			"\n"
			"void main() {\n"
			"	// Shift the subpixel weights according to the subpixel position of this specific glyph (the atlas only contains the glyph with a subpixel shift of 0)\n"
			"	// Based on the shifting code from the paper Higher Quality 2D Text Rendering by Nicolas P. Rougier, Listing 2. Subpixel positioning fragment shader, from https://jcgt.org/published/0002/01/04/paper.pdf\n"
			"	vec3 current  = texelFetch(glyph_atlas, ivec2(tex_coords) + ivec2( 0, 0)).rgb;\n"
			"	vec3 previous = texelFetch(glyph_atlas, ivec2(tex_coords) + ivec2(-1, 0)).rgb;\n"
			"	float r = current.r, g = current.g, b = current.b;\n"
			"	if (subpixel_shift <= 1.0/3.0) {\n"
			"		float z = 3.0 * subpixel_shift;\n"
			"		r = mix(current.r, previous.b, z);\n"
			"		g = mix(current.g, current.r, z);\n"
			"		b = mix(current.b, current.g, z);\n"
			"	} else if (subpixel_shift <= 2.0/3.0) {\n"
			"		float z = 3.0 * subpixel_shift - 1.0;\n"
			"		r = mix(previous.b, previous.g, z);\n"
			"		g = mix(current.r,  previous.b, z);\n"
			"		b = mix(current.g,  current.r,  z);\n"
			"	} else if (subpixel_shift < 1.0) {\n"
			"		float z = 3.0 * subpixel_shift - 2.0;\n"
			"		r = mix(previous.g, previous.r, z);\n"
			"		g = mix(previous.b, previous.g, z);\n"
			"		b = mix(current.r,  previous.b, z);\n"
			"	}\n"
			"	vec3 pixel_coverages = vec3(r, g, b);\n"
			"	\n"
			"	// Coverage adjustment variant 1: Increase or decrease the slope of the gradient by a linear factor.\n"
			"	// Gives sharper results than variant 2 but overdoing it degrades quality quickly.\n"
			"	// coverage_adjustment = 0: does nothing\n"
			"	// coverage_adjustment = +0.2: makes the glyphs slightly bolder (multiply slope by 1.2 with coverage 0 as reference point)\n"
			"	// coverage_adjustment = -0.2: makes them slightly thinner (multiply slope by 1.2 with coverage 1 as reference point)\n"
			"	if (coverage_adjustment >= 0) {\n"
			"		pixel_coverages = min(pixel_coverages * (1 + coverage_adjustment), 1);\n"
			"	} else {\n"
			"		pixel_coverages = max((1 - (1 - pixel_coverages) * (1 + -coverage_adjustment)), 0);\n"
			"	}\n"
			"	\n"
			"	// Coverage adjustment variant 2: Use a power function to distort the coverages toward higher or lower values.\n"
			"	// Note: The code might look similar to gamma correction \n"
			"	// coverage_adjustment = 1.0: does nothing\n"
			"	// coverage_adjustment = 0.80: makes the glyphs slightly bolder, nice for source code, etc.\n"
			"	// coverage_adjustment = 1.20: makes them slightly thinner, but can make bright text on bright backgrounds harder to read.\n"
			"	// coverage_adjustment = 2.2 and 0.45: Gives you the look of text distorted by gamma correction (2.2 for black on white, 0.45 = 1/2.2 for white on black).\n"
			"	// Comment variant 1 and uncomment this one to give it a try.\n"
			"	//pixel_coverages = pow(pixel_coverages, vec3(coverage_adjustment));\n"
			"	\n"
			"	// Use dual-source blending to blend each subpixel (color channel) individually.\n"
			"	// Note: The blend equation is setup for pre-multiplied alpha blending. color is already pre-multiplied in the vertex shader.\n"
			"	// color * vec4(pixel_coverages, 1) gives us a color mask where all subpixels of the glyph have the proper values for the text\n"
			"	// color and all other subpixels are 0. This is what we add to the framebuffer (since color is pre-multiplied).\n"
			"	// The blend weights are then set to remove the portion of the background we no longer want. The blend equation does a 1 - alpha\n"
			"	// for each channel so here we set the weights to the part that the glyph color contributes. But only where the glyph actually"
			"	// covers the subpixels, thats what color.a * pixel_coverages does.\n"
			"	fragment_color = color * vec4(pixel_coverages, 1);\n"
			"	blend_weights = vec4(color.a * pixel_coverages, color.a);\n"
			"}\n"
	);
	if (!shader_program)
		return 1;
	
	// Small fixed buffer that just contains the 6 vertices (two triangles) making up one rectange.
	// Note: ltrb is short for left, top, right , bottom and those coordinates are used to describe a rectangle on the
	// screen. Requires only half the data as 4 complete points. Thats mostly for rect_instance_t below but here we use
	// the same convention.
	struct { uint16_t ltrb_index_x, ltrb_index_y; } rect_vertices[] = {
		{ 0, 1 }, // left  top
		{ 0, 3 }, // left  bottom
		{ 2, 1 }, // right top
		{ 0, 3 }, // left  bottom
		{ 2, 3 }, // right bottom
		{ 2, 1 }, // right top
	};
	GLuint rect_vertices_vbo = 0;
	glCreateBuffers(1, &rect_vertices_vbo);
	glNamedBufferStorage(rect_vertices_vbo, sizeof(rect_vertices), rect_vertices, 0);
	
	// Data format and CPU-side buffer for the per-rectangle information.
	// Here we just use one fixed size rect_buffer for demonstration purposes.
	typedef struct { int16_t left, top, right, bottom; } int16_rect_t;
	typedef struct { uint8_t r, g, b, a; } color_t;
	typedef struct {
		int16_rect_t pos;
		int16_rect_t tex_coords;
		color_t      color;
		float        subpixel_shift;
	} rect_instance_t;
	
	int rect_buffer_filled = 0;
	rect_instance_t rect_buffer[255];
	
	GLuint rect_instances_vbo = 0;
	glCreateBuffers(1, &rect_instances_vbo);
	
	// Create the vertex array object (VAO) that reads one entry from rect_vertices_vbo for each vertex and one entry
	// from rect_instances_vbo for each instance and feeds the data into the vertex shader.
	GLuint vao = 0;
	glCreateVertexArrays(1, &vao);
		glVertexArrayVertexBuffer(vao, 0, rect_vertices_vbo,  0, sizeof(rect_vertices[0]));  // Set data source 0 to rect_vertices_vbo, with offset 0 and proper stride
		glVertexArrayVertexBuffer(vao, 1, rect_instances_vbo, 0, sizeof(rect_instance_t));   // Set data source 1 to rect_instances_vbo, with offset 0 and proper stride
		glVertexArrayBindingDivisor(vao, 1, 1);  // Advance data source 1 every 1 instance instead of for every vertex (3rd argument is 1 instead of 0)
	// layout(location = 0) in uvec2 ltrb_index
		glEnableVertexArrayAttrib( vao, 0);     // read ltrb_index from a data source
		glVertexArrayAttribBinding(vao, 0, 0);  // read from data source 0
		glVertexArrayAttribIFormat(vao, 0, 2, GL_UNSIGNED_SHORT, 0);  // read 2 unsigned shorts starting at offset 0 and feed it into the vertex shader as integers instead of float (that's what the I means in glVertexArrayAttribIFormat)
	// layout(location = 1) in vec4  rect_ltrb
		glEnableVertexArrayAttrib( vao, 1);     // read it from a data source
		glVertexArrayAttribBinding(vao, 1, 1);  // read from data source 1
		glVertexArrayAttribFormat( vao, 1, 4, GL_SHORT, false, offsetof(rect_instance_t, pos));
	// layout(location = 2) in vec4  rect_tex_ltrb
		glEnableVertexArrayAttrib( vao, 2);     // read it from a data source
		glVertexArrayAttribBinding(vao, 2, 1);  // read from data source 1
		glVertexArrayAttribFormat( vao, 2, 4, GL_SHORT, false, offsetof(rect_instance_t, tex_coords));
	// layout(location = 3) in vec4  rect_color
		glEnableVertexArrayAttrib( vao, 3);     // read it from a data source
		glVertexArrayAttribBinding(vao, 3, 1);  // read from data source 1
		glVertexArrayAttribFormat( vao, 3, 4, GL_UNSIGNED_BYTE, true, offsetof(rect_instance_t, color));  // read 4 unsigned bytes starting at the offset of the "color" member, convert them to float and normalize the value range 0..255 to 0..1.
	// layout(location = 4) in float rect_subpixel_shift
		glEnableVertexArrayAttrib( vao, 4);     // read it from a data source
		glVertexArrayAttribBinding(vao, 4, 1);  // read from data source 1
		glVertexArrayAttribFormat( vao, 4, 1, GL_FLOAT, false, offsetof(rect_instance_t, subpixel_shift));
	
	
	// A simple mockup of an atlas allocator that you would use to allocate and manage small glyph rectangles in the
	// atlas texture. The mockup uses the codepoint of a character as an index and stores the relevant glyph data there,
	// e.g. glyph_atlas_item_t atlas_item = glyph_atlas_codepoint_to_tex_coords['H'].
	// Additionally we just make each atlas item 32x32 pixel in size. The position in the altas texture is also derived
	// from the codepoint / index: Simple left to right and top to bottom stacking.
	// You wouldn't want such a lousy atlas allocator for anything real. It can only handle the first 127 codepoints
	// (that includes just basic ASCII), can only manage one version of a glyph (no different font sizes) and it wastes
	// phenomenal amounts of space. But it's ok for demonstration purposes while being simple enough to not distract
	// from the font rendering itself.
	// It uses a GL_TEXTURE_RECTANGLE so we can use pixel coordinates instead of coordinates in the range 0..1. But that
	// doesn't really matter since we use texelFetch() in the fragment shader and that works on integer coordinates
	// anyway. Rectangle textures can't have mipmaps but we don't want them for the glyph atlas.
	uint32_t glyph_atlas_width = 512, glyph_atlas_height = 512;
	GLuint glyph_atlas_texture = 0;
	glCreateTextures(GL_TEXTURE_RECTANGLE, 1, &glyph_atlas_texture);
	glTextureStorage2D(glyph_atlas_texture, 1, GL_RGB8, glyph_atlas_width, glyph_atlas_height);
	
	typedef struct { bool filled; int16_rect_t tex_coords; int glyph_index, distance_from_baseline_to_top_px; } glyph_atlas_item_t;
	glyph_atlas_item_t glyph_atlas_items[127] = {};
	
	
	// Load the example font
	void* font_data = fload("Ubuntu-R.ttf", NULL);
	stbtt_fontinfo font_info;
	stbtt_InitFont(&font_info, font_data, 0);
	
	
	bool quit = false;
	while(!quit) {
		// Wait for anything to happen
		SDL_WaitEvent(NULL);
		
		// Process all pending events
		SDL_Event event;
		bool redraw = false;
		while( SDL_PollEvent(&event) ) {
			if (event.type == SDL_QUIT) {
				quit = true;
				break;
			} else if ( event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_EXPOSED ) {
				redraw = true;
			} else if ( event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_RESIZED ) {
				window_width = event.window.data1;
				window_height = event.window.data2;
				glViewport(0, 0, window_width, window_height);
				redraw = true;
			}
		}
		
		// Redraw if necessary
		if (redraw) {
			// Parameters for drawing the example text
			float font_size_pt = 10, pos_x = 10, pos_y = 10, coverage_adjustment = 0.0;
			color_t text_color = (color_t){218, 218, 218, 255};
			const char* text = "The quick brown fox jumps over the lazy dog.";
			
			// Put every glpyh in text into rect_buffer 
			{
				// Get the font metrics, the the stbtt_ScaleForMappingEmToPixels() and stbtt_GetFontVMetrics() documentation
				// for details.
				// 
				// From "Font Size in Pixels or Points" in stb_truetype.h
				// > Windows traditionally uses a convention that there are 96 pixels per inch, thus making 'inch'
				// > measurements have nothing to do with inches, and thus effectively defining a point to be 1.333 pixels.
				float font_size_px = font_size_pt * 1.333333;
				float font_scale = stbtt_ScaleForMappingEmToPixels(&font_info, font_size_px);
				
				int font_ascent = 0, font_descent = 0, font_line_gap = 0;
				stbtt_GetFontVMetrics(&font_info, &font_ascent, &font_descent, &font_line_gap);
				float line_height = (font_ascent - font_descent + font_line_gap) * font_scale;  // Based on the docs of stbtt_GetFontVMetrics()
				float baseline = font_ascent * font_scale;
				
				// Keep track of the current position while we process glyph after glyph
				float current_x = pos_x;
				float current_y = pos_y + round(baseline);
				
				// Iterate over the UTF-8 text codepoint by codepoint. A codepoint is basically the 32 bit ID of a character
				// as defined by Unicode.
				uint32_t prev_codepoint = 0;
				for(utf8_iterator_t it = utf8_first(text); it.codepoint != 0; it = utf8_next(it)) {
					uint32_t codepoint = it.codepoint;
					
					// Apply kerning
					if (prev_codepoint)
						current_x += stbtt_GetCodepointKernAdvance(&font_info, prev_codepoint, codepoint) * font_scale;
					prev_codepoint = codepoint;
					
					if (codepoint == '\n') {
						// Handle line breaks
						current_x = pos_x;
						current_y += round(line_height);
					} else {
						int horizontal_filter_padding = 1, subpixel_positioning_left_padding = 1;
						
						// Check if that glyph is already in the glyph atlas
						assert(codepoint <= 127);
						glyph_atlas_item_t glyph_atlas_item = glyph_atlas_items[codepoint];
						if (glyph_atlas_item.filled) {
							// The atlas item for this codepoint is already filled, so we already rasterized the glyph, put it in the atlas texture and stored
							// the relevant data in an atlas item. Everything is already done, so just use the atlas item.
						} else {
							// The atlas item is not yet filled, meaning the glyph hasn't been rasterized yet. So we do that now and put it into the glyph atlas.
							
							// Find the glyph index first for faster lookup in the following functions. Otherwise stb_truetype has to search through a translation
							// table from codepoint to index at each call.
							int glyph_index = stbtt_FindGlyphIndex(&font_info, codepoint);
							
							// Get glyph dimensions, see stbtt_GetGlyphBitmapBox() and stbtt_GetCodepointBitmapBox() for details.
							int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
							stbtt_GetGlyphBitmapBox(&font_info, glyph_index, font_scale, font_scale, &x0, &y0, &x1, &y1);
							int glyph_width_px = x1 - x0, glyph_height_px = y1 - y0;
							int distance_from_baseline_to_top_px = -y0;  // y0 from stbtt_GetGlyphBitmapBox() is negative (e.g. -11), that's why we flip it here.
							
							// Only render glyphs that actually have some visual representation (skip spaces, etc.)
							if (glyph_width_px > 0 && glyph_height_px > 0) {
								int padded_glyph_width_px  = subpixel_positioning_left_padding + horizontal_filter_padding + glyph_width_px + horizontal_filter_padding;
								int padded_glyph_height_px = glyph_height_px;
								
								// Here you would usually ask the glyph atlas to allocate a region with the size of padded_glyph_width_px and padded_glyph_height_px size.
								// If the atlas is already full you would render all the rectangles already in the buffer because they expect that their glyphs are in the
								// texture atlas. After that is done we can clear out old glyphs to make room for our new glyph here and continue on rendering the text.
								
								// Instead we just use our mockup atlas allocator. Every region in there is 32x32 in size and we assume that the padded glyph fits inside.
								// The position in the atlas texture is derived from the codepoint. Just putting all lower 128 ASCII chars left to right and top to bottom
								// in the atlas.
								// AGAIN: Don't use this for anything other than demonstration purposes. It's horribly limited and inefficient!
								int atlas_item_width = 32, atlas_item_height = 32;
								int atlas_item_x = (codepoint % (glyph_atlas_width  / atlas_item_width )) * atlas_item_width;
								int atlas_item_y = (codepoint / (glyph_atlas_height / atlas_item_height)) * atlas_item_height;
								assert(padded_glyph_width_px <= atlas_item_width && padded_glyph_height_px <= atlas_item_height);
								
								// Create an RGB bitmap with the size of the atlas item and rasterize the glyph into it.
								// This is larger than need be, but avoids coordinate transformation and range checks when applying the FreeType LCD filter below. Also
								// initialize it to zeor for the same reason. We rasterize the glyph as an grayscale image with 3x the horizontal resolution so we have
								// one coverage (grayscale) value for each subpixel.
								// Note: You probably don't want to allocate and free a bitmap each time we render a glyph. You can create a permanent scratch buffer
								// with the maximum glyph size or resize it on demand. We alloc and free here just for demonstration purposes.
								int horizontal_resolution = 3;
								int bitmap_stride     = atlas_item_width * horizontal_resolution;
								int bitmap_size       = bitmap_stride * atlas_item_height;
								uint8_t* glyph_bitmap = calloc(1, bitmap_size);
								// Position of the rasterized glyph within the atlas item when padding is taken into account
								int glyph_offset_x = (subpixel_positioning_left_padding + horizontal_filter_padding) * horizontal_resolution;
								// Rasterize the glyph into glyph_bitmap
								stbtt_MakeGlyphBitmap(&font_info,
									glyph_bitmap + glyph_offset_x,
									atlas_item_width * horizontal_resolution, atlas_item_height, bitmap_stride,
									font_scale * horizontal_resolution, font_scale,
									glyph_index
								);
								
								// Allocate an RGB bitmap with the size of the atlas item and clear it out to black. That way we overwrite the entire atlas item with black,
								// even if the padded glyph is smaller. Not really necessary but keeps the atlas clean.
								// We then apply the FreeType LCD filter by reading from the glyph bitmap, filtering and writing to the atlas item bitmap.
								// Note: As above you probably don't want to allocate a new bitmap for each glyph. Just create another permanent scratch bitmap for this step.
								uint8_t* atlas_item_bitmap = calloc(1, bitmap_size);
								
								// Apply the FreeType LCD filter to avoid subpixel anti-aliasing color fringes,
								// taken from FT_LCD_FILTER_DEFAULT in https://freetype.org/freetype2/docs/reference/ft2-lcd_rendering.html
								// Just iterate over all the subpixels the filter can reach, no need to filter the entire bitmap when the results would just be 0.
								uint8_t filter_weights[5] = { 0x08, 0x4D, 0x56, 0x4D, 0x08 };
								for (int y = 0; y < padded_glyph_height_px; y++) {
									// We don't need to filter the first 4 and last 1 subpixels. The filter kernel is only 5 wide and it can only distribute data
									// at most 2 subpixels in each direction.
									// The first 6 subpixels are just padding (subpixel_positioning_left_padding and horizontal_filter_padding) so the first 4
									// subpixels in atlas_item_bitmap can't collect any data from the first subpixel in glyph_bitmap. Hence we start at 4 instead of 0.
									// The last subpixel is padding again (horizontal_filter_padding) and only the 3rd and 2nd subpixel from the right can collect
									// data from the last subpixel of the glyph_bitmap. So we skip the last subpixel as well.
									int x_end = padded_glyph_width_px * horizontal_resolution - 1;
									for (int x = 4; x < x_end; x++) {
										// Apply the kernel aka filter taps while reading from glyph_bitmap. kernel_x_end makes sure we don't read over the end of the bitmap.
										int sum = 0, filter_weight_index = 0, kernel_x_end = (x == x_end - 1) ? x + 1 : x + 2;
										for (int kernel_x = x - 2; kernel_x <= kernel_x_end; kernel_x++) {
											assert(kernel_x >= 0 && kernel_x < x_end + 1);  // There is 1 more subpixel after the last processed one, so we can access that one just fine.
											assert(y        >= 0 && y        < padded_glyph_height_px);
											int offset = kernel_x + y*bitmap_stride;
											assert(offset >= 0 && offset < bitmap_size);
											sum += glyph_bitmap[offset] * filter_weights[filter_weight_index++];
										}
										
										// Do the division once at the end instead of for each filter weight and make sure we handle overflows.
										// Rounding causes some pixels to accumulate a +1 which overflows from 255 to 0 and causes one subpixel artifacts.
										// Put the result into atlas_item_bitmap.
										sum = sum / 255;
										atlas_item_bitmap[x + y*bitmap_stride] = (sum > 255) ? 255 : sum;
									}
								}
								free(glyph_bitmap);
								
								// Upload the filtered atlas item bitmap into the glyph atlas texture
								glTextureSubImage2D(glyph_atlas_texture, 0, atlas_item_x, atlas_item_y, atlas_item_width, atlas_item_height, GL_RGB, GL_UNSIGNED_BYTE, atlas_item_bitmap);
								free(atlas_item_bitmap);
								
								glyph_atlas_item.tex_coords.left   = atlas_item_x;
								glyph_atlas_item.tex_coords.top    = atlas_item_y;
								glyph_atlas_item.tex_coords.right  = atlas_item_x + padded_glyph_width_px;
								glyph_atlas_item.tex_coords.bottom = atlas_item_y + padded_glyph_height_px;
							} else {
								// The glyph has no visual representation (e.g. space). Just set the glyph atlas entry to some
								// value we can check for later on to see if the glyph has no visual representation.
								glyph_atlas_item.tex_coords.left   = -1;
								glyph_atlas_item.tex_coords.top    = -1;
								glyph_atlas_item.tex_coords.right  = -1;
								glyph_atlas_item.tex_coords.bottom = -1;
							}
							
							// Finish up the glyph atlas item and put it back into the array
							glyph_atlas_item.glyph_index                      = glyph_index;
							glyph_atlas_item.distance_from_baseline_to_top_px = distance_from_baseline_to_top_px;
							glyph_atlas_item.filled                           = true;
							glyph_atlas_items[codepoint] = glyph_atlas_item;
						}
						
						int glyph_advance_width = 0, glyph_left_side_bearing = 0;
						stbtt_GetGlyphHMetrics(&font_info, glyph_atlas_item.glyph_index, &glyph_advance_width, &glyph_left_side_bearing);
						
						// Only render glyphs that actually have some visual representation (skip spaces, etc.)
						if (glyph_atlas_item.tex_coords.left != -1) {
							float glyph_pos_x = current_x + (glyph_left_side_bearing * font_scale);
							float glyph_pos_x_px = 0;
							float glyph_pos_x_subpixel_shift = modff(glyph_pos_x, &glyph_pos_x_px);
							float glyph_pos_y_px = current_y - glyph_atlas_item.distance_from_baseline_to_top_px;
							int glyph_width_with_horiz_filter_padding = glyph_atlas_item.tex_coords.right  - glyph_atlas_item.tex_coords.left;
							int glyph_height                          = glyph_atlas_item.tex_coords.bottom - glyph_atlas_item.tex_coords.top;
							
							rect_buffer[rect_buffer_filled++] = (rect_instance_t){
								.pos.left   = glyph_pos_x_px - (subpixel_positioning_left_padding + horizontal_filter_padding),
								.pos.right  = glyph_pos_x_px - (subpixel_positioning_left_padding + horizontal_filter_padding) + glyph_width_with_horiz_filter_padding,
								.pos.top    = glyph_pos_y_px,
								.pos.bottom = glyph_pos_y_px + glyph_height,
								.subpixel_shift = glyph_pos_x_subpixel_shift,
								.tex_coords     = glyph_atlas_item.tex_coords,
								.color          = text_color
							};
						}
						
						current_x += glyph_advance_width * font_scale;
					}
				}
			}
			
			// Draw all the rects in rect_buffer
			{
				glClearColor(0.25, 0.25, 0.25, 1.0);
				glClear(GL_COLOR_BUFFER_BIT);
				
				// Upload the rect buffer to the GPU.
				// Allow the GPU driver to create a new buffer storage for each draw command. That way it doesn't have to wait for
				// the previous draw command to finish to reuse the same buffer storage.
				glNamedBufferData(rect_instances_vbo, rect_buffer_filled * sizeof(rect_buffer[0]), rect_buffer, GL_DYNAMIC_DRAW);
				
				// Setup pre-multiplied alpha blending (that's why the source factor is GL_ONE) with dual source blending so we can blend
				// each subpixel individually for subpixel anti-aliased glyph rendering (that's what GL_ONE_MINUS_SRC1_COLOR does).
				glEnable(GL_BLEND);
				glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC1_COLOR);
				
				glBindVertexArray(vao);
					glUseProgram(shader_program);
						// layout(location = 0) uniform vec2 half_viewport_size
						// Note: Do a float division on window_width and window_height to properly handle uneven window dimensions.
						// An integer division causes 1px artifacts in the middle of windows due to a wrong transform.
						glProgramUniform2f(shader_program, 0, window_width / 2.0f, window_height / 2.0f);
						// layout(location = 1) uniform float coverage_adjustment
						glProgramUniform1f(shader_program, 1, coverage_adjustment);
						
						glBindTextureUnit(0, glyph_atlas_texture);
						
						glDrawArraysInstanced(GL_TRIANGLES, 0, 6, rect_buffer_filled);
					glUseProgram(0);
				glBindVertexArray(0);
				
				// We don't need the contents of the CPU or GPU buffer anymore
				glInvalidateBufferData(rect_instances_vbo);
				rect_buffer_filled = 0;
				
				SDL_GL_SwapWindow(window);
			}
		}
	}
	
	
	// Cleanup
	glDeleteVertexArrays(1, &vao);
	glDeleteBuffers(1, &rect_vertices_vbo);
	glDeleteBuffers(1, &rect_instances_vbo);
	glDeleteProgram(shader_program);
	glDeleteTextures(1, &glyph_atlas_texture);
	
	SDL_GL_DeleteContext(gl_ctx);
	SDL_DestroyWindow(window);
	
	return 0;
}