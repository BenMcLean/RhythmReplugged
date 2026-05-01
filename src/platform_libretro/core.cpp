#include <libretro.h>

#include "core/app/AppCore.h"
#include "core/app/AppLaunch.h"
#include "frontend_contract/FrontendOptions.h"
#include "platform_libretro/ImGuiLibretroPlatform.h"
#include "platform_libretro/FileSystem.h"
#include "render_gl/GameplayRendererGl.h"
#include "ui/AppUiHost.h"

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#if defined(RR_RENDER_OPENGL_ES3)
#include <GLES3/gl3.h>
#else
#include <imgui_impl_opengl3_loader.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#endif

namespace
{
	using namespace rhythmreplugged::core;
	using namespace rhythmreplugged::render_gl;
	using namespace rhythmreplugged::ui;
	using namespace rhythmreplugged::platform_libretro;

	constexpr unsigned kFrameWidth = 1280;
	constexpr unsigned kFrameHeight = 720;
	constexpr size_t kSerializedPlayStateMaxSize = 4096;
	constexpr retro_usec_t kNominalFrameTimeUsec = 1000000 / kAppFramesPerSecond;
	constexpr retro_usec_t kMinimumFrameTimeUsec = kNominalFrameTimeUsec / 2;
	constexpr retro_usec_t kMaximumFrameTimeUsec = kNominalFrameTimeUsec * 2;

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
	GameplayRendererGl::GraphicsApi g_graphics_api = GameplayRendererGl::GraphicsApi::OpenGl33Core;

	FileSystem g_file_system;
	AppCore g_app(g_file_system);
	OpenGlCoverTextures g_cover_textures(g_file_system);
	GameplayRendererGl g_gameplay_renderer;
	std::string g_root_path;
	bool g_restrict_to_startup_song = false;
	bool g_is_loaded = false;
	bool g_gl_ready = false;
	int g_reported_sample_rate = 0;
	std::vector<std::int16_t> g_pending_audio_samples;
	retro_usec_t g_last_frame_time_usec = kNominalFrameTimeUsec;
	std::uint64_t g_audio_frame_time_remainder = 0;
	float g_mouse_x = static_cast<float>(kFrameWidth) * 0.5f;
	float g_mouse_y = static_cast<float>(kFrameHeight) * 0.5f;
	using GlBindFramebufferProc = void (*)(GLenum target, GLuint framebuffer);
	GlBindFramebufferProc g_gl_bind_framebuffer = nullptr;

	const retro_system_content_info_override kContentInfoOverrides[] = {
		{"ini", true, false},
		{nullptr, false, false},
	};

	const retro_core_option_v2_category kOptionCategories[] = {
		{
			"gameplay",
			"Gameplay",
			"Default song startup preferences.",
		},
		{nullptr, nullptr, nullptr},
	};

