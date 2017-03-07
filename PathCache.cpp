// Copyright (c) 2014-2017 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#include "PathCache.h"

#include <sstream>
#include <memory>

#include <llvm/ADT/iterator_range.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/CFG.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>

using namespace llvm;

///////////////////////////////////////////////////////////////////////////
// Graph algorithms

template <class F>
void forwardIterate(BasicBlock *src, F f) {
  // We consider all exits from a function to loop back to the start
  // edge, so we need to handle that unfortunate case.
  if (isa<ReturnInst>(src->getTerminator())) {
    BasicBlock *entry = &src->getParent()->getEntryBlock();
    f(entry);
  }
  // Go search all the normal successors
  for (auto i = succ_begin(src), e = succ_end(src); i != e; i++) {
    f(*i);
  }
}
template <class F>
void backwardIterate(BasicBlock *src, F f) {
  // We consider all exits from a function to loop back to the start
  // edge. This is the worst. Is there a better way than scanning
  // them all?
  if (src == &src->getParent()->getEntryBlock()) {
    for (auto & block : *src->getParent()) {
      if (isa<ReturnInst>(block.getTerminator())) {
        f(&block);
      }
    }
  }
  // Go search all the normal predecessors
  for (auto i = pred_begin(src), e = pred_end(src); i != e; i++) {
    f(*i);
  }
}
template <class F>
void graphIterate(BasicBlock *src, bool back, F f) {
  if (back) {
    backwardIterate(src, f);
  } else {
    forwardIterate(src, f);
  }
}

// Code to find all simple paths between two basic blocks.
// Could generalize more to graphs if we wanted, but I don't right
// now.

PathID PathCache::addToPath(BasicBlock *b, PathID id) {
  PathCacheKey key = std::make_pair(b, id);

  auto entry = cache_.find(key);
  if (entry != cache_.end()) {
    //errs() << "Found (" << b->getName() << ", " << id << ") as " << entry->second << "\n";
    return entry->second;
  }

  PathID newID = entries_.size();
  BasicBlock *last = isEmpty(id) ? b : getLast(id);
  entries_.push_back(std::make_pair(key, last));

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
                                       bool allowSelfCycle) {
  PathList paths;
  if (src == dst && !allowSelfCycle) {
    PathID path = addToPath(dst, kEmptyPath);
    paths.push_back(path);
    return paths;
  }
  if (grey->count(src)) return paths;

  grey->insert(src);

  forwardIterate(src, [&] (BasicBlock *succ) {
    PathList subpaths = findAllSimplePaths(grey, succ, dst, false);
    std::move(subpaths.begin(), subpaths.end(), std::back_inserter(paths));
  });

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

template <class Post>
void findAllReachableDFS(PathCache::SkipSet *grey,
                         BasicBlock *src,
                         bool backwards,
                         Post post) {

  if (grey->count(src)) return;

  grey->insert(src);
  graphIterate(src, backwards, [&] (BasicBlock *succ) {
    findAllReachableDFS(grey, succ, backwards, post);
  });
  post(src);
}

// Find SCCs using Kosaraju's Algorithm
PathCache::SCCMap PathCache::findSCCs(SkipSet *skip, Function *func) {
  PathCache::SkipSet grey = *skip;

  // Generating an ordering to traverse.
  std::vector<BasicBlock *> order;
  auto post = [&](BasicBlock *node) { order.push_back(node); };
  for (auto & block : *func) {
    findAllReachableDFS(&grey, &block, false, post);
  }

  // Use that ordering a DFS over the reverse graph to compute SCCs.
  grey = *skip;
  SCCMap sccs;
  for (auto * block : make_range(order.rbegin(), order.rend())) {
    if (grey.count(block)) continue;

    findAllReachableDFS(&grey, block, true,
                        [&] (BasicBlock *node) {
      sccs[node] = block;
    });

    /*
    for (BasicBlock * block : *set) {
      errs() << block->getName() << ", ";
    }
    errs() << "\n";
    */
  }

  return sccs;
}

PathCache::SCCMap PathCache::findSCCs(BasicBlock *bindSite, Function *func) {
  SkipSet skip;
  if (bindSite) skip.insert(bindSite);
  return findSCCs(&skip, func);
}

PathCache::SCCMap *PathCache::findSCCsCached(
    BasicBlock *bindSite, Function *func) {
  if (sccCache_.count(bindSite)) {
    return &sccCache_[bindSite];
  }
  sccCache_[bindSite] = findSCCs(bindSite, func);
  return &sccCache_[bindSite];
}

PathList PathCache::findAllSimplePaths(BasicBlock *src, BasicBlock *dst,
                                       bool allowSelfCycle) {
  SkipSet grey;
  return findAllSimplePaths(&grey, src, dst, allowSelfCycle);
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
