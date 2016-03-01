// Copyright (c) 2014-2016 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#ifndef PATH_CACHE_H
#define PATH_CACHE_H

#include <llvm/IR/BasicBlock.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/SmallPtrSet.h>


namespace llvm {

typedef int PathID;
typedef SmallVector<PathID, 2> PathList;
typedef std::vector<BasicBlock *> Path;

// Structure to manage path information, which we do in order to
// provide small unique path identifiers.
class PathCache {
public:
  void clear() { entries_.clear(); cache_.clear(); }
  PathList findAllSimplePaths(BasicBlock *src, BasicBlock *dst,
                              bool includeReturnLoop = true,
                              bool allowSelfCycle = true);
  typedef SmallPtrSet<BasicBlock *, 8> SkipSet;
  PathList findAllSimplePaths(SkipSet *grey, BasicBlock *src, BasicBlock *dst,
                              bool includeReturnLoop = true,
                              bool allowSelfCycle = true);

  Path extractPath(PathID k) const;

  static const PathID kEmptyPath = -1;
  typedef std::pair<BasicBlock *, PathID> PathCacheEntry;

  bool isEmpty(PathID k) const { return k == kEmptyPath; }

  PathCacheEntry const &getEntry(PathID k) const { return entries_[k]; }
  BasicBlock *getHead(PathID k) const { return entries_[k].first; }
  PathID getTail(PathID k) const { return entries_[k].second; }

  // For debugging:
  std::string formatPath(PathID pathid) const;
  void dumpPaths(const PathList &paths) const;

private:

  std::vector<PathCacheEntry> entries_;
  DenseMap<PathCacheEntry, PathID> cache_;

  PathID addToPath(BasicBlock *b, PathID id);
};

}

#endif
