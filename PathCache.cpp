// Copyright (c) 2014-2016 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#include "PathCache.h"

#include <sstream>

#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/CFG.h>
#include <llvm/Support/raw_ostream.h>

using namespace llvm;

///////////////////////////////////////////////////////////////////////////
// Graph algorithms


// Code to find all simple paths between two basic blocks.
// Could generalize more to graphs if we wanted, but I don't right
// now.

PathID PathCache::addToPath(BasicBlock *b, PathID id) {
  PathCacheEntry key = std::make_pair(b, id);

  auto entry = cache_.find(key);
  if (entry != cache_.end()) {
    //errs() << "Found (" << b->getName() << ", " << id << ") as " << entry->second << "\n";
    return entry->second;
  }

  PathID newID = entries_.size();
  entries_.push_back(key);

  cache_.insert(std::make_pair(key, newID));
  //errs() << "Added (" << b->getName() << ", " << id << ") as " << newID << "\n";
  return newID;
}

Path PathCache::extractPath(PathID k) const {
  Path path;
  while (!isEmpty(k)) {
    path.push_back(getHead(k));
    k = getTail(k);
  }
  return path;
}

PathList PathCache::findAllSimplePaths(SkipSet *grey,
                                       BasicBlock *src, BasicBlock *dst,
                                       bool includeReturnLoop,
                                       bool allowSelfCycle) {
  PathList paths;
  if (src == dst && !allowSelfCycle) {
    PathID path = addToPath(dst, kEmptyPath);
    paths.push_back(path);
    return paths;
  }
  if (grey->count(src)) return paths;

  grey->insert(src);

  // We consider all exits from a function to loop back to the start
  // edge, so we need to handle that unfortunate case.
  if (includeReturnLoop && isa<ReturnInst>(src->getTerminator())) {
    BasicBlock *entry = &src->getParent()->getEntryBlock();
    paths = findAllSimplePaths(grey, entry, dst, includeReturnLoop, false);
  }
  // Go search all the normal successors
  for (auto i = succ_begin(src), e = succ_end(src); i != e; i++) {
    PathList subpaths = findAllSimplePaths(grey, *i, dst,
                                           includeReturnLoop, false);
    std::move(subpaths.begin(), subpaths.end(), std::back_inserter(paths));
  }

  // Add our step to all of the vectors
  for (auto & path : paths) {
    path = addToPath(src, path);
  }

  // Remove it from the set of things we've seen. We might come
  // through here again.
  // We can't really do any sort of memoization, since in a cyclic
  // graph the possible simple paths depend not just on what node we
  // are on, but our previous path (to avoid looping).
  grey->erase(src);

  return paths;
}

void findAllReachableDFS(PathCache::SkipSet *grey,
                         BasicBlock *src,
                         bool includeReturnLoop) {

  if (grey->count(src)) return;

  grey->insert(src);

  // We consider all exits from a function to loop back to the start
  // edge, so we need to handle that unfortunate case.
  if (includeReturnLoop && isa<ReturnInst>(src->getTerminator())) {
    BasicBlock *entry = &src->getParent()->getEntryBlock();
    findAllReachableDFS(grey, entry, includeReturnLoop);
  }
  // Go search all the normal successors
  for (auto i = succ_begin(src), e = succ_end(src); i != e; i++) {
    findAllReachableDFS(grey, *i, includeReturnLoop);
  }
}

PathCache::SkipSet PathCache::findAllReachable(
    SkipSet *skip, BasicBlock *src,
    bool includeReturnLoop) {
  SkipSet grey = *skip;
  findAllReachableDFS(&grey, src, includeReturnLoop);
  for (auto bb : *skip) {
    grey.erase(bb);
  }

  return grey;
}


PathList PathCache::findAllSimplePaths(BasicBlock *src, BasicBlock *dst,
                                       bool includeReturnLoop,
                                       bool allowSelfCycle) {
  SkipSet grey;
  return findAllSimplePaths(&grey, src, dst, includeReturnLoop, allowSelfCycle);
}

////
std::string PathCache::formatPath(PathID pathid) const {
  std::ostringstream buffer;
  Path path = extractPath(pathid);
  bool first = true;
  for (auto block : path) {
    if (!first) buffer << "->";
    first = false;
    buffer << block->getName().str();
  }
  return buffer.str();
}
void PathCache::dumpPaths(const PathList &paths) const {
  for (auto & pathid : paths) {
    errs() << formatPath(pathid) << "\n";
  }
}
