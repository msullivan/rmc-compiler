// BUG: this whole thing depends on the specifics of how the clang version I
// am using emits llvm bitcode for the hacky RMC protocol.
// We rely on how basic blocks get named, on the labels forcing things
// into their own basic blocks, and probably will rely on this block
// having one predecessor and one successor. We could probably even
// force those to be empty without too much work by adding more labels...

// BUG: the handling of of the edge cut map is bogus. Right now we are
// working around this by only ever having syncs at the start of
// blocks.

#include "RMCInternal.h"

#include "PathCache.h"

#include <llvm/Pass.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/InlineAsm.h>
#include <llvm/IR/CFG.h>
#include <llvm/IR/InstIterator.h>

#include <llvm/Transforms/Scalar.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/SmallPtrSet.h>

#include <llvm/Support/raw_ostream.h>

#include <ostream>
#include <fstream>
#include <sstream>
#include <iostream>
#include <map>
#include <set>

#include <z3++.h>


using namespace llvm;

// Some tuning parameters

// Should we use Z3's optimizer; this is a #define because it tunes
// what interface we use. Maybe should be defined by Makefile or
// whatever.
#define USE_Z3_OPTIMIZER 1

// Should we pick the initial upper bound by seeing what the solver
// produces without constraints instead of by binary searching up?
const bool kGuessUpperBound = false;
// If we guess an upper bound, should we hope that it is optimal and
// check the bound - 1 before we binary search?
const bool kCheckFirstGuess = false;
// Should we invert all bool variables; sort of useful for testing
const bool kInvertBools = false;

///////
// Printing functions I wish didn't have to be here
raw_ostream& operator<<(raw_ostream& os, const RMCEdgeType& t) {
  switch (t) {
    case VisibilityEdge:
      os << "v";
      break;
    case ExecutionEdge:
      os << "x";
      break;
    case NoEdge:
      os << "no";
      break;
    default:
      os << "?";
      break;
  }
   return os;
}

raw_ostream& operator<<(raw_ostream& os, const RMCEdge& e) {
  e.print(os);
  return os;
}


// Compute the transitive closure of the action graph
void transitiveClosure(std::vector<Action> &actions,
                       Action::TransEdges Action::* edgeset) {
  // Use Warshall's algorithm to compute the transitive closure. More
  // or less. I can probably be a little more clever since I have
  // adjacency lists, right? But n is small and this is really easy.
  for (auto & k : actions) {
    for (auto & i : actions) {
      for (auto & j : actions) {
        if ((i.*edgeset).count(&k) && (k.*edgeset).count(&j)) {
          (i.*edgeset).insert(&j);
        }
      }
    }
  }
}

///////////////////////////////////////////////////////////////////////////

enum RMCTarget {
  TargetX86,
  TargetARM
};
// It is sort of bogus to make this global.
RMCTarget target;

// Generate a unique str that we add to comments in our inline
// assembly to keep llvm from getting clever and merging them.
// This is awful.
std::string uniqueStr() {
  // This isn't threadsafe but whatever
  static int i = 0;
  std::ostringstream buffer;
  buffer << i++;
  return buffer.str();
}

// We use this wrapper to make sure that llvm won't try to merge the
// inline assembly operations.
InlineAsm *makeAsm(FunctionType *Ty,
                   const char *AsmString, StringRef Constraints,
                   bool hasSideEffects) {
  return InlineAsm::get(Ty, AsmString + (" #" + uniqueStr()),
                        Constraints, hasSideEffects);
}