	const retro_core_option_v2_definition kCoreOptionDefinitions[] = {
		{
			::rhythmreplugged::frontend_contract::kFrontendOptionDefinitions[0].libretro_key,
			::rhythmreplugged::frontend_contract::kFrontendOptionDefinitions[0].display_name,
			nullptr,
			::rhythmreplugged::frontend_contract::kFrontendOptionDefinitions[0].description,
			nullptr,
			"gameplay",
			{
				{
					::rhythmreplugged::frontend_contract::kFrontendInstrumentChoices[0].value,
					::rhythmreplugged::frontend_contract::kFrontendInstrumentChoices[0].label,
				},
				{
					::rhythmreplugged::frontend_contract::kFrontendInstrumentChoices[1].value,
					::rhythmreplugged::frontend_contract::kFrontendInstrumentChoices[1].label,
				},
				{
					::rhythmreplugged::frontend_contract::kFrontendInstrumentChoices[2].value,
					::rhythmreplugged::frontend_contract::kFrontendInstrumentChoices[2].label,
				},
				{
					::rhythmreplugged::frontend_contract::kFrontendInstrumentChoices[3].value,
					::rhythmreplugged::frontend_contract::kFrontendInstrumentChoices[3].label,
				},
				{
					::rhythmreplugged::frontend_contract::kFrontendInstrumentChoices[4].value,
					::rhythmreplugged::frontend_contract::kFrontendInstrumentChoices[4].label,
				},
				{
					::rhythmreplugged::frontend_contract::kFrontendInstrumentChoices[5].value,
					::rhythmreplugged::frontend_contract::kFrontendInstrumentChoices[5].label,
				},
			},
			::rhythmreplugged::frontend_contract::kFrontendOptionDefinitions[0].default_value,
		},
		{
			::rhythmreplugged::frontend_contract::kFrontendOptionDefinitions[1].libretro_key,
			::rhythmreplugged::frontend_contract::kFrontendOptionDefinitions[1].display_name,
			nullptr,
			::rhythmreplugged::frontend_contract::kFrontendOptionDefinitions[1].description,
			nullptr,
			"gameplay",
			{
				{
					::rhythmreplugged::frontend_contract::kFrontendDifficultyChoices[0].value,
					::rhythmreplugged::frontend_contract::kFrontendDifficultyChoices[0].label,
				},
				{
					::rhythmreplugged::frontend_contract::kFrontendDifficultyChoices[1].value,
					::rhythmreplugged::frontend_contract::kFrontendDifficultyChoices[1].label,
				},
				{
					::rhythmreplugged::frontend_contract::kFrontendDifficultyChoices[2].value,
					::rhythmreplugged::frontend_contract::kFrontendDifficultyChoices[2].label,
				},
				{
					::rhythmreplugged::frontend_contract::kFrontendDifficultyChoices[3].value,
					::rhythmreplugged::frontend_contract::kFrontendDifficultyChoices[3].label,
				},
				{
					::rhythmreplugged::frontend_contract::kFrontendDifficultyChoices[4].value,
					::rhythmreplugged::frontend_contract::kFrontendDifficultyChoices[4].label,
				},
			},
			::rhythmreplugged::frontend_contract::kFrontendOptionDefinitions[1].default_value,
		},
		{},
	};

	const retro_core_options_v2 kCoreOptions = {
		const_cast<retro_core_option_v2_category *>(kOptionCategories),
		const_cast<retro_core_option_v2_definition *>(kCoreOptionDefinitions),
	};

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

	const char *glsl_version_for_api(GameplayRendererGl::GraphicsApi api)
	{
		switch (api)
		{
		case GameplayRendererGl::GraphicsApi::OpenGlEs3:
			return "#version 300 es";
		case GameplayRendererGl::GraphicsApi::OpenGl33Core:
		default:
			return "#version 330 core";
		}
	}

