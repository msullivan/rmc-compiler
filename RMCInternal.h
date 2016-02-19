// Copyright (c) 2014-2016 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

#ifndef RMC_INTERNAL_H
#define RMC_INTERNAL_H

#include "sassert.h"

#include <utility>
#include <tuple>

#include "PathCache.h"

#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/ADT/TinyPtrVector.h>

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Support/raw_ostream.h>

#include <llvm/IR/Dominators.h>
#include <llvm/Analysis/LoopInfo.h>

// std::forward_as_tuple is basically just better for destructuring
// assignment than std::tie is, I think, since it can handle nesting
// (since it allows rvalue references as arguments).
// We want it named something better than forward_as_tuple, though!
// We just reimplement it because apparently #define is evil these
// days or something.
template<class... Types>
constexpr std::tuple<Types&&...> unpack(Types&&... args) {
  return std::tuple<Types&&...>(std::forward<Types>(args)...);
}

// Force a pair to be viewed as just a std::pair and not as any weird
// LLVM internal thing like DenseMapPair. I needed to do this on OS X
// but not on linux, presumbably because of differences in the
// pair/tuple implementations?
template<class T1, class T2>
constexpr std::pair<T1, T2> fix_pair(std::pair<T1, T2> x) {
  return x;
}

namespace llvm {

//// Indicator for edge types
enum RMCEdgeType {
  // This order needs to correspond with the values in rmc-core.h
  ExecutionEdge,
  VisibilityEdge,
  PushEdge,
  NoEdge
};
raw_ostream& operator<<(raw_ostream& os, const RMCEdgeType& t);

//// Information for a node in the RMC graph.
enum ActionType {
  ActionPrePost,
  ActionNop,
  ActionComplex,
  ActionPush,
  ActionSimpleRead,
  ActionSimpleWrites, // needs to be paired with a dep
  ActionSimpleRMW,
};
struct Action {
  Action(BasicBlock *p_bb,
         BasicBlock *p_startBlock = nullptr, BasicBlock *p_endBlock = nullptr,
         std::string p_name = "") :
    bb(p_bb),
    startBlock(p_startBlock),
    endBlock(p_endBlock),
    name(p_name),
    type(ActionComplex)
    {}
  void operator=(const Action &) = delete;
  Action(const Action &) = delete;
  Action(Action &&) = default; // move constructor!

  BasicBlock *bb;
  BasicBlock *startBlock;
  BasicBlock *endBlock;

  std::string name;

  // Some basic info about what sort of instructions live in the action
  ActionType type;
  bool isPush{false};
  int stores{0};
  int loads{0};
  int RMWs{0};
  int calls{0};
  Instruction *soleLoad{nullptr};

  // Edges in the graph.
  // XXX: Would we be better off storing this some other way?
  // a <ptr, type> pair?
  // And should we store v edges in x
  // XXX: we should keep an array of the edge sets.
  SmallPtrSet<Action *, 2> execEdges;
  SmallPtrSet<Action *, 2> visEdges;
  SmallPtrSet<Action *, 2> pushEdges;

  typedef SmallPtrSet<Action *, 8> TransEdges;
  TransEdges execTransEdges;
  TransEdges visTransEdges;
  TransEdges pushTransEdges;
};

//// Info about an RMC edge
struct RMCEdge {
  RMCEdgeType edgeType;
  // Null pointers for src and dst indicate pre and post edges, respectively.
  // This is a little frumious.
  Action *src;
  Action *dst;

  bool operator<(const RMCEdge& rhs) const {
    return std::tie(edgeType, src, dst)
      < std::tie(rhs.edgeType, rhs.src, rhs.dst);
  }

  void print(raw_ostream &os) const {
    // substr(5) is to drop "_rmc_" from the front
    auto srcName = src ? src->bb->getName().substr(5) : "pre";
    auto dstName = dst ? dst->bb->getName().substr(5) : "post";
    os << srcName << " -" << edgeType << "-> " << dstName;
  }
};
raw_ostream& operator<<(raw_ostream& os, const RMCEdge& e);

//// Cuts in the graph
enum CutType {
  CutNone,
  CutCtrlIsync, // needs to be paired with a dep
  CutCtrl,
  CutIsync,
  CutLwsync,
  CutSync,
  CutData,
};
struct BlockCut {
  BlockCut() : type(CutNone), isFront(false), read(nullptr) {}
  BlockCut(CutType ptype, bool pfront, Value *pread = nullptr)
    : type(ptype), isFront(pfront), read(pread) {}
  CutType type;
  bool isFront;
  Value *read;
};

struct EdgeCut {
  EdgeCut() {}
  EdgeCut(CutType ptype, BasicBlock *psrc, BasicBlock *pdst,
          Value *pread = nullptr,
          PathID ppath = PathCache::kEmptyPath)
    : type(ptype), src(psrc), dst(pdst), read(pread), path(ppath) {}

  CutType type{CutNone};
  BasicBlock *src{nullptr};
  BasicBlock *dst{nullptr};
  Value *read{nullptr};
  PathID path{PathCache::kEmptyPath};
};

enum CutStrength {
  NoCut,
  DataCut, // Is cut for one loop iteration, needs an xcut
  SoftCut, // Is cut for one loop iteration, needs a ctrl
  HardCut,
};

// Utility functions
bool branchesOn(BasicBlock *bb, Value *load,
                ICmpInst **icmpOut = nullptr, int *outIdx = nullptr);
bool addrDepsOn(Instruction *instr, Value *load,
                PathCache *cache = nullptr,
                PathID path = PathCache::kEmptyPath,
                std::vector<Instruction *> *trail = nullptr);

// Class to track the analysis of the function and insert the syncs.
class RealizeRMC {
private:
  Function &func_;
  Pass * const underlyingPass_;
  DominatorTree &domTree_;
  LoopInfo &loopInfo_;
  const bool useSMT_;

  bool actionsBoundOutside_{true};

  int numNormalActions_{0};
  std::vector<Action> actions_;
  std::vector<RMCEdge> edges_;
  SmallPtrSet<Action *, 4> pushes_;
  DenseMap<BasicBlock *, Action *> bb2action_;
  DenseMap<BasicBlock *, BlockCut> cuts_;
  PathCache pc_;

  // Functions
  BasicBlock *splitBlock(BasicBlock *Old, Instruction *SplitPt);

  // Analysis routines
  void findActions();
  void findEdges();
  Action *makePrePostAction(BasicBlock *bb);
  TinyPtrVector<Action *> collectEdges(StringRef name);
  void processEdge(CallInst *call);
  bool processPush(CallInst *call);

  // Shared compilation
  void cutPushes();

  // non-SMT compilation
  CutStrength isPathCut(const RMCEdge &edge, PathID path,
                        bool enforceSoft, bool justCheckCtrl);
  CutStrength isEdgeCut(const RMCEdge &edge,
                        bool enforceSoft = false, bool justCheckCtrl = false);
  bool isCut(const RMCEdge &edge);
  void cutEdge(RMCEdge &edge);
  void cutEdges();

  // SMT compilation
  void insertCut(const EdgeCut &cut);
  std::vector<EdgeCut> smtAnalyzeInner();
  std::vector<EdgeCut> smtAnalyze();

public:
  RealizeRMC(Function &F, Pass *underlyingPass,
             DominatorTree &domTree,
             LoopInfo &loopInfo, bool useSMT)
    : func_(F), underlyingPass_(underlyingPass),
      domTree_(domTree), loopInfo_(loopInfo),
      useSMT_(useSMT) {}
  ~RealizeRMC() { }
  bool run();
};

}

#endif
