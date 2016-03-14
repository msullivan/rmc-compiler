// Copyright (c) 2014-2016 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#include "sassert.h"
#include "RMCInternal.h"

#include <exception>

#if USE_Z3

#include "PathCache.h"

#include <llvm/IR/Function.h>
#include <llvm/IR/CFG.h>

#include <llvm/IR/Dominators.h>

#include "smt.h"

#undef NDEBUG
#include <assert.h>

using namespace llvm;

#define LONG_PATH_NAMES 1

// Some tuning parameters

// Should we pick the initial upper bound by seeing what the solver
// produces without constraints instead of by binary searching up?
const bool kGuessUpperBound = true;
const int kFirstUpperBound = 16;
// If we guess an upper bound, should we hope that it is optimal and
// check the bound - 1 before we binary search?
const bool kCheckFirstGuess = false;
// Should we invert all bool variables; sort of useful for testing
const bool kInvertBools = false;

// Costs for different sorts of things that we insert.
// XXX: These numbers are just made up.
// And will vary based on platform.
// (sync == lwsync on ARM but not on POWER and *definitely* not on x86)
const int kSyncCost = 80;
const int kLwsyncCost = 50;
const int kIsyncCost = 20;
const int kUseCtrlCost = 1;
const int kAddCtrlCost = 7;
const int kUseDataCost = 1;
const int kMakeReleaseCost = 49;
const int kMakeAcquireCost = 49;

// OK, isync actually sucks on ARM mostly, I think, Don't bother.
const bool kUseIsync = false;
const bool kAcqRelAbuse = false;

bool debugSpew = false;


// Z3 utility functions
SmtExpr boolToInt(SmtExpr const &flag, int cost = 1) {
  SmtContext &c = flag.ctx();
  return ite(flag, c.int_val(cost), c.int_val(0));
}


// Code for optimizing
// FIXME: should we try to use some sort of bigint?
typedef smt_uint Cost;

bool isCostUnder(SmtSolver &s, SmtExpr &costVar, Cost cost) {
  s.push();
  s.add(costVar <= s.ctx().int_val(cost));
  bool isSat = doCheck(s);
  s.pop();

  errs() << "Trying cost " << cost << ": " << isSat << "\n";
  return isSat;
}

// Generic binary search over a monotonic predicate
typedef std::function<bool (Cost)> CostPred;

// Given a monotonic predicate, find the first value in [lo, hi]
// for which it holds. It needs to hold for hi.
Cost findFirstTrue(CostPred pred, Cost lo, Cost hi) {
  if (lo >= hi) return hi;
  Cost mid = lo + (hi - lo) / 2; // funny to avoid overflow

  if (pred(mid)) {
    return findFirstTrue(pred, lo, mid);
  } else {
    return findFirstTrue(pred, mid + 1, hi);
  }
}

// Given a monotonic predicate pred that is not always false, find the
// lowest value c for which pred(c) is true.
Cost findFirstTrue(CostPred pred) {
  Cost lo = 0, hi = kFirstUpperBound;

  // Search upwards to find some value for which pred(c) holds.
  while (!pred(hi)) {
    lo = hi + 1;
    hi *= 2;
    assert(hi != 0); // fail if we overflow
  }

  return findFirstTrue(pred, lo, hi);
}

// Given a solver and an expression, find a solution that minimizes
// the expression through repeated calls to the solver.
void handMinimize(SmtSolver &s, SmtExpr &costVar) {
  auto costPred = [&] (Cost cost) { return isCostUnder(s, costVar, cost); };
  Cost minCost;
  if (kGuessUpperBound) {
    // This is in theory arbitrarily worse but might be better in
    // practice. Although the cost is bounded by the number of things
    // we could do, so...
    doCheck(s);
    Cost upperBound = extractInt(s.get_model().eval(costVar));
    errs() << "Upper bound: " << upperBound << "\n";
    // The solver seems to often "just happen" to find the optimal
    // solution, so maybe do a quick check on upperBound-1
    if (kCheckFirstGuess && upperBound > 0 && !costPred(--upperBound)) {
      minCost = upperBound + 1;
    } else if (upperBound == 0) {
      minCost = upperBound;
    } else {
      minCost = findFirstTrue(costPred, 0, upperBound);
    }
  } else {
    minCost = findFirstTrue(costPred);
  }
  s.add(costVar == s.ctx().int_val(minCost));
}

// Given a solver and an expression, find a solution that minimizes
// the expression.
void minimize(SmtSolver &s, SmtExpr &costVar) {
#if USE_Z3_OPTIMIZER
  s.minimize(costVar);
#else
  handMinimize(s, costVar);
#endif
}


