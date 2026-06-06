cmake_minimum_required(VERSION 3.21)
cmake_policy(SET CMP0091 NEW)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
	message(FATAL_ERROR
		"CMAKE_TOOLCHAIN_FILE is not set. Configure with a CMake preset after setting "
		"VCPKG_ROOT, or define VCPKG_ROOT in CMakeUserPresets.json.")
endif()

project(RhythmReplugged LANGUAGES C CXX)

set(RR_ARCH_DIR "")
if(DEFINED VCPKG_TARGET_TRIPLET AND NOT VCPKG_TARGET_TRIPLET STREQUAL "")
	string(REPLACE "-windows-static" "" RR_ARCH_DIR "${VCPKG_TARGET_TRIPLET}")
	string(REPLACE "-windows" "" RR_ARCH_DIR "${RR_ARCH_DIR}")
	string(REPLACE "-linux" "" RR_ARCH_DIR "${RR_ARCH_DIR}")
else()
	if(CMAKE_GENERATOR_PLATFORM)
		set(RR_ARCH_DIR "${CMAKE_GENERATOR_PLATFORM}")
	else()
		set(RR_ARCH_DIR "${CMAKE_SYSTEM_PROCESSOR}")
	endif()
endif()

if(RR_ARCH_DIR STREQUAL "")
	set(RR_ARCH_DIR "unknown-arch")
endif()

string(TOLOWER "${CMAKE_SYSTEM_NAME}" RR_OS_DIR)
string(TOLOWER "${RR_ARCH_DIR}" RR_ARCH_DIR)
set(RR_DIST_PLATFORM_DIR "${RR_OS_DIR}-${RR_ARCH_DIR}")