// Some llvm nonsense. I should probably find a way to clean this up.
// do we put ~{dirflag},~{fpsr},~{flags} for the x86 ones? don't think so.
Instruction *makeBarrier(Instruction *to_precede) {
  LLVMContext &C = to_precede->getContext();
  FunctionType *f_ty = FunctionType::get(FunctionType::getVoidTy(C), false);
  InlineAsm *a = makeAsm(f_ty, "# barrier", "~{memory}", true);
  return CallInst::Create(a, None, "", to_precede);
}
Instruction *makeSync(Instruction *to_precede) {
  LLVMContext &C = to_precede->getContext();
  FunctionType *f_ty = FunctionType::get(FunctionType::getVoidTy(C), false);
  InlineAsm *a = nullptr;
  if (target == TargetARM) {
    a = makeAsm(f_ty, "dmb @ sync", "~{memory}", true);
  } else if (target == TargetX86) {
    a = makeAsm(f_ty, "mfence # sync", "~{memory}", true);
  }
  return CallInst::Create(a, None, "", to_precede);
}
Instruction *makeLwsync(Instruction *to_precede) {
  LLVMContext &C = to_precede->getContext();
  FunctionType *f_ty = FunctionType::get(FunctionType::getVoidTy(C), false);
  InlineAsm *a = nullptr;
  if (target == TargetARM) {
    a = makeAsm(f_ty, "dmb @ lwsync", "~{memory}", true);
  } else if (target == TargetX86) {
    a = makeAsm(f_ty, "# lwsync", "~{memory}", true);
  }
  return CallInst::Create(a, None, "", to_precede);
}
Instruction *makeIsync(Instruction *to_precede) {
  LLVMContext &C = to_precede->getContext();
  FunctionType *f_ty =
    FunctionType::get(FunctionType::getVoidTy(C), false);
  InlineAsm *a = nullptr;
  if (target == TargetARM) {
    a = makeAsm(f_ty, "isb @ isync", "~{memory}", true);
  } else if (target == TargetX86) {
    a = makeAsm(f_ty, "# isync", "~{memory}", true);
  }
  return CallInst::Create(a, None, "", to_precede);
}
Instruction *makeCtrl(Value *v, Instruction *to_precede) {
  LLVMContext &C = v->getContext();
  FunctionType *f_ty =
    FunctionType::get(FunctionType::getVoidTy(C), v->getType(), false);
  InlineAsm *a = nullptr;
  if (target == TargetARM) {
    a = makeAsm(f_ty, "cmp $0, $0;beq 1f;1: @ ctrl",
                "r,~{memory}", true);
  } else if (target == TargetX86) {
    a = makeAsm(f_ty, "# ctrl", "r,~{memory}", true);
  }
  return CallInst::Create(a, v, "", to_precede);
}
Instruction *makeCtrlIsync(Value *v, Instruction *to_precede) {
  Instruction *i = makeIsync(to_precede);
  makeCtrl(v, i);
  return i;
}
Instruction *makeCopy(Value *v, Instruction *to_precede) {
  FunctionType *f_ty = FunctionType::get(v->getType(), v->getType(), false);
  InlineAsm *a = makeAsm(f_ty, "# copy", "=r,0", false); /* false?? */
  return CallInst::Create(a, v, "__rmc_bs_copy", to_precede);
}
// We also need to add a thing for fake data deps, which is more annoying.

///////////////////////////////////////////////////////////////////////////
//// Code to pull random crap out of LLVM functions

StringRef getStringArg(Value *v) {
  const Value *g = cast<Constant>(v)->stripPointerCasts();
  const Constant *ptr = cast<GlobalVariable>(g)->getInitializer();
  StringRef str = cast<ConstantDataSequential>(ptr)->getAsString();
  // Drop the '\0' at the end
  return str.drop_back();
}

BasicBlock *getSingleSuccessor(const BasicBlock *bb) {
  assert(bb->getTerminator()->getNumSuccessors() == 1 &&
         "ERROR: Actions must not contain control flow!");
  return bb->getTerminator()->getSuccessor(0);
}

///////////////////////////////////////////////////////////////////////////
//// Actual code for the pass

class RealizeRMC {
private:
  Function &func_;

  std::vector<Action> actions_;
  std::vector<RMCEdge> edges_;
  SmallPtrSet<Action *, 4> pushes_;
  DenseMap<BasicBlock *, Action *> bb2action_;
  DenseMap<BasicBlock *, EdgeCut> cuts_;
  PathCache pc_;

  // Functions
  CutStrength isPathCut(const RMCEdge &edge, PathID path,
                        bool enforceSoft, bool justCheckCtrl);
  CutStrength isEdgeCut(const RMCEdge &edge,
                        bool enforceSoft = false, bool justCheckCtrl = false);
  bool isCut(const RMCEdge &edge);

  void findActions();
  void findEdges();
  void cutPushes();
  void cutPrePostEdges();
  void cutEdges();
  void cutEdge(RMCEdge &edge);

  void processEdge(CallInst *call);
  void processPush(CallInst *call);

  void smtAnalyze();

public:
  RealizeRMC(Function &F) : func_(F) { }
  ~RealizeRMC() { }
  bool run();
};