// DenseMap can use pairs of keys as keys, so we represent edges as a
// pair of BasicBlock*s. We represent paths as PathIDs.

// Is it worth having our own mapping? Is z3 going to be doing a bunch
// of string lookups anyways? Dunno.
typedef BasicBlock* BlockKey;
typedef std::pair<BasicBlock *, BasicBlock *> EdgeKey;
typedef PathID PathKey;
typedef std::pair<BasicBlock *, PathID> BlockPathKey;
typedef std::pair<EdgeKey, PathID> EdgePathKey;
typedef std::pair<BasicBlock *, EdgeKey> BlockEdgeKey;
BlockKey makeBlockKey(BasicBlock *block) {
  return block;
}
EdgeKey makeEdgeKey(BasicBlock *src, BasicBlock *dst) {
  return std::make_pair(src, dst);
}
PathKey makePathKey(PathID path) {
  return path;
}
BlockPathKey makeBlockPathKey(BasicBlock *block, PathID path) {
  return std::make_pair(block, path);
}
EdgePathKey makeEdgePathKey(BasicBlock *src, BasicBlock *dst, PathID path) {
  return std::make_pair(makeEdgeKey(src, dst), path);
}
BlockEdgeKey makeBlockEdgeKey(BasicBlock *block,
                              BasicBlock *src, BasicBlock *dst) {
  return std::make_pair(block, makeEdgeKey(src, dst));
}

std::string makeVarString(BasicBlock *key) {
  return key ? key->getName().str() : "<outside>";
}

#if LONG_PATH_NAMES
// This is an awful hack; we stick the PathCache into TLS so we can use
// the whole expanded path as the key.
__thread PathCache *debugPathCache = nullptr;
std::string makeVarString(PathKey &key) {
  return debugPathCache->formatPath(key);
}
#else
std::string makeVarString(PathKey &key) {
  std::ostringstream buffer;
  buffer << "path #" << key;
  return buffer.str();
}
#endif

template <typename T, typename U>
std::string makeVarString(std::pair<T, U> &key) {
  return makeVarString(key.first) + ", " + makeVarString(key.second);
}

template<typename Key> struct DeclMap {
  DeclMap(SmtSort isort, const char *iname) : sort(isort), name(iname) {}
  DenseMap<Key, SmtExpr> map;
  SmtSort sort;
  std::string name;
};

template<typename Key>
SmtExpr getFunc(DeclMap<Key> &map, Key key, bool *alreadyThere = nullptr) {
  auto entry = map.map.find(key);
  if (entry != map.map.end()) {
    if (alreadyThere) *alreadyThere = true;
    return entry->second;
  } else {
    if (alreadyThere) *alreadyThere = false;
  }

  SmtContext &c = map.sort.ctx();
  std::string name = map.name + "(" + makeVarString(key) + ")";
  SmtExpr e = c.constant(name.c_str(), map.sort);
  // Can use inverted boolean variables to help test optimization.
  if (kInvertBools && map.sort.is_bool()) e = !e;

  map.map.insert(std::make_pair(key, e));

  return e;
}
SmtExpr getBlockFunc(DeclMap<BlockKey> &map,
                     BasicBlock *block,
                     bool *alreadyThere = nullptr) {
  return getFunc(map, makeBlockKey(block), alreadyThere);
}
SmtExpr getEdgeFunc(DeclMap<EdgeKey> &map,
                     BasicBlock *src, BasicBlock *dst,
                     bool *alreadyThere = nullptr) {
  return getFunc(map, makeEdgeKey(src, dst), alreadyThere);
}
SmtExpr getPathFunc(DeclMap<PathKey> &map,
                     PathID path,
                     bool *alreadyThere = nullptr) {
  return getFunc(map, makePathKey(path), alreadyThere);
}

///////////////

