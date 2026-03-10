#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

// GL headers — platform-specific
#if defined(__ANDROID__) || defined(IMGUI_IMPL_OPENGL_ES3)
#include <GLES3/gl3.h>
#else
// Use the imgui embedded GL loader on desktop (provides GL 3.x functions)
#include "imgui_impl_opengl3_loader.h"
#endif

// GL constants not in the imgui loader
#ifndef GL_LINE_STRIP
#define GL_LINE_STRIP 0x0003
#endif

#include <vector>
#include <cstdio>
#include <cstring>
#include "plotting.h"

// ============================================================================
// Vertex format: position (float x2) + color (ubyte x4)
// ============================================================================
struct PlotVertex
{
	float x, y;
	uint8_t r, g, b, a;
};

// ============================================================================
// Shader sources
// ============================================================================
#if defined(__ANDROID__) || defined(IMGUI_IMPL_OPENGL_ES3)
#define GLSL_VERSION "#version 300 es\n"
#define GLSL_PRECISION "precision mediump float;\n"
#else
#define GLSL_VERSION "#version 130\n"
#define GLSL_PRECISION ""
#endif

static const char* g_vert_src =
	GLSL_VERSION
	GLSL_PRECISION
	"in vec2 a_pos;\n"
	"in vec4 a_color;\n"
	"out vec4 v_color;\n"
	"uniform mat4 u_proj;\n"
	"void main() {\n"
	"  gl_Position = u_proj * vec4(a_pos, 0.0, 1.0);\n"
	"  v_color = a_color;\n"
	"}\n";

static const char* g_frag_src =
	GLSL_VERSION
	GLSL_PRECISION
#if defined(__ANDROID__) || defined(IMGUI_IMPL_OPENGL_ES3)
	"out vec4 fragColor;\n"
#endif
	"in vec4 v_color;\n"
	"void main() {\n"
#if defined(__ANDROID__) || defined(IMGUI_IMPL_OPENGL_ES3)
	"  fragColor = v_color;\n"
#else
	"  gl_FragColor = v_color;\n"
#endif
	"}\n";

// ============================================================================
// Helper: compile a shader, return 0 on failure
// ============================================================================
static GLuint compile_shader(GLenum type, const char* src)
{
	GLuint s = glCreateShader(type);
	glShaderSource(s, 1, &src, nullptr);
	glCompileShader(s);
	GLint ok = 0;
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok)
	{
		char buf[512];
		glGetShaderInfoLog(s, sizeof(buf), nullptr, buf);
		printf("Shader compile error: %s\n", buf);
		glDeleteShader(s);
		return 0;
	}
	return s;
}

// ============================================================================
// fpoint_t
// ============================================================================
fpoint_t::fpoint_t()
	: x(0.0f)
	, y(0.0f)
{
}

fpoint_t::fpoint_t(float x_val, float y_val)
	: x(x_val)
	, y(y_val)
{
}

// ============================================================================
// Line
// ============================================================================
Line::Line()
	: points()
	, color()
	, xsource(NULL)
	, ysource(NULL)
	, mode(TIME_MODE)
	, xscale(1.f)
	, xoffset(0.f)
	, yscale(1.f)
	, yoffset(0.f)
	, enqueue_cap(2000)
{
	color.r = 0;
	color.g = 0;
	color.b = 0;
	color.a = 0xFF;
}

Line::Line(int capacity)
	: points(capacity)
	, color()
	, xsource(NULL)
	, ysource(NULL)
	, mode(TIME_MODE)
	, xscale(1.f)
	, xoffset(0.f)
	, yscale(1.f)
	, yoffset(0.f)
	, enqueue_cap(2000)
{
	color.r = 0;
	color.g = 0;
	color.b = 0;
	color.a = 0xFF;
}

// ============================================================================
// Plotter
// ============================================================================
Plotter::Plotter()
	: window_width(0)
	, window_height(0)
	, num_widths(1)
	, lines()
	, sys_sec(0.0f)
	, m_vbo(0)
	, m_ebo(0)
	, m_vao(0)
	, m_shader(0)
	, m_u_proj(-1)
	, m_gl_ready(false)
{
}

Plotter::~Plotter()
{
	teardown_gl_resources();
}

bool Plotter::init_gl_resources()
{
	// Compile shaders
	GLuint vs = compile_shader(GL_VERTEX_SHADER, g_vert_src);
	GLuint fs = compile_shader(GL_FRAGMENT_SHADER, g_frag_src);
	if (!vs || !fs)
	{
		return false;
	}

	m_shader = glCreateProgram();
	glAttachShader(m_shader, vs);
	glAttachShader(m_shader, fs);
	glLinkProgram(m_shader);
	glDeleteShader(vs);
	glDeleteShader(fs);

	GLint ok = 0;
	glGetProgramiv(m_shader, GL_LINK_STATUS, &ok);
	if (!ok)
	{
		char buf[512];
		glGetProgramInfoLog(m_shader, sizeof(buf), nullptr, buf);
		printf("Shader link error: %s\n", buf);
		glDeleteProgram(m_shader);
		m_shader = 0;
		return false;
	}

	m_u_proj = glGetUniformLocation(m_shader, "u_proj");
	GLint a_pos_loc = glGetAttribLocation(m_shader, "a_pos");
	GLint a_color_loc = glGetAttribLocation(m_shader, "a_color");

	// Create VAO, VBO, EBO
	glGenVertexArrays(1, &m_vao);
	glGenBuffers(1, &m_vbo);
	glGenBuffers(1, &m_ebo);

	glBindVertexArray(m_vao);

	glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
	// attr 0: a_pos — 2 floats at offset 0
	glVertexAttribPointer(a_pos_loc, 2, GL_FLOAT, GL_FALSE, sizeof(PlotVertex), (void*)0);
	glEnableVertexAttribArray(a_pos_loc);
	// attr 1: a_color — 4 ubytes normalized at offset 8
	glVertexAttribPointer(a_color_loc, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(PlotVertex), (void*)(2 * sizeof(float)));
	glEnableVertexAttribArray(a_color_loc);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);

	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

	m_gl_ready = true;
	return true;
}

