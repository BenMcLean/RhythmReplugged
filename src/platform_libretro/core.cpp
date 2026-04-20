#include <libretro.h>

#include "core/app/AppCore.h"
#include "platform_libretro/ImGuiLibretroPlatform.h"
#include "platform_libretro/NativeFileSystem.h"
#include "ui/AppUiHost.h"

#include <SDL3/SDL_opengl.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <string>

namespace
{
	using namespace rhythmreplugged;

	constexpr unsigned kFrameWidth = 1280;
	constexpr unsigned kFrameHeight = 720;
	constexpr char kOpenGlGlslVersion[] = "#version 130";

	#if defined(_WIN32)
	#define RR_LIBRETRO_EXPORT extern "C" __declspec(dllexport)
	#else
	#define RR_LIBRETRO_EXPORT extern "C"
	#endif

	retro_environment_t g_environment = nullptr;
	retro_video_refresh_t g_video_refresh = nullptr;
	retro_audio_sample_t g_audio_sample = nullptr;
	retro_audio_sample_batch_t g_audio_sample_batch = nullptr;
	retro_input_poll_t g_input_poll = nullptr;
	retro_input_state_t g_input_state = nullptr;
	retro_log_callback g_log_callback{};
	retro_hw_render_callback g_hw_render{};

	NativeFileSystem g_file_system;
	AppCore g_app(g_file_system);
	OpenGlCoverTextures g_cover_textures;
	RetroInputState g_previous_input{};
	std::string g_root_path;
	bool g_is_loaded = false;
	bool g_gl_ready = false;
	using GlBindFramebufferProc = void(APIENTRY *)(GLenum target, GLuint framebuffer);
	GlBindFramebufferProc g_gl_bind_framebuffer = nullptr;

	void log_message(retro_log_level level, const char *format, ...)
	{
		if (g_log_callback.log == nullptr)
			return;

		char buffer[1024];
		va_list args;
		va_start(args, format);
		vsnprintf(buffer, sizeof(buffer), format, args);
		va_end(args);
		g_log_callback.log(level, "%s", buffer);
	}