// We precompute this so that the solver doesn't need to consider
// these values while trying to optimize the problems.
// We should maybe compute these with normal linear algebra instead of
// giving it to the SMT solver, though.
// N.B. that capacity gets invented out of nowhere in loops
DenseMap<EdgeKey, int> computeCapacities(const LoopInfo &loops, Function &F) {
  SmtContext c;
  SmtSolver s(c);

  DeclMap<BasicBlock *> nodeCapM(c.int_sort(), "node_cap");
  DeclMap<EdgeKey> edgeCapM(c.int_sort(), "edge_cap");

  //// Build the equations.
  SmtExpr incomingEntryCap = c.int_val(0);
  for (auto & block : F) {
    SmtExpr nodeCap = getFunc(nodeCapM, &block);

    // Compute the node's incoming capacity
    SmtExpr incomingCap = c.int_val(0);
    for (auto i = pred_begin(&block), e = pred_end(&block); i != e; ++i) {
      incomingCap = incomingCap + getEdgeFunc(edgeCapM, *i, &block);
    }
    if (&block != &F.getEntryBlock()) {
      s.add(nodeCap == incomingCap.simplify());
    }

    // Setup equations for outgoing edges
    auto numerator =
      [&] (BasicBlock *target) {
      // If the block is a loop exit block, we make the probability
      // higher for exits that stay in the loop.
      // TODO: handle loop nesting in a smarter way.
      auto *loop = loops[&block];
      if (loop && !loop->isLoopExiting(&block)) return 1;
      return loops[target] == loop ? 1 : 4;
    };

    auto i = succ_begin(&block), e = succ_end(&block);
    int childCount = e - i;
    int denominator = 0;
    for (; i != e; ++i) {
      denominator += numerator(*i);
    }
    i = succ_begin(&block), e = succ_end(&block);
    for (; i != e; ++i) {
      // For now, we assume even probabilities.
      // Would be an improvement to do better
      SmtExpr edgeCap = getEdgeFunc(edgeCapM, &block, *i);
      // We want: c(v, u) == Pr(v, u) * c(v). Since Pr(v, u) ~= n(v,u)/d(v,u),
      // we do
      // d(v,u) * c(v, u) == c(v) * n(v,u)
      s.add(edgeCap * c.int_val(denominator) ==
            nodeCap * c.int_val(numerator(*i)));
    }
    // Populate the capacities for the fictional back edges to the
    // function entry point.
    // Blocks that don't have any succesors end in a return or the like.
    if (childCount == 0) {
      SmtExpr returnCap = getEdgeFunc(edgeCapM, &block, &F.getEntryBlock());
      s.add(returnCap == nodeCap);
      incomingEntryCap = incomingEntryCap + returnCap;
    }
  }

  SmtExpr entryNodeCap = getFunc(nodeCapM, &F.getEntryBlock());
  // Make the entry node equations add up.
  s.add(entryNodeCap == incomingEntryCap);
  // Keep all zeros from working:
  s.add(entryNodeCap > c.int_val(0));

  //// Extract a solution.
  DenseMap<EdgeKey, int> caps;

  bool success = doCheck(s);
  assert_(success);
  SmtModel model = s.get_model();
  for (auto & entry : edgeCapM.map) {
    EdgeKey edge = entry.first;
    int cap = extractInt(model.eval(entry.second));
    caps.insert(std::make_pair(edge, cap));
    //errs() << "Edge cap: " << makeVarString(edge) << ": " << cap << "\n";
  }
  // Cram the node weights in with <block, nullptr> keys
  for (auto & entry : nodeCapM.map) {
    EdgeKey edge = std::make_pair(entry.first, nullptr);
    int cap = extractInt(model.eval(entry.second));
    caps.insert(std::make_pair(edge, cap));
  }


  //errs() << "Entry cap: " << extractInt(model.eval(entryNodeCap)) << "\n";

  return caps;
}


struct VarMaps {
  // Sigh.
  PathCache &pc;
  DenseMap<BasicBlock *, Action *> &bb2action;
  DominatorTree &domTree;

  DeclMap<EdgeKey> sync;
  DeclMap<EdgeKey> lwsync;
  DeclMap<BlockKey> release;
  DeclMap<BlockKey> acquire;
  // XXX: Make arrays keyed by edge type
  DeclMap<EdgeKey> pcut;
  DeclMap<EdgeKey> vcut;
  DeclMap<BlockEdgeKey> xcut;
  DeclMap<PathKey> pathPcut;
  DeclMap<PathKey> pathVcut;
  DeclMap<BlockPathKey> pathXcut;

  DeclMap<EdgeKey> isync;
  DeclMap<PathKey> pathIsync;
  DeclMap<BlockPathKey> pathCtrlIsync;

  DeclMap<BlockEdgeKey> usesCtrl;
  DeclMap<BlockPathKey> pathCtrl;
  DeclMap<EdgeKey> allPathsCtrl;

  // The edge here isn't actually a proper edge in that it isn't
  // necessarily two blocks connected in the CFG
  DeclMap<EdgePathKey> usesData;
  DeclMap<std::pair<PathID, BlockPathKey>> pathData;
};

// Generalized it.
typedef std::function<SmtExpr (PathID path)> PathFunc;

typedef std::function<SmtExpr (PathID path, bool *already)> GetPathVarFunc;
typedef std::function<SmtExpr (BasicBlock *src, BasicBlock *dst, PathID path)>
  EdgeFunc;

