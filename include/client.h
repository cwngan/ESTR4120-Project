#include "cxxopts.hpp"

struct ClientOptions {
  // Hostname of the server
  std::string hostname;
  // Port of the server
  std::string port;
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
  int fd;
  int epoll_fd;

  int client_id;

  void setup();
  void init_connect();
  void process();
};