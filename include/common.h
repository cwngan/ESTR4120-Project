#define MAX_PACKET_SIZE 1500
#define MAX_DATA_SIZE 1396
#define EPOLL_MAX_EVENTS 1

struct RequestPacketHeader {
  enum Type {
    Connect,
    GetConnections,
    ConnectClient,
    DisconnectClient,
    Mute,
    Deafen
  };
  Type type;
};

struct ResponsePacketHeader {
  enum Type {
    Connect,
    GetConnections,
    ConnectClient,
    DisconnectClient,
    Mute,
    Deafen
  };
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
  bool muted;
  bool deafened;
};

struct ConnectClientRequestPacket {
  int id;
};

struct ConnectClientResponsePacket {
  int id;
};

struct DisconnectClientRequestPacket {
  int id;
};

struct DisconnectClientResponsePacket {
  int id;
};

struct MuteRequestPacket {
  bool mute;
};

struct MuteResponsePacket {
  bool status;
};

struct DeafenRequestPacket {
  bool deafen;
};

struct DeafenResponsePacket {
  bool status;
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
  unsigned long data_length;
};

void set_nonblocking(int sock_fd);