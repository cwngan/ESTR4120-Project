#define MAX_PACKET_SIZE 1500
#define MAX_DATA_SIZE 1396

struct PacketHeader {
  enum Type { Audio, Connect };
  Type type;
};

struct AudioPacketHeader {
  unsigned int src_client;
  unsigned int dest_client;
  unsigned int data_length;
};

struct ConnectResponsePacket {
  unsigned int id;
};

void set_nonblocking(int sock_fd);