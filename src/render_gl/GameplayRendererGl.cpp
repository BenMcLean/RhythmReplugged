#include "render_gl/GameplayRendererGl.h"

#include <imgui_impl_opengl3_loader.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#ifndef GL_DEPTH_BUFFER_BIT
#define GL_DEPTH_BUFFER_BIT 0x00000100
#endif

namespace
{
	using namespace rhythmreplugged;

	constexpr float kPi = 3.1415926535f;

	struct Vec3
	{
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
	};

	struct Mat4
	{
		float v[16]{};
	};

	struct RenderVertex
	{
		float x;
		float y;
		float z;
		float r;
		float g;
		float b;
		float a;
		float fade;
	};

	struct MeshBatch
	{
		std::vector<RenderVertex> vertices;
		std::vector<std::uint32_t> indices;

		void clear()
		{
			vertices.clear();
			indices.clear();
		}
	};

	float radians(float degrees)
	{
		return degrees * (kPi / 180.0f);
	}

	Vec3 subtract(const Vec3 &a, const Vec3 &b)
	{
		return {a.x - b.x, a.y - b.y, a.z - b.z};
	}

	float dot(const Vec3 &a, const Vec3 &b)
	{
		return a.x * b.x + a.y * b.y + a.z * b.z;
	}

	Vec3 cross(const Vec3 &a, const Vec3 &b)
	{
		return {
			a.y * b.z - a.z * b.y,
			a.z * b.x - a.x * b.z,
			a.x * b.y - a.y * b.x,
		};
	}

	Vec3 normalize(const Vec3 &value)
	{
		const float length = std::sqrt(dot(value, value));
		if (length <= 0.00001f)
			return {0.0f, 0.0f, 0.0f};

		return {value.x / length, value.y / length, value.z / length};
	}

	Mat4 identity_matrix()
	{
		Mat4 result{};
		result.v[0] = 1.0f;
		result.v[5] = 1.0f;
		result.v[10] = 1.0f;
		result.v[15] = 1.0f;
		return result;
	}

	Mat4 multiply(const Mat4 &a, const Mat4 &b)
	{
		Mat4 result{};
		for (int column = 0; column < 4; ++column)
		{
			for (int row = 0; row < 4; ++row)
			{
				float value = 0.0f;
				for (int inner = 0; inner < 4; ++inner)
					value += a.v[inner * 4 + row] * b.v[column * 4 + inner];
				result.v[column * 4 + row] = value;
			}
		}
		return result;
	}

	Mat4 perspective(float field_of_view_radians, float aspect_ratio, float z_near, float z_far)
	{
		Mat4 result{};
		const float inv_tan = 1.0f / std::tan(field_of_view_radians * 0.5f);
		result.v[0] = inv_tan / aspect_ratio;
		result.v[5] = inv_tan;
		result.v[10] = (z_far + z_near) / (z_near - z_far);
		result.v[11] = -1.0f;
		result.v[14] = (2.0f * z_far * z_near) / (z_near - z_far);
		return result;
	}

	Mat4 look_at(const Vec3 &eye, const Vec3 &target, const Vec3 &up)
	{
		const Vec3 forward = normalize(subtract(target, eye));
		const Vec3 right = normalize(cross(forward, up));
		const Vec3 actual_up = cross(right, forward);

		Mat4 result = identity_matrix();
		result.v[0] = right.x;
		result.v[4] = right.y;
		result.v[8] = right.z;
		result.v[1] = actual_up.x;
		result.v[5] = actual_up.y;
		result.v[9] = actual_up.z;
		result.v[2] = -forward.x;
		result.v[6] = -forward.y;
		result.v[10] = -forward.z;
		result.v[12] = -dot(right, eye);
		result.v[13] = -dot(actual_up, eye);
		result.v[14] = dot(forward, eye);
		return result;
	}

	Color4 with_alpha_scale(Color4 color, float alpha_scale)
	{
		color.a *= alpha_scale;
		return color;
	}

