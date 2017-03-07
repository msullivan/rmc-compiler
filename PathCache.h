// Copyright (c) 2014-2017 Michael J. Sullivan
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
                              bool allowSelfCycle = true);
  typedef SmallPtrSet<BasicBlock *, 8> SkipSet;
  PathList findAllSimplePaths(SkipSet *grey, BasicBlock *src, BasicBlock *dst,
                              bool allowSelfCycle = true);

  // this isn't really path related but...  We calculate SCCs as a map
  // from blocks to a "canonical" element of the SCC. This can be
  // turned into a map from blocks to SCC sets in linear time but
  // since we only need to check if blocks are in the same SCC we
  // don't need to.
  typedef DenseMap<BasicBlock *, BasicBlock *> SCCMap;
  SCCMap findSCCs(SkipSet *skip, Function *func);
  SCCMap findSCCs(BasicBlock *bindSite, Function *func);
  SCCMap *findSCCsCached(BasicBlock *bindSite, Function *func);
  SkipSet pathSCCs(BasicBlock *bindSite, PathID pathid);

  Path extractPath(PathID k) const;

  static const PathID kEmptyPath = -1;
  typedef std::pair<BasicBlock *, PathID> PathCacheKey;
  typedef std::pair<PathCacheKey, BasicBlock *> PathCacheEntry;

  bool isEmpty(PathID k) const { return k == kEmptyPath; }

  PathCacheEntry const &getEntry(PathID k) const { return entries_[k]; }
  PathCacheKey const &getKey(PathID k) const { return getEntry(k).first; }
  BasicBlock *getHead(PathID k) const { return getKey(k).first; }
  PathID getTail(PathID k) const { return getKey(k).second; }
  BasicBlock *getLast(PathID k) { return getEntry(k).second; }

  // For debugging:
  std::string formatPath(PathID pathid) const;
  void dumpPaths(const PathList &paths) const;

private:
  std::vector<PathCacheEntry> entries_;
  DenseMap<PathCacheKey, PathID> cache_;
  DenseMap<BasicBlock *, SCCMap> sccCache_;

  PathID addToPath(BasicBlock *b, PathID id);
};

}

#endif
