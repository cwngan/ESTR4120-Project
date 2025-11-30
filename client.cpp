#include "client.h"
#include "audio_common.h"
#include "common.h"
#include "cxxopts.hpp"
#include "spdlog/common.h"
#include "spdlog/sinks/daily_file_sink.h"
#include "spdlog/spdlog.h"
#include "utils.h"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <netdb.h>
#include <opus.h>
#include <opus_defines.h>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

void Client::print_interaction_menu() {
  std::cout << std::endl;
  std::cout << "Your client ID: " << client_id << std::endl;
  std::cout << std::endl;
  std::cout << "Interactive Menu" << std::endl;
  std::cout << "1\t - Print connected clients" << std::endl;
  std::cout << "2 <id>\t - Connect to clients" << std::endl;
  std::cout << "3 <id>\t - Disconnect from client" << std::endl;
  std::cout << "4\t - " << (muted ? "Unmute" : "Mute") << std::endl;
  std::cout << "5\t - " << (deafened ? "Undeafen" : "Deafen") << std::endl;
  std::cout << "0\t - Quit" << std::endl;
  std::cout << "----------------------------\nEnter action: " << std::flush;
}

ClientOptions::ClientOptions() : opts("client") {
  // clang-format off
  opts.add_options()
    ("h,hostname", "Hostname of the server", cxxopts::value<std::string>())
    ("p,port", "Port of the server", cxxopts::value<std::string>())
    ("a,audio-port", "Port of the audio server", cxxopts::value<std::string>())
    ("help", "Print help", cxxopts::value<bool>()->default_value("false"))
    ("debug", "Output debug logs", cxxopts::value<bool>()->default_value("false"))
    ("trace", "Output trace logs (EXTREMELY LARGE FILES)", cxxopts::value<bool>()->default_value("false"));
  // clang-format on
}

void ClientOptions::parse_options(int argc, char **argv) {
  cxxopts::ParseResult result;

  try {
    result = opts.parse(argc, argv);
  } catch (cxxopts::exceptions::exception &e) {
    throw std::invalid_argument("Failed to parse options: " +
                                std::string(e.what()));
  }

  hostname = result["hostname"].as<std::string>();
  port = result["port"].as<std::string>();
  audio_port = result["audio-port"].as<std::string>();

  debug = result["debug"].as<bool>();
  trace = result["trace"].as<bool>();
}

void Client::setup_main_connection() {
  main_fd = create_client_socket(options.hostname, options.port, SOCK_STREAM);
  spdlog::info("Client started");

  RequestPacketHeader header{.type = RequestPacketHeader::Type::Connect};
  if (send(main_fd, &header, sizeof(header), 0) < 0)
    throw std::runtime_error("Error sending connection init packet");
  spdlog::info("Sent connection init packet");

  ResponsePacketHeader response_header;
  ConnectResponsePacket response;
  char buffer[MAX_PACKET_SIZE];

  if (recv(main_fd, buffer, MAX_PACKET_SIZE, 0) < 0)
    throw std::runtime_error("Error receiving connection init response packet");
  memcpy(&response_header, buffer, sizeof(response_header));
  memcpy(&response, buffer + sizeof(response_header), sizeof(response));

  spdlog::info("Assigned id: {}", response.id);
  client_id = response.id;
}

void Client::setup_audio_connection() {
  audio_fd =
      create_client_socket(options.hostname, options.audio_port, SOCK_DGRAM);

  set_nonblocking(audio_fd);
  spdlog::info("Audio server socket established");

  AudioPacketHeader header{.type = AudioPacketHeader::Type::Connect,
                           .client_id = client_id};
  send(audio_fd, &header, sizeof(header), 0);

  AudioConnectResponsePacket response;
  recv(main_fd, &response, sizeof(response), 0);

  epoll_fd = epoll_create1(0);
  epoll_event event;
  event.events = EPOLLIN;
  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, audio_fd, &event) < 0)
    throw std::runtime_error("Error listening for events on audio fd");

  if (!response.success)
    throw std::runtime_error("Failed to connect audio server");

  spdlog::info("Successfully connected to audio server");
}

void Client::setup_interaction() {
  epoll_event event;
  event.events = EPOLLIN;
  event.data.fd = STDIN_FILENO;
  epoll_ctl(epoll_fd, EPOLL_CTL_ADD, STDIN_FILENO, &event);
}

