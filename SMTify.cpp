// Copyright (c) 2014-2017 Michael J. Sullivan
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

#define NO_PATH_SUFFIX_SHARING 1

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

// Having -1 as a cost indicates not supporting the use of that
// feature on the architecture. Everything but sync defaults to not
// supported, since sync is unavoidable.
//
// Whether features are enabled gets fed into the DeclMaps/getFunc
// infrastructure so that operations that are always disabled are
// hardcoded as false in the SMT system. We also do some direct checks
// of these flags in various places as an optimization to avoid
// generating big SMT formulas we know will be false.
// We could do a lot more pruning.
struct TuningParams {
  int syncCost{100}; // mandatory.
  int lwsyncCost{-1};
  int dmbstCost{-1};
  int dmbldCost{-1};
  int isyncCost{-1};
  int useCtrlCost{-1};
  int addCtrlCost{-1};
  int useDataCost{-1};
  int makeReleaseCost{-1};
  int makeAcquireCost{-1};
  bool relAbuse{false};
};
bool paramEnabled(int param) { return param >= 0; }

// Screw you, C++, for not having designated initializers
TuningParams x86Params() {
  TuningParams p;
  p.syncCost = 800;
  // this "lwsync" is really just a compiler barrier on x86
  p.lwsyncCost = 500;
  // ponder: maybe using release/acquire would be better for compiler
  // reasons.
  return p;
}
TuningParams powerParams() {
  TuningParams p;
  p.syncCost = 800;
  p.lwsyncCost = 500;
  p.isyncCost = 200;
  p.useCtrlCost = 1;
  p.addCtrlCost = 70;
  p.useDataCost = 1;
  return p;
}
TuningParams armParams() {
  TuningParams p;
  p.syncCost = 500;
  p.dmbstCost = 350; // XXX???
  p.useCtrlCost = 1;
  p.addCtrlCost = 70;
  p.useDataCost = 1;
  return p;
}
TuningParams armv8Params() {
  TuningParams p;
  p.syncCost = 800;
  p.lwsyncCost = 500;
  p.dmbstCost = 350; // XXX???
  p.dmbldCost = 300; // XXX???
  p.useCtrlCost = 1;
  p.addCtrlCost = 70;
  p.useDataCost = 1;
  p.makeReleaseCost = 240;
  p.makeAcquireCost = 240;
  p.relAbuse = true;
  return p;
}

TuningParams archParams(RMCTarget target) {
  if (target == TargetX86) {
    return x86Params();
  } else if (target == TargetPOWER) {
    return powerParams();
  } else if (target == TargetARM) {
    return armParams();
  } else if (target == TargetARMv8) {
    return armv8Params();
  }
  assert(false && "invalid architecture!");
  std::terminate();
}

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
  DeclMap(SmtSort isort, const char *iname, bool ienabled = true)
    : sort(isort), name(iname), enabled(ienabled) {}
  DenseMap<Key, SmtExpr> map;
  const SmtSort sort;
  const std::string name;
  const bool enabled;
};

