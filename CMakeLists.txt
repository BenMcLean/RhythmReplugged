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

find_package(SDL2 CONFIG REQUIRED)
find_package(SDL2_image CONFIG REQUIRED)
find_package(Vorbis CONFIG REQUIRED)
find_path(MINIAUDIO_INCLUDE_DIRS "miniaudio.h")

set(BUILD_MIDILIBRARY_ONLY ON CACHE BOOL "Build only the midifile library" FORCE)
add_subdirectory(external/midifile)

add_executable(RhythmReplugged
	src/BitmapFont.cpp
	src/BitmapFont.h
	src/main.cpp
)

target_link_libraries(RhythmReplugged PRIVATE
	SDL2::SDL2
	SDL2::SDL2main
	SDL2_image::SDL2_image
	Vorbis::vorbisfile
	midifile
)

target_include_directories(RhythmReplugged PRIVATE
	${MINIAUDIO_INCLUDE_DIRS}
	${CMAKE_CURRENT_SOURCE_DIR}/external/midifile/include
)