SmtExpr forAllPaths(SmtSolver &s, VarMaps &m,
                    BasicBlock *src, BasicBlock *dst, PathFunc func,
                    BasicBlock *skipBlock = nullptr) {
  // Now try all the paths
  SmtExpr allPaths = s.ctx().bool_val(true);

  PathCache::SkipSet skip;
  if (skipBlock) skip.insert(skipBlock);
  PathList paths = m.pc.findAllSimplePaths(&skip, src, dst);
  for (auto & path : paths) {
    allPaths = allPaths && func(path);
  }
  return allPaths.simplify();
}

SmtExpr forAllPathEdges(SmtSolver &s, VarMaps &m,
                        PathID path,
                        GetPathVarFunc getVar,
                        EdgeFunc func) {
  SmtContext &c = s.ctx();

  PathID rest;
  if (m.pc.isEmpty(path) || m.pc.isEmpty(rest = m.pc.getTail(path))) {
    return c.bool_val(false);
  }

  bool alreadyMade;
  SmtExpr isCut = getVar(path, &alreadyMade);
  if (alreadyMade) return isCut;

  BasicBlock *src = m.pc.getHead(path), *dst = m.pc.getHead(rest);
  SmtExpr somethingCut = func(src, dst, path) ||
    forAllPathEdges(s, m, rest, getVar, func);
//  s.add(isCut == somethingCut.simplify());
  s.add(isCut == somethingCut);

  return isCut;
}


//// Real stuff now: the definitions of all the functions
// XXX: there is a lot of duplication...
SmtExpr makeCtrl(SmtSolver &s, VarMaps &m,
                 BasicBlock *dep, BasicBlock *src, BasicBlock *dst) {
  // We can only add a ctrl dep in locations that are dominated by the
  // load.
  //
  // Because we rely on not having critical edges, we only worry about
  // whether the *src* is dominated. If the src is dominated but the
  // dst is not, then we will always insert a ctrl in the src (since
  // the dst must have multiple incoming edges while the src only has
  // one outgoing).
  //
  // If the outgoing dep isn't an instruction, then it's a parameter
  // and so we treat it like it dominates.
  Instruction *load = dyn_cast<Instruction>(m.bb2action[dep]->outgoingDep);
  if (src == dep || !load || m.domTree.dominates(load, src)) {
    return getFunc(m.usesCtrl, makeBlockEdgeKey(dep, src, dst));
  } else {
    return s.ctx().bool_val(false);
  }
}

SmtExpr makePathIsync(SmtSolver &s, VarMaps &m,
                      PathID path) {
  return forAllPathEdges(
    s, m, path,
    [&] (PathID path, bool *b) { return getPathFunc(m.pathIsync, path, b); },
    [&] (BasicBlock *src, BasicBlock *dst, PathID path) {
      return getEdgeFunc(m.isync, src, dst);
    });
}

SmtExpr makePathCtrlIsync(SmtSolver &s, VarMaps &m,
                          PathID path) {
  SmtContext &c = s.ctx();
  if (m.pc.isEmpty(path)) return c.bool_val(false);
  BasicBlock *dep = m.pc.getHead(path);
  // Can't have a control dependency when it's not a load.
  // XXX: we can maybe be more granular about things.
  if (!m.bb2action[dep]||!m.bb2action[dep]->outgoingDep) return c.bool_val(false);

  return forAllPathEdges(
    s, m, path,
    [&] (PathID path, bool *b) {
      return getFunc(m.pathCtrlIsync, makeBlockPathKey(dep, path), b); },
    [&] (BasicBlock *src, BasicBlock *dst, PathID path) {
      return makeCtrl(s, m, dep, src, dst) && makePathIsync(s, m, path);
    });
}


SmtExpr makePathCtrl(SmtSolver &s, VarMaps &m, PathID path) {
  SmtContext &c = s.ctx();
  if (m.pc.isEmpty(path)) return c.bool_val(false);
  BasicBlock *dep = m.pc.getHead(path);
  // Can't have a control dependency when it's not a load.
  // XXX: we can maybe be more granular about things.
  if (!m.bb2action[dep]||!m.bb2action[dep]->outgoingDep) return c.bool_val(false);

  return forAllPathEdges(
    s, m, path,
    [&] (PathID path, bool *b) {
      return getFunc(m.pathCtrl, makeBlockPathKey(dep, path), b); },
    [&] (BasicBlock *src, BasicBlock *dst, PathID path) {
      return makeCtrl(s, m, dep, src, dst);
    });
}