	float lane_left_edge(const InstrumentLaneView &lane, int fret_lane)
	{
		return lane.lane_center_x - (lane.lane_width * 0.5f) +
			(static_cast<float>(fret_lane) * lane.lane_width / 5.0f);
	}

	float lane_right_edge(const InstrumentLaneView &lane, int fret_lane)
	{
		return lane_left_edge(lane, fret_lane) + (lane.lane_width / 5.0f);
	}

	float lane_center(const InstrumentLaneView &lane, int fret_lane)
	{
		return (lane_left_edge(lane, fret_lane) + lane_right_edge(lane, fret_lane)) * 0.5f;
	}

	float note_depth(float offset_seconds, float visible_depth_seconds)
	{
		const float seconds = visible_depth_seconds > 0.001f ? visible_depth_seconds : 1.5f;
		const float normalized = offset_seconds / seconds;
		return -normalized * 10.0f;
	}

	float depth_fade(float depth)
	{
		const float normalized = std::clamp((-depth) / 10.0f, 0.0f, 1.0f);
		return 1.0f - normalized * 0.65f;
	}

	void append_quad(
		MeshBatch &batch,
		const Vec3 &a,
		const Vec3 &b,
		const Vec3 &c,
		const Vec3 &d,
		Color4 color,
		float fade)
	{
		const std::uint32_t base_index = static_cast<std::uint32_t>(batch.vertices.size());
		batch.vertices.push_back({a.x, a.y, a.z, color.r, color.g, color.b, color.a, fade});
		batch.vertices.push_back({b.x, b.y, b.z, color.r, color.g, color.b, color.a, fade});
		batch.vertices.push_back({c.x, c.y, c.z, color.r, color.g, color.b, color.a, fade});
		batch.vertices.push_back({d.x, d.y, d.z, color.r, color.g, color.b, color.a, fade});
		batch.indices.push_back(base_index + 0);
		batch.indices.push_back(base_index + 1);
		batch.indices.push_back(base_index + 2);
		batch.indices.push_back(base_index + 0);
		batch.indices.push_back(base_index + 2);
		batch.indices.push_back(base_index + 3);
	}

	void append_xy_quad(
		MeshBatch &batch,
		float left,
		float right,
		float top,
		float bottom,
		float z,
		Color4 color,
		float fade)
	{
		append_quad(
			batch,
			{left, top, z},
			{right, top, z},
			{right, bottom, z},
			{left, bottom, z},
			color,
			fade);
	}

	void append_xz_quad(
		MeshBatch &batch,
		float left,
		float right,
		float near_z,
		float far_z,
		float y,
		Color4 color,
		float fade)
	{
		append_quad(
			batch,
			{left, y, near_z},
			{right, y, near_z},
			{right, y, far_z},
			{left, y, far_z},
			color,
			fade);
	}

	void append_highway_background(MeshBatch &batch, const InstrumentLaneView &lane, const HighwayStyleView &style)
	{
		const float near_z = 1.4f;
		const float far_z = -10.5f;
		const float background_margin = 0.65f;
		append_xz_quad(
			batch,
			lane.lane_center_x - lane.lane_width * 0.5f - background_margin,
			lane.lane_center_x + lane.lane_width * 0.5f + background_margin,
			near_z,
			far_z,
			0.0f,
			style.background_bottom_color,
			1.0f);

		const float lane_width = lane.lane_width / 5.0f;
		for (int fret = 0; fret < 5; ++fret)
		{
			Color4 lane_color = style.lane_colors[fret];
			const bool is_held = lane.lane_held[static_cast<size_t>(fret)];
			const bool is_sustaining = lane.lane_sustaining[static_cast<size_t>(fret)];
			const float shade = is_held ? 0.28f : (is_sustaining ? 0.20f : 0.12f);
			lane_color.r *= shade;
			lane_color.g *= shade;
			lane_color.b *= shade;
			lane_color.a = 1.0f;
			append_xz_quad(
				batch,
				lane.lane_center_x - lane.lane_width * 0.5f + lane_width * static_cast<float>(fret) + style.lane_gap * 0.5f,
				lane.lane_center_x - lane.lane_width * 0.5f + lane_width * static_cast<float>(fret + 1) - style.lane_gap * 0.5f,
				0.0f,
				-10.0f,
				0.02f,
				lane_color,
				1.0f);
		}

		for (int separator = 1; separator < 5; ++separator)
		{
			const float x = lane.lane_center_x - lane.lane_width * 0.5f + lane_width * static_cast<float>(separator);
			append_xz_quad(
				batch,
				x - 0.01f,
				x + 0.01f,
				0.0f,
				-10.0f,
				0.03f,
				with_alpha_scale(style.lane_border_color, 0.85f),
				1.0f);
		}
	}