bool nameMatches(StringRef blockName, StringRef target,
                 const char *prefix) {
  StringRef name = prefix + target.str() + "_";
  if (!blockName.startswith(name)) return false;
  // Now make sure the rest is an int
  APInt dummy;
  return !blockName.drop_front(name.size()).getAsInteger(10, dummy);
}

bool blockMatches(const BasicBlock &block, StringRef target) {
  if (!nameMatches(block.getName(), target, "_rmc_")) return false;
  // Make sure that the block exit node is all in order: one exit, to the
  // end block.
  // We should maybe have a real error message at some point.
  // We could also maybe do better than this?
  assert(nameMatches(getSingleSuccessor(&block)->getName(),
                     target, "__rmc_end_") &&
         "ERROR: Actions must not contain control flow!");
  return true;
}


void RealizeRMC::processEdge(CallInst *call) {
  // Pull out what the operands have to be.
  // We just assert if something is wrong, which is not great UX.
  bool isVisibility = cast<ConstantInt>(call->getOperand(0))
    ->getValue().getBoolValue();
  RMCEdgeType edgeType = isVisibility ? VisibilityEdge : ExecutionEdge;
  StringRef srcName = getStringArg(call->getOperand(1));
  StringRef dstName = getStringArg(call->getOperand(2));

  // Since multiple blocks can have the same tag, we search for
  // them by name.
  // We could make this more efficient by building maps but I don't think
  // it is going to matter.
  for (auto & srcBB : func_) {
    if (!blockMatches(srcBB, srcName)) continue;
    for (auto & dstBB : func_) {
      if (!blockMatches(dstBB, dstName)) continue;
      Action *src = bb2action_[&srcBB];
      Action *dst = bb2action_[&dstBB];

      // Insert into the graph and the list of edges
      if (edgeType == VisibilityEdge) {
        src->visEdges.insert(dst);
      } else {
        src->execEdges.insert(dst);
      }

      edges_.push_back({edgeType, src, dst});
    }
  }

  // Handle pre and post edges now
  if (srcName == "pre") {
    for (auto & dstBB : func_) {
      if (!blockMatches(dstBB, dstName)) continue;
      Action *dst = bb2action_[&dstBB];
      dst->preEdge = edgeType;
      edges_.push_back({edgeType, nullptr, dst});
    }
  }
  if (dstName == "post") {
    for (auto & srcBB : func_) {
      if (!blockMatches(srcBB, srcName)) continue;
      Action *src = bb2action_[&srcBB];
      src->postEdge = edgeType;
      edges_.push_back({edgeType, src, nullptr});
    }
  }

}

void RealizeRMC::processPush(CallInst *call) {
  Action *a = bb2action_[call->getParent()];
  pushes_.insert(a);
  a->isPush = true;
}

void RealizeRMC::findEdges() {
  for (inst_iterator is = inst_begin(func_), ie = inst_end(func_); is != ie;) {
    // Grab the instruction and advance the iterator at the start, since
    // we might delete the instruction.
    Instruction *i = &*is;
    is++;

    CallInst *call = dyn_cast<CallInst>(i);
    if (!call) continue;
    Function *target = call->getCalledFunction();
    // We look for calls to the bogus functions __rmc_edge_register
    // and __rmc_push, pull out the information about them, and delete
    // the calls.
    if (!target) continue;
    if (target->getName() == "__rmc_edge_register") {
      processEdge(call);
    } else if (target->getName() == "__rmc_push") {
      processPush(call);
    } else {
      continue;
    }

    // Delete the bogus call.
    i->eraseFromParent();
  }
}

void analyzeAction(Action &info) {
  LoadInst *soleLoad = nullptr;
  for (auto & i : *info.bb) {
    if (LoadInst *load = dyn_cast<LoadInst>(&i)) {
      info.loads++;
      soleLoad = load;
    } else if (isa<StoreInst>(i)) {
      info.stores++;
    // What else counts as a call? I'm counting fences I guess.
    } else if (isa<CallInst>(i) || isa<FenceInst>(i)) {
      info.calls++;
    } else if (isa<AtomicCmpXchgInst>(i) || isa<AtomicRMWInst>(i)) {
      info.RMWs++;
    }
  }
  // Try to characterize what this action does.
  // These categories might not be the best.
  if (info.isPush) {
    assert(info.loads+info.stores+info.calls+info.RMWs == 0);
    info.type = ActionPush;
  } else if (info.loads == 1 && info.stores+info.calls+info.RMWs == 0) {
    info.soleLoad = soleLoad;
    info.type = ActionSimpleRead;
  } else if (info.stores >= 1 && info.loads+info.calls+info.RMWs == 0) {
    info.type = ActionSimpleWrites;
  } else if (info.RMWs == 1 && info.stores+info.loads+info.calls == 0) {
    info.type = ActionSimpleRMW;
  } else {
    info.type = ActionComplex;
  }
}