set(RR_DIST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dist")
set(RR_DIST_SDL3_DIR "${RR_DIST_DIR}/$<CONFIG>/sdl3/${RR_DIST_PLATFORM_DIR}")
set(RR_DIST_LIBRETRO_DIR "${RR_DIST_DIR}/$<CONFIG>/libretro/${RR_DIST_PLATFORM_DIR}")

if(MSVC AND DEFINED VCPKG_TARGET_TRIPLET AND VCPKG_TARGET_TRIPLET MATCHES "-windows-static$")
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(RR_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
option(RR_BUILD_SDL3_FRONTEND "Build the SDL3 desktop frontend." ON)
option(RR_BUILD_LIBRETRO_CORE "Build the libretro core." ON)
option(RR_RENDER_USE_GLES "Build the OpenGL renderer against OpenGL ES 3 headers instead of desktop OpenGL." OFF)

add_subdirectory(external/midifile)

set(RR_GL_LIBRARIES "")
if(RR_RENDER_USE_GLES)
	find_path(GLES3_INCLUDE_DIRS "GLES3/gl3.h" REQUIRED)
	find_library(GLES_LIBRARY NAMES GLESv2 GLESv3 REQUIRED)
	list(APPEND RR_GL_LIBRARIES ${GLES_LIBRARY})
else()
	find_package(OpenGL REQUIRED)
	list(APPEND RR_GL_LIBRARIES OpenGL::GL)
endif()
find_package(Threads REQUIRED)
find_package(Vorbis CONFIG REQUIRED)
find_path(STB_INCLUDE_DIRS "stb_image.h" REQUIRED)
find_path(IMGUI_INCLUDE_DIRS "imgui.h" REQUIRED)
find_library(IMGUI_LIBRARY_RELEASE NAMES imgui REQUIRED)
find_library(IMGUI_LIBRARY_DEBUG NAMES imguid imgui REQUIRED)

if(RR_BUILD_SDL3_FRONTEND)
	find_package(SDL3 CONFIG REQUIRED)
	find_path(MINIAUDIO_INCLUDE_DIRS "miniaudio.h" REQUIRED)
endif()

set(RR_VCPKG_ROOT "")
if(CMAKE_TOOLCHAIN_FILE MATCHES [[^(.*)[/\\]scripts[/\\]buildsystems[/\\]vcpkg\.cmake$]])
	set(RR_VCPKG_ROOT "${CMAKE_MATCH_1}")
endif()

file(GLOB RR_IMGUI_SDL3_BACKEND_SOURCES
	"${RR_VCPKG_ROOT}/buildtrees/imgui/src/*/backends/imgui_impl_sdl3.cpp"
)
file(GLOB RR_IMGUI_OPENGL3_BACKEND_SOURCES
	"${RR_VCPKG_ROOT}/buildtrees/imgui/src/*/backends/imgui_impl_opengl3.cpp"
)

list(LENGTH RR_IMGUI_SDL3_BACKEND_SOURCES RR_IMGUI_SDL3_BACKEND_SOURCE_COUNT)
list(LENGTH RR_IMGUI_OPENGL3_BACKEND_SOURCES RR_IMGUI_OPENGL3_BACKEND_SOURCE_COUNT)
if(RR_IMGUI_OPENGL3_BACKEND_SOURCE_COUNT EQUAL 0)
	message(FATAL_ERROR "Could not locate Dear ImGui OpenGL backend sources in the vcpkg buildtree.")
endif()
if(RR_BUILD_SDL3_FRONTEND AND RR_IMGUI_SDL3_BACKEND_SOURCE_COUNT EQUAL 0)
	message(FATAL_ERROR "Could not locate Dear ImGui SDL3 backend sources in the vcpkg buildtree.")
endif()

list(GET RR_IMGUI_OPENGL3_BACKEND_SOURCES 0 RR_IMGUI_OPENGL3_BACKEND_SOURCE)
get_filename_component(RR_IMGUI_BACKEND_INCLUDE_DIR "${RR_IMGUI_OPENGL3_BACKEND_SOURCE}" DIRECTORY)
if(RR_BUILD_SDL3_FRONTEND)
	list(GET RR_IMGUI_SDL3_BACKEND_SOURCES 0 RR_IMGUI_SDL3_BACKEND_SOURCE)
endif()

add_library(rr_frontend_contract INTERFACE)
target_include_directories(rr_frontend_contract INTERFACE
	${RR_SRC_DIR}
)

add_library(rr_gl_api INTERFACE)
target_include_directories(rr_gl_api INTERFACE
	${RR_IMGUI_BACKEND_INCLUDE_DIR}
	${GLES3_INCLUDE_DIRS}
)
target_link_libraries(rr_gl_api INTERFACE
	${RR_GL_LIBRARIES}
)
if(RR_RENDER_USE_GLES)
	target_compile_definitions(rr_gl_api INTERFACE
		RR_RENDER_OPENGL_ES3
		IMGUI_IMPL_OPENGL_ES3
	)
else()
	target_compile_definitions(rr_gl_api INTERFACE
		RR_RENDER_OPENGL33_CORE
	)
endif()

add_library(rr_imgui_core STATIC IMPORTED GLOBAL)
set_target_properties(rr_imgui_core PROPERTIES
	IMPORTED_CONFIGURATIONS "Debug;Release;MinSizeRel;RelWithDebInfo"
	IMPORTED_LOCATION_DEBUG "${IMGUI_LIBRARY_DEBUG}"
	IMPORTED_LOCATION_RELEASE "${IMGUI_LIBRARY_RELEASE}"
	IMPORTED_LOCATION_MINSIZEREL "${IMGUI_LIBRARY_RELEASE}"
	IMPORTED_LOCATION_RELWITHDEBINFO "${IMGUI_LIBRARY_RELEASE}"
	INTERFACE_INCLUDE_DIRECTORIES "${IMGUI_INCLUDE_DIRS}"
)

add_library(rr_core
	${RR_SRC_DIR}/core/app/AppLaunch.cpp
	${RR_SRC_DIR}/core/app/AppLaunch.h
	${RR_SRC_DIR}/core/app/AppCore.cpp
	${RR_SRC_DIR}/core/app/AppCore.h
	${RR_SRC_DIR}/core/app/AppTypes.h
	${RR_SRC_DIR}/core/audio/SongPlayer.cpp
	${RR_SRC_DIR}/core/audio/SongPlayer.h
	${RR_SRC_DIR}/core/chart/MidiChart.cpp
	${RR_SRC_DIR}/core/chart/MidiChart.h
	${RR_SRC_DIR}/core/concurrency/BackgroundWorker.cpp
	${RR_SRC_DIR}/core/concurrency/BackgroundWorker.h
	${RR_SRC_DIR}/core/utils/TextDecoding.cpp
	${RR_SRC_DIR}/core/utils/TextDecoding.h
	${RR_SRC_DIR}/core/menu/DifficultySelectMenu.cpp
	${RR_SRC_DIR}/core/menu/DifficultySelectMenu.h
	${RR_SRC_DIR}/core/menu/InstrumentSelectMenu.cpp
	${RR_SRC_DIR}/core/menu/InstrumentSelectMenu.h
	${RR_SRC_DIR}/core/menu/ModeSelectMenu.cpp
	${RR_SRC_DIR}/core/menu/ModeSelectMenu.h
	${RR_SRC_DIR}/core/menu/SongBrowser.cpp
	${RR_SRC_DIR}/core/menu/SongBrowser.h
	${RR_SRC_DIR}/core/play/AudioMixer.cpp
	${RR_SRC_DIR}/core/play/AudioMixer.h
	${RR_SRC_DIR}/core/play/GameplayOptions.cpp
	${RR_SRC_DIR}/core/play/GameplayOptions.h
	${RR_SRC_DIR}/core/play/SongPreloader.cpp
	${RR_SRC_DIR}/core/play/SongPreloader.h
	${RR_SRC_DIR}/core/play/SongSession.cpp
	${RR_SRC_DIR}/core/play/SongSession.h
	${RR_SRC_DIR}/core/play/Transport.cpp
	${RR_SRC_DIR}/core/play/Transport.h
	${RR_SRC_DIR}/core/songs/SongIni.cpp
	${RR_SRC_DIR}/core/songs/SongIni.h
	${RR_SRC_DIR}/frontend_contract/AudioTypes.h
	${RR_SRC_DIR}/frontend_contract/FrontendOptions.cpp
	${RR_SRC_DIR}/frontend_contract/FrontendOptions.h
	${RR_SRC_DIR}/frontend_contract/RetroFileSystem.h
	${RR_SRC_DIR}/frontend_contract/RetroInput.h
)

target_include_directories(rr_core PUBLIC
	${RR_SRC_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/external/midifile/include
)

target_link_libraries(rr_core PUBLIC
	rr_frontend_contract
PRIVATE
	midifile
	Threads::Threads
	Vorbis::vorbisfile
)

add_library(rr_ui
	${RR_SRC_DIR}/ui/AppUi.cpp
	${RR_SRC_DIR}/ui/AppUi.h
	${RR_SRC_DIR}/ui/AppUiHost.cpp
	${RR_SRC_DIR}/ui/AppUiHost.h
	${RR_SRC_DIR}/ui/OpenGlCoverTextures.cpp
	${RR_SRC_DIR}/ui/OpenGlCoverTextures.h
	${RR_SRC_DIR}/ui/UiNavigation.cpp
	${RR_SRC_DIR}/ui/UiNavigation.h
)

target_include_directories(rr_ui PUBLIC
	${RR_SRC_DIR}
	${STB_INCLUDE_DIRS}
)

target_link_libraries(rr_ui PUBLIC
	rr_core
	rr_gl_api
	rr_imgui_core
)

add_library(rr_render_gl
	${RR_SRC_DIR}/render_gl/GameplayRendererGl.cpp
	${RR_SRC_DIR}/render_gl/GameplayRendererGl.h
)

target_include_directories(rr_render_gl PUBLIC
	${RR_SRC_DIR}
)

target_link_libraries(rr_render_gl PUBLIC
	rr_core
	rr_gl_api
)

if(RR_BUILD_SDL3_FRONTEND)
	add_executable(RhythmReplugged
		${RR_SRC_DIR}/platform_sdl3/MiniaudioOutput.cpp
		${RR_SRC_DIR}/platform_sdl3/MiniaudioOutput.h
		${RR_SRC_DIR}/platform_sdl3/FileSystem.cpp
		${RR_SRC_DIR}/platform_sdl3/FileSystem.h
		${RR_SRC_DIR}/platform_sdl3/main.cpp
		${RR_IMGUI_SDL3_BACKEND_SOURCE}
		${RR_IMGUI_OPENGL3_BACKEND_SOURCE}
	)

	target_include_directories(RhythmReplugged PRIVATE
		${RR_SRC_DIR}
		${MINIAUDIO_INCLUDE_DIRS}
		${RR_IMGUI_BACKEND_INCLUDE_DIR}
	)

	target_link_libraries(RhythmReplugged PRIVATE
		rr_core
		rr_render_gl
		rr_ui
		SDL3::SDL3
	)

	add_custom_target(stage_sdl3
		COMMAND ${CMAKE_COMMAND} -E make_directory "${RR_DIST_SDL3_DIR}"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"$<TARGET_FILE:RhythmReplugged>"
			"${RR_DIST_SDL3_DIR}/$<TARGET_FILE_NAME:RhythmReplugged>"
		DEPENDS RhythmReplugged
		VERBATIM
	)
endif()

if(RR_BUILD_LIBRETRO_CORE)
	add_library(rhythmreplugged_libretro MODULE
		${RR_SRC_DIR}/platform_libretro/core.cpp
		${RR_SRC_DIR}/platform_libretro/ImGuiLibretroPlatform.cpp
		${RR_SRC_DIR}/platform_libretro/ImGuiLibretroPlatform.h
		${RR_SRC_DIR}/platform_libretro/FileSystem.cpp
		${RR_SRC_DIR}/platform_libretro/FileSystem.h
		${RR_IMGUI_OPENGL3_BACKEND_SOURCE}
	)

	target_include_directories(rhythmreplugged_libretro PRIVATE
		${RR_SRC_DIR}
		${CMAKE_CURRENT_SOURCE_DIR}/external/libretro-common/include
	)

	target_link_libraries(rhythmreplugged_libretro PRIVATE
		rr_core
		rr_render_gl
		rr_ui
	)

	set_target_properties(rhythmreplugged_libretro PROPERTIES
		PREFIX ""
		OUTPUT_NAME "rhythmreplugged_libretro"
	)

	add_custom_target(stage_libretro
		COMMAND ${CMAKE_COMMAND} -E make_directory "${RR_DIST_LIBRETRO_DIR}"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"$<TARGET_FILE:rhythmreplugged_libretro>"
			"${RR_DIST_LIBRETRO_DIR}/$<TARGET_FILE_NAME:rhythmreplugged_libretro>"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${CMAKE_CURRENT_SOURCE_DIR}/rhythmreplugged_libretro.info"
			"${RR_DIST_LIBRETRO_DIR}/rhythmreplugged_libretro.info"
		DEPENDS rhythmreplugged_libretro
		VERBATIM
	)

	if(RR_BUILD_SDL3_FRONTEND)
		add_custom_target(windows_artifacts
			DEPENDS RhythmReplugged rhythmreplugged_libretro
		)
	endif()
endif()

add_custom_target(stage)
if(TARGET stage_sdl3)
	add_dependencies(stage stage_sdl3)
endif()
if(TARGET stage_libretro)
	add_dependencies(stage stage_libretro)
endif()

add_executable(rr_transport_harness
	${RR_SRC_DIR}/core/play/TransportParityHarness.cpp
)

target_include_directories(rr_transport_harness PRIVATE
	${RR_SRC_DIR}
)

target_link_libraries(rr_transport_harness PRIVATE
	rr_core
)
