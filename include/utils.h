#include <string>

int create_server_socket(std::string port, int sock_type, int backlog);
int create_client_socket(std::string hostname, std::string port, int socktype);