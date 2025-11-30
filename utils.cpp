#include "utils.h"
#include "spdlog/spdlog.h"
#include <fcntl.h>
#include <netdb.h>

int create_server_socket(std::string port, int sock_type, int backlog) {
  addrinfo hints, *res;
  memset(&hints, 0, sizeof(addrinfo));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = sock_type;
  hints.ai_flags = AI_PASSIVE;

  spdlog::debug("Getting addrinfo at port {}", port);

  if (getaddrinfo(NULL, port.c_str(), &hints, &res))
    throw std::runtime_error("Error getting addr info");

  int sock_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (sock_fd == -1)
    throw std::runtime_error("Error creating socket");

  int yes = 1;
  setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

  if (bind(sock_fd, res->ai_addr, res->ai_addrlen) == -1)
    throw std::runtime_error("Error binding socket to port.");

  if (sock_type == SOCK_STREAM && listen(sock_fd, backlog) == -1)
    throw std::runtime_error("Error listening for connection.");

  return sock_fd;
}

int create_client_socket(std::string hostname, std::string port, int socktype) {
  addrinfo hints, *res;
  memset(&hints, 0, sizeof(addrinfo));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = socktype;
  hints.ai_flags = AI_PASSIVE;

  spdlog::debug("Getting addrinfo at port {}", port);

  if (getaddrinfo(hostname.c_str(), port.c_str(), &hints, &res))
    throw std::runtime_error("Error getting addr info");

  int sock_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (sock_fd == -1)
    throw std::runtime_error("Error creating socket");

  int yes = 1;
  setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

  if (connect(sock_fd, res->ai_addr, res->ai_addrlen) < 0)
    throw std::runtime_error("Error connecting to server");

  return sock_fd;
}

void set_nonblocking(int sock_fd) {
  int flags = fcntl(sock_fd, F_GETFL, 0);
  if (flags == -1) {
    throw std::runtime_error("Error getting socket flags: " +
                             std::string(strerror(errno)));
  }
  if (fcntl(sock_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
    throw std::runtime_error("Error setting socket to non-blocking: " +
                             std::string(strerror(errno)));
  }
}