	::rhythmreplugged::frontend_contract::RetroInputState poll_input()
	{
		::rhythmreplugged::frontend_contract::RetroInputState input;
		input.mouse_x = g_mouse_x;
		input.mouse_y = g_mouse_y;
		if (g_input_poll != nullptr)
			g_input_poll();
		if (g_input_state == nullptr)
			return input;

		auto joypad_pressed = [](unsigned id)
		{
			return g_input_state(0, RETRO_DEVICE_JOYPAD, 0, id) != 0;
		};
		auto keyboard_pressed = [](unsigned id)
		{
			return g_input_state(0, RETRO_DEVICE_KEYBOARD, 0, id) != 0;
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

		// Keyboard navigation is kept separate from the five-lane gameplay keys
		// so number-row frets do not trigger browser actions or ImGui navigation.
		input.up = input.up || keyboard_pressed(RETROK_UP);
		input.down = input.down || keyboard_pressed(RETROK_DOWN);
		input.a = input.a || keyboard_pressed(RETROK_RETURN) || keyboard_pressed(RETROK_SPACE);
		input.b = input.b || keyboard_pressed(RETROK_0) || keyboard_pressed(RETROK_BACKSPACE);
		input.l = input.l || keyboard_pressed(RETROK_LEFTBRACKET);
		input.r = input.r || keyboard_pressed(RETROK_RIGHTBRACKET);
		input.lane_1 = keyboard_pressed(RETROK_1);
		input.lane_2 = keyboard_pressed(RETROK_2);
		input.lane_3 = keyboard_pressed(RETROK_3);
		input.lane_4 = keyboard_pressed(RETROK_4);
		input.lane_5 = keyboard_pressed(RETROK_5);
		for (unsigned index = 0; index < input.letter_keys.size(); ++index)
			input.letter_keys[index] = keyboard_pressed(RETROK_a + index);

		const int mouse_delta_x = g_input_state(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X);
		const int mouse_delta_y = g_input_state(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y);
		if (mouse_delta_x != 0 || mouse_delta_y != 0)
		{
			g_mouse_x = std::clamp(g_mouse_x + static_cast<float>(mouse_delta_x), 0.0f, static_cast<float>(kFrameWidth - 1));
			g_mouse_y = std::clamp(g_mouse_y + static_cast<float>(mouse_delta_y), 0.0f, static_cast<float>(kFrameHeight - 1));
			input.mouse_active = true;
		}

		const int pointer_pressed = g_input_state(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_PRESSED);
		const int pointer_offscreen = g_input_state(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_IS_OFFSCREEN);
		if (pointer_pressed != 0 && pointer_offscreen == 0)
		{
			const int pointer_x = g_input_state(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_X);
			const int pointer_y = g_input_state(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_Y);
			g_mouse_x = std::clamp(
				(static_cast<float>(pointer_x) + 32767.0f) * (static_cast<float>(kFrameWidth - 1) / 65534.0f),
				0.0f,
				static_cast<float>(kFrameWidth - 1));
			g_mouse_y = std::clamp(
				(static_cast<float>(pointer_y) + 32767.0f) * (static_cast<float>(kFrameHeight - 1) / 65534.0f),
				0.0f,
				static_cast<float>(kFrameHeight - 1));
			input.mouse_active = true;
		}

		input.mouse_x = g_mouse_x;
		input.mouse_y = g_mouse_y;
		input.mouse_left = g_input_state(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT) != 0;
		input.mouse_right = g_input_state(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT) != 0;
		input.mouse_middle = g_input_state(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_MIDDLE) != 0;
		input.mouse_wheel_x =
			static_cast<float>(g_input_state(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_HORIZ_WHEELUP) -
				g_input_state(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_HORIZ_WHEELDOWN));
		input.mouse_wheel_y =
			static_cast<float>(g_input_state(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_WHEELUP) -
				g_input_state(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_WHEELDOWN));
		if (input.mouse_left || input.mouse_right || input.mouse_middle ||
			input.mouse_wheel_x != 0.0f || input.mouse_wheel_y != 0.0f)
		{
			input.mouse_active = true;
		}

		return input;
	}

	std::string query_frontend_directory(unsigned cmd)
	{
		if (g_environment == nullptr)
			return {};

		const char *path = nullptr;
		if (!g_environment(cmd, &path) || path == nullptr || path[0] == '\0')
			return {};

		return path;
	}

	std::string query_browser_root_hint()
	{
		std::string path = query_frontend_directory(RETRO_ENVIRONMENT_GET_FILE_BROWSER_START_DIRECTORY);
		if (!path.empty())
			return path;

		path = query_frontend_directory(RETRO_ENVIRONMENT_GET_CONTENT_DIRECTORY);
		if (!path.empty())
			return path;

		path = query_frontend_directory(RETRO_ENVIRONMENT_GET_PLAYLIST_DIRECTORY);
		if (!path.empty())
			return path;

		return query_frontend_directory(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY);
	}

	int current_output_sample_rate()
	{
		const int sample_rate = g_app.sample_rate();
		return sample_rate > 0 ? sample_rate : 44100;
	}

	void fill_system_av_info(retro_system_av_info &info)
	{
		info.geometry.base_width = kFrameWidth;
		info.geometry.base_height = kFrameHeight;
		info.geometry.max_width = kFrameWidth;
		info.geometry.max_height = kFrameHeight;
		info.geometry.aspect_ratio = static_cast<float>(kFrameWidth) / static_cast<float>(kFrameHeight);
		info.timing.fps = static_cast<double>(kAppFramesPerSecond);
		info.timing.sample_rate = static_cast<double>(current_output_sample_rate());
	}

	void update_frontend_av_info()
	{
		if (g_environment == nullptr)
			return;

		retro_system_av_info info{};
		fill_system_av_info(info);
		g_environment(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &info);
	}

	void frame_time_callback(retro_usec_t usec)
	{
		if (usec > 0)
			g_last_frame_time_usec = usec;
	}

	void register_core_options()
	{
		if (g_environment == nullptr)
			return;

		g_environment(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2, const_cast<retro_core_options_v2 *>(&kCoreOptions));
	}

	::rhythmreplugged::frontend_contract::FrontendOptions query_frontend_options()
	{
		::rhythmreplugged::frontend_contract::FrontendOptions options;
		if (g_environment == nullptr)
			return options;

		for (const auto &definition : ::rhythmreplugged::frontend_contract::frontend_option_definitions())
		{
			retro_variable variable{};
			variable.key = definition.libretro_key;
			if (!g_environment(RETRO_ENVIRONMENT_GET_VARIABLE, &variable) || variable.value == nullptr)
				continue;

			::rhythmreplugged::frontend_contract::set_frontend_option_value(options, definition.id, variable.value);
		}

		return options;
	}

	void sync_frontend_options()
	{
		g_app.set_frontend_options(query_frontend_options());
	}

	void sync_frontend_sample_rate()
	{
		const int sample_rate = current_output_sample_rate();
		if (sample_rate == g_reported_sample_rate)
			return;

		g_pending_audio_samples.clear();
		g_audio_frame_time_remainder = 0;
		update_frontend_av_info();
		g_reported_sample_rate = sample_rate;
	}

	void context_reset()
	{
		if (g_gl_ready)
		{
			g_gameplay_renderer.on_context_lost();
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

		g_gameplay_renderer.set_graphics_api(g_graphics_api);
		if (!ImGui_ImplOpenGL3_Init(glsl_version_for_api(g_graphics_api)))
		{
			log_message(RETRO_LOG_ERROR, "ImGui OpenGL init failed.\n");
			return;
		}

		std::string renderer_error;
		g_gameplay_renderer.on_context_restored(renderer_error);
		if (!renderer_error.empty())
		{
			log_message(RETRO_LOG_ERROR, "Gameplay renderer init failed: %s\n", renderer_error.c_str());
			ImGui_ImplOpenGL3_Shutdown();
			return;
		}

		g_gl_ready = true;
	}

	void context_destroy()
	{
		if (!g_gl_ready)
			return;

		g_gameplay_renderer.on_context_lost();
		g_cover_textures.clear();
		ImGui_ImplOpenGL3_Shutdown();
		g_gl_ready = false;
		g_gl_bind_framebuffer = nullptr;
	}

	void queue_audio_from_stream()
	{
		if (g_app.mode() != AppMode::Gameplay)
			return;

		const int sample_rate = g_app.sample_rate();
		if (sample_rate <= 0)
			return;

		const retro_usec_t frame_time_usec = (std::clamp)(g_last_frame_time_usec, kMinimumFrameTimeUsec, kMaximumFrameTimeUsec);
		g_audio_frame_time_remainder +=
			static_cast<std::uint64_t>(sample_rate) * static_cast<std::uint64_t>(frame_time_usec);
		const size_t frame_count = static_cast<size_t>(g_audio_frame_time_remainder / 1000000ull);
		g_audio_frame_time_remainder %= 1000000ull;
		if (frame_count == 0)
			return;

		const size_t sample_offset = g_pending_audio_samples.size();
		g_pending_audio_samples.resize(sample_offset + frame_count * 2);
		g_app.render_interleaved_s16(g_pending_audio_samples.data() + sample_offset, frame_count);
	}

	void submit_audio()
	{
		queue_audio_from_stream();
		if (g_pending_audio_samples.empty())
			return;

		if (g_audio_sample_batch != nullptr)
		{
			const size_t pending_frames = g_pending_audio_samples.size() / 2;
			const size_t consumed_frames = g_audio_sample_batch(g_pending_audio_samples.data(), pending_frames);
			const size_t consumed_samples = (std::min)(g_pending_audio_samples.size(), consumed_frames * 2);
			g_pending_audio_samples.erase(
				g_pending_audio_samples.begin(),
				g_pending_audio_samples.begin() + static_cast<std::ptrdiff_t>(consumed_samples));
			return;
		}

		if (g_audio_sample == nullptr)
			return;

		for (size_t frame = 0; frame < g_pending_audio_samples.size() / 2; ++frame)
		{
			const size_t index = frame * 2;
			g_audio_sample(g_pending_audio_samples[index], g_pending_audio_samples[index + 1]);
		}

		g_pending_audio_samples.clear();
	}

	void clear_pending_audio_queue()
	{
		g_pending_audio_samples.clear();
		g_audio_frame_time_remainder = 0;
	}

	bool configure_hw_render()
	{
		if (g_environment == nullptr)
		{
			log_message(RETRO_LOG_ERROR, "No libretro environment callback is available.\n");
			return false;
		}

		struct ContextCandidate
		{
			retro_hw_context_type context_type;
			GameplayRendererGl::GraphicsApi graphics_api;
			unsigned major;
			unsigned minor;
			const char *label;
		};

		std::vector<ContextCandidate> candidates;
		auto append_candidate = [&](retro_hw_context_type context_type,
			GameplayRendererGl::GraphicsApi graphics_api,
			unsigned major,
			unsigned minor,
			const char *label)
		{
			for (const ContextCandidate &candidate : candidates)
			{
				if (candidate.context_type == context_type)
					return;
			}

			candidates.push_back({context_type, graphics_api, major, minor, label});
		};

#if defined(RR_RENDER_OPENGL_ES3)
		append_candidate(RETRO_HW_CONTEXT_OPENGLES3, GameplayRendererGl::GraphicsApi::OpenGlEs3, 3, 0, "OpenGL ES 3");
#else
		append_candidate(RETRO_HW_CONTEXT_OPENGLES3, GameplayRendererGl::GraphicsApi::OpenGlEs3, 3, 0, "OpenGL ES 3");
		append_candidate(RETRO_HW_CONTEXT_OPENGL_CORE, GameplayRendererGl::GraphicsApi::OpenGl33Core, 3, 3, "OpenGL 3.3 Core");
#endif

		for (const ContextCandidate &candidate : candidates)
		{
			g_hw_render = {};
			g_hw_render.context_type = candidate.context_type;
			g_hw_render.context_reset = context_reset;
			g_hw_render.context_destroy = context_destroy;
			g_hw_render.depth = true;
			g_hw_render.stencil = true;
			g_hw_render.bottom_left_origin = true;
			g_hw_render.version_major = candidate.major;
			g_hw_render.version_minor = candidate.minor;
			g_hw_render.cache_context = false;
			g_hw_render.debug_context = false;
			if (g_environment(RETRO_ENVIRONMENT_SET_HW_RENDER, &g_hw_render))
			{
				g_graphics_api = candidate.graphics_api;
				log_message(RETRO_LOG_INFO, "Configured libretro hardware rendering with %s.\n", candidate.label);
				return true;
			}
		}

#if defined(RR_RENDER_OPENGL_ES3)
		log_message(RETRO_LOG_ERROR, "Frontend rejected OpenGL ES 3 hardware rendering.\n");
#else
		log_message(RETRO_LOG_ERROR, "Frontend rejected OpenGL ES 3 and OpenGL 3.3 Core hardware rendering.\n");
#endif
		return false;
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
	{
		g_environment(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &g_log_callback);
		register_core_options();
		retro_vfs_interface_info vfs_info{};
		vfs_info.required_interface_version = 3;
		if (g_environment(RETRO_ENVIRONMENT_GET_VFS_INTERFACE, &vfs_info) && vfs_info.iface != nullptr)
			g_file_system.set_vfs_interface(vfs_info.required_interface_version, vfs_info.iface);
		else
			g_file_system.set_vfs_interface(0, nullptr);
		bool support_no_game = true;
		g_environment(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &support_no_game);
		g_environment(RETRO_ENVIRONMENT_SET_CONTENT_INFO_OVERRIDE, const_cast<retro_system_content_info_override *>(kContentInfoOverrides));
		retro_frame_time_callback frame_time{};
		frame_time.callback = frame_time_callback;
		frame_time.reference = kNominalFrameTimeUsec;
		g_environment(RETRO_ENVIRONMENT_SET_FRAME_TIME_CALLBACK, &frame_time);
	}
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
	g_app.set_audio_batch_enabled(false);
	g_app.set_frontend_options(query_frontend_options());
	g_reported_sample_rate = 0;
	g_pending_audio_samples.clear();
	g_last_frame_time_usec = kNominalFrameTimeUsec;
	g_audio_frame_time_remainder = 0;
}

RR_LIBRETRO_EXPORT void retro_deinit(void)
{
	context_destroy();
	g_cover_textures.clear();
	g_app.retro_deinit();
	g_root_path.clear();
	g_is_loaded = false;
	g_gl_ready = false;
	g_reported_sample_rate = 0;
	g_pending_audio_samples.clear();
	g_last_frame_time_usec = kNominalFrameTimeUsec;
	g_audio_frame_time_remainder = 0;
	g_gameplay_renderer.shutdown();
	ImGui::DestroyContext();
}

RR_LIBRETRO_EXPORT void retro_get_system_info(struct retro_system_info *info)
{
	if (info == nullptr)
		return;

	info->library_name = "Rhythm Replugged";
	info->library_version = "0.1";
	info->valid_extensions = "ini|png|jpg|jpeg|bmp|mid|midi|chart|txt|ogg";
	info->need_fullpath = true;
	info->block_extract = false;
}

RR_LIBRETRO_EXPORT void retro_get_system_av_info(struct retro_system_av_info *info)
{
	if (info == nullptr)
		return;

	fill_system_av_info(*info);
	g_reported_sample_rate = current_output_sample_rate();
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
	sync_frontend_options();
	clear_pending_audio_queue();
	AppLaunchRequest launch_request;
	launch_request.songs_root_path = g_root_path;
	launch_request.restrict_to_startup_song = g_restrict_to_startup_song;
	launch_request.frontend_options = query_frontend_options();
	g_is_loaded = g_app.retro_init(launch_request, error_message);
	if (!g_is_loaded)
		log_message(RETRO_LOG_ERROR, "Failed to reset content '%s': %s\n", g_root_path.c_str(), error_message.c_str());
	else
		update_frontend_av_info();
}

RR_LIBRETRO_EXPORT bool retro_load_game(const struct retro_game_info *game)
{
	if (!configure_hw_render())
		return false;

	const ::rhythmreplugged::frontend_contract::FrontendOptions frontend_options = query_frontend_options();
	g_app.set_frontend_options(frontend_options);
	clear_pending_audio_queue();

	if (game == nullptr || game->path == nullptr)
	{
		std::string error_message;
		g_root_path.clear();
		AppLaunchInputs launch_inputs;
		launch_inputs.fallback_songs_root_path = query_browser_root_hint();
		launch_inputs.frontend_options = frontend_options;
		const AppLaunchRequest launch_request = resolve_app_launch_request(g_file_system, launch_inputs);
		g_root_path = launch_request.songs_root_path;
		g_restrict_to_startup_song = launch_request.restrict_to_startup_song;
		g_is_loaded = g_app.retro_init(launch_request, error_message);
		if (!g_is_loaded)
			log_message(RETRO_LOG_ERROR, "App init failed with no explicit content: %s\n", error_message.c_str());
		else
			update_frontend_av_info();
		return g_is_loaded;
	}

	AppLaunchInputs launch_inputs;
	launch_inputs.content_path = game->path;
	launch_inputs.fallback_songs_root_path = query_browser_root_hint();
	launch_inputs.frontend_options = frontend_options;
	const AppLaunchRequest launch_request = resolve_app_launch_request(g_file_system, launch_inputs);
	g_root_path = launch_request.songs_root_path;
	g_restrict_to_startup_song = launch_request.restrict_to_startup_song;
	if (g_root_path.empty())
	{
		log_message(RETRO_LOG_ERROR, "Could not derive a song root from '%s'.\n", game->path);
		return false;
	}

	std::string error_message;
	g_is_loaded = g_app.retro_init(launch_request, error_message);
	if (!g_is_loaded)
	{
		log_message(RETRO_LOG_ERROR, "App init failed for '%s': %s\n", g_root_path.c_str(), error_message.c_str());
		return false;
	}

	update_frontend_av_info();
	return true;
}

RR_LIBRETRO_EXPORT void retro_unload_game(void)
{
	g_cover_textures.clear();
	g_app.retro_deinit();
	g_root_path.clear();
	g_restrict_to_startup_song = false;
	g_is_loaded = false;
	clear_pending_audio_queue();
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
	const size_t gameplay_size = g_app.gameplay_play_state_serialized_size();
	return gameplay_size > 0 ? (std::max)(gameplay_size, kSerializedPlayStateMaxSize) : kSerializedPlayStateMaxSize;
}

RR_LIBRETRO_EXPORT bool retro_serialize(void *data, size_t size)
{
	if (data == nullptr)
		return false;

	std::vector<std::uint8_t> bytes;
	std::string error_message;
	if (!g_app.serialize_gameplay_play_state(bytes, error_message))
	{
		if (!error_message.empty())
			log_message(RETRO_LOG_WARN, "Save-state serialize rejected: %s\n", error_message.c_str());
		return false;
	}
	if (size < bytes.size())
	{
		log_message(RETRO_LOG_WARN, "Save-state buffer too small: need %zu bytes, got %zu.\n", bytes.size(), size);
		return false;
	}

	std::memcpy(data, bytes.data(), bytes.size());
	return true;
}

RR_LIBRETRO_EXPORT bool retro_unserialize(const void *data, size_t size)
{
	if (data == nullptr || size == 0)
		return false;

	clear_pending_audio_queue();

	std::string error_message;
	if (!g_app.deserialize_gameplay_play_state(static_cast<const std::uint8_t *>(data), size, error_message))
	{
		if (!error_message.empty())
			log_message(RETRO_LOG_WARN, "Save-state restore rejected: %s\n", error_message.c_str());
		return false;
	}

	return true;
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

	sync_frontend_options();
	const ::rhythmreplugged::frontend_contract::RetroInputState input = poll_input();

	g_app.retro_run(input);
	sync_frontend_sample_rate();
	submit_audio();
	if (g_app.mode() != AppMode::Gameplay)
		g_app.finalize_audio_stop();

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
	g_gameplay_renderer.render(g_app.gameplay_snapshot().scene, static_cast<int>(kFrameWidth), static_cast<int>(kFrameHeight));
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

	if (g_video_refresh != nullptr)
		g_video_refresh(RETRO_HW_FRAME_BUFFER_VALID, kFrameWidth, kFrameHeight, 0);
}