bool Client::process() {
  epoll_event events[EPOLL_MAX_EVENTS];
  int n_events = epoll_wait(epoll_fd, events, EPOLL_MAX_EVENTS, -1);
  bool status = true;
  for (int i = 0; i < n_events && status; i++) {
    status = true;
    if (events[i].data.fd == STDIN_FILENO) {
      status = process_interaction();
    } else if (events[i].data.fd == main_fd) {
      status = process_main_packet();
    } else {
      status = process_audio_packet();
    }
  }
  return status;
}

bool Client::process_interaction() {
  spdlog::debug("STDIN activity");
  std::string line;
  std::getline(std::cin, line);
  if (line.size() == 0)
    return true;

  std::vector<std::string> actions;
  std::stringstream ss(line);
  std::string word;
  while (ss >> word)
    actions.push_back(word);

  if (actions[0] == "0")
    return false;

  if (actions[0] == "1") {
    get_connections();
  } else if (actions[0] == "2" && actions.size() >= 2) {
    connect_client(std::stoi(actions[1]));
  } else if (actions[0] == "3" && actions.size() >= 2) {
    disconnect_client(std::stoi(actions[1]));
  } else if (actions[0] == "4") {
    if (muted)
      unmute();
    else
      mute();
  } else if (actions[0] == "5") {
    if (deafened)
      undeafen();
    else
      deafen();
  } else {
    std::cout << "Invalid action!\n";
    print_interaction_menu();
  }

  return true;
}

bool Client::process_main_packet() {
  spdlog::debug("Main fd activity");
  std::vector<unsigned char> buffer(MAX_PACKET_SIZE);
  int size = recv(main_fd, buffer.data(), MAX_PACKET_SIZE, 0);
  if (size < 0) {
    spdlog::error("Error receiving data from main server. Reason: {}",
                  strerror(errno));
    return false;
  } else if (size == 0) {
    spdlog::info("Server disconnected");
    return false;
  }
  buffer.resize(size);
  int offset = 0;

  ResponsePacketHeader packet_header;
  memcpy(&packet_header, buffer.data() + offset, sizeof(packet_header));
  offset += sizeof(packet_header);

  spdlog::debug("Received packet type {}",
                static_cast<int>(packet_header.type));
  switch (packet_header.type) {
  case ResponsePacketHeader::Type::Connect: {
    // ignore connect response packets
    break;
  }
  case ResponsePacketHeader::Type::GetConnections: {
    GetConnectionsResponsePacketHeader content_header;
    memcpy(&content_header, buffer.data() + offset, sizeof(content_header));
    offset += sizeof(content_header);

    spdlog::debug("header.clients: {}", content_header.clients);

    std::cout << "\nCurrent connections\n";
    std::cout << "----------------------------\n";
    for (int i = 0; i < content_header.clients; i++) {
      GetConnectionsResponsePacketEntry entry;
      memcpy(&entry, buffer.data() + offset, sizeof(entry));
      offset += sizeof(entry);
      std::cout << entry.client_id;
      if (entry.muted || entry.deafened) {
        std::cout << " (";
        if (entry.muted)
          std::cout << "m";
        if (entry.deafened)
          std::cout << "d";
        std::cout << ")";
      }
      std::cout << "\t - ";
      spdlog::debug("entry.length: {}, entry.client_id: {}", entry.length,
                    entry.client_id);
      if (entry.length == 0)
        std::cout << "No connected client";
      for (int j = 0; j < entry.length; j++) {
        int id;
        memcpy(&id, buffer.data() + offset, sizeof(id));
        offset += sizeof(id);
        std::cout << id;
        if (j < entry.length - 1)
          std::cout << ", ";
      }
      std::cout << std::endl;
    }

    break;
  }
  case ResponsePacketHeader::Type::ConnectClient: {
    ConnectClientResponsePacket res;
    memcpy(&res, buffer.data() + offset, sizeof(res));
    if (res.id >= 0) {
      spdlog::debug("Received connection packet, id: {}", res.id);
      if (cb_data->ring_buffers.find(res.id) == cb_data->ring_buffers.end()) {
        cb_data->ring_buffers[res.id] = create_ring_buffer(SAMPLE_RATE);
        cb_data->connected_clients++;
        spdlog::debug("Created ring buffer with size {}", SAMPLE_RATE);
        int error;
        OpusDecoder *decoder_state =
            opus_decoder_create(SAMPLE_RATE, CHANNELS, &error);
        opus_decoder_ctl(decoder_state, OPUS_SET_BITRATE(BITRATE));
        cb_data->decoder_states[res.id] = {.seq_number = -1,
                                           .decoder_state = decoder_state};
      }
      if (!muted)
        input->start();
      if (!deafened)
        output->start();
      std::cout << "\nSuccessfully connected to client " << res.id << std::endl;
    } else {
      std::cout << "\nFailed to connect to client\n";
    }
    break;
  }
  case ResponsePacketHeader::Type::DisconnectClient: {
    DisconnectClientResponsePacket res;
    memcpy(&res, buffer.data() + offset, sizeof(res));
    if (res.id >= 0) {
      spdlog::debug("Received disconnection packet, id: {}", res.id);
      if (cb_data->ring_buffers.find(res.id) != cb_data->ring_buffers.end()) {
        ma_pcm_rb_uninit(cb_data->ring_buffers[res.id]);
        cb_data->ring_buffers.erase(res.id);
        cb_data->connected_clients--;
        opus_decoder_destroy(cb_data->decoder_states[res.id].decoder_state);
        cb_data->decoder_states.erase(res.id);
      }
      if (cb_data->connected_clients == 0) {
        input->pause();
        output->pause();
      }
      std::cout << "\nSuccessfully disconnected from client " << res.id
                << std::endl;
    } else {
      std::cout << "\nFailed to disconnect from client\n";
    }
    break;
  }
  case ResponsePacketHeader::Type::Mute: {
    MuteResponsePacket res;
    memcpy(&res, buffer.data() + offset, sizeof(res));
    if (res.status) {
      std::cout << "\nSuccessfully muted\n";
    } else {
      std::cout << "\nFailed to mute\n";
    }
    break;
  }
  case ResponsePacketHeader::Type::Deafen: {
    DeafenResponsePacket res;
    memcpy(&res, buffer.data() + offset, sizeof(res));
    if (res.status) {
      std::cout << "\nSuccessfully deafened\n";
    } else {
      std::cout << "\nFailed to deafen\n";
    }
    break;
  }
  }

  print_interaction_menu();
  return true;
}

