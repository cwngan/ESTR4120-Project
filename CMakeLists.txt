cmake_minimum_required(VERSION 3.18.0)

project(VoiceChat)

set(CMAKE_EXPORT_COMPILE_COMMAND ON)
set(EXECUTABLE_OUTPUT_PATH bin/)

include(FetchContent)

FetchContent_Declare(
  cxxopts
  GIT_REPOSITORY https://github.com/jarro2783/cxxopts.git
  GIT_TAG v3.1.1
)

FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG v1.16.0
)

FetchContent_Declare(
  miniaudio
  GIT_REPOSITORY https://github.com/mackron/miniaudio.git
  GIT_TAG 0.11.23
)

FetchContent_MakeAvailable(cxxopts spdlog miniaudio)

find_package(PkgConfig REQUIRED)
pkg_check_modules(OPUS REQUIRED IMPORTED_TARGET opus)

find_package(Threads REQUIRED)

add_compile_options(-O3)

if (OPUS_FOUND)
  add_executable(client client.cpp audio_input.cpp audio_output.cpp audio_common.cpp common.cpp utils.cpp)
  target_link_libraries(client PRIVATE PkgConfig::OPUS)
  target_link_libraries(client PRIVATE Threads::Threads)
  target_link_libraries(client PRIVATE spdlog::spdlog)
  target_link_libraries(client PRIVATE dl)
  target_link_libraries(client PRIVATE cxxopts::cxxopts)
  target_link_libraries(client PRIVATE miniaudio)
  target_include_directories(client PRIVATE include)
  target_include_directories(client PRIVATE ${OPUS_INCLUDE_DIRS})
endif()

add_executable(server server.cpp audio_server.cpp common.cpp utils.cpp)
target_include_directories(server PRIVATE include)
target_link_libraries(server PRIVATE cxxopts::cxxopts)
target_link_libraries(server PRIVATE spdlog::spdlog)