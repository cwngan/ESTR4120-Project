cmake_minimum_required(VERSION 4.1.1)

project(VoiceChat)
find_package(PkgConfig REQUIRED)
pkg_check_modules(OPUS REQUIRED IMPORTED_TARGET opus)

if (OPUS_FOUND)
  add_executable(VoiceChat main.cpp audio_input.cpp audio_output.cpp common.cpp)
  target_link_libraries(VoiceChat PRIVATE PkgConfig::OPUS)
  target_include_directories(VoiceChat PRIVATE include)
  target_include_directories(VoiceChat PRIVATE ${OPUS_INCLUDE_DIRS})
endif()