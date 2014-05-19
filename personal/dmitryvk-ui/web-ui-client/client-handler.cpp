#include <iostream>
#include <stdexcept>
#include <cstring>
#include <sstream>
#include <memory>
#include <functional>
#include <condition_variable>
#include <chrono>
#include <string>

#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include "webserver.hpp"

using std::string;
using std::runtime_error;
using std::strerror;
using std::cerr;
using std::endl;
using std::shared_ptr;
using std::vector;
using std::ostringstream;
using std::to_string;
using std::logic_error;
using std::stoul;

ClientHandler::ClientHandler(WebServer& server, int fd, SocketAddress client_addr):
  server(server),
  socket(fd),
  client_addr(client_addr) {
}

void ClientHandler::serve() {
  ErrorValue err = read_http_request(client_request);
  if (!err.success) {
    cerr << "Error serving client: " << err.message << endl;
    return;
  }
  // cerr << "got request (" << request.size() << ") bytes" << endl;
  // {
  //   string str(reinterpret_cast<char*>(client_request.data()), client_request.size());
  //   cerr << "body: " << str << endl;
  // }
  const HttpRequest& parsed_request = HttpRequest::parse(client_request);
  auto it = server.url_handlers.find(parsed_request.url.absolute_path);
  std::function<const HttpResponse(const HttpRequest&)> handler;
  if (it == server.url_handlers.end()) {
    handler = WebServer::default_http_handler;
  } else {
    handler = it->second;
  }
  const HttpResponse& response = handler(parsed_request);
  vector<unsigned char> serialized_response = response.serialize();
  
  err = send_http_response(serialized_response);
  if (!err.success) {
    cerr << "Error sending response: " << err.message << endl;
    return;
  }
  
  // if (!err.success) {
  //   cerr << "Error sending request: " << err.message << endl;
  //   try_to_report_error_to_client(500, err);
  //   return;
  // }
  return;
}

enum DfaStateForHttpRequest {
  initial = 0,
  seen_cr = 1,
  seen_crlf = 2,
  seen_crlfcr = 3,
  seen_crlfcrlf = 4
};

DfaStateForHttpRequest
DfaStateForHttpRequest_next_state(DfaStateForHttpRequest state,
                                  unsigned char next_char) {
  switch (state) {
  case DfaStateForHttpRequest::initial: {
    if (next_char == '\r') {
      return DfaStateForHttpRequest::seen_cr;
    } else {
      return DfaStateForHttpRequest::initial;
    }
  }
  case DfaStateForHttpRequest::seen_cr: {
    if (next_char == '\r') {
      return DfaStateForHttpRequest::seen_cr;
    } else if (next_char == '\n') {
      return DfaStateForHttpRequest::seen_crlf;
    } else {
      return DfaStateForHttpRequest::initial;
    }
  }
  case DfaStateForHttpRequest::seen_crlf: {
    if (next_char == '\r') {
      return DfaStateForHttpRequest::seen_crlfcr;
    } else if (next_char == '\n') {
      return DfaStateForHttpRequest::initial;
    } else {
      return DfaStateForHttpRequest::initial;
    }
  }
  case DfaStateForHttpRequest::seen_crlfcr: {
    if (next_char == '\r') {
      return DfaStateForHttpRequest::seen_cr;
    } else if (next_char == '\n') {
      return DfaStateForHttpRequest::seen_crlfcrlf;
    } else {
      return DfaStateForHttpRequest::initial;
    }
  }
  case DfaStateForHttpRequest::seen_crlfcrlf: {
    return DfaStateForHttpRequest::seen_crlfcrlf;
  }
  default: throw logic_error("DfaStateForHttpRequest_next_state: invalid state");
  }
}