SmtExpr makeAllPathsCtrl(SmtSolver &s, VarMaps &m,
                         BasicBlock *src, BasicBlock *dst) {
  SmtExpr isCtrl = getEdgeFunc(m.allPathsCtrl, src, dst);
  SmtExpr allPaths = forAllPaths(
    s, m, src, dst,
    [&] (PathID path) { return makePathCtrl(s, m, path); });
  s.add(isCtrl == allPaths);
  return isCtrl;
}

SmtExpr makePathCtrlCut(SmtSolver &s, VarMaps &m,
                        PathID path, Action &tail) {
  // XXX: do we care about actually using the variable pathCtrlCut?
  if (tail.type == ActionSimpleWrites) {
    return makePathCtrl(s, m, path);
  } else {
    // I think isb actually sucks on ARM, so make whether we try it
    // configurable.
    if (kUseIsync) {
      return makePathCtrlIsync(s, m, path);
    } else {
      return s.ctx().bool_val(false);
    }
  }
}

SmtExpr makeData(SmtSolver &s, VarMaps &m,
                 BasicBlock *dep, BasicBlock *dst,
                 PathID path) {
  Action *src = m.bb2action[dep];
  Action *tail = m.bb2action[dst];
  if (src && tail && src->outgoingDep && tail->incomingDep &&
      addrDepsOn(tail->incomingDep, src->outgoingDep, &m.pc, path))
    return getFunc(m.usesData, makeEdgePathKey(src->bb, tail->bb, path));

  return s.ctx().bool_val(false);
}

// Does it make sense for this to be a path variable at all???
SmtExpr makePathDataCut(SmtSolver &s, VarMaps &m,
                        PathID fullPath, Action &tail) {
  SmtContext &c = s.ctx();
  if (m.pc.isEmpty(fullPath)) return c.bool_val(false);
  BasicBlock *dep = m.pc.getHead(fullPath);
  // Can't have a addr dependency when it's not a load.
  // XXX: we can maybe be more granular about things.
  if (!m.bb2action[dep]||!m.bb2action[dep]->outgoingDep) return c.bool_val(false);

   return forAllPathEdges(
    s, m, fullPath,
    // We need to include both the fullPath and the postfix because
    // we don't want to share postfixes incorrectly.
    [&] (PathID path, bool *b) {
      return getFunc(m.pathData,
                     std::make_pair(fullPath, makeBlockPathKey(dep, path)), b);
    },
    [&] (BasicBlock *src, BasicBlock *dst, PathID path) {
      SmtExpr cut = makeData(s, m, dep, dst, fullPath);
      path = m.pc.getTail(path);
      if (dst != tail.bb) {
        cut = cut &&
          (makePathCtrlCut(s, m, path, tail) ||
           makePathDataCut(s, m, path, tail));
      }
      return cut;
    });
}

SmtExpr makeEdgeVcut(SmtSolver &s, VarMaps &m,
                     BasicBlock *src, BasicBlock *dst,
                     bool isPush) {
  // Instead of generating a notion of push edges and making sure that
  // they are cut by syncs, we just insert syncs exactly where PUSHes
  // are written. We want to actually take advantage of these syncs,
  // so check for them here. (If there is already a sync we generate
  // a bunch of equations for the path which all evaluate to "true",
  // which is kind of silly. We could avoid some of this if we cared.)
  // We only need to check one of src and dst since arcs with PUSH as
  // an endpoint are dropped.
  // EXCEPT: Now we *do* have push edges, but only when they are
  // written out explicitly. If you write a push explicitly, we do
  // what was said above, but if you write a push edge, we represent
  // it as such. Originally we had intended to generate push edges
  // based on the constraints on pushes, but this doesn't work since
  // we can't find them all.
  if (m.bb2action[src] && m.bb2action[src]->isPush) {
    return s.ctx().bool_val(true);
  } else if (isPush) {
    return getEdgeFunc(m.sync, src, dst);
  } else {
    return getEdgeFunc(m.lwsync, src, dst) ||
      getEdgeFunc(m.sync, src, dst);
  }
}


SmtExpr makePathVcut(SmtSolver &s, VarMaps &m,
                     PathID path, bool isPush) {
  return forAllPathEdges(
    s, m, path,
    [&] (PathID path, bool *b) {
      return getPathFunc(isPush ? m.pathPcut : m.pathVcut, path, b); },
    [&] (BasicBlock *src, BasicBlock *dst, PathID path) {
      return makeEdgeVcut(s, m, src, dst, isPush);
    });
}


SmtExpr makeXcut(SmtSolver &s, VarMaps &m, Action &src, Action &dst,
                 BasicBlock *bindSite);
SmtExpr makeVcut(SmtSolver &s, VarMaps &m, Action &src, Action &dst,
                 RMCEdgeType edgeType);

