#pragma once

#include "core/app/AppTypes.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace rhythmreplugged::render_gl
{
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

	class GameplayRendererGl
	{
	public:
		enum class GraphicsApi
		{
			OpenGl33Core,
			OpenGlEs3,
		};

		void set_graphics_api(GraphicsApi api);
		bool initialize(std::string &error_message);
		void shutdown();

		void on_context_lost();
		void on_context_restored(std::string &error_message);

		void render(const core::GameplaySceneView &scene, int framebuffer_width, int framebuffer_height);

	private:
		bool create_device_objects(std::string &error_message);
		void destroy_device_objects();

		unsigned int shader_program_ = 0;
		unsigned int vertex_shader_ = 0;
		unsigned int fragment_shader_ = 0;
		unsigned int vao_ = 0;
		unsigned int vbo_ = 0;
		unsigned int ebo_ = 0;
		int u_mvp_location_ = -1;
		int u_use_vertex_fade_location_ = -1;
		MeshBatch mesh_batch_;
		size_t vbo_capacity_bytes_ = 0;
		size_t ebo_capacity_bytes_ = 0;
		GraphicsApi graphics_api_ = GraphicsApi::OpenGl33Core;
		bool initialized_ = false;
	};
}
