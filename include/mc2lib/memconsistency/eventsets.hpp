/*
 * Copyright (c) 2014-2015, Marco Elver
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the
 *    distribution.
 *
 *  * Neither the name of the software nor the names of its contributors
 *    may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef MC2LIB_MEMCONSISTENCY_EVENTSETS_HPP_
#define MC2LIB_MEMCONSISTENCY_EVENTSETS_HPP_

#include <exception>
#include <iomanip>
#include <sstream>
#include <string>

#include "../sets.hpp"
#include "../types.hpp"

namespace mc2lib {

/**
 * @namespace mc2lib::memconsistency
 * @brief Various formal models for expressing memory consistency semantics.
 */
namespace memconsistency {

class Iiid {
 public:
  struct Hash {
    typedef std::hash<types::Poi>::result_type result_type;
    result_type operator()(const Iiid& k) const {
      return std::hash<types::Poi>()(k.poi);
    }
  };

  Iiid() : pid(0), poi(0) {}

  Iiid(types::Pid pid_, types::Poi poi_) : pid(pid_), poi(poi_) {}

  operator std::string() const {
    std::ostringstream oss;
    oss << "P" << std::setfill('0') << std::setw(2) << pid << ": "
        << std::setfill('0') << std::setw(sizeof(types::Poi) * 2) << std::hex
        << poi;
    return oss.str();
  }

  bool operator==(const Iiid& rhs) const {
    return pid == rhs.pid && poi == rhs.poi;
  }

  bool operator!=(const Iiid& rhs) const {
    return pid != rhs.pid || poi != rhs.poi;
  }

  bool operator<(const Iiid& rhs) const {
    return pid < rhs.pid || (pid == rhs.pid && poi < rhs.poi);
  }

  Iiid& operator++() {
    ++poi;
    return *this;
  }

  Iiid Next() const { return Iiid(pid, poi + 1); }

  Iiid Prev() const {
    assert(poi > 0);
    return Iiid(pid, poi - 1);
  }

 public:
  types::Pid pid;
  types::Poi poi;
};

class Event {
 public:
  struct Hash {
    Iiid::Hash::result_type operator()(const Event& k) const {
      return Iiid::Hash()(k.iiid);
    }
  };

  typedef std::uint32_t TypeMask;

  enum Type {
    kNone = 0x00000000,

    // Memory operations:

    kRead = 0x00000001,

    kWrite = 0x00000002,

    kAcquire = 0x00000004,

    kRelease = 0x00000008,

    kMemoryOperation = kRead | kWrite | kAcquire | kRelease,

    // Auxiliary attributes:

    kRegInAddr = 0x00000010,

    kRegInData = 0x00000020,

    kRegOut = 0x00000040,

    kBranch = 0x00000080,

    // For user declared attributes
    kNext = 0x00000100
  };

  Event() : addr(0), type(kNone) {}

  Event(TypeMask type_, types::Addr addr_, Iiid iiid_)
      : addr(addr_), type(type_), iiid(iiid_) {}

  operator std::string() const {
    std::ostringstream oss;
    oss << "[" << static_cast<std::string>(iiid) << "] ";

    std::ostringstream memtype;

    if (type == kNone) {
      memtype << "None";
    } else {
      bool found_type = false;

      if (AllType(kRead)) {
        memtype << "Read";
        found_type = true;
      }

      if (AllType(kWrite)) {
        memtype << (found_type ? "|" : "") << "Write";
        found_type = true;
      }

      if (AllType(kAcquire)) {
        memtype << (found_type ? "|" : "") << "Acquire";
        found_type = true;
      }

      if (AllType(kRelease)) {
        memtype << (found_type ? "|" : "") << "Release";
        found_type = true;
      }

      if (AllType(kRegInAddr)) {
        memtype << (found_type ? "|" : "") << "RegInAddr";
        found_type = true;
      }

      if (AllType(kRegInData)) {
        memtype << (found_type ? "|" : "") << "RegInData";
        found_type = true;
      }

      if (AllType(kRegOut)) {
        memtype << (found_type ? "|" : "") << "RegOut";
        found_type = true;
      }

      if (AllType(kBranch)) {
        memtype << (found_type ? "|" : "") << "Branch";
        // found_type = true;
      }
    }

    oss << std::setfill(' ') << std::setw(8) << memtype.str() << " @ "
        << std::hex << addr;
    return oss.str();
  }

  bool operator==(const Event& rhs) const {
    return type == rhs.type && addr == rhs.addr && iiid == rhs.iiid;
  }

  bool operator!=(const Event& rhs) const {
    return type != rhs.type || addr != rhs.addr || iiid != rhs.iiid;
  }

  // This function in no way says anything about event ordering. Used for
  // ordered map.
  bool operator<(const Event& rhs) const { return iiid < rhs.iiid; }

  bool AllType(TypeMask type_mask) const {
    assert(type_mask != kNone);
    return (type & type_mask) == type_mask;
  }

  bool AnyType(TypeMask type_mask) const {
    assert(type_mask != kNone);
    return (type & type_mask) != 0;
  }

 public:
  types::Addr addr;
  TypeMask type;
  Iiid iiid;
};

typedef sets::Set<sets::Types<Event>> EventSet;
typedef sets::Relation<sets::Types<Event>> EventRel;
typedef sets::RelationSeq<sets::Types<Event>> EventRelSeq;

class Error : public std::exception {
 public:
  explicit Error(const char* w) : what_(w) {}

  const char* what() const noexcept override { return what_; }

 private:
  const char* what_;
};

}  // namespace memconsistency
}  // namespace mc2lib

#endif /* MEMCONSISTENCY_EVENTSETS_HPP_ */

/* vim: set ts=2 sts=2 sw=2 et : */
