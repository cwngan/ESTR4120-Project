#define MAX_PACKET_SIZE 1500
#define MAX_DATA_SIZE 1396
#define EPOLL_MAX_EVENTS 1

struct RequestPacketHeader {
  enum Type : short {
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
  enum Type : short {
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
  enum Type : short { Connect, Data };
  Type type;
  int client_id;
};

struct AudioConnectResponsePacket {
  bool success;
};

struct AudioDataPacketHeader {
  int seq_number;
  unsigned long data_length;
  bool ping = false;
};