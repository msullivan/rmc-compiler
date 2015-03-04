#include "RMCInternal.h"

#include "PathCache.h"

#include <llvm/IR/Function.h>
#include <llvm/IR/CFG.h>

#include <llvm/IR/Dominators.h>

#include "smt.h"

using namespace llvm;

#define LONG_PATH_NAMES 1

// Some tuning parameters

// Should we pick the initial upper bound by seeing what the solver
// produces without constraints instead of by binary searching up?
const bool kGuessUpperBound = false;
const int kFirstUpperBound = 16;
// If we guess an upper bound, should we hope that it is optimal and
// check the bound - 1 before we binary search?
const bool kCheckFirstGuess = false;
// Should we invert all bool variables; sort of useful for testing
const bool kInvertBools = false;

// Costs for different sorts of things that we insert.
// XXX: These numbers are just made up.
const int kLwsyncCost = 50;
const int kIsyncCost = 20;
const int kUseCtrlCost = 1;
const int kAddCtrlCost = 7;
const int kUseDataCost = 1;

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
typedef std::pair<BasicBlock *, BasicBlock *> EdgeKey;
typedef PathID PathKey;
typedef std::pair<BasicBlock *, PathID> BlockPathKey;
typedef std::pair<EdgeKey, PathID> EdgePathKey;
typedef std::pair<BasicBlock *, EdgeKey> BlockEdgeKey;
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
  return key->getName().str();
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
DenseMap<EdgeKey, int> computeCapacities(Function &F) {
  SmtContext c;
  SmtSolver s(c);

  DeclMap<BasicBlock *> nodeCapM(c.int_sort(), "node_cap");
  DeclMap<EdgeKey> edgeCapM(c.int_sort(), "edge_cap");

  //// Build the equations.
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
    auto i = succ_begin(&block), e = succ_end(&block);
    int childCount = e - i;
    for (; i != e; ++i) {
      // For now, we assume even probabilities.
      // Would be an improvement to do better
      SmtExpr edgeCap = getEdgeFunc(edgeCapM, &block, *i);
      // We want: c(v, u) == Pr(v, u) * c(v). Since Pr(v, u) ~= 1/n, we do
      // n * c(v, u) == c(v)
      s.add(edgeCap * c.int_val(childCount) == nodeCap);
    }
    // Populate the capacities for the fictional back edges to the
    // function entry point.
    // XXX: we don't bother establishing the equation holds on the
    // *other* side, but I think it should work out OK.
    if (childCount == 0) {
      s.add(getEdgeFunc(edgeCapM, &block, &F.getEntryBlock()) == nodeCap);
    }
  }

  // Keep all zeros from working:
  s.add(getFunc(nodeCapM, &F.getEntryBlock()) > c.int_val(0));

  //// Extract a solution.
  // XXX: will this get returned efficiently?
  DenseMap<EdgeKey, int> caps;

  doCheck(s);
  SmtModel model = s.get_model();
  for (auto & entry : edgeCapM.map) {
    EdgeKey edge = entry.first;
    SmtExpr cst = entry.second;
    int cap = extractInt(model.eval(cst));
    caps.insert(std::make_pair(edge, cap));
  }

  return caps;
}


struct VarMaps {
  // Sigh.
  PathCache &pc;
  DenseMap<BasicBlock *, Action *> &bb2action;
  DominatorTree &domTree;

