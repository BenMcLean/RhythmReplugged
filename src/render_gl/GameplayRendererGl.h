#pragma once

#include "core/app/AppTypes.h"

#include <string>

namespace rhythmreplugged::render_gl
{
	class GameplayRendererGl
	{
	public:
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
		bool initialized_ = false;
	};
}
