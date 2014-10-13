#ifndef RMC_INTERNAL_H
#define RMC_INTERNAL_H

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Support/raw_ostream.h>

namespace llvm {

//// Indicator for edge types
enum RMCEdgeType {
  NoEdge,
  VisibilityEdge,
  ExecutionEdge
};

//// Information for a node in the RMC graph.
enum ActionType {
  ActionComplex,
  ActionPush,
  ActionSimpleRead,
  ActionSimpleWrites, // needs to be paired with a dep
  ActionSimpleRMW
};
struct Action {
  Action(BasicBlock *p_bb) :
    bb(p_bb),
    type(ActionComplex),
    isPush(false),
    stores(0), loads(0), RMWs(0), calls(0), soleLoad(nullptr),
    preEdge(NoEdge), postEdge(NoEdge)
    {}
  void operator=(const Action &) LLVM_DELETED_FUNCTION;
  Action(const Action &) LLVM_DELETED_FUNCTION;
  Action(Action &&) = default; // move constructor!

  BasicBlock *bb;

  // Some basic info about what sort of instructions live in the action
  ActionType type;
  bool isPush;
  int stores;
  int loads;
  int RMWs;
  int calls;
  LoadInst *soleLoad;

  // Pre and post edges
  RMCEdgeType preEdge;
  RMCEdgeType postEdge;

  // Edges in the graph.
  // XXX: Would we be better off storing this some other way?
  // a <ptr, type> pair?
  // And should we store v edges in x
  SmallPtrSet<Action *, 2> execEdges;
  SmallPtrSet<Action *, 2> visEdges;

  typedef SmallPtrSet<Action *, 8> TransEdges;
  TransEdges execTransEdges;
  TransEdges visTransEdges;
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

//// Cuts in the graph
enum CutType {
  CutNone,
  CutCtrlIsync, // needs to be paired with a dep
  CutLwsync,
  CutSync
};
struct EdgeCut {
  EdgeCut() : type(CutNone), isFront(false), read(nullptr) {}
  EdgeCut(CutType ptype, bool pfront, Value *pread = nullptr)
    : type(ptype), isFront(pfront), read(pread) {}
  CutType type;
  bool isFront;
  Value *read;
};

enum CutStrength {
  NoCut,
  SoftCut, // Is cut for one loop iteration
  HardCut
};


}

#endif
