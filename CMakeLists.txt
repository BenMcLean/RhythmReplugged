cmake_minimum_required(VERSION 3.21)
cmake_policy(SET CMP0091 NEW)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
	message(FATAL_ERROR
		"CMAKE_TOOLCHAIN_FILE is not set. Configure with a CMake preset after setting "
		"VCPKG_ROOT, or define VCPKG_ROOT in CMakeUserPresets.json.")
endif()

project(RhythmReplugged LANGUAGES C CXX)

if(MSVC AND DEFINED VCPKG_TARGET_TRIPLET AND VCPKG_TARGET_TRIPLET MATCHES "-windows-static$")
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(RR_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
option(RR_BUILD_SDL3_FRONTEND "Build the SDL3 desktop frontend." ON)
option(RR_BUILD_LIBRETRO_CORE "Build the libretro core." ON)

add_subdirectory(external/midifile)

find_package(OpenGL REQUIRED)
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
	${RR_SRC_DIR}/core/audio/PrototypePlayer.cpp
	${RR_SRC_DIR}/core/audio/PrototypePlayer.h
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
)

target_include_directories(rr_ui PUBLIC
	${RR_SRC_DIR}
	${STB_INCLUDE_DIRS}
	${RR_IMGUI_BACKEND_INCLUDE_DIR}
)

target_link_libraries(rr_ui PUBLIC
	rr_core
	rr_imgui_core
PRIVATE
	OpenGL::GL
)

add_library(rr_render_gl
	${RR_SRC_DIR}/render_gl/GameplayRendererGl.cpp
	${RR_SRC_DIR}/render_gl/GameplayRendererGl.h
)

target_include_directories(rr_render_gl PUBLIC
	${RR_SRC_DIR}
	${RR_IMGUI_BACKEND_INCLUDE_DIR}
)

target_link_libraries(rr_render_gl PUBLIC
	rr_core
PRIVATE
	OpenGL::GL
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
		OpenGL::GL
		SDL3::SDL3
	)
endif()

if(RR_BUILD_LIBRETRO_CORE)
	add_library(platform_libretro MODULE
		${RR_SRC_DIR}/platform_libretro/core.cpp
		${RR_SRC_DIR}/platform_libretro/ImGuiLibretroPlatform.cpp
		${RR_SRC_DIR}/platform_libretro/ImGuiLibretroPlatform.h
		${RR_SRC_DIR}/platform_libretro/FileSystem.cpp
		${RR_SRC_DIR}/platform_libretro/FileSystem.h
		${RR_IMGUI_OPENGL3_BACKEND_SOURCE}
	)

	target_include_directories(platform_libretro PRIVATE
		${RR_SRC_DIR}
		${CMAKE_CURRENT_SOURCE_DIR}/external/libretro-common/include
		${RR_IMGUI_BACKEND_INCLUDE_DIR}
	)

	target_link_libraries(platform_libretro PRIVATE
		rr_core
		rr_render_gl
		rr_ui
		OpenGL::GL
	)

	set_target_properties(platform_libretro PROPERTIES
		PREFIX ""
		OUTPUT_NAME "rhythmreplugged_libretro"
	)
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
