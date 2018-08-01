#pragma once

#include <stdexcept>
#include <string>

#include <linux/perf_event.h>

#ifdef __cplusplus
extern "C" {
#endif
#include <jevents.h>
#ifdef __cplusplus
}
#endif

namespace arch {

class pmc_data::data {
  // Initialize the racy libjevents from main thread
  struct jevent_initializer {
    jevent_initializer() {
      read_events(NULL);
    }
  };
  static struct jevent_initializer init_;
  struct perf_event_attr attr_;

public:
  data(const std::string &name) {
    int err = resolve_event(name.c_str(), &attr_);
    if (err) {
      using namespace std::string_literals;
      throw std::runtime_error("Error resolving event \""s + name + "\"\n");
    }
  }

  const struct perf_event_attr attr() const { return attr_; }
};

} // namespace arch
