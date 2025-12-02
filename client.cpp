#include "client.h"
#include "audio_common.h"
#include "common.hpp"
#include "cxxopts.hpp"
#include "spdlog/common.h"
#include "spdlog/sinks/daily_file_sink.h"
#include "spdlog/spdlog.h"
#include "utils.h"

#include <chrono>
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
  std::cout << "6 <num>\t - Set bitrate (current: "
            << (bitrate == -1 ? "maximum"
                              : std::to_string(bitrate / 1000) + "kbps")
            << ")" << std::endl;
  std::cout << "7 <num>\t - Set audio mode: 1 AUDIO, 2 VOIP, 3 LOWDELAY"
            << std::endl;
  std::cout << "8\t - Show number of skipped packets" << std::endl;
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
  } else if (actions[0] == "6" && actions.size() >= 2) {
    bitrate = std::stoi(actions[1]);
    opus_encoder_ctl(cb_data->encoder_state, OPUS_SET_BITRATE(bitrate));
    print_interaction_menu();
  } else if (actions[0] == "7" && actions.size() >= 2) {
    int mode = std::stoi(actions[1]);
    if (mode == 1)
      opus_encoder_ctl(cb_data->encoder_state,
                       OPUS_SET_APPLICATION(OPUS_APPLICATION_AUDIO));
    if (mode == 2)
      opus_encoder_ctl(cb_data->encoder_state,
                       OPUS_SET_APPLICATION(OPUS_APPLICATION_VOIP));
    if (mode == 3)
      opus_encoder_ctl(
          cb_data->encoder_state,
          OPUS_SET_APPLICATION(OPUS_APPLICATION_RESTRICTED_LOWDELAY));
    print_interaction_menu();
  } else if (actions[0] == "8") {
    std::cout << "Number of skipped packets: " << jitter_buffer->skipped
              << std::endl;
    print_interaction_menu();
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
        opus_decoder_ctl(decoder_state, OPUS_SET_BITRATE(OPUS_BITRATE_MAX));
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
  if (data_header.ping) {
    spdlog::debug("Received ping for packet {} at {}", data_header.seq_number,
                  std::chrono::system_clock::now().time_since_epoch().count());
    cb_data->pings.insert(data_header.seq_number);
  }
  std::vector<unsigned char> data(data_header.data_length);
  memcpy(data.data(), buffer.data() + sizeof(header) + sizeof(data_header),
         data.size());

  jitter_buffer->write_frame(data.data(), static_cast<int>(data.size()),
                             data_header.seq_number, header.client_id);

  spdlog::trace("wrote {} frames of audio data in jitter buffer for client {}",
                FRAME_COUNT, header.client_id);

  return true;
}