void RealizeRMC::findActions() {
  // First, collect all the basic blocks that are actions
  SmallPtrSet<BasicBlock *, 8> basicBlocks;
  for (auto & block : func_) {
    if (block.getName().startswith("_rmc_")) {
      basicBlocks.insert(&block);
    }
  }

  // Now, make the vector of actions and a mapping from BasicBlock *.
  actions_.reserve(basicBlocks.size());
  for (auto bb : basicBlocks) {
    actions_.emplace_back(bb);
    bb2action_[bb] = &actions_.back();
  }
}

// Check if v1 and v2 are the same where maybe v2 has been
// passed to a bs copy inline asm routine.
// We could make our checking a bit better
bool isSameValueWithBS(Value *v1, Value *v2) {
  if (v1 == v2) return true;
  CallInst *call = dyn_cast<CallInst>(v2);
  if (!call) return false;
  return call->getName() == "__rmc_bs_copy" &&
    call->getOperand(0) == v1;
}

CutStrength RealizeRMC::isPathCut(const RMCEdge &edge,
                                  PathID pathid,
                                  bool enforceSoft,
                                  bool justCheckCtrl) {
  Path path = pc_.extractPath(pathid);
  if (path.size() <= 1) return HardCut;

  bool hasSoftCut = false;
  Instruction *soleLoad = edge.src->soleLoad;

  // Paths are backwards.
  for (auto i = path.begin(), e = path.end(); i != e; i++) {
    bool isFront = i == path.begin(), isBack = i == e-1;
    BasicBlock *bb = *i;

    auto cut_i = cuts_.find(bb);
    if (cut_i != cuts_.end()) {
      const EdgeCut &cut = cut_i->second;
      bool cutHits = !(isFront && cut.isFront) && !(isBack && !cut.isFront);
      // sync and lwsync cuts
      if (cut.type >= CutLwsync && cutHits) {
        return HardCut;
      }
      // ctrlisync cuts
      if (edge.edgeType == ExecutionEdge &&
          cut.type == CutCtrlIsync &&
          cut.read == soleLoad &&
          cutHits) {
        return SoftCut;
      }
    }

    if (isBack) continue;
    // If the destination is a write, and this is an execution edge,
    // and the source is a read, then we can just take advantage of a
    // control dependency to get a soft cut.  Also, if we are just
    // checking to make sure there is a control dep, we don't care
    // about what the dest does..
    if (edge.edgeType == VisibilityEdge || !soleLoad) continue;
    if (!(edge.dst->type == ActionSimpleWrites ||
          edge.dst->type == ActionSimpleRMW ||
          justCheckCtrl)) continue;
    if (hasSoftCut) continue;

    // Look for control dependencies on a read.
    // TODO: we should be able to follow values through phi nodes,
    // since we are path dependent anyways.
    BranchInst *br = dyn_cast<BranchInst>(bb->getTerminator());
    if (!br || !br->isConditional()) continue;
    // TODO: We only check one level of things. Check deeper?
    //if (br->getCondition() == soleLoad) {hasSoftCut = true; continue;}

    // We pretty heavily restrict what operations we handle here.
    // Some would just be wrong (like call), but really icmp is
    // the main one, so. Probably we should be able to also
    // pick through casts and wideness changes.
    ICmpInst *icmp = dyn_cast<ICmpInst>(br->getCondition());
    if (!icmp) continue;
    int idx = 0;
    for (auto v : icmp->operand_values()) {
      if (isSameValueWithBS(soleLoad, v)) {
        hasSoftCut = true;
        break;
      }
      idx++;
    }

    if (!hasSoftCut || !enforceSoft) continue;

    // In order to keep LLVM from optimizing our stuff away we
    // insert a dummy copy of the load and a compiler barrier in the
    // target.
    if (icmp->getOperand(idx) == soleLoad) {
      // Only put in the copy if we haven't done it already.
      Instruction *dummyCopy = makeCopy(soleLoad, icmp);
      icmp->setOperand(idx, dummyCopy);
    }
    // If we were clever we would avoid putting in multiple barriers,
    // but really who cares.
    BasicBlock *next = *(i+1);
    makeBarrier(&next->front());
  }

  return hasSoftCut ? SoftCut : NoCut;
}