SmtExpr makePathXcut(SmtSolver &s, VarMaps &m,
                     PathID path, Action &tail,
                     BasicBlock *bindSite) {
  bool alreadyMade;
  SmtExpr isCut = getFunc(m.pathXcut, makeBlockPathKey(bindSite, path),
                          &alreadyMade);
  if (alreadyMade) return isCut;

  BasicBlock *head = m.pc.getHead(path);
  bool isSelfPath = head == tail.bb;

  SmtExpr pathCtrlCut = makePathCtrlCut(s, m, path, tail);
  Action *headAction = m.bb2action[head];
  // XXX: We don't bother passing along the bind site to
  // allPathsCtrl. I think this is mostly fine. Think about it.
  if (!isSelfPath) {
    pathCtrlCut = pathCtrlCut &&
      (makeAllPathsCtrl(s, m, head, head) ||
       makeXcut(s, m, *headAction, *headAction, bindSite));
  }
  SmtExpr pathDataCut = makePathDataCut(s, m, path, tail);
  if (!isSelfPath) {
    pathDataCut = pathDataCut &&
      makeXcut(s, m, *headAction, *headAction, bindSite);
  }

  s.add(isCut ==
        (makePathVcut(s, m, path, false) || pathCtrlCut || pathDataCut));

  return isCut;
}

SmtExpr makeRelAcqCut(SmtSolver &s, VarMaps &m, Action &src, Action &dst,
                      RMCEdgeType type) {
  SmtExpr relAcq = s.ctx().bool_val(false);

  // W1 -v-> W/RW2  -- W/RW2 = rel
  if (type == VisibilityEdge &&
      src.type == ActionSimpleWrites &&
      (dst.type == ActionSimpleWrites || dst.type == ActionSimpleRMW)) {
    relAcq = relAcq || getBlockFunc(m.release, dst.bb);
  }
  // R/RW1 -x-> *   -- R/RW1 = acq
  if (type == ExecutionEdge &&
      (src.type == ActionSimpleRead || src.type == ActionSimpleRMW)) {
    relAcq = relAcq || getBlockFunc(m.acquire, src.bb);
  }
  // R/RW1 -v-> W/RW2 -- complicated
  if ((type == VisibilityEdge || type == ExecutionEdge) &&
      (src.type == ActionSimpleRead || src.type == ActionSimpleRMW) &&
      (dst.type == ActionSimpleWrites || dst.type == ActionSimpleRMW)) {

    // On x86 and (I think) on ARMv8, it suffices to make W/RW2 = rel
    // On x86 we could probably also use an acquire but why bother.
    // XXX: double check the ARMv8 thing against the model paper, not
    // just the ARM documentation
    if (kAcqRelAbuse) {
      relAcq = relAcq || getBlockFunc(m.release, dst.bb);
    } else {
      relAcq = relAcq || (getBlockFunc(m.release, dst.bb) &&
                          getBlockFunc(m.acquire, src.bb));
    }
  }

  return relAcq.simplify();
}

SmtExpr makeXcut(SmtSolver &s, VarMaps &m, Action &src, Action &dst,
                 BasicBlock *bindSite) {
  // For complex sources we fall back to doing a pure visibility cut.
  // We couldn't generate anything nice starting from a complex action
  // source anyways and the xcut code doesn't handle multi-block
  // actions right.
  if (src.type == ActionComplex) {
    return makeVcut(s, m, src, dst, ExecutionEdge);
  }

  bool alreadyMade;
  SmtExpr isCut = getFunc(m.xcut, makeBlockEdgeKey(bindSite, src.bb, dst.bb),
                          &alreadyMade);
  if (alreadyMade) return isCut;

  SmtExpr allPathsCut = forAllPaths(
    s, m, src.bb, dst.bb,
    [&] (PathID path) { return makePathXcut(s, m, path, dst, bindSite); },
    bindSite);
  SmtExpr relAcqCut = makeRelAcqCut(s, m, src, dst, ExecutionEdge);
  s.add(isCut == (allPathsCut || relAcqCut));

  return isCut;
}


SmtExpr makeVcut(SmtSolver &s, VarMaps &m, Action &src, Action &dst,
                 RMCEdgeType edgeType) {
  bool isPush = edgeType == PushEdge;
  SmtExpr isCut = getEdgeFunc(isPush ? m.pcut : m.vcut,
                              src.outBlock, dst.bb);

  SmtExpr allPathsCut = forAllPaths(
    s, m, src.outBlock, dst.bb,
    [&] (PathID path) { return makePathVcut(s, m, path, isPush); });
  SmtExpr relAcqCut = makeRelAcqCut(s, m, src, dst, edgeType);
  s.add(isCut == (allPathsCut || relAcqCut));

  return isCut;
}

