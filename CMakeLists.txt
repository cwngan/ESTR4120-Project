cmake_minimum_required(VERSION 3.18.0)

project(VoiceChat)

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

FetchContent_MakeAvailable(cxxopts)
FetchContent_MakeAvailable(spdlog)

find_package(PkgConfig REQUIRED)
pkg_check_modules(OPUS REQUIRED IMPORTED_TARGET opus)

if (OPUS_FOUND)
  add_executable(client client.cpp audio_input.cpp audio_output.cpp audio_common.cpp)
  target_link_libraries(client PRIVATE PkgConfig::OPUS)
  target_include_directories(client PRIVATE include)
  target_include_directories(client PRIVATE ${OPUS_INCLUDE_DIRS})
endif()

add_executable(server server.cpp)
target_include_directories(server PRIVATE include)
target_link_libraries(server PRIVATE cxxopts::cxxopts)
target_link_libraries(server PRIVATE spdlog::spdlog)