CutStrength RealizeRMC::isEdgeCut(const RMCEdge &edge,
                                  bool enforceSoft, bool justCheckCtrl) {
  CutStrength strength = HardCut;
  PathList paths = pc_.findAllSimplePaths(edge.src->bb, edge.dst->bb, true);
  pc_.dumpPaths(paths);
  for (auto & path : paths) {
    CutStrength pathStrength = isPathCut(edge, path,
                                         enforceSoft, justCheckCtrl);
    if (pathStrength < strength) strength = pathStrength;
  }

  return strength;
}

bool RealizeRMC::isCut(const RMCEdge &edge) {
  switch (isEdgeCut(edge)) {
    case HardCut: return true;
    case NoCut: return false;
    case SoftCut:
      if (isEdgeCut(RMCEdge{edge.edgeType, edge.src, edge.src}, false, true)
          > NoCut) {
        isEdgeCut(edge, true, true);
        isEdgeCut(RMCEdge{edge.edgeType, edge.src, edge.src}, true, true);
        return true;
      } else {
        return false;
      }
  }
}


void RealizeRMC::cutPushes() {
  // We just insert pushes wherever we see one, for now.
  // We could also have a notion of push edges derived from
  // the edges to and from a push action.
  for (auto action : pushes_) {
    assert(action->isPush);
    BasicBlock *bb = action->bb;
    makeSync(&bb->front());
    cuts_[bb] = EdgeCut(CutSync, true);
    // XXX: since we can't actually handle cuts on the front and the
    // back and because the sync is the only thing in the block and so
    // cuts at both the front and back, we insert a bogus EdgeCut in
    // the next block.
    cuts_[getSingleSuccessor(bb)] = EdgeCut(CutSync, true);
  }
}

void RealizeRMC::cutPrePostEdges() {
  for (auto & edge : edges_) {
    // TODO: be able to not do dumb shit when there is a push
    if (!edge.src) { // pre
      // Pre edges always need an lwsync
      BasicBlock *bb = edge.dst->bb;
      makeLwsync(&bb->front());
      cuts_[bb] = EdgeCut(CutLwsync, true);
    } else if (!edge.dst) { // post
      Action *a = edge.src;
      BasicBlock *bb = getSingleSuccessor(a->bb);

      // We generate ctrlisync for simple xo reads and lwsync otherwise
      if (edge.edgeType == ExecutionEdge && a->soleLoad) {
        makeCtrlIsync(a->soleLoad, &bb->front());
        cuts_[bb] = EdgeCut(CutCtrlIsync, true, a->soleLoad);
      } else {
        makeLwsync(&bb->front());
        cuts_[bb] = EdgeCut(CutLwsync, true);
      }
    }
  }
}

void RealizeRMC::cutEdge(RMCEdge &edge) {
  if (isCut(edge)) return;

  // As a first pass, we just insert lwsyncs at the start of the destination.
  BasicBlock *bb = edge.dst->bb;
  makeLwsync(&bb->front());
  // XXX: we need to make sure we can't ever fail to track a cut at one side
  // of a block because we inserted one at the other! Argh!
  cuts_[bb] = EdgeCut(CutLwsync, true);
}

void RealizeRMC::cutEdges() {
  // Maybe we should actually use the graph structure we built?
  for (auto & edge : edges_) {
    // Drop pre/post edges, which we needed to handle earlier
    if (!edge.src || !edge.dst) continue;
    cutEdge(edge);
  }
}

void dumpGraph(std::vector<Action> &actions) {
  // Debug spew!!
  for (auto & src : actions) {
    for (auto dst : src.execTransEdges) {
      errs() << "Edge: " << RMCEdge{ExecutionEdge, &src, dst} << "\n";
    }
    for (auto dst : src.visTransEdges) {
      errs() << "Edge: " << RMCEdge{VisibilityEdge, &src, dst} << "\n";
    }
  }
  errs() << "\n";
}

void buildActionGraph(Function &F, std::vector<Action> &actions) {
  // Copy the initial edge specifications into the transitive graph
  for (auto & a : actions) {
    a.visTransEdges.insert(a.visEdges.begin(), a.visEdges.end());
    a.execTransEdges.insert(a.execEdges.begin(), a.execEdges.end());
    // Visibility implies execution.
    a.execTransEdges.insert(a.visEdges.begin(), a.visEdges.end());
  }

  // Now compute the closures
  transitiveClosure(actions, &Action::execTransEdges);
  transitiveClosure(actions, &Action::visTransEdges);

  dumpGraph(actions);
}