bool Client::process_audio_packet() {
  spdlog::trace("Audio fd activity");
  std::vector<unsigned char> buffer(MAX_PACKET_SIZE);
  int size = recv(audio_fd, buffer.data(), MAX_PACKET_SIZE, 0);
  if (size < 0) {
    spdlog::error("Error receiving data from audio server. Reason: {}",
                  strerror(errno));
    return false;
  } else if (size == 0) {
    spdlog::info("Audio server disconnected");
    return false;
  }
  buffer.resize(size);
  AudioPacketHeader header;
  memcpy(&header, buffer.data(), sizeof(header));
  if (header.type != AudioPacketHeader::Type::Data) {
    spdlog::error("Unsupported packet type from audio server");
    return true;
  }

  if (cb_data->ring_buffers.find(header.client_id) ==
      cb_data->ring_buffers.end()) {
    return true;
  }

  AudioDataPacketHeader data_header;
  memcpy(&data_header, buffer.data() + sizeof(header), sizeof(data_header));
  std::vector<unsigned char> data(data_header.data_length);
  memcpy(data.data(), buffer.data() + sizeof(header) + sizeof(data_header),
         data.size());

  int *current_seq_num = &cb_data->decoder_states[header.client_id].seq_number;
  // ignore old packet
  if (*current_seq_num != -1 &&
      (data_header.seq_number < *current_seq_num ||
       data_header.seq_number - *current_seq_num > (1 << 30))) {

    spdlog::debug("received old packet seq no. {}, current at {}",
                  data_header.seq_number, *current_seq_num);
    return true;
  }

  if (*current_seq_num >= 0 && *current_seq_num < data_header.seq_number) {
    spdlog::debug("skipping seq no. to {}, current at {}",
                  data_header.seq_number, *current_seq_num);
    void *write_ptr;
    ma_uint32 target_frames_to_write =
        FRAME_COUNT * (data_header.seq_number - *current_seq_num);
    ma_uint32 frames_written = 0;
    while (frames_written < target_frames_to_write) {
      ma_uint32 frames_to_write = target_frames_to_write - frames_written;
      if (ma_pcm_rb_acquire_write(cb_data->ring_buffers[header.client_id],
                                  &frames_to_write, &write_ptr) != MA_SUCCESS) {

        spdlog::debug("Failed to acquire write pointer");
        return true;
      }
      auto decoded_bytes = opus_decode_float(
          cb_data->decoder_states[header.client_id].decoder_state, NULL, 0,
          static_cast<float *>(write_ptr), FRAME_COUNT, 0);
      ma_pcm_rb_commit_write(cb_data->ring_buffers[header.client_id],
                             frames_to_write);
      frames_written += frames_to_write;
      spdlog::debug("commited writing {} ({} bytes)/{} frames of empty data",
                    frames_written, decoded_bytes, target_frames_to_write);
    }
  }

  void *write_ptr;
  ma_uint32 frames_to_write = FRAME_COUNT;
  if (ma_pcm_rb_acquire_write(cb_data->ring_buffers[header.client_id],
                              &frames_to_write, &write_ptr) != MA_SUCCESS ||
      frames_to_write != FRAME_COUNT) {
    spdlog::debug("Buffer full");
    return true;
  }

  auto decoded_bytes = opus_decode_float(
      cb_data->decoder_states[header.client_id].decoder_state,
      buffer.data() + sizeof(header) + sizeof(data_header),
      data_header.data_length, static_cast<float *>(write_ptr), FRAME_COUNT, 0);

  spdlog::trace("decoding with parameters: frameCount={}, size={}", FRAME_COUNT,
                data_header.data_length);
  ma_pcm_rb_commit_write(cb_data->ring_buffers[header.client_id],
                         frames_to_write);

  cb_data->decoder_states[header.client_id].seq_number =
      data_header.seq_number == INT32_MAX ? 0 : data_header.seq_number + 1;

  spdlog::trace(
      "write {} bytes, {} frames of audio data in ring buffer for client {}",
      decoded_bytes, frames_to_write, header.client_id);

  return true;
}