void Plotter::teardown_gl_resources()
{
	if (!m_gl_ready)
	{
		return;
	}
	if (m_vao) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
	if (m_vbo) { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
	if (m_ebo) { glDeleteBuffers(1, &m_ebo); m_ebo = 0; }
	if (m_shader) { glDeleteProgram(m_shader); m_shader = 0; }
	m_gl_ready = false;
}

bool Plotter::init(int width, int height)
{
	if (width <= 0 || height <= 0)
	{
		return false;
	}

	window_width = width;
	window_height = height;
	int line_capacity = 0;

	// Initialize with one line
	lines.resize(1);
	lines[0].points.resize(line_capacity);
	lines[0].points.clear();
	lines[0].xsource = &sys_sec;
	int color_idx = (lines.size() % NUM_COLORS);
	lines[0].color = template_colors[color_idx];

	if (!m_gl_ready)
	{
		if (!init_gl_resources())
		{
			return false;
		}
	}

	return true;
}

int sat_pix_to_window(int val, int thresh)
{
	if(val < 1)
	{
		return 1;
	}
	else if(val > (thresh-1))
	{
		return (thresh-1);
	}
	return val;
}

void Plotter::render()
{
	if (!m_gl_ready)
	{
		return;
	}

	// Build orthographic projection matrix (column-major)
	// Maps (0..window_width, 0..window_height) to clip space (-1..1)
	float L = 0.0f;
	float R = (float)window_width;
	float B = 0.0f;
	float T = (float)window_height;
	float proj[16];
	memset(proj, 0, sizeof(proj));
	proj[0]  = 2.0f / (R - L);          // m[0][0]
	proj[5]  = 2.0f / (T - B);          // m[1][1]
	proj[10] = -1.0f;                    // m[2][2]
	proj[12] = -(R + L) / (R - L);      // m[3][0]
	proj[13] = -(T + B) / (T - B);      // m[3][1]
	proj[15] = 1.0f;                     // m[3][3]

	glUseProgram(m_shader);
	glUniformMatrix4fv(m_u_proj, 1, GL_FALSE, proj);
	glBindVertexArray(m_vao);

	// Temporary buffers for vertex + index data
	static std::vector<PlotVertex> verts;
	static std::vector<uint16_t> indices;

	for (int i = 0; i < (int)lines.size(); i++)
	{
		Line* line = &lines[i];
		int num_points = (int)line->points.size();

		if (num_points < 2)
		{
			continue;
		}

		// Build vertex data
		verts.resize(num_points);
		indices.resize(num_points);
		for (int j = 0; j < num_points; j++)
		{
			int x = 0;
			int y = 0;
			if (line->mode == TIME_MODE)
			{
				x = (int)((line->points[j].x - line->points.front().x) * line->xscale);
				y = (int)(line->points[j].y * line->yscale + line->yoffset + (float)window_height / 2.f);
			}
			else
			{
				x = (int)(line->points[j].x * line->xscale + line->xoffset + (float)window_width / 2.f);
				y = (int)(line->points[j].y * line->yscale + line->yoffset + (float)window_height / 2.f);
			}
			x = sat_pix_to_window(x, window_width);
			y = sat_pix_to_window(y, window_width);

			verts[j].x = (float)x;
			verts[j].y = (float)y;
			verts[j].r = line->color.r;
			verts[j].g = line->color.g;
			verts[j].b = line->color.b;
			verts[j].a = line->color.a;

			indices[j] = (uint16_t)j;
		}

		// Upload and draw
		glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
		glBufferData(GL_ARRAY_BUFFER, num_points * sizeof(PlotVertex), verts.data(), GL_STREAM_DRAW);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, num_points * sizeof(uint16_t), indices.data(), GL_STREAM_DRAW);
		glDrawElements(GL_LINE_STRIP, num_points, GL_UNSIGNED_SHORT, nullptr);
	}

	glBindVertexArray(0);
	glUseProgram(0);
}


bool Line::enqueue_data(int screen_width)
{
	if(xsource == NULL || ysource == NULL)
	{
		return false;	//fail due to bad pointer reference
	}
	//enqueue data
	if(points.size() < enqueue_cap)	//cap on buffer width - may want to expand
	{
		points.push_back(fpoint_t(*xsource, *ysource));
	}
	else if(points.size() > enqueue_cap)
	{
		points.resize(enqueue_cap);
	}
	if(points.size() == enqueue_cap)
	{
		std::rotate(points.begin(), points.begin() + 1, points.end());
		points.back() = fpoint_t(*xsource, *ysource);
	}

	if(mode == TIME_MODE)
	{
		//THEN calculate xscale based on current buffer state
		if(points.size() >= 2)
		{
			float div = points.back().x - points.front().x;
			if(div > 0)
			{
				xscale = screen_width/div;
			}
			else if(div < 0) //decreasing time
			{
				points.clear();	//this just sets size=0 - can preallocate and clear for speed
			}
		}
	}
	return true;
}
