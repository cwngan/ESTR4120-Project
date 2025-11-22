#include "server.h"
#include "cxxopts.hpp"
#include "spdlog/common.h"
#include "spdlog/spdlog.h"
#include "utils.h"
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <netdb.h>
#include <ostream>
#include <stdexcept>
#include <string>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

ServerOptions::ServerOptions() : opts("server") {
  // clang-format off
  opts.add_options()
    ("p,port", "Port of the main server", cxxopts::value<std::string>())
    ("a,audio-port", "Port of the audio server", cxxopts::value<std::string>())
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
  audio_port = result["audio-port"].as<std::string>();
}

void MainServer::setup() {
  int sock_fd = create_server_socket(options.port, SOCK_STREAM, BACKLOG);
  spdlog::info("Main STREAM server started on port {}", options.port);

  set_nonblocking(sock_fd);
  server_fd = sock_fd;

  epoll_fd = epoll_create1(0);
  epoll_event event;
  event.events = EPOLLIN;
  event.data.ptr = new ServerEvent;
  event.data.fd = server_fd;
  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event) < 0)
    throw std::runtime_error("epoll_ctl: server_fd");

  audio_server = new AudioServer;
  audio_server->main_server = this;
  audio_server->setup();
}

void MainServer::process() {
  int n_events = epoll_wait(epoll_fd, events, EPOLL_MAX_EVENTS, -1);
  if (n_events < 0)
    throw std::runtime_error("epoll_wait()");

  for (int i = 0; i < n_events; i++) {
    int fd = events[i].data.fd;
    if (fd == server_fd) {
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

        Client *client = new Client{.main_fd = client_fd,
                                    .main_addr = client_addr,
                                    .main_addr_len = client_len};
        epoll_event event;
        event.events = EPOLLIN;
        event.data.fd = client_fd;
        event.data.ptr = client;
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) < 0) {
          close(client_fd);
          throw std::runtime_error("Error adding client socket to epoll");
        }

        std::vector<char> raw_hostname(NI_MAXHOST);
        std::vector<char> raw_service(NI_MAXSERV);

        if (getnameinfo(reinterpret_cast<sockaddr *>(&client->main_addr),
                        client->main_addr_len, raw_hostname.data(),
                        raw_hostname.size(), raw_service.data(),
                        raw_service.size(),
                        NI_NUMERICHOST | NI_NUMERICSERV) < 0)
          throw std::runtime_error("Error getting information of client");
        client->hostname =
            std::string(raw_hostname.begin(), raw_hostname.end());
        client->service = std::string(raw_service.begin(), raw_service.end());
        spdlog::info("Client {}:{} connected fd {}", client->hostname,
                     client->service, client_fd);
      }
    } else if (fd == audio_server->fd) {
      audio_server->handle_event();
    } else {
      // client activity
      Client *client = static_cast<Client *>(events[i].data.ptr);
      handle_client(client);
    }
  }
}

void MainServer::handle_client(Client *client) {
  std::vector<char> raw_packet(MAX_PACKET_SIZE);
  ssize_t size = recv(client->main_fd, raw_packet.data(), MAX_PACKET_SIZE, 0);
  if (size < 0) {
    spdlog::error("Error receiving data from {}:{}. Reason: {}",
                  client->hostname, client->service, strerror(errno));
  }
  if (size <= 0) {
    spdlog::info("{}:{} disconnected", client->hostname, client->service);
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client->main_fd, NULL);
    close(client->main_fd);
    delete client;
    return;
  }

  raw_packet.resize(size);

  PacketHeader header;
  memcpy(&header, raw_packet.data(), sizeof(header));

  switch (header.type) {
  case PacketHeader::Type::Connect:
    handle_connect_packet(client, raw_packet);
    break;
  }
};

void MainServer::handle_connect_packet(Client *client,
                                       std::vector<char> &raw_packet) {
  spdlog::info("Received connect packet from {}:{}", client->hostname,
               client->service);
  clients.push_back(client);
  ConnectResponsePacket res{.id =
                                static_cast<unsigned int>(clients.size() - 1)};

  spdlog::info("Assigning client {}:{} to id {}", client->hostname,
               client->service, res.id);

  send(client->main_fd, &res, sizeof(res), 0);
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