bool RealizeRMC::run() {
  findActions();
  findEdges();

  if (actions_.empty() && edges_.empty()) return false;

  errs() << "Stuff to do for: " << func_.getName() << "\n";
  for (auto & edge : edges_) {
    errs() << "Found an edge: " << edge << "\n";
  }

  // Analyze the instructions in actions to see what they do.
  for (auto & action : actions_) {
    analyzeAction(action);
  }

  buildActionGraph(func_, actions_);

  cutPushes();
  cutPrePostEdges();
  cutEdges();

  smtAnalyze();

  return true;
}

// The actual pass. It has a bogus setup return and otherwise
// calls out to RealizeRMC.
class RealizeRMCPass : public FunctionPass {
public:
  static char ID;
  RealizeRMCPass() : FunctionPass(ID) {
    // This is definitely not the right way to do this.
    char *target_cstr = getenv("RMC_PLATFORM");
    std::string target_env = target_cstr ? target_cstr : "";
    if (target_env == "x86") {
      target = TargetX86;
    } else if (target_env == "arm") {
      target = TargetARM;
    } else {
      assert(false && "not given a supported target");
    }
  }
  ~RealizeRMCPass() { }
  virtual bool runOnFunction(Function &F) {
    RealizeRMC rmc(F);
    return rmc.run();
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequiredID(BreakCriticalEdgesID);
    AU.setPreservesCFG();
  }
};


///////////////////////////////////////////////////////////////////////////
// SMT stuff

#if USE_Z3_OPTIMIZER
typedef z3::optimize solver;
#else
typedef z3::solver solver;
#endif

// Z3 utility functions
z3::expr boolToInt(z3::expr const &e) {
  z3::context &c = e.ctx();
  return ite(e, c.int_val(1), c.int_val(0));
}

bool extractBool(z3::expr const &e) {
  auto b = Z3_get_bool_value(e.ctx(), e);
  assert(b != Z3_L_UNDEF);
  return b == Z3_L_TRUE;
}
int extractInt(z3::expr const &e) {
  int i;
  auto b = Z3_get_numeral_int(e.ctx(), e, &i);
  assert(b == Z3_TRUE);
  return i;
}

// Code for optimizing
// FIXME: should we try to use some sort of bigint?
typedef __uint64 Cost;

bool isCostUnder(solver &s, z3::expr &costVar, Cost cost) {
  s.push();
  s.add(costVar <= s.ctx().int_val(cost));
  z3::check_result result = s.check();
  assert(result != z3::unknown);
  s.pop();

  bool isSat = result == z3::sat;
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
  Cost lo = 1, hi = 1;

  // Search upwards to find some value for which pred(c) holds.
  while (!pred(hi)) {
    lo = hi;
    hi *= 2;
    assert(hi != 0); // fail if we overflow
  }

  return findFirstTrue(pred, lo+1, hi);
}

// Given a solver and an expression, find a solution that minimizes
// the expression..
void handMinimize(solver &s, z3::expr &costVar) {
  auto costPred = [&] (Cost cost) { return isCostUnder(s, costVar, cost); };
  Cost minCost;
  if (kGuessUpperBound) {
    // This is in theory arbitrarily worse but might be better in
    // practice. Although the cost is bounded by the number of things
    // we could do, so...
    s.check();
    Cost upperBound = extractInt(s.get_model().eval(costVar));
    errs() << "Upper bound: " << upperBound << "\n";
    // The solver seems to often "just happen" to find the optimal
    // solution, so maybe do a quick check on upperBound-1
    if (kCheckFirstGuess && upperBound > 0 && !costPred(--upperBound)) {
      minCost = upperBound + 1;
    } else {
      minCost = findFirstTrue(costPred, 1, upperBound);
    }
  } else {
    minCost = findFirstTrue(costPred);
  }
  s.add(costVar == s.ctx().int_val(minCost));
}


// DenseMap can use pairs of keys as keys, so we represent edges as a
// pair of BasicBlock*s. We represent paths as PathIDs.