  DeclMap<EdgeKey> lwsync;
  DeclMap<EdgeKey> vcut;
  DeclMap<EdgeKey> xcut;
  DeclMap<PathKey> pathVcut;
  DeclMap<PathKey> pathXcut;

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
                    BasicBlock *src, BasicBlock *dst, PathFunc func) {
  // Now try all the paths
  SmtExpr allPaths = s.ctx().bool_val(true);
  PathList paths = m.pc.findAllSimplePaths(src, dst, true, true);
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
  if (src == dep || m.domTree.dominates(m.bb2action[dep]->soleLoad, src)) {
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
  if (!m.bb2action[dep]||!m.bb2action[dep]->soleLoad) return c.bool_val(false);

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
  if (!m.bb2action[dep]||!m.bb2action[dep]->soleLoad) return c.bool_val(false);

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
                         PathID path, Action *tail) {
  // XXX: do we care about actually using the variable pathCtrlCut?
  // TODO: support isync cuts also
  if (tail->type == ActionSimpleWrites) {
    return makePathCtrl(s, m, path);
  } else {
    return makePathCtrlIsync(s, m, path);
  }
}

SmtExpr makeData(SmtSolver &s, VarMaps &m,
                 BasicBlock *dep, BasicBlock *dst,
                 PathID path) {
  Action *src = m.bb2action[dep];
  Action *tail = m.bb2action[dst];
  if (src && tail && src->soleLoad && tail->soleLoad &&
      addrDepsOn(tail->soleLoad, src->soleLoad, &m.pc, path))
    return getFunc(m.usesData, makeEdgePathKey(src->bb, tail->bb, path));

  return s.ctx().bool_val(false);
}

// Does it make sense for this to be a path variable at all???
SmtExpr makePathDataCut(SmtSolver &s, VarMaps &m,
                        PathID fullPath, Action *tail) {
  SmtContext &c = s.ctx();
  if (m.pc.isEmpty(fullPath)) return c.bool_val(false);
  BasicBlock *dep = m.pc.getHead(fullPath);
  // Can't have a addr dependency when it's not a load.
  // XXX: we can maybe be more granular about things.
  if (!m.bb2action[dep]||!m.bb2action[dep]->soleLoad) return c.bool_val(false);

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
      if (dst != tail->bb) {
        cut = cut &&
          (makePathCtrlCut(s, m, path, tail) ||
           makePathDataCut(s, m, path, tail));
      }
      return cut;
    });
}

SmtExpr makeEdgeVcut(SmtSolver &s, VarMaps &m,
                     BasicBlock *src, BasicBlock *dst) {
  // Instead of generating a notion of push edges and making sure that
  // they are cut by syncs, we just insert syncs exactly where PUSHes
  // are written. We want to actually take advantage of these syncs,
  // so check for them here. (If there is already a sync we generate
  // a bunch of equations for the path which all evaluate to "true",
  // which is kind of silly. We could avoid some of this if we cared.)
  // We only need to check one of src and dst since arcs with PUSH as
  // an endpoint are dropped.
  if (m.bb2action[src] && m.bb2action[src]->isPush) {
    return s.ctx().bool_val(true);
  } else {
    return getEdgeFunc(m.lwsync, src, dst);
  }
}


SmtExpr makePathVcut(SmtSolver &s, VarMaps &m,
                     PathID path) {
  return forAllPathEdges(
    s, m, path,
    [&] (PathID path, bool *b) { return getPathFunc(m.pathVcut, path, b); },
    [&] (BasicBlock *src, BasicBlock *dst, PathID path) {
      return makeEdgeVcut(s, m, src, dst);
    });
}


SmtExpr makeXcut(SmtSolver &s, VarMaps &m, BasicBlock *src, Action *dst);

SmtExpr makePathXcut(SmtSolver &s, VarMaps &m,
                     PathID path, Action *tail) {
  bool alreadyMade;
  SmtExpr isCut = getPathFunc(m.pathXcut, path, &alreadyMade);
  if (alreadyMade) return isCut;

  BasicBlock *head = m.pc.getHead(path);
  bool isSelfPath = head == tail->bb;

  SmtExpr pathCtrlCut = makePathCtrlCut(s, m, path, tail);
  if (!isSelfPath) {
    pathCtrlCut = pathCtrlCut &&
      (makeAllPathsCtrl(s, m, head, head) ||
      makeXcut(s, m, head, m.bb2action[head]));
  }
  SmtExpr pathDataCut = makePathDataCut(s, m, path, tail);
  if (!isSelfPath) {
    pathDataCut = pathDataCut &&
      makeXcut(s, m, head, m.bb2action[head]);
  }

  s.add(isCut ==
        (makePathVcut(s, m, path) || pathCtrlCut || pathDataCut));

  return isCut;
}

SmtExpr makeXcut(SmtSolver &s, VarMaps &m, BasicBlock *src, Action *dst) {
  bool alreadyMade;
  SmtExpr isCut = getEdgeFunc(m.xcut, src, dst->bb, &alreadyMade);
  if (alreadyMade) return isCut;

  SmtExpr allPathsCut = forAllPaths(
    s, m, src, dst->bb,
    [&] (PathID path) { return makePathXcut(s, m, path, dst); });
  s.add(isCut == allPathsCut);

  return isCut;
}

