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
    ("help", "Print help", cxxopts::value<bool>()->default_value("false"))
    ("debug", "Output debug logs", cxxopts::value<bool>()->default_value("false"))
    ("trace", "Output trace logs (EXTREMELY LARGE FILES)", cxxopts::value<bool>()->default_value("false"));
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

  debug = result["debug"].as<bool>();
  trace = result["trace"].as<bool>();
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
                                    .id = -1,
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

void MainServer::send_disconnect_packet(Client *target, int client_id) {
  char
      buf[sizeof(ResponsePacketHeader) + sizeof(DisconnectClientRequestPacket)];
  ResponsePacketHeader res_header{
      .type = ResponsePacketHeader::Type::DisconnectClient};
  memcpy(buf, &res_header, sizeof(res_header));

  DisconnectClientResponsePacket res{.id = client_id};
  memcpy(buf + sizeof(res_header), &res, sizeof(res));
  send(target->main_fd, buf, sizeof(buf), 0);
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
    if (client->id != -1 && connections.find(client->id) != connections.end()) {
      for (int connected_id : connections[client->id]) {
        if (connected_id == client->id)
          continue;
        send_disconnect_packet(client, connected_id);
        connections[connected_id].erase(client->id);
      }
      connections.erase(client->id);
      clients[client->id] = NULL;
      client_count--;
    }
    close(client->main_fd);

    delete client;
    return;
  }

  raw_packet.resize(size);

  RequestPacketHeader header;
  memcpy(&header, raw_packet.data(), sizeof(header));

  switch (header.type) {
  case RequestPacketHeader::Type::Connect:
    handle_connect_packet(client, raw_packet);
    break;
  case RequestPacketHeader::Type::GetConnections:
    handle_get_connected_clients_packet(client, raw_packet);
    break;
  case RequestPacketHeader::Type::ConnectClient:
    handle_connect_client_packet(client, raw_packet);
    break;
  case RequestPacketHeader::Type::DisconnectClient:
    handle_disconnect_client_packet(client, raw_packet);
    break;
  case RequestPacketHeader::Type::Mute:
    handle_mute(client, raw_packet);
    break;
  case RequestPacketHeader::Type::Deafen:
    handle_deafen(client, raw_packet);
    break;
  }
};

void MainServer::handle_connect_packet(Client *client,
                                       std::vector<char> &raw_packet) {
  spdlog::info("Received connect packet from {}:{}", client->hostname,
               client->service);
  client->id = clients.size();
  clients.push_back(client);
  client_count++;
  connections[client->id] = {};

  char buffer[MAX_PACKET_SIZE];
  int offset = 0;

  ResponsePacketHeader header{.type = ResponsePacketHeader::Type::Connect};
  memcpy(buffer + offset, &header, sizeof(header));
  offset += sizeof(header);

  ConnectResponsePacket res{.id = client->id};
  memcpy(buffer + offset, &res, sizeof(res));
  offset += sizeof(res);

  spdlog::info("Assigning client {}:{} to id {}", client->hostname,
               client->service, res.id);

  send(client->main_fd, &buffer, offset, 0);
}

void MainServer::handle_get_connected_clients_packet(
    Client *client, std::vector<char> &raw_packet) {
  spdlog::info("Received get connected clients packet from {}:{}",
               client->hostname, client->service);

  char buffer[MAX_PACKET_SIZE];
  int offset = 0;

  ResponsePacketHeader header{.type =
                                  ResponsePacketHeader::Type::GetConnections};
  memcpy(buffer + offset, &header, sizeof(header));
  offset += sizeof(header);

  GetConnectionsResponsePacketHeader content_header{.clients = client_count};
  memcpy(buffer + offset, &content_header, sizeof(content_header));
  offset += sizeof(content_header);

  // overflow if too many connections
  // need to work on this later
  for (auto &conn_entry : connections) {
    int id = conn_entry.first;
    auto &connection = conn_entry.second;
    if (clients[id] == NULL)
      continue;
    GetConnectionsResponsePacketEntry entry{
        .client_id = id,
        .length = static_cast<unsigned int>(connection.size()),
        .muted = clients[id]->muted,
        .deafened = clients[id]->deafened};
    memcpy(buffer + offset, &entry, sizeof(entry));
    offset += sizeof(entry);
    for (int id : connection) {
      memcpy(buffer + offset, &id, sizeof(id));
      offset += sizeof(id);
    }
  }

  spdlog::debug("Sending get connected clients response of size {}", offset);
  send(client->main_fd, buffer, offset, 0);
}

