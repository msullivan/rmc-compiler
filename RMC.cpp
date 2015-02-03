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
#include <llvm/ADT/iterator_range.h>

#include <llvm/IR/Dominators.h>
#include <llvm/Analysis/LoopInfo.h>


#include <llvm/Support/raw_ostream.h>

#include <ostream>
#include <fstream>
#include <sstream>
#include <iostream>
#include <map>
#include <set>

using namespace llvm;

///////
// Printing functions I wish didn't have to be here
namespace llvm {

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

}

// Compute the transitive closure of the action grap
template <typename Range>
void transitiveClosure(Range &actions,
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

Instruction *getNextInstr(Instruction *i) {
  BasicBlock::iterator I = *i;
  return ++I == i->getParent()->end() ? nullptr : &*I;
}
Instruction *getPrevInstr(Instruction *i) {
  BasicBlock::iterator I = *i;
  return I == i->getParent()->begin() ? nullptr : &*--I;
}

// Code to detect our inline asm things
bool isInstrInlineAsm(Instruction *i, const char *string) {
  if (CallInst *call = dyn_cast_or_null<CallInst>(i)) {
    if (InlineAsm *iasm = dyn_cast<InlineAsm>(call->getCalledValue())) {
      if (iasm->getAsmString().find(string) != std::string::npos) {
        return true;
      }
    }
  }
  return false;
}
bool isInstrIsync(Instruction *i) { return isInstrInlineAsm(i, " isync #"); }
bool isInstrBarrier(Instruction *i) { return isInstrInlineAsm(i, " barrier #");}

///////////////////////////////////////////////////////////////////////////
//// Actual code for the pass

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

Action *RealizeRMC::makePrePostAction(BasicBlock *bb) {
  actions_.emplace_back(bb);
  Action *a = &actions_.back();
  // This will trip if something is pre/post'd more than once
  assert(!bb2action_[bb]);
  bb2action_[bb] = a;
  a->type = ActionPrePost;
  return a;
}

void registerEdge(std::vector<RMCEdge> &edges,
                  RMCEdgeType edgeType,
                  Action *src, Action *dst) {
  // Insert into the graph and the list of edges
  if (edgeType == VisibilityEdge) {
    src->visEdges.insert(dst);
  } else {
    src->execEdges.insert(dst);
  }
  edges.push_back({edgeType, src, dst});
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
      registerEdge(edges_, edgeType, src, dst);
    }
  }

  // Handle pre and post edges now
  if (srcName == "pre") {
    for (auto & dstBB : func_) {
      if (!blockMatches(dstBB, dstName)) continue;
      Action *src = makePrePostAction(dstBB.getSinglePredecessor());
      Action *dst = bb2action_[&dstBB];
      registerEdge(edges_, edgeType, src, dst);
    }
  }
  if (dstName == "post") {
    for (auto & srcBB : func_) {
      if (!blockMatches(srcBB, srcName)) continue;
      Action *src = bb2action_[&srcBB];
      Action *dst = makePrePostAction(getSingleSuccessor(&srcBB));
      registerEdge(edges_, edgeType, src, dst);
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
    Instruction *i = &*is++;

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

    // Delete the bogus call. There might be uses if we didn't mem2reg.
    BasicBlock::iterator ii(i);
    ReplaceInstWithValue(i->getParent()->getInstList(),
                         ii, ConstantInt::get(i->getType(), 0));
  }
}

