#pragma once

#include "common.h"
#include "cxxopts.hpp"
#include <cstddef>
#include <netdb.h>
#include <queue>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unordered_map>
#include <unordered_set>

#define BACKLOG 10

struct ServerOptions {
  // Port of the server
  std::string port;
  std::string audio_port;
  bool help;
  bool debug;
  bool trace;

  cxxopts::Options opts;

  ServerOptions();

  /**
   * @brief Parse command line options
   * @throws exceptions when parsing fails or required arguments are
   * missing/invalid
   */
  void parse_options(int argc, char *argv[]);
};

struct SocketInformation {
  int sock_fd;
  addrinfo *addr;
};

struct Client {
  int main_fd;
  int id;
  bool dgram_connected = false;

  bool muted;
  bool deafened;

  sockaddr_storage main_addr;
  socklen_t main_addr_len;

  std::string hostname;
  std::string service;

  sockaddr_storage audio_addr;
  socklen_t audio_addr_len;

  std::string audio_hostname;
  std::string audio_service;
};

struct AudioServer;

struct MainServer {
  static void send_disconnect_packet(Client *target, int client_id);

  ServerOptions options;
  std::vector<Client *> clients;
  unsigned int client_count;
  std::unordered_map<int, std::unordered_set<int>> connections;
  AudioServer *audio_server;

  int server_fd;
  int epoll_fd;
  epoll_event events[EPOLL_MAX_EVENTS];

  void setup();
  void process();
  void handle_client(Client *client);
  void handle_connect_packet(Client *client, std::vector<char> &raw_packet);
  void handle_get_connected_clients_packet(Client *client,
                                           std::vector<char> &raw_packet);
  void handle_connect_client_packet(Client *client,
                                    std::vector<char> &raw_packet);
  void handle_disconnect_client_packet(Client *client,
                                       std::vector<char> &raw_packet);
  void handle_mute(Client *client, std::vector<char> &raw_packet);
  void handle_deafen(Client *client, std::vector<char> &raw_packet);
};

struct AudioServer {
  MainServer *main_server;

  int fd;

  void setup();
  void handle_event();
  void handle_connect_packet(Client *client, sockaddr_storage client_addr,
                             socklen_t client_len);
  void handle_data_packet(Client *client, std::vector<char> &raw_packet);
};

struct ServerEvent {};