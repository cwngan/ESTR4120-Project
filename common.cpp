#include <cstring>
#include <fcntl.h>
#include <stdexcept>

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