// Is it worth having our own mapping? Is z3 going to be doing a bunch
// of string lookups anyways? Dunno.
typedef std::pair<BasicBlock *, BasicBlock *> EdgeKey;
typedef PathID PathKey;
EdgeKey makeEdgeKey(BasicBlock *src, BasicBlock *dst) {
  return std::make_pair(src, dst);
}
PathKey makePathKey(PathID path) {
  return path;
}

std::string makeVarString(EdgeKey &key) {
  std::ostringstream buffer;
  buffer << "(" << key.first->getName().str()
         << ", " << key.second->getName().str() << ")";
  return buffer.str();
}
std::string makeVarString(PathKey &key) {
  std::ostringstream buffer;
  buffer << "(path #" << key << ")";
  return buffer.str();
}
std::string makeVarString(BasicBlock *key) {
  std::ostringstream buffer;
  buffer << "(" << key->getName().str() << ")";
  return buffer.str();
}

template<typename Key> struct DeclMap {
  DeclMap(z3::sort isort, const char *iname) : sort(isort), name(iname) {}
  DenseMap<Key, z3::expr> map;
  z3::sort sort;
  const char *name;
};

template<typename Key>
z3::expr getFunc(DeclMap<Key> &map, Key key, bool *alreadyThere = nullptr) {
  auto entry = map.map.find(key);
  if (entry != map.map.end()) {
    if (alreadyThere) *alreadyThere = true;
    return entry->second;
  }
  if (alreadyThere) *alreadyThere = false;

  z3::context &c = map.sort.ctx();
  std::string name = map.name + makeVarString(key);
  z3::expr e = c.constant(name.c_str(), map.sort);
  // Can use inverted boolean variables to help test optimization.
  if (kInvertBools && map.sort.is_bool()) e = !e;

  map.map.insert(std::make_pair(key, e));

  return e;
}
z3::expr getEdgeFunc(DeclMap<EdgeKey> &map,
                     BasicBlock *src, BasicBlock *dst,
                     bool *alreadyThere = nullptr) {
  return getFunc(map, makeEdgeKey(src, dst), alreadyThere);
}
z3::expr getPathFunc(DeclMap<PathKey> &map,
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
  z3::context c;
  solver s(c);

  DeclMap<BasicBlock *> nodeCapM(c.int_sort(), "node_cap");
  DeclMap<EdgeKey> edgeCapM(c.int_sort(), "edge_cap");

  //// Build the equations.
  for (auto & block : F) {
    z3::expr nodeCap = getFunc(nodeCapM, &block);

    // Compute the node's incoming capacity
    z3::expr incomingCap = c.int_val(0);
    for (auto i = pred_begin(&block), e = pred_end(&block); i != e; i++) {
      incomingCap = incomingCap + getEdgeFunc(edgeCapM, *i, &block);
    }
    if (&block != &F.getEntryBlock()) {
      s.add(nodeCap == incomingCap.simplify());
    }

    // Setup equations for outgoing edges
    auto i = succ_begin(&block), e = succ_end(&block);
    int childCount = e - i;
    for (; i != e; i++) {
      // For now, we assume even probabilities.
      // Would be an improvement to do better
      z3::expr edgeCap = getEdgeFunc(edgeCapM, &block, *i);
      // We want: c(v, u) == Pr(v, u) * c(v). Since Pr(v, u) ~= 1/n, we do
      // n * c(v, u) == c(v)
      s.add(edgeCap * childCount == nodeCap);
    }
  }

  // Keep all zeros from working:
  s.add(getFunc(nodeCapM, &F.getEntryBlock()) > 0);

  //// Extract a solution.
  // XXX: will this get returned efficiently?
  DenseMap<EdgeKey, int> caps;

  s.check();
  z3::model model = s.get_model();
  for (auto & entry : edgeCapM.map) {
    EdgeKey edge = entry.first;
    z3::expr cst = entry.second;
    int cap = extractInt(model.eval(cst));
    caps.insert(std::make_pair(edge, cap));
  }

  return caps;
}


struct VarMaps {
  PathCache &pc;
  DeclMap<EdgeKey> lwsync;
  DeclMap<EdgeKey> vcut;
  DeclMap<PathKey> pathVcut;
};

// Generalized it.
typedef std::function<z3::expr (PathID path)> PathFunc;

typedef std::function<z3::expr (PathID path, bool *already)> GetPathVarFunc;
typedef std::function<z3::expr (BasicBlock *src, BasicBlock *dst, PathID rest)>
  EdgeFunc;

