//===--------------- llvm-compat.hpp - Garbage compat layer  --------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This is an absolutely rubbish compatability layer that fakes and/or
// fills in for other parts of llvm that the CommandLine library
// depends on.
//
// It is possible that performance will be substantially worse with
// the new version, since I bet there is a lot more string copying
// (all of llvm's many string related abstractions have been flattened
// into std::string) and all of llvm's small internal data structures
// have been replaced with std structures.
//
// The main things missing right now are:
//  * edit distance support
//  * file MemoryBuffer support for "response files"
//  * utf16 support
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_COMPAT_H
#define LLVM_COMPAT_H

#include <cstddef>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <cstring>
#include <sstream>
#include <cstdlib>
#include <memory>

namespace llvm {

namespace sys {
static inline std::string getHostCPUName() { return "<wtvr>"; }
static inline std::string getDefaultTargetTriple() { return "<triple?>"; }
namespace path {
// TODO: just get the filename?
static inline std::string filename(const char *s) { return s; }
}
}

// We need a few more string operations, so we subclass std::string
// and then use lolstring in place of every single string related
// abstraction that llvm has.
struct lolstring : std::string {
  lolstring() : std::string() {}
  lolstring(const std::string &s) : std::string(s) {}
  lolstring(std::string &&s) : std::string(s) {}
  lolstring(const char *s) : std::string(s) {}
  lolstring(const char *s, size_t n) : std::string(s, s+n) {}
  template< class InputIt >
  lolstring( InputIt first, InputIt last) : std::string(first, last) {}

  std::pair<lolstring, lolstring> split(char delim) {
    auto pos = find(delim);
    if (pos == std::string::npos) return std::make_pair(*this, lolstring());

    return std::make_pair(substr(0, pos),
                          substr(pos+1, size()-pos-1));
  }

  template<typename T>
  lolstring operator+(const T& rhs) {
    return lolstring(static_cast<const std::string&>(*this) + rhs);
  }
  std::string str() {
    return std::string(*this);
  }
  const char *data() const { return size() ? std::string::data() : nullptr; }

  bool getAsInteger(int base, int &value) {
    errno = 0;
    value = strtol(c_str(), nullptr, base);
    return errno != 0;
  }
  bool getAsInteger(int base, unsigned int &value) {
    errno = 0;
    value = strtoul(c_str(), nullptr, base);
    return errno != 0;
  }
  bool getAsInteger(int base, unsigned long long &value) {
    errno = 0;
    value = strtoull(c_str(), nullptr, base);
    return errno != 0;
  }

  unsigned edit_distance(const lolstring &rhs,
                         bool AllowReplacements,
                         int MaxEditDistance) {
    // TODO: compute edit distance
    return MaxEditDistance ? MaxEditDistance+1 : 10000000;
  }
};

struct outwrap {
  std::ostream &out_;
  outwrap(std::ostream &out) : out_(out) {}
  outwrap(const outwrap &rhs) : out_(rhs.out_) {}

  template<typename T>
  std::ostream &operator <<(T t) { return out_ << t; }

  std::ostream &indent(int n) {
    // TODO: actually indent?
    return out_;
  }
};
using raw_ostream = outwrap;

typedef lolstring StringRef;
typedef lolstring Twine;
template<typename T>
using SmallVectorImpl = std::vector<T>;

template<typename T>
using ArrayRef = std::vector<T>;

template<typename T>
using StringMap = std::map<lolstring, T>;

template<typename T, int n>
using SmallVector = std::vector<T>;

template<typename T, int n>
using SmallPtrSet = std::set<T>;

template<int n>
using SmallString = lolstring;

// ErrorOr is for pointer like things, so we just use the underlying
// thing. Anything that wants to access the actual error will have a
// bad day.
template<typename T>
using ErrorOr = T;

// This is not, in general, thread safe. But neither is the underlying
// object in the only case we use it, so whatever.
template<typename T>
class ManagedStatic {
private:
  T *val_{nullptr};
  void setup() { if (!val_) val_ = new T(); }
public:
  T *operator->() { setup();  return val_; }
};

using raw_string_ostream = std::stringstream;

// Thing for saving pointers to c-strings that get freed all together.
struct BumpPtrAllocator { std::vector<std::unique_ptr<char[]> > things;};
class StringSaver {
private:
  BumpPtrAllocator &alloc_;
public:
  StringSaver(BumpPtrAllocator &alloc) : alloc_(alloc) {}
  const char *save(StringRef S) {
    char *cptr = new char[S.size()+1];
    std::memcpy(cptr, S.c_str(), S.size()+1);
    alloc_.things.push_back(std::unique_ptr<char []>(cptr));
    return cptr;
  }
};
class BumpPtrStringSaver : public StringSaver {
public:
  BumpPtrStringSaver(BumpPtrAllocator &alloc) : StringSaver(alloc) {}
};

// Copied from "include/llvm/ADT/STLExtras.h"
template <class IteratorTy>
inline void array_pod_sort(
  IteratorTy Start, IteratorTy End,
  int (*Compare)(
    const typename std::iterator_traits<IteratorTy>::value_type *,
    const typename std::iterator_traits<IteratorTy>::value_type *)) {
  // Don't inefficiently call qsort with one element or trigger undefined
  // behavior with an empty sequence.
  auto NElts = End - Start;
  if (NElts <= 1) return;
  qsort(&*Start, NElts, sizeof(*Start),
        reinterpret_cast<int (*)(const void *, const void *)>(Compare));
}

class MemoryBuffer {
  std::vector<char> buf_;

public:
  MemoryBuffer() {}

  static ErrorOr<std::unique_ptr<MemoryBuffer>> getFile(
    const std::string &filename) {
    // This is not really the right way to do such things
    std::ifstream f(filename, std::ios::binary);
    if (!f.good()) return nullptr;

    std::unique_ptr<MemoryBuffer> buf(new MemoryBuffer());

    int c;
    const int kEOF = std::ifstream::traits_type::eof();
    while ((c = f.get()) != kEOF) {
      buf->buf_.push_back(c);
    }
    return buf;
  }

  const char *getBufferStart() const { return &buf_[0]; }
  const char *getBufferEnd() const { return &buf_[buf_.size()]; }
  size_t getBufferSize() const { return buf_.size(); }
};

// TODO: actually implement?
static inline bool hasUTF16ByteOrderMark(ArrayRef<char> S) { return false; }
static inline bool convertUTF16ToUTF8String(
  ArrayRef<char> SrcBytes, std::string &Out) {
  return false;
}

//

#define llvm_unreachable(x) abort()
#define LLVM_END_WITH_NULL

// There are a bunch of macros used by CommandLine.cpp that we don't
// want to pollute client programs with, so we only turn them on when
// included by the lib part.

#ifdef CL_LIB_COMPAT

#define getKey() first // StringMap's iterator has a getKey function

#define PACKAGE_NAME "llvm-cl-sully"
#define PACKAGE_VERSION "0.1"

#define report_fatal_error(x) abort()

#define DEBUG(x) do { } while(0)

static outwrap __errs(std::cerr);
static outwrap __outs(std::cout);

#define errs() __errs
#define outs() __outs
#define dbgs() __errs

#endif

}

#endif