	std::string to_lower_copy(std::string text)
	{
		std::transform(text.begin(), text.end(), text.begin(),
			[](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
		return text;
	}

	std::string choose_song_root(const std::string &content_path)
	{
		const std::string canonical_content = g_file_system.canonicalize_path(content_path);
		if (canonical_content.empty())
			return {};

		if (g_file_system.path_is_directory(canonical_content))
		{
			if (g_file_system.path_exists(canonical_content + "/song.ini"))
				return g_file_system.parent_path(canonical_content);
			return canonical_content;
		}

		const std::filesystem::path path(canonical_content);
		const std::string filename = to_lower_copy(path.filename().string());
		if (filename == "song.ini" ||
			filename == "song.ogg" ||
			filename == "notes.mid" ||
			filename == "notes.midi" ||
			filename == "notes.chart" ||
			filename == "notes.txt")
		{
			return path.parent_path().parent_path().generic_string();
		}

		return path.parent_path().generic_string();
	}

	bool pressed(bool current, bool previous)
	{
		return current && !previous;
	}

	RetroInputState poll_input()
	{
		RetroInputState input;
		if (g_input_poll != nullptr)
			g_input_poll();
		if (g_input_state == nullptr)
			return input;

		auto joypad_pressed = [](unsigned id)
		{
			return g_input_state(0, RETRO_DEVICE_JOYPAD, 0, id) != 0;
		};

		input.up = joypad_pressed(RETRO_DEVICE_ID_JOYPAD_UP);
		input.down = joypad_pressed(RETRO_DEVICE_ID_JOYPAD_DOWN);
		input.left = joypad_pressed(RETRO_DEVICE_ID_JOYPAD_LEFT);
		input.right = joypad_pressed(RETRO_DEVICE_ID_JOYPAD_RIGHT);
		input.a = joypad_pressed(RETRO_DEVICE_ID_JOYPAD_A);
		input.b = joypad_pressed(RETRO_DEVICE_ID_JOYPAD_B);
		input.x = joypad_pressed(RETRO_DEVICE_ID_JOYPAD_X);
		input.y = joypad_pressed(RETRO_DEVICE_ID_JOYPAD_Y);
		input.start = joypad_pressed(RETRO_DEVICE_ID_JOYPAD_START);
		input.select = joypad_pressed(RETRO_DEVICE_ID_JOYPAD_SELECT);
		input.l = joypad_pressed(RETRO_DEVICE_ID_JOYPAD_L);
		input.r = joypad_pressed(RETRO_DEVICE_ID_JOYPAD_R);
		return input;
	}

	void context_reset()
	{
		if (g_gl_ready)
		{
			g_cover_textures.clear();
			ImGui_ImplOpenGL3_Shutdown();
			g_gl_ready = false;
		}

		if (g_hw_render.get_proc_address != nullptr)
		{
			g_gl_bind_framebuffer = reinterpret_cast<GlBindFramebufferProc>(
				g_hw_render.get_proc_address("glBindFramebuffer"));
			if (g_gl_bind_framebuffer == nullptr)
			{
				g_gl_bind_framebuffer = reinterpret_cast<GlBindFramebufferProc>(
					g_hw_render.get_proc_address("glBindFramebufferEXT"));
			}
		}

		if (!ImGui_ImplOpenGL3_Init(kOpenGlGlslVersion))
		{
			log_message(RETRO_LOG_ERROR, "ImGui OpenGL init failed.\n");
			return;
		}

		g_gl_ready = true;
	}

	void context_destroy()
	{
		if (!g_gl_ready)
			return;

		g_cover_textures.clear();
		ImGui_ImplOpenGL3_Shutdown();
		g_gl_ready = false;
		g_gl_bind_framebuffer = nullptr;
	}

	void submit_audio()
	{
		const AudioBatch &batch = g_app.audio_batch();
		if (batch.samples.empty())
			return;

		if (g_audio_sample_batch != nullptr)
		{
			g_audio_sample_batch(batch.samples.data(), batch.frame_count());
			return;
		}

		if (g_audio_sample == nullptr)
			return;

		for (size_t frame = 0; frame < batch.frame_count(); ++frame)
		{
			const size_t index = frame * 2;
			g_audio_sample(batch.samples[index], batch.samples[index + 1]);
		}
	}
}

RR_LIBRETRO_EXPORT unsigned retro_api_version(void)
{
	return RETRO_API_VERSION;
}

RR_LIBRETRO_EXPORT void retro_set_environment(retro_environment_t cb)
{
	g_environment = cb;
	if (g_environment != nullptr)
		g_environment(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &g_log_callback);
}

RR_LIBRETRO_EXPORT void retro_set_video_refresh(retro_video_refresh_t cb)
{
	g_video_refresh = cb;
}

RR_LIBRETRO_EXPORT void retro_set_audio_sample(retro_audio_sample_t cb)
{
	g_audio_sample = cb;
}

RR_LIBRETRO_EXPORT void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
	g_audio_sample_batch = cb;
}

RR_LIBRETRO_EXPORT void retro_set_input_poll(retro_input_poll_t cb)
{
	g_input_poll = cb;
}

RR_LIBRETRO_EXPORT void retro_set_input_state(retro_input_state_t cb)
{
	g_input_state = cb;
}

RR_LIBRETRO_EXPORT void retro_init(void)
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	initialize_app_imgui(kDefaultUiScale);
	initialize_imgui_libretro_platform();
	g_app.set_audio_batch_enabled(true);
}

RR_LIBRETRO_EXPORT void retro_deinit(void)
{
	context_destroy();
	g_cover_textures.clear();
	g_app.retro_deinit();
	g_root_path.clear();
	g_is_loaded = false;
	g_gl_ready = false;
	ImGui::DestroyContext();
}

RR_LIBRETRO_EXPORT void retro_get_system_info(struct retro_system_info *info)
{
	if (info == nullptr)
		return;

	info->library_name = "Rhythm Replugged";
	info->library_version = "0.1";
	info->valid_extensions = "ini|mid|midi|chart|txt|ogg";
	info->need_fullpath = true;
	info->block_extract = false;
}

RR_LIBRETRO_EXPORT void retro_get_system_av_info(struct retro_system_av_info *info)
{
	if (info == nullptr)
		return;

	info->geometry.base_width = kFrameWidth;
	info->geometry.base_height = kFrameHeight;
	info->geometry.max_width = kFrameWidth;
	info->geometry.max_height = kFrameHeight;
	info->geometry.aspect_ratio = static_cast<float>(kFrameWidth) / static_cast<float>(kFrameHeight);
	info->timing.fps = static_cast<double>(kAppFramesPerSecond);
	info->timing.sample_rate = 44100.0;
}

RR_LIBRETRO_EXPORT void retro_set_controller_port_device(unsigned port, unsigned device)
{
	(void)port;
	(void)device;
}

RR_LIBRETRO_EXPORT void retro_reset(void)
{
	if (g_root_path.empty())
		return;

	std::string error_message;
	g_is_loaded = g_app.retro_init(g_root_path, error_message);
	if (!g_is_loaded)
		 log_message(RETRO_LOG_ERROR, "Failed to reset content '%s': %s\n", g_root_path.c_str(), error_message.c_str());
}

