#include <algorithm>
#include <cassert>
#include <cctype>
#include <functional>
#include <iomanip>
#include <iostream>
#include <ostream>
#include <stdint.h>

#include "streambuffer.h"

tee_buffer::tee_buffer(std::streambuf *sink1, std::streambuf *sink2)
    : sink1_(sink1), sink2_(sink2) {}

tee_buffer::~tee_buffer() {}

int tee_buffer::overflow(int ch) {
  if (ch != traits_type::eof()) {
    int const ret1 = sink1_->sputc(ch);
    int const ret2 = sink2_->sputc(ch);
    return ret1 == traits_type::eof() || ret2 == traits_type::eof()
               ? traits_type::eof()
               : ch;
  } else {
    return traits_type::eof();
  }
}

int tee_buffer::sync() {
  int const ret1 = sink1_->pubsync();
  int const ret2 = sink2_->pubsync();
  return ret1 == 0 && ret2 == 0 ? 0 : -1;
}


teestream::teestream(std::ostream &o1, std::ostream &o2)
    : std::ostream(&tbuf), tbuf(o1.rdbuf(), o2.rdbuf()) {}

shm_buffer::shm_buffer(char *const begin, const std::ptrdiff_t size) {
  setp(begin, begin + size);
}

shm_buffer::~shm_buffer() {}

