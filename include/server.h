#include "common.h"
#include "cxxopts.hpp"
#include <netdb.h>
#include <sys/epoll.h>
#include <sys/socket.h>

#define BACKLOG 10
#define MAX_EVENTS 1

struct ServerOptions {
  // Port of the server
  std::string port;
  bool help;

  cxxopts::Options opts;

  ServerOptions();

  /**
   * @brief Parse command line options
   * @throws exceptions when parsing fails or required arguments are
   * missing/invalid
   */
  void parse_options(int argc, char *argv[]);
};

struct Client {
  int fd;
  sockaddr_storage addr;
  socklen_t addr_len;

  std::vector<char> hostname;
  std::vector<char> service;
};

struct MainServer {
  ServerOptions options;
  std::vector<sockaddr_storage> clients;

  int server_fd;
  int epoll_fd;
  epoll_event events[MAX_EVENTS];

  void setup();
  void process();
  void handle_client(Client *client);
  void handle_connect_packet(Client *client, std::vector<char> &raw_packet);
  void handle_audio_packet(std::vector<char> &raw_packet);
};

struct ServerEvent {};