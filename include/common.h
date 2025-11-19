#define MAX_DATA_SIZE 1400

struct AudioPacket {
  unsigned int src_client;
  unsigned int dest_client;
  unsigned int length;
  char data[MAX_DATA_SIZE];
};