void analyzeAction(Action &info) {
  // Don't analyze the dummy pre/post actions!
  if (info.type == ActionPrePost) return;

  LoadInst *soleLoad = nullptr;
  for (auto & i : *info.bb) {
    if (LoadInst *load = dyn_cast<LoadInst>(&i)) {
      ++info.loads;
      soleLoad = load;
    } else if (isa<StoreInst>(i)) {
      ++info.stores;
    // What else counts as a call? I'm counting fences I guess.
    } else if (isa<CallInst>(i) || isa<FenceInst>(i)) {
      ++info.calls;
    } else if (isa<AtomicCmpXchgInst>(i) || isa<AtomicRMWInst>(i)) {
      ++info.RMWs;
    }
  }
  // Try to characterize what this action does.
  // These categories might not be the best.
  if (info.isPush) {
    // shouldn't do anything else; but might be a store if we didn't mem2reg
    assert(info.loads+info.calls+info.RMWs == 0 && info.stores <= 1);
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
  // We, somewhat tastelessly, reserve space for 3x the number of
  // actions we actually have so that we have space for new pre/post
  // actions that we might need to dummy up.
  // We need to have all the space reserved in advance so that our
  // pointers don't get invalidated when a resize happens.
  actions_.reserve(3 * basicBlocks.size());
  numNormalActions_ = basicBlocks.size();
  for (auto bb : basicBlocks) {
    actions_.emplace_back(bb);
    bb2action_[bb] = &actions_.back();
  }
}


// Fix up an action so that loads and stores that happen in it won't
// get messed up by LLVM too much. I'm not really sure how to make
// this notion particularly precise. Right now we mark loads and
// stores as Monotonic, which I /think/ is what we need. (It is at least,
// by spec, good enough to squash the bug that made me *definitely*
// need something like this, which was a program that looped on a read
// having the read hoisted out of the loop.)
// We could also try setting volatile also/instead?
void fixupAccessesAction(Action &info) {
  if (info.type == ActionPrePost) return;

  for (auto & i : *info.bb) {
    if (LoadInst *load = dyn_cast<LoadInst>(&i)) {
      load->setAtomic(Monotonic);
    } else if (StoreInst *store = dyn_cast<StoreInst>(&i)) {
      store->setAtomic(Monotonic);
    }
    // XXX: Handle other things?? RMWs??
  }
}

// Return the CallInst if the value is a bs copy, otherwise null
CallInst *getBSCopy(Value *v) {
  CallInst *call = dyn_cast<CallInst>(v);
  if (!call) return nullptr;
  // This is kind of dubious
  return call->getName().startswith("__rmc_bs_copy") ? call : nullptr;
}

// Look through a possible bs copy to find the real underlying value
Value *getRealValue(Value *v) {
  CallInst *copy = getBSCopy(v);
  return copy ? copy->getOperand(0) : v;
}

BasicBlock *getPathPred(PathCache *cache, PathID path, BasicBlock *block) {
  // XXX: is this the right way to handle self loops?
  //errs() << "looking for " << block->getName() << "\n";
  if (!cache) return nullptr;
  BasicBlock *pred = nullptr;
  while (!cache->isEmpty(path)) {
    BasicBlock *b = cache->getHead(path);
    //errs() << "looking at " << b->getName() << "\n";
    if (pred && b == block) return pred;
    pred = b;
    path = cache->getTail(path);
  }
  return nullptr;
}


// FIXME: reorganize the namespace stuff?. Or put this in the class.
namespace llvm {

// Look for control dependencies on a read.
bool branchesOn(BasicBlock *bb, Value *load,
                ICmpInst **icmpOut, int *outIdx) {
  // TODO: we should be able to follow values through phi nodes,
  // since we are path dependent anyways.
  BranchInst *br = dyn_cast<BranchInst>(bb->getTerminator());
  if (!br || !br->isConditional()) return false;
  // TODO: We only check one level of things. Check deeper?

  // We pretty heavily restrict what operations we handle here.
  // Some would just be wrong (like call), but really icmp is
  // the main one, so. Probably we should be able to also
  // pick through casts and wideness changes.
  ICmpInst *icmp = dyn_cast<ICmpInst>(br->getCondition());
  if (!icmp) return false;
  int idx = 0;
  for (auto v : icmp->operand_values()) {
    if (getRealValue(v) == load) {
      if (icmpOut) *icmpOut = icmp;
      if (outIdx) *outIdx = idx;
      return true;
    }
    ++idx;
  }

  return false;
}

// Look for address dependencies on a read.
bool addrDepsOnSearch(Value *pointer, Value *load,
                      PathCache *cache, PathID path) {
  if (pointer == load) {
    return true;
  } else if (getBSCopy(pointer)) {
    return addrDepsOnSearch(getRealValue(pointer), load, cache, path);
  }
  Instruction *instr = dyn_cast<Instruction>(pointer);
  if (!instr) return false;

  // We trace through GEPs and BitCasts.
  // I wish we weren't recursive. Maybe we should restrict how we
  // trace through GEPs?
  // TODO: less heavily restrict what we use?
  if (isa<GetElementPtrInst>(instr) || isa<BitCastInst>(instr)) {
    for (auto v : instr->operand_values()) {
      if (addrDepsOnSearch(v, load, cache, path)) {
        return true;
      }
    }
  }
  // Trace through PHI nodes also, using any information about the
  // path we took to get here
  if (PHINode *phi = dyn_cast<PHINode>(instr)) {
    if (BasicBlock *pred = getPathPred(cache, path, phi->getParent())) {
      return addrDepsOnSearch(phi->getIncomingValueForBlock(pred),
                              load, cache, path);
    }
  }


  return false;
}

// Look for address dependencies on a read.
bool addrDepsOn(Instruction *instr, Value *load,
                PathCache *cache, PathID path) {
  Value *pointer = instr->getOperand(0);
  return addrDepsOnSearch(pointer, load, cache, path);
}

}

// Rewrite an instruction so that an operand is a dummy copy to
// hide information from llvm's optimizer
void hideOperand(Instruction *instr, int i) {
  Value *v = instr->getOperand(i);
  // Only put in the copy if we haven't done it already.
  if (!getBSCopy(v)) {
    Instruction *dummyCopy = makeCopy(v, instr);
    instr->setOperand(i, dummyCopy);
  }
}
void hideOperands(Instruction *instr) {
  for (int i = 0; i < instr->getNumOperands(); i++) {
    hideOperand(instr, i);
  }
}

void enforceBranchOn(BasicBlock *next, ICmpInst *icmp, int idx) {
  // In order to keep LLVM from optimizing our stuff away we
  // insert dummy copies of the operands and a compiler barrier in the
  // target.
  hideOperands(icmp);
  if (!isInstrBarrier(&next->front())) makeBarrier(&next->front());
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
  for (auto i = path.begin(), e = path.end(); i != e; ++i) {
    bool isFront = i == path.begin(), isBack = i == e-1;
    BasicBlock *bb = *i;

    auto cut_i = cuts_.find(bb);
    if (cut_i != cuts_.end()) {
      const BlockCut &cut = cut_i->second;
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

    // Is there a branch on the load?
    int idx;
    ICmpInst *icmp;
    hasSoftCut = branchesOn(bb, soleLoad, &icmp, &idx);

    if (hasSoftCut && enforceSoft) {
      BasicBlock *next = *(i+1);
      enforceBranchOn(next, icmp, idx);
    }
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
    case NoCut:
      // Try a data cut
      // See if we have a data dep in a very basic way.
      // FIXME: Should be able to handle writes also!
      // FIXME: Do we need to enforce?
      // We need to make sure we have a cut from src->src but it *isn't*
      // enough to just check ctrl!
      if (edge.src->soleLoad && edge.dst->soleLoad &&
          addrDepsOn(edge.dst->soleLoad, edge.src->soleLoad) &&
          isEdgeCut(RMCEdge{edge.edgeType, edge.src, edge.src}, false, false)
          > NoCut) {
        isEdgeCut(RMCEdge{edge.edgeType, edge.src, edge.src}, true, false);
        return true;
      } else {
        return false;
      }
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
    cuts_[bb] = BlockCut(CutSync, true);
    // XXX: since we can't actually handle cuts on the front and the
    // back and because the sync is the only thing in the block and so
    // cuts at both the front and back, we insert a bogus BlockCut in
    // the next block.
    cuts_[getSingleSuccessor(bb)] = BlockCut(CutSync, true);
  }
}

void RealizeRMC::cutEdge(RMCEdge &edge) {
  if (isCut(edge)) return;

  // As a first pass, we just insert lwsyncs at the start of the destination.
  BasicBlock *bb = edge.dst->bb;
  makeLwsync(&bb->front());
  // XXX: we need to make sure we can't ever fail to track a cut at one side
  // of a block because we inserted one at the other! Argh!
  cuts_[bb] = BlockCut(CutLwsync, true);
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

void buildActionGraph(std::vector<Action> &actions, int numReal) {
  // Copy the initial edge specifications into the transitive graph
  for (auto & a : actions) {
    a.visTransEdges.insert(a.visEdges.begin(), a.visEdges.end());
    a.execTransEdges.insert(a.execEdges.begin(), a.execEdges.end());
    // Visibility implies execution.
    a.execTransEdges.insert(a.visEdges.begin(), a.visEdges.end());
  }

  // Now compute the closures. We only compute the closure for the non
  // pre/post edges.
  auto realActions = make_range(actions.begin(), actions.begin() + numReal);
  transitiveClosure(realActions, &Action::execTransEdges);
  transitiveClosure(realActions, &Action::visTransEdges);

  //dumpGraph(actions);
}

void removeUselessEdges(std::vector<Action> &actions) {
  for (auto & src : actions) {
    ActionType st = src.type;

    // Move the set out and filter by building a new one to avoid
    // iterator invalidation woes.
    auto newExec = std::move(src.execTransEdges);
    src.execTransEdges.clear();
    for (Action *dst : newExec) {
      ActionType dt = dst->type;
      if (!(st == ActionPush || dt == ActionPush ||
            (st == ActionSimpleWrites && dt == ActionSimpleRead) ||
            (st == ActionSimpleWrites && dt == ActionSimpleWrites))) {
        src.execTransEdges.insert(dst);
      }
    }

    auto newVis = std::move(src.visTransEdges);
    src.visTransEdges.clear();
    for (Action *dst : newVis) {
      ActionType dt = dst->type;
      if (!(st == ActionPush || dt == ActionPush ||
            /* R->R has same force as execution, and we made execution
             * versions of all the vis edges. */
            (st == ActionSimpleRead && dt == ActionSimpleRead) ||
            (st == ActionSimpleWrites && dt == ActionSimpleRead))) {
        src.visTransEdges.insert(dst);
      }
    }
  }
}

// Find the instruction to insert a cut in front of.
// This is either the last instruction of the source or the first
// instruction of the destination, depending on whether the source has
// multiple outgoing edges. Because we broke critical edges, we can
// not have that there are multiple incoming to dst and multiple
// outgoing from source.
//
// If the cut is a control cut, we do some fairly bogus checking to
// see if the last instruction is an isync so that we can make sure we
// don't much up the ordering.
// This relies on isync's being processed before ctrls :/
Instruction *getCutInstr(const EdgeCut &cut) {
  TerminatorInst *term = cut.src->getTerminator();
  if (term->getNumSuccessors() > 1) return &cut.dst->front();
  if (cut.type == CutCtrl && isInstrIsync(getPrevInstr(term))) {
    return getPrevInstr(term);
  }
  return term;
}

void RealizeRMC::insertCut(const EdgeCut &cut) {
  errs() << cut.type << ": "
         << cut.src->getName() << " -> " << cut.dst->getName() << "\n";

  switch (cut.type) {
  case CutLwsync:
    // FIXME: it would be nice if we were clever enough to notice when
    // every edge out of a block as the same cut and merge them.
    makeLwsync(getCutInstr(cut));
    break;
  case CutIsync:
    makeIsync(getCutInstr(cut));
    break;
  case CutCtrl:
  {
    int idx; ICmpInst *icmp;
    bool branches = branchesOn(cut.src, cut.read, &icmp, &idx);
    if (branches) {
      enforceBranchOn(cut.dst, icmp, idx);
    } else {
      makeCtrl(cut.read, getCutInstr(cut));
    }
    break;
  }
  default:
    assert(false && "Unimplemented insertCut case");
  }

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
  // Also change their memory accesses to have an atomic ordering.
  for (auto & action : actions_) {
    analyzeAction(action);
    fixupAccessesAction(action);
  }

  buildActionGraph(actions_, numNormalActions_);

  cutPushes();

  if (!useSMT_) {
    cutEdges();
  } else {
    removeUselessEdges(actions_);
    auto cuts = smtAnalyze();
    errs() << "Applying SMT results:\n";
    for (auto & cut : cuts) {
      insertCut(cut);
    }
  }

  return true;
}

// The actual pass. It has a bogus setup routine and otherwise
// calls out to RealizeRMC.
class RealizeRMCPass : public FunctionPass {
private:
  bool useSMT_;

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

    // OK, do we want to use SMT?
    useSMT_ = getenv("RMC_USE_SMT") != nullptr;
  }
  ~RealizeRMCPass() { }

  virtual bool doInitialization(Module &M) {
    // This is a bogus hack that we use to suppress testing all but a
    // particular function; this is purely for debugging purposes
    char *only_test = getenv("RMC_ONLY_TEST");
    if (!only_test) return false;
    for (auto i = M.begin(), e = M.end(); i != e;) {
      Function *F = &*i++;
      if (F->getName() != only_test) {
        F->deleteBody();
        // Remove instead of erase can leak, but there might be
        // references and this is just a debugging hack anyways.
        F->removeFromParent();
      }
    }
    return true;
  }
  virtual bool runOnFunction(Function &F) {
    DominatorTree &dom = getAnalysis<DominatorTreeWrapperPass>().getDomTree();
    LoopInfo &li = getAnalysis<LoopInfo>();
    RealizeRMC rmc(F, dom, li, useSMT_);
    return rmc.run();
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequiredID(BreakCriticalEdgesID);
    AU.addRequired<DominatorTreeWrapperPass>();
    AU.addRequired<LoopInfo>();
    AU.setPreservesCFG();
  }
};


char RealizeRMCPass::ID = 0;
RegisterPass<RealizeRMCPass> X("realize-rmc", "Compile RMC annotations");
