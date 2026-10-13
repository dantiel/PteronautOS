#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>

constexpr int ASYNC_WRITE_FLAG_COPY = 1;
constexpr size_t RESPONSE_TRY_AGAIN = static_cast<size_t>(-1);
const std::string emptyString;
enum { RESPONSE_HEADERS, RESPONSE_CONTENT, RESPONSE_WAIT_ACK, RESPONSE_END, RESPONSE_FAILED };
static int allocationFailures = 0;
static size_t largestAllocation = 0;
static void *testMalloc(size_t n) {
  largestAllocation = std::max(largestAllocation, n);
  if (allocationFailures) { --allocationFailures; return nullptr; }
  return std::malloc(n);
}

struct Client {
  std::string wire;
  size_t room = 8192, limit = 8192, unacked = 0, maxUnacked = 0;
  int failures = 0, outputFailures = 0;
  bool closed = false;
  size_t space() { return room; }
  size_t add(const char *data, size_t n, int flags) {
    assert(flags == ASYNC_WRITE_FLAG_COPY);
    if (failures) { --failures; return 0; }
    n = std::min(n, std::min(room, limit));
    wire.append(data, n);
    unacked += n;
    maxUnacked = std::max(maxUnacked, unacked);
    return n;
  }
  bool send() {
    if (outputFailures) { --outputFailures; return false; }
    return true;
  }
  void close(bool = false) { closed = true; }
  size_t ack() { size_t n = unacked; unacked = 0; return n; }
};
struct AsyncWebServerRequest {
  Client tcp;
  Client *client() { return &tcp; }
};
struct AsyncAbstractResponse {
  uint8_t *_pending = nullptr;
  size_t _pendingLength = 0, _pendingOffset = 0, _pendingContentLength = 0;
  size_t _headOffset = 0;
  bool _pendingFinal = false;
  std::string _head = "HTTP/1.1 200 OK\r\n\r\n";
  size_t _ackedLength = 0, _writtenLength = 0, _contentLength = 0, _sentLength = 0;
  int _state = RESPONSE_HEADERS;
  bool _chunked = false, _sendContentLength = true, valid = true;
  bool stallSource = false, jsonStyle = false, emptySource = false;
  std::string body;
  size_t position = 0, fills = 0;
  ~AsyncAbstractResponse() { std::free(_pending); }
  bool _sourceValid() { return valid; }
  size_t _fillBufferAndProcessTemplates(uint8_t *out, size_t capacity) {
    ++fills;
    if (stallSource) { stallSource = false; return RESPONSE_TRY_AGAIN; }
    if (emptySource) return 0;
    // AsyncJsonResponse uses _sentLength, files/PROGMEM own a read cursor.
    size_t from = jsonStyle ? _sentLength : position;
    size_t n = std::min(capacity, body.size() - from);
    memcpy(out, body.data() + from, n);
    position = from + n;
    return n;
  }
  size_t _ackSmall(AsyncWebServerRequest *, size_t);
};
#define malloc testMalloc
// RESPONSE_IMPLEMENTATION
#undef malloc

struct AsyncBasicResponse {
  std::string _smallHead = "HTTP/1.1 503 Busy\r\n\r\n", _content;
  size_t _smallHeadOffset = 0, _ackedLength = 0, _writtenLength = 0;
  size_t _contentLength = 0, _sentLength = 0;
  int _state = RESPONSE_HEADERS;
  size_t _ack(AsyncWebServerRequest *, size_t, uint32_t);
};
// BASIC_IMPLEMENTATION

static std::string unchunk(std::string input) {
  std::string result;
  while (true) {
    size_t line = input.find("\r\n"); assert(line != std::string::npos);
    size_t n = std::stoul(input.substr(0, line), nullptr, 16);
    input.erase(0, line + 2);
    assert(input.size() >= n + 2 && input.substr(n, 2) == "\r\n");
    result += input.substr(0, n); input.erase(0, n + 2);
    if (!n) { assert(input.empty()); return result; }
  }
}

static void transfer(int mode, size_t size, size_t limit, bool failures) {
  AsyncAbstractResponse r;
  AsyncWebServerRequest request;
  r._chunked = mode == 1;
  r._sendContentLength = mode != 2;
  r.jsonStyle = mode == 3;
  for (size_t i = 0; i < size; ++i) r.body += static_cast<char>(i % 251);
  r._contentLength = size;
  if (failures) r._head += std::string(1500, 'H');
  std::string head = r._head;
  request.tcp.limit = limit;
  r.stallSource = failures;
  allocationFailures = failures ? 3 : 0;
  for (size_t tick = 0; tick < 200000 && r._state != RESPONSE_END; ++tick) {
    assert(!request.tcp.closed || mode == 2);
    if (failures && tick % 13 == 0) request.tcp.failures = 1;
    request.tcp.outputFailures = failures ? 1 : 0;
    request.tcp.room = failures && tick % 7 == 0 ? 0 : 8192;
    size_t ack = tick % 3 == 0 ? request.tcp.ack() : 0;
    r._ackSmall(&request, ack);
  }
  assert(r._state == RESPONSE_END);
  assert(r._sentLength == size);
  assert(request.tcp.wire.substr(0, head.size()) == head);
  std::string payload = request.tcp.wire.substr(head.size());
  assert((r._chunked ? unchunk(payload) : payload) == r.body);
  assert(request.tcp.maxUnacked <= 512);
  assert(r._pending == nullptr);
}

int main() {
  unsigned tests = 0;
  for (int mode = 0; mode < 4; ++mode)
    for (size_t size : {0, 1, 504, 512, 513, 56698, 91141})
      for (size_t limit : {size_t(31), size_t(8192)})
        for (bool failures : {false, true}) {
          transfer(mode, size, limit, failures); ++tests;
        }
  // OOM before reading: source and send state must not move.
  AsyncAbstractResponse r; AsyncWebServerRequest request;
  r._state = RESPONSE_CONTENT; r._contentLength = 10; r.body = "0123456789";
  allocationFailures = 1;
  r._ackSmall(&request, 0);
  assert(r.fills == 0 && r._sentLength == 0 && !request.tcp.closed);
  // Truncated fixed-length source must fail instead of fabricating success.
  r.emptySource = true;
  r._ackSmall(&request, 0);
  assert(r._state == RESPONSE_FAILED && request.tcp.closed);
  assert(largestAllocation <= 512);
  for (size_t size : {0, 1, 512, 6000}) {
    AsyncBasicResponse basic; AsyncWebServerRequest req;
    basic._content = std::string(size, 'B'); basic._contentLength = size;
    const std::string expected = basic._smallHead + basic._content;
    req.tcp.limit = 17;
    for (int tick = 0; tick < 10000 && basic._state != RESPONSE_END; ++tick) {
      req.tcp.failures = tick % 7 == 0;
      req.tcp.outputFailures = 1;
      basic._ack(&req, tick % 3 == 0 ? req.tcp.ack() : 0, 0);
    }
    assert(basic._state == RESPONSE_END);
    assert(req.tcp.wire == expected && req.tcp.maxUnacked <= 512);
  }
  printf("PASS: %u streaming + 4 basic HTTP transfers + OOM/truncated-source checks (ASan/UBSan)\n", tests);
}
