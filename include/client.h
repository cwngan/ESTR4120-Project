#include "audio_common.h"
#include "audio_input.h"
#include "audio_output.h"
#include "cxxopts.hpp"

struct ClientOptions {
  // Hostname of the server
  std::string hostname;
  // Port of the server
  std::string port;
  // Port of the audio server
  std::string audio_port;
  bool help;
  bool debug;
  bool trace;

  cxxopts::Options opts;

  ClientOptions();

  /**
   * @brief Parse command line options
   * @throws exceptions when parsing fails or required arguments are
   * missing/invalid
   */
  void parse_options(int argc, char *argv[]);
};

struct Client {
  ClientOptions options;
  int main_fd;
  int audio_fd;
  int epoll_fd;

  int client_id;

  bool muted = false;
  bool deafened = false;

  AudioInput *input;
  AudioOutput *output;
  CallbackData *cb_data;
  int seq_number;

  void print_interaction_menu();

  void setup_main_connection();
  void init_connect();
  void setup_audio_connection();
  void setup_interaction();
  bool process();

  bool process_interaction();
  bool process_main_packet();
  bool process_audio_packet();

  void capture_data_handler(std::vector<unsigned char> &data);
  void get_connections();
  void connect_client(int id);
  void disconnect_client(int id);
  void unmute();
  void mute();
  void undeafen();
  void deafen();
};