void Client::capture_data_handler(std::vector<unsigned char> &data) {
  // spdlog::trace("captured audio data of {} bytes", data.size());
  AudioPacketHeader header{.type = AudioPacketHeader::Type::Data,
                           .client_id = client_id};
  AudioDataPacketHeader data_header{.seq_number = seq_number,
                                    .data_length = data.size()};
  int packet_size = sizeof(header) + sizeof(data_header) + data.size();
  unsigned char *packet = new unsigned char[packet_size];
  memcpy(packet, &header, sizeof(header));
  memcpy(packet + sizeof(header), &data_header, sizeof(data_header));
  memcpy(packet + sizeof(header) + sizeof(data_header), data.data(),
         data.size());
  send(audio_fd, packet, packet_size, 0);

  if (seq_number == INT32_MAX)
    seq_number = 0;
  else
    seq_number++;
}

void Client::get_connections() {
  RequestPacketHeader header{.type = RequestPacketHeader::Type::GetConnections};
  send(main_fd, &header, sizeof(header), 0);
}

void Client::connect_client(int id) {
  char buf[sizeof(RequestPacketHeader) + sizeof(ConnectClientRequestPacket)];

  RequestPacketHeader header{.type = RequestPacketHeader::Type::ConnectClient};
  memcpy(buf, &header, sizeof(header));

  ConnectClientRequestPacket req{.id = id};
  memcpy(buf + sizeof(header), &req, sizeof(req));

  send(main_fd, buf, sizeof(buf), 0);
}

void Client::disconnect_client(int id) {
  char buf[sizeof(RequestPacketHeader) + sizeof(DisconnectClientRequestPacket)];

  RequestPacketHeader header{.type =
                                 RequestPacketHeader::Type::DisconnectClient};
  memcpy(buf, &header, sizeof(header));

  DisconnectClientRequestPacket req{.id = id};
  memcpy(buf + sizeof(header), &req, sizeof(req));

  send(main_fd, buf, sizeof(buf), 0);
}

void Client::mute() {
  input->pause();
  muted = true;

  char buf[sizeof(RequestPacketHeader) + sizeof(MuteRequestPacket)];

  RequestPacketHeader header{.type = RequestPacketHeader::Type::Mute};
  memcpy(buf, &header, sizeof(header));

  MuteRequestPacket req{.mute = true};
  memcpy(buf + sizeof(header), &req, sizeof(req));

  send(main_fd, buf, sizeof(buf), 0);
}

void Client::unmute() {
  input->start();
  muted = false;

  char buf[sizeof(RequestPacketHeader) + sizeof(MuteRequestPacket)];

  RequestPacketHeader header{.type = RequestPacketHeader::Type::Mute};
  memcpy(buf, &header, sizeof(header));

  MuteRequestPacket req{.mute = false};
  memcpy(buf + sizeof(header), &req, sizeof(req));

  send(main_fd, buf, sizeof(buf), 0);
}

void Client::deafen() {
  output->pause();
  deafened = true;

  char buf[sizeof(RequestPacketHeader) + sizeof(DeafenRequestPacket)];

  RequestPacketHeader header{.type = RequestPacketHeader::Type::Deafen};
  memcpy(buf, &header, sizeof(header));

  DeafenRequestPacket req{.deafen = true};
  memcpy(buf + sizeof(header), &req, sizeof(req));

  send(main_fd, buf, sizeof(buf), 0);
}

