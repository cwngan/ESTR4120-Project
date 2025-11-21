#define MAX_PACKET_SIZE 1500
#define MAX_DATA_SIZE 1396
#define EPOLL_MAX_EVENTS 1

struct PacketHeader {
  enum Type { Connect };
  Type type;
};

struct ConnectResponsePacket {
  unsigned int id;
};

struct AudioPacketHeader {
  enum Type { Connect, Data };
  Type type;
  unsigned int client_id;
};

struct AudioConnectResponsePacket {
  bool success;
};

struct AudioDataPacketHeader {
  unsigned int dest_client;
  unsigned long data_length;
};

void set_nonblocking(int sock_fd);