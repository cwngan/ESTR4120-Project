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

  unsigned int client_id;

  AudioInput *input;
  AudioOutput *output;
  CallbackData *cb_data;

  void setup_main_connection();
  void init_connect();
  void setup_audio_connection();
  void setup_interaction();
  bool process();

  bool process_interaction();
  void process_main_packet();
  void process_audio_packet();

  void capture_data_handler(std::vector<unsigned char> &data);
  void get_clients();
  void connect_client(int id);
  void disconnect_client(int id);
  void start_stream();
  void stop_stream();
};