template<typename T>
void processMap(DeclMap<T> &map, SmtModel &model,
                const std::function<void (T&)> &func) {
  for (auto & entry : map.map) {
    if (extractBool(model.eval(entry.second))) {
      func(entry.first);
    }
  }
}

std::vector<EdgeCut> RealizeRMC::smtAnalyzeInner() {
  // XXX: Workaround a Z3 bug. When 'enable_sat' is set, we sometimes
  // hit an exception (which should probably be an assertion) in
  // inc_sat_solver. Setting opt.enable_set=false disables
  // inc_sat_solver, which makes the problem go away.
  // I should try to minimize this and file a bug.
  z3::set_param("opt.enable_sat", false);

  SmtContext c;
  SmtSolver s(c);

#if LONG_PATH_NAMES
  debugPathCache = &pc_; /* :( */
#endif

  VarMaps m = {
    pc_,
    bb2action_,
    domTree_,
    DeclMap<EdgeKey>(c.bool_sort(), "sync"),
    DeclMap<EdgeKey>(c.bool_sort(), "lwsync"),
    DeclMap<BlockKey>(c.bool_sort(), "release"),
    DeclMap<BlockKey>(c.bool_sort(), "acquire"),
    DeclMap<EdgeKey>(c.bool_sort(), "pcut"),
    DeclMap<EdgeKey>(c.bool_sort(), "vcut"),
    DeclMap<BlockEdgeKey>(c.bool_sort(), "xcut"),
    DeclMap<PathKey>(c.bool_sort(), "path_pcut"),
    DeclMap<PathKey>(c.bool_sort(), "path_vcut"),
    DeclMap<BlockPathKey>(c.bool_sort(), "path_xcut"),

    DeclMap<EdgeKey>(c.bool_sort(), "isync"),
    DeclMap<PathKey>(c.bool_sort(), "path_isync"),
    DeclMap<BlockPathKey>(c.bool_sort(), "path_ctrl_isync"),


    DeclMap<BlockEdgeKey>(c.bool_sort(), "uses_ctrl"),
    DeclMap<BlockPathKey>(c.bool_sort(), "path_ctrl"),
    DeclMap<EdgeKey>(c.bool_sort(), "all_paths_ctrl"),

    DeclMap<EdgePathKey>(c.bool_sort(), "uses_data"),
    DeclMap<std::pair<PathID, BlockPathKey>>(c.bool_sort(), "path_data"),
  };

  // Compute the capacity function
  DenseMap<EdgeKey, int> edgeCap = computeCapacities(loopInfo_, func_);
  auto weight =
    [&] (BasicBlock *src, BasicBlock *dst) {
    // The weight of an edge is based on its graph capacity and its loop depth.
    // We use 4^depth as a scaling factor for loop depth so that a loop body
    // will have twice the cost of its parent (since the parent probably has
    // twice the capacity).
    // XXX: Turning that all off in favor of trying to be smarter about
    // probabilities for loop exits.
    //return edgeCap[makeEdgeKey(src, dst)] * pow(4, loopInfo_.getLoopDepth(src));
    return edgeCap[makeEdgeKey(src, dst)];
  };
  auto block_weight =
    [&] (BasicBlock *block) {
    return edgeCap[makeEdgeKey(block, nullptr)];
  };

  //////////
  // HOK. Make sure everything is cut.
  for (auto & src : actions_) {
    for (auto edgeType : kEdgeTypes) {
      for (auto & entry : src.transEdges[edgeType]) {
        Action &dst = *entry.first;

        if (edgeType == ExecutionEdge) {
          // For execution edges, we check all the binding sites.
          for (BasicBlock *bindSite : entry.second) {
            s.add(makeXcut(s, m, src, dst, bindSite));
          }
        } else {
          // I'm pretty sure binding isn't useful for vis edges, so we
          // don't bother checking binding sites here.
          s.add(makeVcut(s, m, src, dst, edgeType));
        }
      }
    }
  }

  //////////
  // OK, now build a cost function. This will probably take a lot of
  // tuning.
  SmtExpr costVar = c.int_const("cost");
  SmtExpr cost = c.int_val(0);

  BasicBlock *src, *dst;
  SmtExpr v = c.bool_val(false);

  // Sync cost
  for (auto & entry : m.sync.map) {
    unpack(unpack(src, dst), v) = fix_pair(entry);
    cost = cost +
      boolToInt(v, kSyncCost*weight(src, dst)+1);
  }
  // Lwsync cost
  for (auto & entry : m.lwsync.map) {
    unpack(unpack(src, dst), v) = fix_pair(entry);
    cost = cost +
      boolToInt(v, kLwsyncCost*weight(src, dst)+1);
  }
  // Release cost
  for (auto & entry : m.release.map) {
    unpack(src, v) = fix_pair(entry);
    cost = cost +
      boolToInt(v, kMakeReleaseCost*block_weight(src)+1);
  }
  // Acquire cost
  for (auto & entry : m.acquire.map) {
    unpack(src, v) = fix_pair(entry);
    cost = cost +
      boolToInt(v, kMakeAcquireCost*block_weight(src)+1);
  }
  // Isync cost
  for (auto & entry : m.isync.map) {
    unpack(unpack(src, dst), v) = fix_pair(entry);
    cost = cost +
      boolToInt(v, kIsyncCost*weight(src, dst)+1);
  }
  // Ctrl cost
  for (auto & entry : m.usesCtrl.map) {
    BasicBlock *dep;
    unpack(unpack(dep, unpack(src, dst)), v) = fix_pair(entry);
    auto ctrlWeight =
      branchesOn(src, bb2action_[dep]->outgoingDep) ? kUseCtrlCost : kAddCtrlCost;
    cost = cost +
      boolToInt(v, ctrlWeight*weight(src, dst));
  }
  // Data dep cost
  for (auto & entry : m.usesData.map) {
    PathID path;
    unpack(unpack(unpack(src, dst), path), v) = fix_pair(entry);
    // XXX: this is a hack that depends on us only using actions in
    // usesData things
    BasicBlock *pred = bb2action_[dst]->bb->getSinglePredecessor();
    cost = cost +
      boolToInt(v, kUseDataCost*weight(pred, dst));
  }

  s.add(costVar == cost.simplify());

  //////////
  // Print out the model for debugging
  if (debugSpew) dumpSolver(s);

  // Optimize the cost.
  minimize(s, costVar);

  // OK, go solve it.
  doCheck(s);
  SmtModel model = s.get_model();

  // Print out the results for debugging
  if (debugSpew) dumpModel(model);

  std::vector<EdgeCut> cuts;

  // Find the syncs we are inserting
  processMap<EdgeKey>(m.sync, model, [&] (EdgeKey &edge) {
    cuts.push_back(EdgeCut(CutSync, edge.first, edge.second));
  });
  // Find the lwsyncs we are inserting
  processMap<EdgeKey>(m.lwsync, model, [&] (EdgeKey &edge) {
    cuts.push_back(EdgeCut(CutLwsync, edge.first, edge.second));
  });
  // Find the releases we are inserting
  processMap<BlockKey>(m.release, model, [&] (BlockKey &block) {
    cuts.push_back(EdgeCut(CutRelease, block, nullptr));
  });
  // Find the Acquires we are inserting
  processMap<BlockKey>(m.acquire, model, [&] (BlockKey &block) {
    cuts.push_back(EdgeCut(CutAcquire, block, nullptr));
  });
  // Find the isyncs we are inserting
  processMap<EdgeKey>(m.isync, model, [&] (EdgeKey &edge) {
    cuts.push_back(EdgeCut(CutIsync, edge.first, edge.second));
  });
  // Find the controls to preserve
  processMap<BlockEdgeKey>(m.usesCtrl, model, [&] (BlockEdgeKey &entry) {
    EdgeKey edge; BasicBlock *dep;
    unpack(dep, edge) = entry;
    Value *read = bb2action_[dep]->outgoingDep;
    cuts.push_back(EdgeCut(CutCtrl, edge.first, edge.second, read));
  });
  // Find data deps to preserve
  processMap<EdgePathKey>(m.usesData, model, [&] (EdgePathKey &entry) {
    BasicBlock *src, *dst; PathID path;
    unpack(unpack(src, dst), path) = entry;
    Value *read = bb2action_[src]->outgoingDep;
    cuts.push_back(EdgeCut(CutData, src, dst, read, path));
  });


  if (debugSpew) errs() << "\n";

  return cuts;
}

std::vector<EdgeCut> RealizeRMC::smtAnalyze() {
  try {
    return smtAnalyzeInner();
  } catch (z3::exception &e) {
    errs() << "Unexpected Z3 error: " << e.msg() << "\n";
    std::terminate();
  }
}


#else /* !USE_Z3 */
#include <exception>
using namespace llvm;
std::vector<EdgeCut> RealizeRMC::smtAnalyzeInner() {
  std::terminate();
}
std::vector<EdgeCut> RealizeRMC::smtAnalyze() {
  std::terminate();
}
#endif