template<typename Key>
SmtExpr getFunc(DeclMap<Key> &map, Key key, bool *alreadyThere = nullptr) {
  SmtContext &c = map.sort.ctx();

  if (!map.enabled) {
    if (alreadyThere) *alreadyThere = true;
    // only works for booleans!!
    return c.bool_val(false);
  }

  auto entry = map.map.find(key);
  if (entry != map.map.end()) {
    if (alreadyThere) *alreadyThere = true;
    return entry->second;
  } else {
    if (alreadyThere) *alreadyThere = false;
  }

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
  TuningParams params;

  DeclMap<EdgeKey> sync;
  DeclMap<EdgeKey> lwsync;
  DeclMap<EdgeKey> dmbst;
  DeclMap<EdgeKey> dmbld;
  DeclMap<PathKey> pathDmbld;
  // release and acquire are really morally BlockKeyd things, but
  // we make them EdgeKey (with an edge to the unique successor block)
  // in order to have it match interfaces with the most of the other cuts
  DeclMap<EdgeKey> release;
  DeclMap<EdgeKey> acquire;
  // XXX: Make arrays keyed by edge type
  DeclMap<BlockEdgeKey> pcut;
  DeclMap<BlockEdgeKey> vcut;
  DeclMap<BlockEdgeKey> xcut;
  // Wait, pathPcut and pathVcut *don't* need to be keyed by blocks.
  // But if I made them arrays keyed by edge type, it would make sense...
  DeclMap<BlockPathKey> pathPcut;
  DeclMap<BlockPathKey> pathVcut;
  DeclMap<BlockPathKey> pathXcut;

  DeclMap<EdgeKey> isync;
  DeclMap<PathKey> pathIsync;
  DeclMap<BlockPathKey> pathCtrlIsync;

  DeclMap<BlockEdgeKey> usesCtrl;
  DeclMap<BlockPathKey> pathCtrl;
  DeclMap<EdgeKey> allPathsCtrl;

  // The edge here isn't actually a proper edge in that it isn't
  // necessarily two blocks connected in the CFG
  DeclMap<std::pair<BlockKey, EdgePathKey>> usesData;
  DeclMap<std::pair<BlockKey, std::pair<PathID, BlockPathKey>>> pathData;
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

// I built a *lot* of infrastructure around the idea that we would
// share the suffixes of paths to reduce the size of the problem. It
// turns out, though, that certain things are a lot simpler if we
// *don't* share path suffixes, and it isn't clear it helps much
// anyways...
#if NO_PATH_SUFFIX_SHARING
SmtExpr forAllPathEdges(SmtSolver &s, VarMaps &m,
                        PathID path,
                        GetPathVarFunc getVar,
                        EdgeFunc func) {
  SmtContext &c = s.ctx();

  bool alreadyMade;
  SmtExpr isCut = getVar(path, &alreadyMade);
  if (alreadyMade) return isCut;

  PathID rest;
  SmtExpr somethingCut = c.bool_val(false);
  while (!m.pc.isEmpty(path) && !m.pc.isEmpty(rest = m.pc.getTail(path))) {
    BasicBlock *src = m.pc.getHead(path), *dst = m.pc.getHead(rest);
    somethingCut = somethingCut || func(src, dst, path);
    path = rest;
  }

  s.add(isCut == somethingCut.simplify());

  return isCut;
}
#else
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
  s.add(isCut == somethingCut);

  return isCut;
}
#endif

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
  if (!m.bb2action[dep] || !m.bb2action[dep]->outgoingDep)
    return c.bool_val(false);

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
  if (!m.bb2action[dep] || !m.bb2action[dep]->outgoingDep)
    return c.bool_val(false);

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
  if (!m.usesCtrl.enabled) return s.ctx().bool_val(false);

  // XXX: do we care about actually using the variable pathCtrlCut?
  if (tail.type == ActionSimpleWrites) {
    return makePathCtrl(s, m, path);
  } else {
    // I think isb actually sucks on ARM, so make whether we try it
    // configurable.
    if (m.isync.enabled) {
      return makePathCtrlIsync(s, m, path);
    } else {
      return s.ctx().bool_val(false);
    }
  }
}

SmtExpr makeData(SmtSolver &s, VarMaps &m,
                 BasicBlock *dep, BasicBlock *dst,
                 PathID path, BasicBlock *bindSite) {
  Action *src = m.bb2action[dep];
  Action *tail = m.bb2action[dst];
  if (src && tail && src->outgoingDep && tail->incomingDep &&
      addrDepsOn(tail->incomingDep, src->outgoingDep, &m.pc, bindSite, path))
    return getFunc(m.usesData,
                   std::make_pair(makeBlockKey(bindSite),
                                  makeEdgePathKey(src->bb, tail->bb, path)));

  return s.ctx().bool_val(false);
}

// Does it make sense for this to be a path variable at all???
SmtExpr makePathDataCut(SmtSolver &s, VarMaps &m,
                        PathID fullPath, Action &tail, BasicBlock *bindSite) {
  SmtContext &c = s.ctx();
  if (!m.usesData.enabled) return c.bool_val(false);
  if (m.pc.isEmpty(fullPath)) return c.bool_val(false);
  BasicBlock *dep = m.pc.getHead(fullPath);
  // Can't have a addr dependency when it's not a load.
  // XXX: we can maybe be more granular about things.
  if (!m.bb2action[dep] || !m.bb2action[dep]->outgoingDep)
    return c.bool_val(false);

  // XXX: CLEANUP: Is this breaking it up into a chain thing worth the
  // effort at all? I think at this point it would only help for
  // data->ctrl, since we handle data->data as part of addrDepsOn now.
  return forAllPathEdges(
    s, m, fullPath,
    // We need to include both the fullPath and the postfix because
    // we don't want to share postfixes incorrectly.
    [&] (PathID path, bool *b) {
      return getFunc(m.pathData,
                     std::make_pair(
                       makeBlockKey(bindSite),
                       std::make_pair(fullPath, makeBlockPathKey(dep, path))),
                       b);
    },
    [&] (BasicBlock *src, BasicBlock *dst, PathID path) {
      SmtExpr cut = makeData(s, m, dep, dst, fullPath, bindSite);
      path = m.pc.getTail(path);
      if (dst != tail.bb) {
        cut = cut &&
          (makePathCtrlCut(s, m, path, tail) ||
           makePathDataCut(s, m, path, tail, bindSite));
      }
      return cut;
    });
}