void Client::capture_data_handler(std::vector<unsigned char> &data) {
  // spdlog::trace("captured audio data of {} bytes", data.size());
  AudioPacketHeader header{.type = AudioPacketHeader::Type::Data,
                           .client_id = client_id};
  AudioDataPacketHeader data_header{
      .seq_number = seq_number,
      .data_length = data.size(),
      .ping = seq_number % (SAMPLE_RATE / FRAME_COUNT) == 0};
  if (data_header.ping)
    spdlog::debug("Sent ping for packet {} at {}", seq_number,
                  std::chrono::system_clock::now().time_since_epoch().count());
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

JitterBuffer::JitterBuffer(Client *client, size_t size)
    : client(client), size(size) {}

void JitterBuffer::write_frame(unsigned char *src, int data_length,
                               int seq_number, int client_id) {
  mutex.lock();

  spdlog::trace("in write_frame, write head: {}, read_head: {}",
                write_heads[client_id], read_heads[client_id]);
  int bytes_per_encoded_data = MAX_AUDIO_DATA_SIZE;
  if (initial_seq == -1)
    initial_seq = seq_number;
  if (buffers.find(client_id) == buffers.end()) {
    buffers[client_id].assign(size * bytes_per_encoded_data, 0);
    data_lengths[client_id].assign(size, 0);
    read_heads[client_id] = 0;
    write_heads[client_id] = 0;
  }

  int encoded_size = MAX_AUDIO_DATA_SIZE;
  int buffer_index = ((seq_number - initial_seq) % size + size) % size;
  memcpy(buffers[client_id].data() + buffer_index * bytes_per_encoded_data, src,
         data_length);
  data_lengths[client_id][buffer_index] = data_length;
  write_heads[client_id] = (buffer_index > write_heads[client_id] ||
                            write_heads[client_id] - buffer_index > size / 2)
                               ? buffer_index
                               : write_heads[client_id];
  mutex.unlock();
}

bool JitterBuffer::read_frame(float *dest, int client_id) {
  mutex.lock();
  spdlog::trace("in read_frame, write head: {}, read_head: {}",
                write_heads[client_id], read_heads[client_id]);
  if (buffers.find(client_id) == buffers.end() ||
      (write_heads[client_id] >= read_heads[client_id] &&
       write_heads[client_id] - read_heads[client_id] <
           JITTER_DELAY / FRAME_COUNT)) {
    mutex.unlock();
    return false;
  }

  if ((write_heads[client_id] < read_heads[client_id]
           ? write_heads[client_id] + size
           : write_heads[client_id]) -
          read_heads[client_id] >
      MAX_DELAY / FRAME_COUNT) {
    int prev = read_heads[client_id];
    read_heads[client_id] =
        write_heads[client_id] - (MAX_DELAY - JITTER_DELAY) / FRAME_COUNT / 2;
    // skipped += read_heads[client_id] - prev;
  }

  int bytes_per_encoded_data = MAX_AUDIO_DATA_SIZE;

  size_t data_length = data_lengths[client_id][read_heads[client_id]];
  unsigned char *decode_addr;
  if (data_length == 0) {
    decode_addr = NULL;
    skipped++;
  } else {
    decode_addr = buffers[client_id].data() +
                  read_heads[client_id] * bytes_per_encoded_data;
  }

  auto decoded_bytes = opus_decode_float(
      client->cb_data->decoder_states[client_id].decoder_state, decode_addr,
      data_lengths[client_id][read_heads[client_id]], dest, FRAME_COUNT, 0);
  spdlog::trace("decoding with parameters: frameCount={}, size={}", FRAME_COUNT,
                data_length);

  // client->cb_data->decoder_states[client_id].seq_number =
  //     data_header.seq_number == INT32_MAX ? 0 : data_header.seq_number + 1;
  data_lengths[client_id][read_heads[client_id]] = 0;
  read_heads[client_id]++;
  read_heads[client_id] %= size;
  mutex.unlock();

  return true;
}

void JitterBuffer::push_to_rb() {
  auto next_wake = std::chrono::steady_clock::now();
  int frame_interval = 1000 / (SAMPLE_RATE / FRAME_COUNT);
  while (!shutting_down) {
    spdlog::trace("running thread at time {}",
                  next_wake.time_since_epoch().count());
    next_wake += std::chrono::milliseconds(frame_interval);

    spdlog::trace("ring buffer count: {}",
                  client->cb_data->ring_buffers.size());
    for (auto entry : client->cb_data->ring_buffers) {
      int id = entry.first;
      auto rb = entry.second;
      void *write_ptr;
      ma_uint32 frames_to_write = FRAME_COUNT;

      if (ma_pcm_rb_acquire_write(rb, &frames_to_write, &write_ptr) !=
              MA_SUCCESS ||
          frames_to_write != FRAME_COUNT) {
        spdlog::debug("Buffer for client {} full", id);
        continue;
      }

      if (!read_frame(static_cast<float *>(write_ptr), id)) {
        continue;
      }
      ma_pcm_rb_commit_write(rb, frames_to_write);

      spdlog::trace("write {} frames of audio data in ring buffer "
                    "for client {}",
                    frames_to_write, id);
    }
    std::this_thread::sleep_until(next_wake);
  }
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
  client.decoded_data = std::vector<float>(FRAME_COUNT);
  client.jitter_buffer = new JitterBuffer(&client, SAMPLE_RATE / FRAME_COUNT);

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
  opus_encoder_ctl(encoder_state, OPUS_SET_BITRATE(OPUS_BITRATE_MAX));

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

  std::thread push_thread([&client]() { client.jitter_buffer->push_to_rb(); });

  client.print_interaction_menu();

  while (client.process())
    continue;

  std::cout << "\nShutting down...\n";

  client.jitter_buffer->shutting_down = true;
  push_thread.join();

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