ErrorValue ClientHandler::read_http_request(vector<unsigned char>& result) {
  // cerr << "reading http request" << endl;
  DfaStateForHttpRequest dfa = DfaStateForHttpRequest::initial;
  result.clear();
  vector<unsigned char> buf;
  buf.resize(16384);
  while (true) {
    ssize_t rc = read(socket.GetFd(), buf.data(), buf.size());
    if (rc == 0) {
      return ErrorValue::error("Client closed connection before sending request");
    }
    if (rc < 0 && errno != EINTR) {
      return ErrorValue::error_from_errno("Error reading request: ");
    }
    size_t buf_length = static_cast<size_t>(rc);
    for (unsigned int idx = 0; idx < buf_length; ++idx) {
      dfa = DfaStateForHttpRequest_next_state(dfa, buf.at(idx));
      if (dfa == DfaStateForHttpRequest::seen_crlfcrlf) {
        size_t used_buf_length = idx + 1;
        result.insert(result.end(), buf.begin(), buf.begin() + used_buf_length);
        return ErrorValue::ok();
      }
    }
    result.insert(result.end(), buf.begin(), buf.begin() + buf_length);
  }
}
 
static ErrorValue connect_socket(Socket& socket, const string& hostname, uint16_t port) {
  addrinfo gni_hints;
  memset(&gni_hints, 0, sizeof(gni_hints));
  gni_hints.ai_family = AF_INET;
  gni_hints.ai_socktype = SOCK_STREAM;
  gni_hints.ai_flags = AI_V4MAPPED | AI_ADDRCONFIG;
  struct addrinfo* addr;
  int rc = getaddrinfo(hostname.c_str(), NULL, &gni_hints, &addr);
  if (rc != 0) {
    ostringstream msg_stream;
    msg_stream << "Failed to do hostname lookup for " << hostname << ": ";
    msg_stream << gai_strerror(rc);
    return ErrorValue::error(msg_stream.str());
  }

  if (addr == nullptr) {
    return ErrorValue::error("Failed to do hostname lookup: no addressed returned");
  }

  struct sockaddr_in socket_addr;
  memcpy(&socket_addr, addr->ai_addr, sizeof(socket_addr));
  freeaddrinfo(addr);
  socket_addr.sin_port = htons(port);

  // cerr << "got hostname addr: " << ip4_to_string(ntohl(socket_addr.sin_addr.s_addr)) << endl;

  // cerr << "connecting" << endl;

  if (connect(socket.GetFd(), reinterpret_cast<sockaddr*>(&socket_addr), sizeof(socket_addr)) != 0) {
    return ErrorValue::error_from_errno("Failed to connect to host: ");
  }

  // cerr << "got connection" << endl;

  return ErrorValue::ok();
}

void ClientHandler::try_to_report_error_to_client(unsigned status_code, const ErrorValue& value) {
  ostringstream message_stream;
  message_stream << "HTTP/1.0 " << status_code << " Error" << "\r\n"
                 << "Content-Type: text/plain; charset:utf-8\r\n"
                 << "Content-Length: " << (1 + value.message.length()) << "\r\n"
                 << "\r\n"
                 << value.message.c_str() << "\n";
  string response_body = message_stream.str();
  size_t written = 0;
  while (written < response_body.length()) {
    ssize_t rc = send(socket.GetFd(),
                      response_body.c_str() + written,
                      response_body.length() - written,
                      MSG_NOSIGNAL);
    if (rc == 0 || (rc == -1 && errno != EINTR)) {
      return;
    }
    written += rc;
  }
}

ErrorValue ClientHandler::send_http_response(const std::vector<unsigned char>& response) {
  size_t written = 0;
  while (written < response.size()) {
    ssize_t rc = send(socket.GetFd(),
                      &response.at(0) + written,
                      response.size() - written,
                      MSG_NOSIGNAL);
    if (rc == 0) {
      return ErrorValue::error("Peer closed its connection");
    }
    if (rc == -1 && errno != EINTR) {
      return ErrorValue::error_from_errno("Error writing to client: ");
    }
    written += rc;
  }
  return ErrorValue::ok();
}