SmtExpr makeEdgeVcut(SmtSolver &s, VarMaps &m,
                     BasicBlock *src, BasicBlock *dst,
                     bool isPush, bool dmbst) {
  if (isPush) {
    return getEdgeFunc(m.sync, src, dst);
  } else {
    SmtExpr cut = getEdgeFunc(m.lwsync, src, dst) ||
      getEdgeFunc(m.sync, src, dst);
    if (dmbst) cut = cut || getEdgeFunc(m.dmbst, src, dst);
    return cut;
  }
}


SmtExpr makePathVcut(SmtSolver &s, VarMaps &m,
                     PathID path,
                     bool isPush) {
  bool dmbst = false;
  // XXX: We want to be able to use dmb st to cut visibility edges,
  // which could potentially be a big win. Unfortunately, I think it
  // means we need to actually have separate maps for the different
  // sorts of cuts, because of the path suffix sharing we do... So
  // instead we disable the path suffix sharing...
  if (NO_PATH_SUFFIX_SHARING && m.dmbst.enabled) {
    Action *head = m.bb2action[m.pc.getHead(path)];
    // If the source is simple writes, we can use a dmb st for
    // visibility. dmb st only orders writes, but visibility edges
    // only meaningfully affect writes.
    if (head && head->type == ActionSimpleWrites) {
      dmbst = true;
    }
  }

  return forAllPathEdges(
    s, m, path,
    [&] (PathID path, bool *b) {
      return getFunc(isPush ? m.pathPcut : m.pathVcut,
                     makeBlockPathKey(nullptr, path), b); },
    [&] (BasicBlock *src, BasicBlock *dst, PathID path) {
      return makeEdgeVcut(s, m, src, dst, isPush, dmbst);
    });
}


SmtExpr makeXcut(SmtSolver &s, VarMaps &m, Action &src, Action &dst,
                 BasicBlock *bindSite);
SmtExpr makeVcut(SmtSolver &s, VarMaps &m, Action &src, Action &dst,
                 BasicBlock *bindSite, RMCEdgeType edgeType);

SmtExpr makePathDmbldCut(SmtSolver &s, VarMaps &m,
                         PathID path) {
  if (!m.dmbld.enabled) return s.ctx().bool_val(false);
  return forAllPathEdges(
    s, m, path,
    [&] (PathID path, bool *b) {
      return getPathFunc(m.pathDmbld, path, b); },
    [&] (BasicBlock *src, BasicBlock *dst, PathID path) {
      return getEdgeFunc(m.dmbld, src, dst);
    });
}

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
  SmtExpr pathDataCut = makePathDataCut(s, m, path, tail, bindSite);
  if (!isSelfPath) {
    pathDataCut = pathDataCut &&
      makeXcut(s, m, *headAction, *headAction, bindSite);
  }

  s.add(isCut ==
        (makePathDmbldCut(s, m, path) ||
         makePathVcut(s, m, path, false) ||
         pathCtrlCut || pathDataCut));

  return isCut;
}


SmtExpr getRelease(SmtSolver &s, VarMaps &m, Action &a) {
  if (a.allSC) return s.ctx().bool_val(true);
  if (!m.release.enabled) return s.ctx().bool_val(false);
  return getEdgeFunc(m.release, a.bb, getSingleSuccessor(a.bb));
}
SmtExpr getAcquire(SmtSolver &s, VarMaps &m, Action &a) {
  if (a.allSC) return s.ctx().bool_val(true);
  if (!m.acquire.enabled) return s.ctx().bool_val(false);
  return getEdgeFunc(m.acquire, a.bb, getSingleSuccessor(a.bb));
}