z3::expr forAllPaths(solver &s, VarMaps &m,
                     BasicBlock *src, BasicBlock *dst, PathFunc func) {
  // Now try all the paths
  z3::expr allPaths = s.ctx().bool_val(true);
  PathList paths = m.pc.findAllSimplePaths(src, dst);
  for (auto & path : paths) {
    allPaths = allPaths && func(path);
  }
  return allPaths.simplify();
}

z3::expr forAllPathEdges(solver &s, VarMaps &m,
                         PathID path,
                         GetPathVarFunc getVar,
                         EdgeFunc func) {
  z3::context &c = s.ctx();

  PathID rest;
  if (m.pc.isEmpty(path) || m.pc.isEmpty(rest = m.pc.getTail(path))) {
    return c.bool_val(false);
  }

  bool alreadyMade;
  z3::expr isCut = getVar(path, &alreadyMade);
  if (alreadyMade) return isCut;

  BasicBlock *src = m.pc.getHead(path), *dst = m.pc.getHead(rest);
  z3::expr somethingCut = getEdgeFunc(m.lwsync, src, dst) ||
    forAllPathEdges(s, m, rest, getVar, func);
  s.add(isCut == somethingCut.simplify());

  return isCut;
}


//// Real stuff now.
z3::expr makePathVcut(solver &s, VarMaps &m,
                      PathID path) {
  return forAllPathEdges(
    s, m, path,
    [&] (PathID path, bool *b) { return getPathFunc(m.pathVcut, path, b); },
    [&] (BasicBlock *src, BasicBlock *dst, PathID path) {
      return getEdgeFunc(m.lwsync, src, dst);
    });
}

void RealizeRMC::smtAnalyze() {
  z3::context c;
  solver s(c);

  VarMaps m = {
    pc_,
    DeclMap<EdgeKey>(c.bool_sort(), "lwsync"),
    DeclMap<EdgeKey>(c.bool_sort(), "vcut"),
    DeclMap<PathKey>(c.bool_sort(), "path_vcut"),
  };

  // Compute the capacity function
  DenseMap<EdgeKey, int> edgeCap = computeCapacities(func_);

  //////////
  // HOK. Make sure everything is cut. Just lwsync for now.
  for (auto & src : actions_) {
    for (auto dst : src.execTransEdges) {
      z3::expr isCut = getEdgeFunc(m.vcut, src.bb, dst->bb);
      s.add(isCut);

      z3::expr allPathsCut = forAllPaths(
        s, m, src.bb, dst->bb,
        [&] (PathID path) { return makePathVcut(s, m, path); });
      s.add(isCut == allPathsCut);
    }
  }

  //////////
  // OK, now build a cost function. This will probably take a lot of
  // tuning.
  z3::expr costVar = c.int_const("cost");
  z3::expr cost = c.int_val(0);
  for (auto & block : func_) {
    BasicBlock *src = &block;
    for (auto i = succ_begin(src), e = succ_end(src); i != e; i++) {
      BasicBlock *dst = *i;
      cost = cost +
        (boolToInt(getEdgeFunc(m.lwsync, src, dst)) *
         edgeCap[makeEdgeKey(src, dst)]);
    }
  }
  s.add(costVar == cost.simplify());

  //////////
  // Print out the model for debugging
  std::cout << "Built a thing: \n" << s << "\n\n";

  // Optimize the cost. There are a number of possible approaches to
  // this.
#if USE_Z3_OPTIMIZER
  s.minimize(costVar);
#else
  handMinimize(s, costVar);
#endif

  // OK, go solve it.
  s.check();

  //////////
  // Output the model
  z3::model model = s.get_model();
  // traversing the model
  for (unsigned i = 0; i < model.size(); i++) {
    z3::func_decl v = model[i];
    // this problem contains only constants
    assert(v.arity() == 0);
    std::cout << v.name() << " = " << model.get_const_interp(v) << "\n";
  }

  // Find the lwsyncs we are inserting
  errs() << "\nLwsyncs to insert:\n";
  for (auto & entry : m.lwsync.map) {
    EdgeKey edge = entry.first;
    z3::expr cst = entry.second;
    bool val = extractBool(model.eval(cst));
    if (val) {
      errs() << edge.first->getName() << " -> "
             << edge.second->getName() << "\n";
    }
  }
  errs() << "\n";
}

char RealizeRMCPass::ID = 0;
RegisterPass<RealizeRMCPass> X("realize-rmc", "Compile RMC annotations");
