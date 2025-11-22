#define MAX_PACKET_SIZE 1500
#define MAX_DATA_SIZE 1396
#define EPOLL_MAX_EVENTS 1

struct RequestPacketHeader {
  enum Type { Connect, GetConnections };
  Type type;
};

struct ResponsePacketHeader {
  enum Type { Connect, GetConnections };
  Type type;
};

struct ConnectResponsePacket {
  int id;
};

struct GetConnectionsResponsePacketHeader {
  unsigned int clients;
};

struct GetConnectionsResponsePacketEntry {
  int client_id;
  unsigned int length;
};

struct AudioPacketHeader {
  enum Type { Connect, Data };
  Type type;
  int client_id;
};

struct AudioConnectResponsePacket {
  bool success;
};

struct AudioDataPacketHeader {
  int dest_client;
  unsigned long data_length;
};

void set_nonblocking(int sock_fd);