cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
	if(DEFINED ENV{VCPKG_ROOT} AND EXISTS "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
		set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
			CACHE STRING "Vcpkg toolchain file")
	elseif(EXISTS "C:/tools/vcpkg/scripts/buildsystems/vcpkg.cmake")
		set(CMAKE_TOOLCHAIN_FILE "C:/tools/vcpkg/scripts/buildsystems/vcpkg.cmake"
			CACHE STRING "Vcpkg toolchain file")
	endif()
endif()

project(RhythmReplugged LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(RR_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

find_package(SDL3 CONFIG REQUIRED)
find_package(SDL3_image CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(Vorbis CONFIG REQUIRED)
find_package(unofficial-inih CONFIG REQUIRED)
find_path(MINIAUDIO_INCLUDE_DIRS "miniaudio.h")

add_library(rr_libretro_contract INTERFACE)
target_include_directories(rr_libretro_contract INTERFACE
	${RR_SRC_DIR}
)

add_library(rr_core
	${RR_SRC_DIR}/core/app/AppCore.cpp
	${RR_SRC_DIR}/core/app/AppCore.h
	${RR_SRC_DIR}/core/audio/PrototypePlayer.cpp
	${RR_SRC_DIR}/core/audio/PrototypePlayer.h
	${RR_SRC_DIR}/core/menu/SongBrowser.cpp
	${RR_SRC_DIR}/core/menu/SongBrowser.h
	${RR_SRC_DIR}/core/play/AudioMixer.cpp
	${RR_SRC_DIR}/core/play/AudioMixer.h
	${RR_SRC_DIR}/core/play/SongSession.cpp
	${RR_SRC_DIR}/core/play/SongSession.h
	${RR_SRC_DIR}/core/play/Transport.cpp
	${RR_SRC_DIR}/core/play/Transport.h
	${RR_SRC_DIR}/core/songs/SongIni.cpp
	${RR_SRC_DIR}/core/songs/SongIni.h
	${RR_SRC_DIR}/libretro_contract/RetroAudio.h
	${RR_SRC_DIR}/libretro_contract/RetroFileSystem.h
	${RR_SRC_DIR}/libretro_contract/RetroInput.h
	${RR_SRC_DIR}/libretro_contract/RetroTypes.h
)

target_include_directories(rr_core PUBLIC
	${RR_SRC_DIR}
)

target_link_libraries(rr_core PUBLIC
	rr_libretro_contract
PRIVATE
	Vorbis::vorbisfile
	unofficial::inih::libinih
)

add_executable(RhythmReplugged
	${RR_SRC_DIR}/platform_sdl3/Sdl3AudioOutput.cpp
	${RR_SRC_DIR}/platform_sdl3/Sdl3AudioOutput.h
	${RR_SRC_DIR}/platform_sdl3/Sdl3FileSystem.cpp
	${RR_SRC_DIR}/platform_sdl3/Sdl3FileSystem.h
	${RR_SRC_DIR}/platform_sdl3/main.cpp
)

target_include_directories(RhythmReplugged PRIVATE
	${RR_SRC_DIR}
	${MINIAUDIO_INCLUDE_DIRS}
)

target_link_libraries(RhythmReplugged PRIVATE
	rr_core
	imgui::imgui
	SDL3::SDL3
	SDL3_image::SDL3_image
)