	void append_measure_lines(
		MeshBatch &batch,
		const InstrumentLaneView &lane,
		const HighwayStyleView &style,
		float visible_depth_seconds)
	{
		for (const HighwayMeasureLineView &line : lane.visible_measure_lines)
		{
			const float z = note_depth(line.offset_seconds, visible_depth_seconds) + lane.lane_depth_offset;
			if (z > 1.4f || z < -10.5f)
				continue;

			const float thickness = line.is_measure ? 0.09f : (line.is_strong ? 0.06f : 0.035f);
			const Color4 color = line.is_measure ? style.measure_line_color : style.beat_line_color;
			append_xz_quad(
				batch,
				lane.lane_center_x - lane.lane_width * 0.5f,
				lane.lane_center_x + lane.lane_width * 0.5f,
				z + thickness,
				z - thickness,
				0.05f,
				color,
				depth_fade(z));
		}
	}

	void append_sustains(
		MeshBatch &batch,
		const InstrumentLaneView &lane,
		const HighwayStyleView &style,
		float visible_depth_seconds)
	{
		for (const HighwayNoteView &note : lane.visible_notes)
		{
			if (note.lane < 0 || note.lane >= 5)
				continue;
			if (note.length_seconds <= 0.08f)
				continue;

			const float start_z = note_depth(note.start_offset_seconds, visible_depth_seconds) + lane.lane_depth_offset;
			const float end_z = note_depth(note.start_offset_seconds + note.length_seconds, visible_depth_seconds) + lane.lane_depth_offset;
			const float near_z = std::min(start_z, end_z);
			const float far_z = std::max(start_z, end_z);
			if (far_z < -10.5f || near_z > 1.6f)
				continue;

			const float x = lane_center(lane, note.lane);
			append_xz_quad(
				batch,
				x - style.sustain_width * 0.5f,
				x + style.sustain_width * 0.5f,
				far_z,
				near_z,
				0.09f,
				style.sustain_color,
				depth_fade(start_z));
		}
	}

	void append_notes(
		MeshBatch &batch,
		const InstrumentLaneView &lane,
		const HighwayStyleView &style,
		float visible_depth_seconds)
	{
		for (const HighwayNoteView &note : lane.visible_notes)
		{
			if (note.lane < 0 || note.lane >= 5)
				continue;

			const float z = note_depth(note.start_offset_seconds, visible_depth_seconds) + lane.lane_depth_offset;
			if (z > 1.8f || z < -10.5f)
				continue;

			const float x = lane_center(lane, note.lane);
			const float width = style.note_width * 0.5f;
			const float height = style.note_height * 0.5f;
			const float y = 0.14f;
			append_xy_quad(
				batch,
				x - width,
				x + width,
				y + height,
				y - height,
				z,
				style.lane_colors[note.lane],
				depth_fade(z));

			append_xy_quad(
				batch,
				x - width * 0.82f,
				x + width * 0.82f,
				y + height * 0.82f,
				y - height * 0.82f,
				z + 0.01f,
				with_alpha_scale({1.0f, 1.0f, 1.0f, 1.0f}, 0.22f),
				depth_fade(z));
		}
	}