void Client::undeafen() {
  output->start();
  deafened = false;

  char buf[sizeof(RequestPacketHeader) + sizeof(DeafenRequestPacket)];

  RequestPacketHeader header{.type = RequestPacketHeader::Type::Deafen};
  memcpy(buf, &header, sizeof(header));

  DeafenRequestPacket req{.deafen = false};
  memcpy(buf + sizeof(header), &req, sizeof(req));

  send(main_fd, buf, sizeof(buf), 0);
}

int main(int argc, char **argv) {
  auto daily_logger =
      spdlog::daily_logger_mt("daily_logger", "logs/client.log");
  spdlog::set_default_logger(daily_logger);
  spdlog::flush_on(spdlog::level::trace);

  ClientOptions options;
  options.parse_options(argc, argv);

  if (options.trace)
    spdlog::set_level(spdlog::level::trace);
  else if (options.debug)
    spdlog::set_level(spdlog::level::debug);

  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_real_distribution<> distrib(0, INT_MAX);

  Client client;
  client.options = options;
  client.seq_number = distrib(gen);

  ma_context context;
  if (ma_context_init(NULL, 0, NULL, &context) != MA_SUCCESS) {
    spdlog::error("Error initializing audio context");
    throw std::runtime_error("ma_context_init()");
  }

  ma_device_info *pPlaybackInfos;
  ma_uint32 playbackCount;
  ma_device_info *pCaptureInfos;
  ma_uint32 captureCount;
  if (ma_context_get_devices(&context, &pPlaybackInfos, &playbackCount,
                             &pCaptureInfos, &captureCount) != MA_SUCCESS) {
    spdlog::error("Error getting devices");
    throw std::runtime_error("ma_context_get_devices()");
  }

  int playback_device, capture_device;

  std::cout << "Playback Devices:\n";
  for (ma_uint32 iDevice = 0; iDevice < playbackCount; iDevice += 1) {
    std::cout << iDevice << " - " << pPlaybackInfos[iDevice].name
              << (pPlaybackInfos[iDevice].isDefault ? " (default)" : "")
              << std::endl;
  }
  std::cout << "----------------------------\nPlayback device: ";
  std::cin >> playback_device;

  std::cout << "\nCapture Devices:\n";
  for (ma_uint32 iDevice = 0; iDevice < captureCount; iDevice += 1) {
    std::cout << iDevice << " - " << pCaptureInfos[iDevice].name
              << (pCaptureInfos[iDevice].isDefault ? " (default)" : "")
              << std::endl;
  }
  std::cout << "----------------------------\nCapture device: ";
  std::cin >> capture_device;

  client.setup_main_connection();
  client.setup_audio_connection();
  client.setup_interaction();

  set_nonblocking(client.main_fd);
  epoll_event event;
  event.events = EPOLLIN;
  event.data.fd = client.main_fd;
  epoll_ctl(client.epoll_fd, EPOLL_CTL_ADD, client.main_fd, &event);

  int error;
  OpusEncoder *encoder_state = opus_encoder_create(
      SAMPLE_RATE, CHANNELS, OPUS_APPLICATION_AUDIO, &error);
  opus_encoder_ctl(encoder_state, OPUS_SET_BITRATE(BITRATE));

  client.cb_data = new CallbackData;
  client.cb_data->encoder_state = encoder_state;
  client.cb_data->decoder_states = {};
  client.cb_data->ring_buffers = {};
  client.cb_data->capture_data_handler =
      [&client](std::vector<unsigned char> &data) {
        client.capture_data_handler(data);
      };

  AudioInput *input = new AudioInput(encoder_state, client.cb_data,
                                     pCaptureInfos[capture_device].id);
  AudioOutput *output =
      new AudioOutput(client.cb_data, pPlaybackInfos[playback_device].id);

  client.input = input;
  client.output = output;

  client.print_interaction_menu();

  while (client.process())
    continue;

  std::cout << "\nShutting down...\n";

  client.input->stop();
  client.output->stop();
  for (auto &entry : client.cb_data->ring_buffers) {
    ma_pcm_rb_uninit(entry.second);
  }
  opus_encoder_destroy(client.cb_data->encoder_state);
  for (auto entry : client.cb_data->decoder_states) {
    opus_decoder_destroy(entry.second.decoder_state);
  }
  delete client.cb_data;

  spdlog::info("Client shutdown");
  spdlog::shutdown();
}