SmtExpr makeRelAcqCut(SmtSolver &s, VarMaps &m, Action &src, Action &dst,
                      RMCEdgeType type) {
  SmtExpr relAcq = s.ctx().bool_val(false);

  // On x86 and on ARMv8, we can get more guarentees from the
  // realization of release/acquire than C++11 actually provides. In
  // particular, ARMv8's release instruction suffices to implement
  // * -v-> W.

  // W1 -v-> W/RW2  -- W/RW2 = rel
  // *  -v-> W/RW2  -- W/RW2 = rel, on ARMv8
  // *  -x-> W2     -- W2 = rel, on ARMv8
  // Note that the first cases establish visbility but unusually
  // *not* execution. They will establish visibility order
  // with the write but won't guarantee that the execution order
  // is right for a failed CAS. Ugh!
  // I think it would be fine for regular RMWs though?
  if ((type == VisibilityEdge || type == ExecutionEdge) &&
      (src.type == ActionSimpleWrites || m.params.relAbuse) &&
      (dst.type == ActionSimpleWrites ||
       (dst.type == ActionSimpleRMW && type == VisibilityEdge))) {
    relAcq = relAcq || getRelease(s, m, dst);
  }
  // R/RW1 -x-> *   -- R/RW1 = acq
  if (type == ExecutionEdge &&
      (src.type == ActionSimpleRead || src.type == ActionSimpleRMW)) {
    relAcq = relAcq || getAcquire(s, m, src);
  }
  // R/RW1 -v-> W/RW2 -- complicated
  // If we /aren't/ doing the abusive non-C11 interpretation of
  // release, we can do an R->W vis edge by marking both.
  if (!m.params.relAbuse &&
      (type == VisibilityEdge || type == ExecutionEdge) &&
      (src.type == ActionSimpleRead || src.type == ActionSimpleRMW) &&
      (dst.type == ActionSimpleWrites || dst.type == ActionSimpleRMW)) {

    relAcq = relAcq || (getRelease(s, m, dst) &&
                        getAcquire(s, m, src));
  }

  return relAcq.simplify();
}