SmtExpr makeVcut(SmtSolver &s, VarMaps &m, BasicBlock *src, BasicBlock *dst) {
  SmtExpr isCut = getEdgeFunc(m.vcut, src, dst);

  SmtExpr allPathsCut = forAllPaths(
    s, m, src, dst,
    [&] (PathID path) { return makePathVcut(s, m, path); });
  s.add(isCut == allPathsCut);

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

std::vector<EdgeCut> RealizeRMC::smtAnalyze() {
  SmtContext c;
  SmtSolver s(c);

#if LONG_PATH_NAMES
  debugPathCache = &pc_; /* :( */
#endif

  VarMaps m = {
    pc_,
    bb2action_,
    domTree_,
    DeclMap<EdgeKey>(c.bool_sort(), "lwsync"),
    DeclMap<EdgeKey>(c.bool_sort(), "vcut"),
    DeclMap<EdgeKey>(c.bool_sort(), "xcut"),
    DeclMap<PathKey>(c.bool_sort(), "path_vcut"),
    DeclMap<PathKey>(c.bool_sort(), "path_xcut"),

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
  DenseMap<EdgeKey, int> edgeCap = computeCapacities(func_);
  auto weight =
    [&] (BasicBlock *src, BasicBlock *dst) {
    // The weight of an edge is based on its graph capacity and its loop depth.
    // We use 4^depth as a scaling factor for loop depth so that a loop body
    // will have twice the cost of its parent (since the parent probably has
    // twice the capacity).
    return edgeCap[makeEdgeKey(src, dst)] * pow(4, loopInfo_.getLoopDepth(src));
  };

  //////////
  // HOK. Make sure everything is cut.
  for (auto & src : actions_) {
    for (auto dst : src.visTransEdges) {
      s.add(makeVcut(s, m, src.bb, dst->bb));
    }
    for (auto dst : src.execTransEdges) {
      s.add(makeXcut(s, m, src.bb, dst));
    }
  }

  //////////
  // OK, now build a cost function. This will probably take a lot of
  // tuning.
  SmtExpr costVar = c.int_const("cost");
  SmtExpr cost = c.int_val(0);

  BasicBlock *src, *dst;
  SmtExpr v = c.bool_val(false);

  // Find the lwsyncs we are inserting
  for (auto & entry : m.lwsync.map) {
    unpack(unpack(src, dst), v) = entry;
    cost = cost +
      boolToInt(v, kLwsyncCost*weight(src, dst)+1);
  }
  for (auto & entry : m.isync.map) {
    unpack(unpack(src, dst), v) = entry;
    cost = cost +
      boolToInt(v, kIsyncCost*weight(src, dst)+1);
  }
  for (auto & entry : m.usesCtrl.map) {
    BasicBlock *dep;
    unpack(unpack(dep, unpack(src, dst)), v) = entry;
    auto ctrlWeight =
      branchesOn(src, bb2action_[dep]->soleLoad) ? kUseCtrlCost : kAddCtrlCost;
    cost = cost +
      boolToInt(v, ctrlWeight*weight(src, dst));
  }
  for (auto & entry : m.usesData.map) {
    PathID path;
    unpack(unpack(unpack(src, dst), path), v) = entry;
    // XXX: this is a hack that depends on us only using actions in
    // usesData things
    BasicBlock *pred = bb2action_[dst]->startBlock;
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

  // Find the lwsyncs we are inserting
  processMap<EdgeKey>(m.lwsync, model, [&] (EdgeKey &edge) {
    cuts.push_back(EdgeCut(CutLwsync, edge.first, edge.second));
  });
  // Find the isyncs we are inserting
  processMap<EdgeKey>(m.isync, model, [&] (EdgeKey &edge) {
    cuts.push_back(EdgeCut(CutIsync, edge.first, edge.second));
  });
  // Find the controls to preserve
  processMap<BlockEdgeKey>(m.usesCtrl, model, [&] (BlockEdgeKey &entry) {
    EdgeKey edge; BasicBlock *dep;
    unpack(dep, edge) = entry;
    Value *read = bb2action_[dep]->soleLoad;
    cuts.push_back(EdgeCut(CutCtrl, edge.first, edge.second, read));
  });
  if (debugSpew) errs() << "\n";

  return cuts;
}
