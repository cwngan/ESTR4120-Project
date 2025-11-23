#include "server.h"
#include "spdlog/spdlog.h"
#include "utils.h"
#include <netdb.h>
#include <sys/socket.h>

void AudioServer::setup() {
  int sock_fd = create_server_socket(main_server->options.audio_port,
                                     SOCK_DGRAM, BACKLOG);
  spdlog::info("Audio DGRAM server started on port {}",
               main_server->options.audio_port);

  set_nonblocking(sock_fd);
  fd = sock_fd;

  epoll_event event;
  event.events = EPOLLIN;
  event.data.fd = fd;
  if (epoll_ctl(main_server->epoll_fd, EPOLL_CTL_ADD, fd, &event) < 0)
    throw std::runtime_error("epoll_ctl: audio_fd");
}

void AudioServer::handle_event() {
  std::vector<char> raw_packet(MAX_PACKET_SIZE);
  sockaddr_storage client_addr;
  socklen_t client_len = sizeof(client_addr);
  int size = MAX_PACKET_SIZE;
  if ((size = recvfrom(fd, raw_packet.data(), raw_packet.size(), 0,
                       reinterpret_cast<sockaddr *>(&client_addr),
                       &client_len)) < 0) {
    spdlog::error("Audio server error receving packet");
    return;
  }
  spdlog::trace("Received audio packet of size {}", size);
  raw_packet.resize(size);

  AudioPacketHeader header;
  memcpy(&header, raw_packet.data(), sizeof(header));

  // check if client id exists
  if (header.client_id >= main_server->clients.size() &&
      main_server->clients[header.client_id] != NULL) {
    spdlog::error("Client does not exist");
    return;
  }

  spdlog::trace("Received packet from client id {}", header.client_id);

  Client *client = main_server->clients[header.client_id];

  switch (header.type) {
  case AudioPacketHeader::Type::Connect:
    handle_connect_packet(client, client_addr, client_len);
    break;
  case AudioPacketHeader::Type::Data:
    handle_data_packet(client, raw_packet);
  }
}

void AudioServer::handle_connect_packet(Client *client,
                                        sockaddr_storage client_addr,
                                        socklen_t client_len) {
  AudioConnectResponsePacket res{.success = false};
  if (!client->dgram_connected) {
    // first packet to audio server
    // map client id to this addr
    client->audio_addr = client_addr;
    client->audio_addr_len = client_len;
    client->dgram_connected = true;

    std::vector<char> raw_hostname(NI_MAXHOST);
    std::vector<char> raw_service(NI_MAXSERV);

    spdlog::debug("ipv4 len: {}", sizeof(sockaddr_in));
    spdlog::debug("ipv6 len: {}", sizeof(sockaddr_in6));
    spdlog::debug("client_len: {}", client_len);
    spdlog::debug("ai_family: {}", client_addr.ss_family);
    AF_INET;
    int nameinfo_res;
    if ((nameinfo_res = getnameinfo(
             reinterpret_cast<sockaddr *>(&client_addr), client_len,
             raw_hostname.data(), raw_hostname.size(), raw_service.data(),
             raw_service.size(), NI_NUMERICHOST | NI_NUMERICSERV)) != 0) {

      spdlog::error("Error getting information of audio client. Reason: {}",
                    gai_strerror(nameinfo_res));
      throw std::runtime_error("Error getting information of audio client.");
    }

    client->audio_hostname =
        std::string(raw_hostname.begin(), raw_hostname.end());
    client->audio_service = std::string(raw_service.begin(), raw_service.end());

    // send ack packet through main server
    res.success = true;
  }
  spdlog::debug("Received connect packet from client {}:{}",
                client->audio_hostname, client->audio_service);

  if (sendto(client->main_fd, &res, sizeof(res), 0,
             reinterpret_cast<sockaddr *>(&client_addr), client_len) < 0)
    spdlog::error("Error sending audio connect response packet to {}:{}",
                  client->hostname, client->service);

  spdlog::info("Sent audio connection response to client {}:{}",
               client->hostname, client->service);
}

void AudioServer::handle_data_packet(Client *client,
                                     std::vector<char> &raw_packet) {
  AudioDataPacketHeader audio_header;
  memcpy(&audio_header, raw_packet.data() + sizeof(AudioPacketHeader),
         sizeof(audio_header));
  std::vector<char> audio_data(audio_header.data_length);
  memcpy(audio_data.data(),
         raw_packet.data() + sizeof(AudioPacketHeader) +
             sizeof(AudioDataPacketHeader),
         audio_header.data_length);

  for (int dst_id : main_server->connections[client->id]) {
    Client *dest_client = main_server->clients[dst_id];
    if (!dest_client)
      continue;
    spdlog::trace("Received data packet from {}:{}", client->audio_hostname,
                  client->audio_service);
    spdlog::trace("dest_client: {}, data_length: {}", dst_id,
                  audio_header.data_length);
    sendto(fd, raw_packet.data(), raw_packet.size(), 0,
           reinterpret_cast<sockaddr *>(&dest_client->audio_addr),
           dest_client->audio_addr_len);
    spdlog::trace("Redirected data packet from {}:{} to {}:{}",
                  client->audio_hostname, client->audio_service,
                  dest_client->audio_hostname, dest_client->audio_service);
  }
}
