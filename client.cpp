#include "client.h"
#include "audio_input.h"
#include "audio_output.h"
#include "common.h"
#include "cxxopts.hpp"
#include "spdlog/spdlog.h"

#include <iostream>
#include <netdb.h>
#include <opus.h>
#include <opus_defines.h>
#include <stdexcept>

int loopback_audio(OpusEncoder *encoder_state, OpusDecoder *decoder_state) {

  CallbackData *cb_data = new CallbackData;
  cb_data->encoder_state = encoder_state;
  cb_data->decoder_state = decoder_state;
  cb_data->ring_buffer = NULL;

  ma_device *input_device = create_input_device(cb_data);
  ma_device *output_device = create_output_device(cb_data);

  opus_encoder_ctl(encoder_state, OPUS_SET_BITRATE(BITRATE));
  opus_decoder_ctl(decoder_state, OPUS_SET_BITRATE(BITRATE));

  ma_device_start(output_device);
  ma_device_start(input_device);

  std::cout << "Press enter to quit..." << std::endl;
  getchar();

  ma_device_stop(output_device);
  ma_device_uninit(output_device);
  ma_device_stop(input_device);
  ma_device_uninit(input_device);

  ma_rb_uninit(cb_data->ring_buffer);
  delete cb_data;

  return 0;
}

ClientOptions::ClientOptions() : opts("client") {
  // clang-format off
  opts.add_options()
    ("h,hostname", "Hostname of the server", cxxopts::value<std::string>())
    ("p,port", "Port of the server", cxxopts::value<std::string>())
    ("help", "Print help", cxxopts::value<bool>()->default_value("false"));
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
}

void Client::setup() {
  addrinfo hints, *res;
  memset(&hints, 0, sizeof(addrinfo));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  spdlog::debug("Getting addrinfo at port {}", options.port);

  if (getaddrinfo(options.hostname.c_str(), options.port.c_str(), &hints, &res))
    throw std::runtime_error("Error getting addr info");

  int sock_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (sock_fd == -1)
    throw std::runtime_error("Error creating socket");

  int yes = 1;
  setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

  if (connect(sock_fd, res->ai_addr, res->ai_addrlen) < 0)
    throw std::runtime_error("Error connecting to server");

  spdlog::info("Client started");

  fd = sock_fd;
}

void Client::init_connect() {
  PacketHeader header{.type = PacketHeader::Type::Connect};
  if (send(fd, &header, sizeof(header), 0) < 0)
    throw std::runtime_error("Error sending connection init packet");
  spdlog::info("Sent connection init packet");

  ConnectResponsePacket response;
  if (recv(fd, &response, sizeof(response), 0) < 0)
    throw std::runtime_error("Error receiving connection init response packet");

  spdlog::info("Assigned id: {}", response.id);
  client_id = response.id;
}

void Client::process() {}

int main(int argc, char **argv) {
  ClientOptions options;
  options.parse_options(argc, argv);

  Client client;
  client.options = options;

  client.setup();
  client.init_connect();

  while (true)
    client.process();

  int error;
  OpusEncoder *encoder_state =
      opus_encoder_create(SAMPLE_RATE, 2, OPUS_APPLICATION_VOIP, &error);
  OpusDecoder *decoder_state = opus_decoder_create(SAMPLE_RATE, 2, &error);

  loopback_audio(encoder_state, decoder_state);
}