	void append_hit_line(MeshBatch &batch, const InstrumentLaneView &lane, const HighwayStyleView &style)
	{
		append_xz_quad(
			batch,
			lane.lane_center_x - lane.lane_width * 0.5f,
			lane.lane_center_x + lane.lane_width * 0.5f,
			0.08f,
			-0.08f,
			0.12f,
			style.hit_line_color,
			1.0f);

		for (int fret = 0; fret < 5; ++fret)
		{
			Color4 color = lane.lane_held[static_cast<size_t>(fret)] || lane.lane_sustaining[static_cast<size_t>(fret)]
				? style.lane_colors[fret]
				: with_alpha_scale(style.lane_border_color, 0.8f);
			const float radius = 0.24f;
			const float center_x = lane_center(lane, fret);
			append_xy_quad(
				batch,
				center_x - radius,
				center_x + radius,
				0.30f,
				0.02f,
				0.12f,
				color,
				1.0f);
		}
	}

	std::pair<int, int> viewport_extent(const RectF &rect, int framebuffer_width, int framebuffer_height)
	{
		const int width = (std::max)(1, static_cast<int>(std::lround(rect.width * static_cast<float>(framebuffer_width))));
		const int height = (std::max)(1, static_cast<int>(std::lround(rect.height * static_cast<float>(framebuffer_height))));
		return {width, height};
	}

	std::array<int, 4> viewport_from_rect(const RectF &rect, int framebuffer_width, int framebuffer_height)
	{
		const int x = static_cast<int>(std::lround(rect.x * static_cast<float>(framebuffer_width)));
		const int width = (std::max)(1, static_cast<int>(std::lround(rect.width * static_cast<float>(framebuffer_width))));
		const int height = (std::max)(1, static_cast<int>(std::lround(rect.height * static_cast<float>(framebuffer_height))));
		const int top = static_cast<int>(std::lround(rect.y * static_cast<float>(framebuffer_height)));
		const int y = framebuffer_height - top - height;
		return {x, y, width, height};
	}

	std::string shader_compile_error(GLuint shader)
	{
		GLint log_length = 0;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_length);
		if (log_length <= 1)
			return "shader compilation failed";

		std::string log(static_cast<size_t>(log_length), '\0');
		glGetShaderInfoLog(shader, log_length, nullptr, log.data());
		return log;
	}

	std::string program_link_error(GLuint program)
	{
		GLint log_length = 0;
		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_length);
		if (log_length <= 1)
			return "shader link failed";

		std::string log(static_cast<size_t>(log_length), '\0');
		glGetProgramInfoLog(program, log_length, nullptr, log.data());
		return log;
	}
}

namespace rhythmreplugged
{
	bool GameplayRendererGl::initialize(std::string &error_message)
	{
		if (initialized_)
			return true;

		if (!create_device_objects(error_message))
			return false;

		initialized_ = true;
		return true;
	}

	void GameplayRendererGl::shutdown()
	{
		destroy_device_objects();
		initialized_ = false;
	}

	void GameplayRendererGl::on_context_lost()
	{
		destroy_device_objects();
		initialized_ = false;
	}

	void GameplayRendererGl::on_context_restored(std::string &error_message)
	{
		destroy_device_objects();
		initialized_ = create_device_objects(error_message);
	}

	bool GameplayRendererGl::create_device_objects(std::string &error_message)
	{
		static constexpr const char *kVertexShaderSource = R"(#version 130
in vec3 a_position;
in vec4 a_color;
in float a_fade;
uniform mat4 u_mvp;
uniform int u_use_vertex_fade;
out vec4 v_color;
void main()
{
	gl_Position = u_mvp * vec4(a_position, 1.0);
	float fade = u_use_vertex_fade != 0 ? a_fade : 1.0;
	v_color = vec4(a_color.rgb, a_color.a * fade);
}
)";

