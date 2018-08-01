#pragma once

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

class pmc;

#include "arch/lookup.h"

class pmc {
  class description {
    std::string name_;
    unsigned size_;
    unsigned offset_;
    arch::pmc_data data_;

  public:
    description(std::string name, const unsigned size, const unsigned offset)
        : name_(std::move(name)), size_(size), offset_(offset), data_(name_) {}
    description(const char *name, const unsigned size, const unsigned offset)
        : name_(name), size_(size), offset_(offset), data_(name_) {}

    const std::string &name() const { return name_; }
    unsigned size() const { return size_; }
    unsigned offset() const { return offset_; }
    const arch::pmc_data::data &data() const { return data_.get(); }
  };

  std::vector<description> pmcs_;

public:
  using value_type = description;
  using iterator = std::vector<description>::iterator;
  using const_iterator = std::vector<description>::const_iterator;

  void add(const char *name, const unsigned size = 8) {
    pmcs_.emplace_back(name, size, pmcs_.size());
  }

  void add(std::string name, const unsigned size = 8) {
    pmcs_.emplace_back(std::move(name), size, pmcs_.size());
  }

  size_t size() const { return pmcs_.size(); }

  unsigned bytesize() const {
    unsigned sum = 0;
    for (const auto &pmc : pmcs_) {
      sum += pmc.size();
    }
    return sum;
  }

  const_iterator begin() const { return pmcs_.cbegin(); }
  const_iterator end() const { return pmcs_.cend(); }
  const_iterator cbegin() const { return pmcs_.cbegin(); }
  const_iterator cend() const { return pmcs_.cend(); }
};