SmtExpr makeXcut(SmtSolver &s, VarMaps &m, Action &src, Action &dst,
                 BasicBlock *bindSite) {
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
                 BasicBlock *bindSite, RMCEdgeType edgeType) {
  bool isPush = edgeType == PushEdge;
  SmtExpr isCut = getFunc(isPush ? m.pcut : m.vcut,
                          makeBlockEdgeKey(bindSite, src.outBlock, dst.bb));

  SmtExpr allPathsCut = forAllPaths(
    s, m, src.outBlock, dst.bb,
    [&] (PathID path) { return makePathVcut(s, m, path, isPush); },
    bindSite);
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

  TuningParams params = archParams(target_);
  SmtContext c;
  SmtSolver s(c);

#if LONG_PATH_NAMES
  debugPathCache = &pc_; /* :( */
#endif

  VarMaps m = {
    pc_,
    bb2action_,
    domTree_,
    params,
    DeclMap<EdgeKey>(c.bool_sort(), "sync"),
    DeclMap<EdgeKey>(c.bool_sort(), "lwsync",
                     paramEnabled(params.lwsyncCost)),
    DeclMap<EdgeKey>(c.bool_sort(), "dmbst",
                     paramEnabled(params.dmbstCost)),
    DeclMap<EdgeKey>(c.bool_sort(), "dmbld",
                     paramEnabled(params.dmbldCost)),
    DeclMap<PathKey>(c.bool_sort(), "pathDmbld",
                     paramEnabled(params.dmbldCost)),
    DeclMap<EdgeKey>(c.bool_sort(), "release",
                     paramEnabled(params.makeReleaseCost)),
    DeclMap<EdgeKey>(c.bool_sort(), "acquire",
                     paramEnabled(params.makeAcquireCost)),
    DeclMap<BlockEdgeKey>(c.bool_sort(), "pcut"),
    DeclMap<BlockEdgeKey>(c.bool_sort(), "vcut"),
    DeclMap<BlockEdgeKey>(c.bool_sort(), "xcut"),
    DeclMap<BlockPathKey>(c.bool_sort(), "path_pcut"),
    DeclMap<BlockPathKey>(c.bool_sort(), "path_vcut"),
    DeclMap<BlockPathKey>(c.bool_sort(), "path_xcut"),

    DeclMap<EdgeKey>(c.bool_sort(), "isync",
                     paramEnabled(params.isyncCost)),
    DeclMap<PathKey>(c.bool_sort(), "path_isync"),
    DeclMap<BlockPathKey>(c.bool_sort(), "path_ctrl_isync"),

    DeclMap<BlockEdgeKey>(c.bool_sort(), "uses_ctrl",
                          paramEnabled(params.addCtrlCost)),
    DeclMap<BlockPathKey>(c.bool_sort(), "path_ctrl"),
    DeclMap<EdgeKey>(c.bool_sort(), "all_paths_ctrl"),

    DeclMap<std::pair<BlockKey, EdgePathKey>>(
      c.bool_sort(), "uses_data",
      paramEnabled(params.useDataCost)),
    DeclMap<std::pair<BlockKey, std::pair<PathID, BlockPathKey>>>(
      c.bool_sort(), "path_data"),
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
    //return edgeCap[makeEdgeKey(src, dst)]*pow(4, loopInfo_.getLoopDepth(src));
    return edgeCap[makeEdgeKey(src, dst)];
  };

  //////////
  // HOK. Make sure everything is cut.
  for (auto & edge : edges_) {
    if (edge.edgeType == ExecutionEdge) {
      s.add(makeXcut(s, m, *edge.src, *edge.dst, edge.bindSite));
    } else {
      s.add(makeVcut(s, m, *edge.src, *edge.dst, edge.bindSite, edge.edgeType));
    }
  }

  // Build a table of all the cut types we can use that can be viewed
  // just as operating on a single edge. This lets us use the same
  // code for calculating cost and extracting results for all of them.
  struct {
    DeclMap<EdgeKey> &map;
    int cost;
    CutType type;
  } cuttypes[] = {
    { m.sync, params.syncCost, CutSync },
    { m.lwsync, params.lwsyncCost, CutLwsync },
    { m.isync, params.isyncCost, CutIsync },
    { m.dmbst, params.dmbstCost, CutDmbSt },
    { m.dmbld, params.dmbldCost, CutDmbLd },
    { m.release, params.makeReleaseCost, CutRelease },
    { m.acquire, params.makeAcquireCost, CutAcquire },
  };

  //////////
  // OK, now build a cost function. This will probably take a lot of
  // tuning.
  SmtExpr costVar = c.int_const("cost");
  SmtExpr cost = c.int_val(0);

  BasicBlock *src, *dst;
  SmtExpr v = c.bool_val(false);

  // Cost for all edge cutting actions
  for (auto & cuttype : cuttypes) {
    for (auto & entry : cuttype.map.map) {
      unpack(unpack(src, dst), v) = fix_pair(entry);
      cost = cost +
        boolToInt(v, cuttype.cost*weight(src, dst)+1);
    }
  }
  // Ctrl cost
  for (auto & entry : m.usesCtrl.map) {
    BasicBlock *dep;
    unpack(unpack(dep, unpack(src, dst)), v) = fix_pair(entry);
    auto ctrlWeight =
      branchesOn(src, bb2action_[dep]->outgoingDep) ?
        params.useCtrlCost : params.addCtrlCost;
    cost = cost +
      boolToInt(v, ctrlWeight*weight(src, dst));
  }
  // Data dep cost
  for (auto & entry : m.usesData.map) {
    PathID path;
    BasicBlock *bindSite;
    unpack(unpack(bindSite, unpack(unpack(src, dst), path)), v) =
      fix_pair(entry);
    // XXX: this is a hack that depends on us only using actions in
    // usesData things
    BasicBlock *pred = bb2action_[dst]->bb->getSinglePredecessor();
    cost = cost +
      boolToInt(v, params.useDataCost*weight(pred, dst));
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

  // Find all edge cuts to insert
  for (auto & cuttype : cuttypes) {
    processMap<EdgeKey>(cuttype.map, model, [&] (EdgeKey &edge) {
      cuts.push_back(EdgeCut(cuttype.type, edge.first, edge.second));
    });
  }
  // Find the controls to preserve/insert
  processMap<BlockEdgeKey>(m.usesCtrl, model, [&] (BlockEdgeKey &entry) {
    EdgeKey edge; BasicBlock *dep;
    unpack(dep, edge) = entry;
    Value *read = bb2action_[dep]->outgoingDep;
    cuts.push_back(EdgeCut(CutCtrl, edge.first, edge.second, read));
  });
  // Find data deps to preserve
  processMap<std::pair<BlockKey, EdgePathKey>>(
    m.usesData, model,
    [&] (std::pair<BlockKey, EdgePathKey> &entry) {
    BasicBlock *src, *dst; PathID path;
    BasicBlock *bindSite;
    unpack(bindSite, unpack(unpack(src, dst), path)) = entry;
    Value *read = bb2action_[src]->outgoingDep;
    cuts.push_back(EdgeCut(CutData, src, dst, read, bindSite, path));
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
