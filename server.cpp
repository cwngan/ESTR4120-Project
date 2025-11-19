#include "server.h"
#include "cxxopts.hpp"
#include "spdlog/common.h"
#include "spdlog/spdlog.h"
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <netdb.h>
#include <ostream>
#include <stdexcept>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

ServerOptions::ServerOptions() : opts("server") {
  // clang-format off
  opts.add_options()
    ("p,port", "Port of the server", cxxopts::value<std::string>())
    ("help", "Print help", cxxopts::value<bool>()->default_value("false"));
  // clang-format on
}

void ServerOptions::parse_options(int argc, char **argv) {
  cxxopts::ParseResult result;

  try {
    result = opts.parse(argc, argv);
  } catch (cxxopts::exceptions::exception &e) {
    throw std::invalid_argument("Failed to parse options: " +
                                std::string(e.what()));
  }
  help = result["help"].as<bool>();
  if (help)
    return;

  port = result["port"].as<std::string>();
}

void MainServer::setup() {
  addrinfo hints, *res;
  memset(&hints, 0, sizeof(addrinfo));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  spdlog::debug("Getting addrinfo at port {}", options.port);

  if (getaddrinfo(NULL, options.port.c_str(), &hints, &res))
    throw std::runtime_error("Error getting addr info");

  int sock_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (sock_fd == -1)
    throw std::runtime_error("Error creating socket");

  int yes = 1;
  setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

  if (bind(sock_fd, res->ai_addr, res->ai_addrlen) == -1)
    throw std::runtime_error("Error binding socket to port.");

  if (listen(sock_fd, BACKLOG) == -1)
    throw std::runtime_error("Error listening for connection.");

  spdlog::info("Main server started on port {}", options.port);

  set_nonblocking(sock_fd);
  server_fd = sock_fd;

  epoll_fd = epoll_create1(0);
  epoll_event event;
  event.events = EPOLLIN;
  event.data.ptr = new ServerEvent;
  event.data.fd = server_fd;
  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event) == -1)
    throw std::runtime_error("epoll_ctl: server_fd");
}

void MainServer::process() {
  int n_events = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
  if (n_events < 0)
    throw std::runtime_error("epoll_wait()");

  for (int i = 0; i < n_events; i++) {
    if (events[i].data.fd == server_fd) {
      // main socket acitivity (connection)
      while (true) {
        sockaddr_storage client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd =
            accept(server_fd, (sockaddr *)&client_addr, &client_len);
        if (client_fd == -1) {
          if (errno == EAGAIN || errno == EWOULDBLOCK)
            break;
          throw std::runtime_error("Error connecting to client");
        }

        set_nonblocking(client_fd);

        Client *client = new Client{
            .fd = client_fd, .addr = client_addr, .addr_len = client_len};
        client->hostname = std::vector<char>(NI_MAXHOST);
        client->service = std::vector<char>(NI_MAXSERV);
        epoll_event event;
        event.events = EPOLLIN;
        event.data.fd = client_fd;
        event.data.ptr = client;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) < 0) {
          close(client_fd);
          throw std::runtime_error("Error adding client socket to epoll");
        }

        if (getnameinfo(reinterpret_cast<sockaddr *>(&client->addr),
                        client->addr_len, client->hostname.data(),
                        client->hostname.size(), client->service.data(),
                        client->service.size(),
                        NI_NUMERICHOST | NI_NUMERICSERV) < 0)
          throw std::runtime_error("Error getting information of client");
        spdlog::info("Client {}:{} connected", client->hostname.data(),
                     client->service.data());
      }
    } else {
      // client activity
      Client *client = static_cast<Client *>(events[i].data.ptr);
      handle_client(client);
    }
  }
}

void MainServer::handle_client(Client *client) {
  std::vector<char> raw_packet(MAX_PACKET_SIZE);
  ssize_t size = recv(client->fd, raw_packet.data(), MAX_PACKET_SIZE, 0);
  if (size < 0) {
    spdlog::error("Error receiving data from {}:{}. Reason: {}",
                  client->hostname.data(), client->service.data(),
                  strerror(errno));
  }
  if (size <= 0) {
    spdlog::info("{}:{} disconnected", client->hostname.data(),
                 client->service.data());
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client->fd, NULL);
    close(client->fd);
    delete client;
    return;
  }

  raw_packet.resize(size);

  PacketHeader header;
  memcpy(&header, raw_packet.data(), sizeof(header));

  switch (header.type) {
  case PacketHeader::Type::Audio:
    handle_audio_packet(raw_packet);
    break;
  case PacketHeader::Type::Connect:
    handle_connect_packet(client, raw_packet);
    break;
  }
};

void MainServer::handle_audio_packet(std::vector<char> &raw_packet) {
  AudioPacketHeader audio_header;
  memcpy(&audio_header, raw_packet.data() + sizeof(PacketHeader),
         sizeof(audio_header));
  std::vector<char> audio_data(audio_header.data_length);
  memcpy(audio_data.data(),
         raw_packet.data() + sizeof(PacketHeader) + sizeof(AudioPacketHeader),
         audio_header.data_length);
  // to do: process the data
}

void MainServer::handle_connect_packet(Client *client,
                                       std::vector<char> &raw_packet) {
  spdlog::info("Received connect packet from {}:{}", client->hostname.data(),
               client->service.data());
  clients.push_back(client->addr);
  ConnectResponsePacket res{.id =
                                static_cast<unsigned int>(clients.size() - 1)};

  spdlog::debug("Assigning client {}:{} to id {}", client->hostname.data(),
                client->service.data(), res.id);

  send(client->fd, &res, sizeof(res), 0);
}

int main(int argc, char **argv) {
  spdlog::set_level(spdlog::level::debug);

  ServerOptions options;
  options.parse_options(argc, argv);
  if (options.help) {
    std::cout << options.opts.help() << std::endl;
  }

  MainServer server{.options = options};
  server.setup();

  while (true)
    server.process();
}