void MainServer::handle_connect_client_packet(Client *client,
                                              std::vector<char> &raw_packet) {
  spdlog::debug("Received connect client packet");
  int offset = sizeof(RequestPacketHeader);
  ConnectClientRequestPacket packet;
  memcpy(&packet, raw_packet.data() + offset, sizeof(packet));

  char buf[sizeof(ResponsePacketHeader) + sizeof(ConnectClientRequestPacket)];
  ResponsePacketHeader res_header{
      .type = ResponsePacketHeader::Type::ConnectClient};
  memcpy(buf, &res_header, sizeof(res_header));

  ConnectClientResponsePacket res;
  if (packet.id >= clients.size() || clients[packet.id] == NULL) {
    res.id = -1;
  } else {
    connections[client->id].insert(packet.id);
    connections[packet.id].insert(client->id);

    // send update to target
    res.id = client->id;
    memcpy(buf + sizeof(res_header), &res, sizeof(res));
    send(clients[packet.id]->main_fd, buf, sizeof(buf), 0);

    res.id = packet.id;
  }
  memcpy(buf + sizeof(res_header), &res, sizeof(res));
  send(client->main_fd, buf, sizeof(buf), 0);
}

void MainServer::handle_disconnect_client_packet(
    Client *client, std::vector<char> &raw_packet) {
  spdlog::debug("Received disconnect client packet");
  int offset = sizeof(RequestPacketHeader);
  DisconnectClientRequestPacket packet;
  memcpy(&packet, raw_packet.data() + offset, sizeof(packet));

  char
      buf[sizeof(ResponsePacketHeader) + sizeof(DisconnectClientRequestPacket)];
  ResponsePacketHeader res_header{
      .type = ResponsePacketHeader::Type::DisconnectClient};
  memcpy(buf, &res_header, sizeof(res_header));

  DisconnectClientResponsePacket res;
  if (packet.id >= clients.size() || clients[packet.id] == NULL) {
    // client does not exist
    res.id = -1;
  } else {
    connections[client->id].erase(packet.id);
    connections[packet.id].erase(client->id);

    // sent update to target
    send_disconnect_packet(clients[packet.id], client->id);

    res.id = packet.id;
  }
  memcpy(buf + sizeof(res_header), &res, sizeof(res));
  send(client->main_fd, buf, sizeof(buf), 0);
}

void MainServer::handle_mute(Client *client, std::vector<char> &raw_packet) {
  spdlog::debug("Received mute packet");
  int offset = sizeof(RequestPacketHeader);
  MuteRequestPacket packet;
  memcpy(&packet, raw_packet.data() + offset, sizeof(packet));

  char buf[sizeof(ResponsePacketHeader) + sizeof(MuteResponsePacket)];
  ResponsePacketHeader res_header{.type = ResponsePacketHeader::Type::Mute};
  memcpy(buf, &res_header, sizeof(res_header));

  MuteResponsePacket res;
  if (client->muted == packet.mute)
    res.status = false;
  else {
    client->muted = packet.mute;
    res.status = true;
  }
  memcpy(buf + sizeof(res_header), &res, sizeof(res));
  send(client->main_fd, buf, sizeof(buf), 0);
}

void MainServer::handle_deafen(Client *client, std::vector<char> &raw_packet) {
  spdlog::debug("Received deafen packet");
  int offset = sizeof(RequestPacketHeader);
  DeafenRequestPacket packet;
  memcpy(&packet, raw_packet.data() + offset, sizeof(packet));

  char buf[sizeof(ResponsePacketHeader) + sizeof(DeafenResponsePacket)];
  ResponsePacketHeader res_header{.type = ResponsePacketHeader::Type::Deafen};
  memcpy(buf, &res_header, sizeof(res_header));

  DeafenResponsePacket res;
  if (client->deafened == packet.deafen)
    res.status = false;
  else {
    client->deafened = packet.deafen;
    res.status = true;
  }
  memcpy(buf + sizeof(res_header), &res, sizeof(res));
  send(client->main_fd, buf, sizeof(buf), 0);
}

int main(int argc, char **argv) {
  ServerOptions options;
  options.parse_options(argc, argv);
  if (options.help) {
    std::cout << options.opts.help() << std::endl;
    return 0;
  }

  if (options.trace)
    spdlog::set_level(spdlog::level::trace);
  else if (options.debug)
    spdlog::set_level(spdlog::level::debug);

  MainServer server{.options = options};
  server.setup();

  while (true)
    server.process();
}