RR_LIBRETRO_EXPORT bool retro_load_game(const struct retro_game_info *game)
{
	if (game == nullptr || game->path == nullptr)
	{
		log_message(RETRO_LOG_ERROR, "No content path was supplied.\n");
		return false;
	}

	g_hw_render = {};
	g_hw_render.context_type = RETRO_HW_CONTEXT_OPENGL;
	g_hw_render.context_reset = context_reset;
	g_hw_render.context_destroy = context_destroy;
	g_hw_render.depth = true;
	g_hw_render.stencil = true;
	g_hw_render.bottom_left_origin = true;
	g_hw_render.version_major = 3;
	g_hw_render.version_minor = 0;
	g_hw_render.cache_context = false;
	g_hw_render.debug_context = false;
	if (g_environment == nullptr || !g_environment(RETRO_ENVIRONMENT_SET_HW_RENDER, &g_hw_render))
	{
		log_message(RETRO_LOG_ERROR, "Frontend rejected OpenGL hardware rendering.\n");
		return false;
	}

	g_root_path = choose_song_root(game->path);
	if (g_root_path.empty())
	{
		log_message(RETRO_LOG_ERROR, "Could not derive a song root from '%s'.\n", game->path);
		return false;
	}

	std::string error_message;
	g_is_loaded = g_app.retro_init(g_root_path, error_message);
	if (!g_is_loaded)
	{
		log_message(RETRO_LOG_ERROR, "App init failed for '%s': %s\n", g_root_path.c_str(), error_message.c_str());
		return false;
	}

	return true;
}

RR_LIBRETRO_EXPORT void retro_unload_game(void)
{
	g_cover_textures.clear();
	g_app.retro_deinit();
	g_root_path.clear();
	g_is_loaded = false;
}

RR_LIBRETRO_EXPORT unsigned retro_get_region(void)
{
	return 0;
}

RR_LIBRETRO_EXPORT void *retro_get_memory_data(unsigned id)
{
	(void)id;
	return nullptr;
}

RR_LIBRETRO_EXPORT size_t retro_get_memory_size(unsigned id)
{
	(void)id;
	return 0;
}

RR_LIBRETRO_EXPORT size_t retro_serialize_size(void)
{
	return 0;
}

RR_LIBRETRO_EXPORT bool retro_serialize(void *data, size_t size)
{
	(void)data;
	(void)size;
	return false;
}

RR_LIBRETRO_EXPORT bool retro_unserialize(const void *data, size_t size)
{
	(void)data;
	(void)size;
	return false;
}

RR_LIBRETRO_EXPORT void retro_cheat_reset(void)
{
}

RR_LIBRETRO_EXPORT void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
	(void)index;
	(void)enabled;
	(void)code;
}

RR_LIBRETRO_EXPORT bool retro_load_game_special(unsigned game_type, const struct retro_game_info *info, size_t num_info)
{
	(void)game_type;
	(void)info;
	(void)num_info;
	return false;
}

RR_LIBRETRO_EXPORT void retro_run(void)
{
	if (!g_is_loaded || !g_gl_ready)
		return;

	const RetroInputState input = poll_input();
	if (g_app.mode() == AppMode::PrototypePlayer)
	{
		if (pressed(input.l, g_previous_input.l))
			g_app.nudge_timing_offset_seconds(-0.005);
		if (pressed(input.r, g_previous_input.r))
			g_app.nudge_timing_offset_seconds(0.005);
		if (pressed(input.select, g_previous_input.select))
			g_app.reset_timing_offset();
	}

	g_app.retro_run(input);
	g_previous_input = input;
	submit_audio();

	ImGui_ImplOpenGL3_NewFrame();
	begin_imgui_libretro_frame(
		input,
		ImVec2(static_cast<float>(kFrameWidth), static_cast<float>(kFrameHeight)),
		1.0f / static_cast<float>(kAppFramesPerSecond));
	render_app_ui(
		g_app,
		ImVec2(static_cast<float>(kFrameWidth), static_cast<float>(kFrameHeight)),
		kDefaultUiScale,
		g_cover_textures);
	ImGui::Render();

	GLuint framebuffer = 0;
	if (g_hw_render.get_current_framebuffer != nullptr)
		framebuffer = static_cast<GLuint>(g_hw_render.get_current_framebuffer());
	if (g_gl_bind_framebuffer != nullptr)
		g_gl_bind_framebuffer(GL_FRAMEBUFFER, framebuffer);
	glViewport(0, 0, static_cast<GLsizei>(kFrameWidth), static_cast<GLsizei>(kFrameHeight));
	glClearColor(12.0f / 255.0f, 14.0f / 255.0f, 20.0f / 255.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

	if (g_video_refresh != nullptr)
		g_video_refresh(RETRO_HW_FRAME_BUFFER_VALID, kFrameWidth, kFrameHeight, 0);
}