		static constexpr const char *kFragmentShaderSource = R"(#version 130
in vec4 v_color;
out vec4 o_color;
void main()
{
	o_color = v_color;
}
)";

		if (imgl3wInit() != 0)
		{
			error_message = "Failed to initialize OpenGL function loader for gameplay renderer.";
			destroy_device_objects();
			return false;
		}

		vertex_shader_ = glCreateShader(GL_VERTEX_SHADER);
		fragment_shader_ = glCreateShader(GL_FRAGMENT_SHADER);
		if (vertex_shader_ == 0 || fragment_shader_ == 0)
		{
			error_message = "Failed to create OpenGL shader objects.";
			destroy_device_objects();
			return false;
		}

		glShaderSource(vertex_shader_, 1, &kVertexShaderSource, nullptr);
		glCompileShader(vertex_shader_);
		GLint compiled = GL_FALSE;
		glGetShaderiv(vertex_shader_, GL_COMPILE_STATUS, &compiled);
		if (compiled != GL_TRUE)
		{
			error_message = shader_compile_error(vertex_shader_);
			destroy_device_objects();
			return false;
		}

		glShaderSource(fragment_shader_, 1, &kFragmentShaderSource, nullptr);
		glCompileShader(fragment_shader_);
		glGetShaderiv(fragment_shader_, GL_COMPILE_STATUS, &compiled);
		if (compiled != GL_TRUE)
		{
			error_message = shader_compile_error(fragment_shader_);
			destroy_device_objects();
			return false;
		}

		shader_program_ = glCreateProgram();
		glAttachShader(shader_program_, vertex_shader_);
		glAttachShader(shader_program_, fragment_shader_);
		glLinkProgram(shader_program_);

		GLint linked = GL_FALSE;
		glGetProgramiv(shader_program_, GL_LINK_STATUS, &linked);
		if (linked != GL_TRUE)
		{
			error_message = program_link_error(shader_program_);
			destroy_device_objects();
			return false;
		}

		u_mvp_location_ = glGetUniformLocation(shader_program_, "u_mvp");
		u_use_vertex_fade_location_ = glGetUniformLocation(shader_program_, "u_use_vertex_fade");
		const GLint position_attribute = glGetAttribLocation(shader_program_, "a_position");
		const GLint color_attribute = glGetAttribLocation(shader_program_, "a_color");
		const GLint fade_attribute = glGetAttribLocation(shader_program_, "a_fade");

		glGenVertexArrays(1, &vao_);
		glGenBuffers(1, &vbo_);
		glGenBuffers(1, &ebo_);
		if (vao_ == 0 || vbo_ == 0 || ebo_ == 0)
		{
			error_message = "Failed to create OpenGL buffers for gameplay renderer.";
			destroy_device_objects();
			return false;
		}

		glBindVertexArray(vao_);
		glBindBuffer(GL_ARRAY_BUFFER, vbo_);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
		if (position_attribute >= 0)
		{
			glEnableVertexAttribArray(static_cast<GLuint>(position_attribute));
			glVertexAttribPointer(static_cast<GLuint>(position_attribute), 3, GL_FLOAT, GL_FALSE, sizeof(RenderVertex), reinterpret_cast<const void *>(offsetof(RenderVertex, x)));
		}
		if (color_attribute >= 0)
		{
			glEnableVertexAttribArray(static_cast<GLuint>(color_attribute));
			glVertexAttribPointer(static_cast<GLuint>(color_attribute), 4, GL_FLOAT, GL_FALSE, sizeof(RenderVertex), reinterpret_cast<const void *>(offsetof(RenderVertex, r)));
		}
		if (fade_attribute >= 0)
		{
			glEnableVertexAttribArray(static_cast<GLuint>(fade_attribute));
			glVertexAttribPointer(static_cast<GLuint>(fade_attribute), 1, GL_FLOAT, GL_FALSE, sizeof(RenderVertex), reinterpret_cast<const void *>(offsetof(RenderVertex, fade)));
		}
		glBindVertexArray(0);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
		return true;
	}

	void GameplayRendererGl::destroy_device_objects()
	{
		if (ebo_ != 0)
		{
			glDeleteBuffers(1, &ebo_);
			ebo_ = 0;
		}
		if (vbo_ != 0)
		{
			glDeleteBuffers(1, &vbo_);
			vbo_ = 0;
		}
		if (vao_ != 0)
		{
			glDeleteVertexArrays(1, &vao_);
			vao_ = 0;
		}
		if (shader_program_ != 0)
		{
			glDeleteProgram(shader_program_);
			shader_program_ = 0;
		}
		if (vertex_shader_ != 0)
		{
			glDeleteShader(vertex_shader_);
			vertex_shader_ = 0;
		}
		if (fragment_shader_ != 0)
		{
			glDeleteShader(fragment_shader_);
			fragment_shader_ = 0;
		}

		u_mvp_location_ = -1;
		u_use_vertex_fade_location_ = -1;
	}

	void GameplayRendererGl::render(const GameplaySceneView &scene, int framebuffer_width, int framebuffer_height)
	{
		if (!initialized_ || framebuffer_width <= 0 || framebuffer_height <= 0)
			return;

		glViewport(0, 0, framebuffer_width, framebuffer_height);
		glDisable(GL_SCISSOR_TEST);
		glClearColor(scene.clear_color.r, scene.clear_color.g, scene.clear_color.b, scene.clear_color.a);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		glEnable(GL_BLEND);
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		glEnable(GL_DEPTH_TEST);

		glUseProgram(shader_program_);
		glBindVertexArray(vao_);

		MeshBatch batch;
		for (const PlayerGameplayView &player : scene.players)
		{
			const std::array<int, 4> viewport = viewport_from_rect(player.normalized_rect, framebuffer_width, framebuffer_height);
			if (viewport[2] <= 0 || viewport[3] <= 0)
				continue;

			glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
			glScissor(viewport[0], viewport[1], viewport[2], viewport[3]);
			glEnable(GL_SCISSOR_TEST);

			const float aspect_ratio = static_cast<float>(viewport[2]) / static_cast<float>(viewport[3]);
			const Vec3 eye = {0.0f, player.camera.camera_height, player.camera.camera_distance};
			const float look_pitch = radians(player.camera.pitch_degrees);
			const float look_depth = 7.5f;
			const Vec3 target = {
				0.0f,
				player.camera.camera_height - std::tan(look_pitch) * (player.camera.camera_distance + look_depth),
				-look_depth,
			};
			const Mat4 projection = perspective(radians(player.camera.field_of_view_degrees), aspect_ratio, 0.1f, 50.0f);
			const Mat4 view = look_at(eye, target, {0.0f, 1.0f, 0.0f});
			const Mat4 mvp = multiply(projection, view);

			glUniformMatrix4fv(u_mvp_location_, 1, GL_FALSE, mvp.v);
			glUniform1i(u_use_vertex_fade_location_, 1);

			batch.clear();
			for (const InstrumentLaneView &lane : player.world.lanes)
			{
				append_highway_background(batch, lane, player.world.style);
				append_measure_lines(batch, lane, player.world.style, player.camera.visible_depth_seconds);
				append_sustains(batch, lane, player.world.style, player.camera.visible_depth_seconds);
				append_notes(batch, lane, player.world.style, player.camera.visible_depth_seconds);
				append_hit_line(batch, lane, player.world.style);
			}

			if (batch.vertices.empty() || batch.indices.empty())
				continue;

			glBindBuffer(GL_ARRAY_BUFFER, vbo_);
			glBufferData(
				GL_ARRAY_BUFFER,
				static_cast<GLsizeiptr>(batch.vertices.size() * sizeof(RenderVertex)),
				batch.vertices.data(),
				GL_STREAM_DRAW);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
			glBufferData(
				GL_ELEMENT_ARRAY_BUFFER,
				static_cast<GLsizeiptr>(batch.indices.size() * sizeof(std::uint32_t)),
				batch.indices.data(),
				GL_STREAM_DRAW);
			glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(batch.indices.size()), GL_UNSIGNED_INT, nullptr);
		}

		glBindVertexArray(0);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
		glUseProgram(0);
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_SCISSOR_TEST);
		glDisable(GL_BLEND);
	}
}
