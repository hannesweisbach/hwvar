#pragma once

#include <cstdlib>
#include <iosfwd>
#include <streambuf>
#include <vector>

class tee_buffer : public std::streambuf {
public:
  explicit tee_buffer(std::streambuf *sink1, std::streambuf *sink2);
  ~tee_buffer();

protected:
  virtual int overflow(int c);
  virtual int sync();

private:
  std::streambuf *sink1_;
  std::streambuf *sink2_;
};

class teestream : public std::ostream {
public:
  teestream(std::ostream &o1, std::ostream &o2);

private:
  tee_buffer tbuf;
};

class shm_buffer : public std::streambuf {
public:
  explicit shm_buffer(char *const begin, std::ptrdiff_t size);
  ~shm_buffer();
};
