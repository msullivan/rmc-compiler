// Copyright (c) 2014-2017 Michael J. Sullivan
// Use of this source code is governed by an MIT-style license that can be
// found in the LICENSE file.

// BUG: this whole thing depends on the specifics of how the clang version I
// am using emits llvm bitcode for the hacky RMC protocol.

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

#include <llvm/Support/CommandLine.h>

#include <llvm/Transforms/IPO/PassManagerBuilder.h>
#include <llvm/IR/LegacyPassManager.h>

#include <ostream>
#include <fstream>
#include <sstream>
#include <iostream>
#include <map>
#include <set>

#undef NDEBUG
#include <assert.h>

using namespace llvm;

static void rmc_error() {
  exit(1);
}

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
  case PushEdge:
    os << "p";
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

// Compute the transitive closure of the action graph
template <typename Range, typename F>
void transitiveClosure(Range &actions,
                       RMCEdgeType type,
                       F merge) {
  // Use Warshall's algorithm to compute the transitive closure. More
  // or less. I can probably be a little more clever since I have
  // adjacency lists, right? But n is small and this is really easy.
  for (auto & k : actions) {
    for (auto & i : actions) {
      for (auto & j : actions) {
        // OK, now we need to transitively join all of the ki and ij
        // edges by merging the bind sites of each combination.
        // (Although probably there is only one of each.)
        // If there *aren't* both ik and kj edges, we skip the inner
        // loop to avoid empty entries being automagically created.
        if (!(i.transEdges[type].count(&k) &&
              k.transEdges[type].count(&j))) continue;

        for (BasicBlock *bind_ik : i.transEdges[type][&k]) {
          for (BasicBlock *bind_kj : k.transEdges[type][&j]) {
            BasicBlock *bind_ij = merge(bind_ik, bind_kj);
            i.transEdges[type][&j].insert(bind_ij);
          }
        }

      }
    }
  }
}

///////////////////////////////////////////////////////////////////////////

// It is sort of bogus to make this global.
RMCTarget target;

bool isARM(RMCTarget target) {
  return target == TargetARM || target == TargetARMv8;
}

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
  if (isARM(target)) {
    a = makeAsm(f_ty, "dmb ish // sync", "~{memory}", true);
  } else if (target == TargetPOWER) {
    a = makeAsm(f_ty, "sync # sync", "~{memory}", true);
  } else if (target == TargetX86) {
    a = makeAsm(f_ty, "mfence # sync", "~{memory}", true);
  }
  return CallInst::Create(a, None, "", to_precede);
}
Instruction *makeLwsync(Instruction *to_precede) {
  LLVMContext &C = to_precede->getContext();
  FunctionType *f_ty = FunctionType::get(FunctionType::getVoidTy(C), false);
  InlineAsm *a = nullptr;
  if (isARM(target)) {
    a = makeAsm(f_ty, "dmb ish // lwsync", "~{memory}", true);
  } else if (target == TargetPOWER) {
    a = makeAsm(f_ty, "lwsync # lwsync", "~{memory}", true);
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
  if (isARM(target)) {
    a = makeAsm(f_ty, "isb // isync", "~{memory}", true);
  } else if (target == TargetPOWER) {
    a = makeAsm(f_ty, "isync # isync", "~{memory}", true);
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
  if (isARM(target)) {
    a = makeAsm(f_ty, "cmp $0, $0;beq 1f;1: // ctrl", "r,~{memory}", true);
  } else if (target == TargetPOWER) {
    a = makeAsm(f_ty, "cmpw $0, $0;beq 1f;1: # ctrl", "r,~{memory}", true);
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
  Value *getRealValue(Value *v);
  FunctionType *f_ty = FunctionType::get(v->getType(), v->getType(), false);
  InlineAsm *a = makeAsm(f_ty, "# copy", "=r,0", false); /* false?? */
  return CallInst::Create(a, v,
                          getRealValue(v)->getName() + ".__rmc_bs_copy",
                          to_precede);
}
// We also need to add a thing for fake data deps, which is more annoying.

///////////////////////////////////////////////////////////////////////////
//// Some annoying LLVM version specific stuff

#if LLVM_VERSION_MAJOR == 3 &&                          \
  (LLVM_VERSION_MINOR >= 5 && LLVM_VERSION_MINOR <= 6)
// Some annoying changes with how LoopInfo interacts with the pass manager
#define LOOPINFO_PASS_NAME LoopInfo
LoopInfo &getLoopInfo(const Pass &pass) {
  return pass.getAnalysis<LoopInfo>();
}
// And the signature of SplitBlock changed...
BasicBlock *RealizeRMC::splitBlock(BasicBlock *Old, Instruction *SplitPt) {
  return llvm::SplitBlock(Old, SplitPt, underlyingPass_);
}

#elif LLVM_VERSION_MAJOR == 3 &&                        \
  (LLVM_VERSION_MINOR >= 7 && LLVM_VERSION_MINOR <= 9)

#define LOOPINFO_PASS_NAME LoopInfoWrapperPass
LoopInfo &getLoopInfo(const Pass &pass) {
  return pass.getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
}

BasicBlock *RealizeRMC::splitBlock(BasicBlock *Old, Instruction *SplitPt) {
  return llvm::SplitBlock(Old, SplitPt, &domTree_, &loopInfo_);
}

#else
#error Unsupported LLVM version
#endif




///////////////////////////////////////////////////////////////////////////
//// Code to pull random crap out of LLVM functions

StringRef getStringArg(Value *v) {
  const Value *g = cast<Constant>(v)->stripPointerCasts();
  const Constant *ptr = cast<GlobalVariable>(g)->getInitializer();
  StringRef str = cast<ConstantDataSequential>(ptr)->getAsString();
  // Drop the '\0' at the end
  return str.drop_back();
}

Instruction *getNextInstr(Instruction *i) {
  BasicBlock::iterator I(*i);
  return ++I == i->getParent()->end() ? nullptr : &*I;
}

Instruction *getNextInsertionPt(Instruction *i) {
  BasicBlock::iterator I(*i);


  // Sometimes we need to insert something immediately after an invoke
  // instruction. In that case we insert it into the normal
  // destination block. Since we split critical edges, that can't have
  // any predecessors.
  if (InvokeInst *invoke = dyn_cast<InvokeInst>(i)) {
    I = invoke->getNormalDest()->getFirstInsertionPt();
  } else {
    assert(!i->isTerminator() && "Can't insert after non-invoke terminators!");
    ++I;
  }

  while (isa<LandingPadInst>(I) || isa<PHINode>(I)) ++I;
  return &*I;
}

Instruction *getPrevInstr(Instruction *i) {
  BasicBlock::iterator I(*i);
  return I == i->getParent()->begin() ? nullptr : &*--I;
}

// Sigh. LLVM 3.7 has a method inside BasicBlock for this, but
// earlier ones don't.
BasicBlock *getSingleSuccessor(BasicBlock *bb) {
  TerminatorInst *term = bb->getTerminator();
  return term->getNumSuccessors() == 1 ? term->getSuccessor(0) : nullptr;
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

void deleteRegisterCall(Instruction *i) {
  // Delete a bogus registration call. There might be uses if we didn't mem2reg.
  BasicBlock::iterator ii(i);
  ReplaceInstWithValue(i->getParent()->getInstList(),
                       ii, ConstantInt::get(i->getType(), 0));
}


///////////////////////////////////////////////////////////////////////////
//// Actual code for the pass

////////////// RMC analysis routines

Action *RealizeRMC::makePrePostAction(BasicBlock *bb) {
  if (bb2action_[bb]) {
    assert(bb2action_[bb]->type == ActionPrePost);
    return bb2action_[bb];
  }

  actions_.emplace_back(bb, bb);
  Action *a = &actions_.back();
  bb2action_[bb] = a;
  a->type = ActionPrePost;
  return a;
}

void registerEdge(std::vector<RMCEdge> &edges,
                  RMCEdgeType edgeType,
                  BasicBlock *bindSite,
                  Action *src, Action *dst) {
  // Insert into the graph and the list of edges
  src->edges[edgeType][dst].insert(bindSite);
  edges.push_back({edgeType, src, dst, bindSite});
}

TinyPtrVector<Action *> RealizeRMC::collectEdges(StringRef name) {
  TinyPtrVector<Action *> matches;
  if (name == "pre" || name == "post") return matches;

  // Since multiple blocks can have the same tag, we search for
  // them by name.
  // We could make this more efficient by building maps but I don't think
  // it is going to matter.
  for (auto & a : actions_) {
    if (a.name == name) {
      matches.push_back(&a);
    }
  }

  if (matches.size() == 0) {
    errs() << "Error: use of nonexistent label '" << name
           << "' in function '" << func_.getName() << "'\n";
    rmc_error();
  }

  return matches;
}

void RealizeRMC::processEdge(CallInst *call) {
  // Pull out what the operands have to be.
  // We just assert if something is wrong, which is not great UX.
  uint64_t val = cast<ConstantInt>(call->getOperand(0))
    ->getValue().getLimitedValue();
  RMCEdgeType edgeType = (RMCEdgeType)val; // a bit dubious
  StringRef srcName = getStringArg(call->getOperand(1));
  StringRef dstName = getStringArg(call->getOperand(2));
  uint64_t bindHere = cast<ConstantInt>(call->getOperand(3))
    ->getValue().getLimitedValue();

  auto srcs = collectEdges(srcName);
  auto dsts = collectEdges(dstName);
  // If bindHere was set, then the binding site is this basic block,
  // otherwise it is nullptr to represent outside the function.
  BasicBlock *bindSite = bindHere ? call->getParent() : nullptr;

  for (auto src : srcs) {
    for (auto dst : dsts) {
      registerEdge(edges_, edgeType, bindSite, src, dst);
    }
  }

  // Handle pre and post edges now
  if (srcName == "pre") {
    for (auto dst : dsts) {
      Action *src = makePrePostAction(dst->bb->getSinglePredecessor());
      registerEdge(edges_, edgeType, bindSite, src, dst);
    }
  }
  if (dstName == "post") {
    for (auto src : srcs) {
      Action *dst = makePrePostAction(getSingleSuccessor(src->outBlock));
      registerEdge(edges_, edgeType, bindSite, src, dst);
    }
  }
}

bool RealizeRMC::processPush(CallInst *call) {
  Action *a = bb2action_[call->getParent()]; // This is dubious.
  // Ignore pushes not in actions. Needed to deal with having wrapper
  // push functions in Rust/C++. Indicate that we should leave the
  // __rmc_push call in place so that if this function gets RMC'd and
  // then inlined into another function before it has been RMC'd, it
  // properly sees the __rmc_push. Any function that wraps an
  // __rmc_push without it being in an action must be always_inline.
  if (a) {
    pushes_.insert(a);
    a->isPush = true;
    return true;
  } else {
    return false;
  }
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
      if (!processPush(call)) continue;
    } else {
      continue;
    }

    deleteRegisterCall(i);
  }
}

// XXX: document this scheme more?
// And think a bit more about whether the disconnect between the
// action location and where things are actually happening can cause
// trouble.
void handleTransfer(Action &info, CallInst *call) {
  uint64_t is_take = cast<ConstantInt>(call->getOperand(1))
    ->getValue().getLimitedValue();
  Value *value = call->getOperand(0);

  if (is_take) {
    // The dependency tracking code wants everything to be an
    // instruction. This doesn't obtain when we are trying to trace a
    // dependency from a parameter, so in that case we make a dummy
    // copy and replace all uses with it.
    if (!isa<Instruction>(value)) {
      // XXX: won't work if there are uses /before/ the TAKE, which
      // there probably shouldn't be, so...
      Instruction *newval = makeCopy(value, call);
      value->replaceAllUsesWith(newval);
      newval->setOperand(0, value);
      value = newval;
    }

    info.type = ActionTake;
    info.outgoingDep = value;
  } else {
    // Get the one Use of the output that we need to make sure to push
    // the value to.
    // XXX: might be fucked up if we didn't run mem2reg
    assert(call->hasOneUse());
    info.type = ActionGive;
    // XXX: This name is a little misleading since the dep isn't actually
    // in this action...
    info.incomingDep = &*call->use_begin();
  }

  // Get rid of the call
  BasicBlock::iterator ii(call);
  ReplaceInstWithValue(call->getParent()->getInstList(),
                       ii, value);
}

template <typename T>
bool actionIsSC(T *i) {
  return i->getOrdering() == AtomicOrdering::SequentiallyConsistent &&
    i->getSynchScope() == CrossThread;
}
bool actionIsSC(AtomicCmpXchgInst *i) {
  return i->getSuccessOrdering() == AtomicOrdering::SequentiallyConsistent &&
    i->getFailureOrdering() == AtomicOrdering::SequentiallyConsistent &&
    i->getSynchScope() == CrossThread;
}

void analyzeAction(Action &info) {
  // Don't analyze the dummy pre/post actions!
  if (info.type == ActionPrePost) return;

  // We search through info.outBlock instead of info.bb because if the
  // action is a multiblock LTAKE, the __rmc_transfer_ call will be in
  // the final block. If it doesn't end up being a transfer, then we
  // call any action where the parts don't match "complex".

  bool allSC = true;

  Instruction *soleLoad = nullptr;
  for (auto & i : *info.outBlock) {
    if (auto *load = dyn_cast<LoadInst>(&i)) {
      ++info.loads;
      soleLoad = &i;
      allSC &= actionIsSC(load);
    } else if (auto *store = dyn_cast<StoreInst>(&i)) {
      ++info.stores;
      allSC &= actionIsSC(store);
    } else if (auto *call = dyn_cast<CallInst>(&i)) {
      // If this is a transfer, mark it as such
      if (Function *target = call->getCalledFunction()) {
        // This is *really* silly. We declare appropriate __rmc_transfer
        // functions as needed at use sites, but if this happens
        // inside of namespaces, the name gets mangled. So we look
        // through the whole string, not just the prefix. Sigh.
        if (target->getName().find("__rmc_transfer_") != StringRef::npos) {
          handleTransfer(info, call);
          return;
        }
      }
      // Don't count functions that don't access memory
      // (for example, critically, llvm.dbg.* intrinsics)
      if (!(call->getCalledFunction() &&
            call->getCalledFunction()->doesNotAccessMemory())) {
        ++info.calls;
      }
      allSC = false;
    // What else counts as a call? I'm counting fences I guess.
    } else if (isa<FenceInst>(i)) {
      ++info.calls;
      allSC = false;
    } else if (auto *rmw = dyn_cast<AtomicRMWInst>(&i)) {
      ++info.RMWs;
      soleLoad = &i;
      allSC &= actionIsSC(rmw);
    } else if (auto *cas = dyn_cast<AtomicCmpXchgInst>(&i)) {
      ++info.RMWs;
      soleLoad = &i;
      allSC &= actionIsSC(cas);
    }
  }

  // Now that we know it isn't a transfer,
  // if the action has multiple basic blocks, call it Complex
  if (info.outBlock != info.bb) {
    info.type = ActionComplex;
    return;
  }

  // Now that we're past all the return cases, we can safely call it
  // allSC if it is.
  info.allSC = allSC;

  // Try to characterize what this action does.
  // These categories might not be the best.
  if (info.isPush) {
    // shouldn't do anything else; but might be a store if we didn't mem2reg
    assert(info.loads+info.calls+info.RMWs == 0 && info.stores <= 1);
    info.type = ActionPush;
  } else if (info.loads == 1 && info.stores+info.calls+info.RMWs == 0) {
    info.outgoingDep = soleLoad;
    info.incomingDep = &soleLoad->getOperandUse(0);
    info.type = ActionSimpleRead;
  } else if (info.stores >= 1 && info.loads+info.calls+info.RMWs == 0) {
    info.type = ActionSimpleWrites;
  } else if (info.RMWs == 1 && info.stores+info.loads+info.calls == 0) {
    info.outgoingDep = soleLoad;
    info.type = ActionSimpleRMW;
  } else if (info.RMWs+info.stores+info.loads+info.calls == 0) {
    info.type = ActionNop;
  } else {
    info.type = ActionComplex;
  }
}

void RealizeRMC::findActions() {
  // First, collect all calls to register actions
  SmallPtrSet<CallInst *, 8> registrations;
  for (inst_iterator is = inst_begin(func_), ie = inst_end(func_); is != ie;
       is++) {
    if (CallInst *call = dyn_cast<CallInst>(&*is)) {
      if (Function *target = call->getCalledFunction()) {
        if (target->getName() == "__rmc_action_register") {
          registrations.insert(call);
        }
      }
    }
  }

  // Now, make the vector of actions and a mapping from BasicBlock *.
  // We, somewhat tastelessly, reserve space for 3x the number of
  // actions we actually have so that we have space for new pre/post
  // actions that we might need to dummy up.
  // We need to have all the space reserved in advance so that our
  // pointers don't get invalidated when a resize happens.
  actions_.reserve(3 * registrations.size());
  numNormalActions_ = registrations.size();
  for (auto reg : registrations) {
    // FIXME: this scheme only works if we've run mem2reg. Otherwise we
    // need to chase through the alloca...
    assert(reg->hasOneUse());
    Instruction *close = cast<Instruction>(*reg->user_begin());

    StringRef name = getStringArg(reg->getOperand(0));

    // Now that we have found the start and the end of the action,
    // split the action into its own (group of) basic blocks so that
    // we can work with it more easily.

    // Split once to make a start block
    BasicBlock *start = splitBlock(reg->getParent(), reg);
    start->setName("_rmc_start_" + name);
    // Split it again so the start block is empty and we have our main block
    BasicBlock *main = splitBlock(start, reg);
    main->setName("_rmc_" + name);
    // Now split the end to get our tail block
    BasicBlock *end = splitBlock(close->getParent(), close);

    // Every action needs to have a well-defined single "out block"
    // that is the last block of the action that doesn't contain any
    // code from after the action (like the end block does). If there
    // isn't such a block, we shave off an empty one from the end
    // block. Note that we can use an existing out block even in
    // multi-block actions as long as there is a unique one.
    BasicBlock *out = end->getSinglePredecessor();
    if (!out) {
      out = end;
      end = splitBlock(close->getParent(), close);
      out->setName("_rmc_out_" + name);
    }
    end->setName("_rmc_end_" + name);

    // There is a subtly here: further processing of actions can cause
    // the out block to get split, leaving our pointer to it wrong
    // (since if it gets split we want the *later* block).
    // We work around this in a hacky way by storing the *end* block
    // instead and then patching them up once we have processed all
    // the actions.
    actions_.emplace_back(main, end, name);
    bb2action_[main] = &actions_.back();

    deleteRegisterCall(reg);
    deleteRegisterCall(close);
  }

  // Now we need to go fix up our out blocks.
  for (auto & action : actions_) {
    BasicBlock *out = action.outBlock->getSinglePredecessor();
    assert(out);
    action.outBlock = out;
  }
}

void dumpGraph(std::vector<Action> &actions) {
  // Debug spew!!
  for (auto & src : actions) {
    for (auto edgeType : kEdgeTypes) {
      for (auto entry : src.transEdges[edgeType]) {
        auto *dst = entry.first;
        for (auto *bindSite : entry.second) {
          errs() << "Edge: " << RMCEdge{edgeType, &src, dst, bindSite} << "\n";
        }
      }
    }
  }
  errs() << "\n";
}

BasicBlock *mergeBindPoints(DominatorTree &domTree,
                            BasicBlock *b1, BasicBlock *b2) {
  if (!b1 || !b2) return nullptr;
  BasicBlock *dom = domTree.findNearestCommonDominator(b1, b2);
  assert(dom == b1 || dom == b2); // XXX testing
  return dom;
}

void buildActionGraph(std::vector<Action> &actions, int numReal,
                      DominatorTree &domTree) {
  // Copy the initial edge specifications into the transitive graph
  for (auto & a : actions) {
    for (auto edgeType : kEdgeTypes) {
      a.transEdges[edgeType].insert(
        a.edges[edgeType].begin(), a.edges[edgeType].end());
    }
    //a.visTransEdges.insert(a.visEdges.begin(), a.visEdges.end());
    //a.execTransEdges.insert(a.execEdges.begin(), a.execEdges.end());
    //a.pushTransEdges.insert(a.pushEdges.begin(), a.pushEdges.end());
    // Visibility implies execution.
    a.transEdges[ExecutionEdge].insert(
        a.edges[VisibilityEdge].begin(), a.edges[VisibilityEdge].end());
    //a.execTransEdges.insert(a.visEdges.begin(), a.visEdges.end());
    // Push implies visibility and execution, but not in a way that we
    // need to track explicitly. Because push edges can't be useless,
    // they'll never get dropped from the graph, so it isn't important
    // to push them into visibility and execution.
    // (Although maybe we ought to, for consistency?)
  }

  auto merge = [&] (BasicBlock *b1, BasicBlock *b2) {
    return mergeBindPoints(domTree, b1, b2);
  };

  // Now compute the closures.  We previously ignored pre/post edges,
  // which was wrong; was it on to /anything/, though?
  //auto realActions = make_range(actions.begin(), actions.begin() + numReal);
  for (auto edgeType : kEdgeTypes) {
    transitiveClosure(actions, edgeType, merge);
  }

#ifdef DEBUG_SPEW
  dumpGraph(actions);
#endif
}

////////////// Chicanary to handle disguising operands

// Return the copied value if the value is a bs copy, otherwise null
Value *getBSCopyValue(Value *v) {
  CallInst *call = dyn_cast<CallInst>(v);
  if (!call) return nullptr;
  // This is kind of dubious
  return call->getName().find(".__rmc_bs_copy") != StringRef::npos?
    call->getOperand(0) : nullptr;
}

// Look through a possible bs copy to find the real underlying value
Value *getRealValue(Value *v) {
  Value *copyVal;
  while ((copyVal = getBSCopyValue(v)) != nullptr)
    v = copyVal;
  return v;
}

// Rewrite an instruction so that an operand is a dummy copy to
// hide information from llvm's optimizer
void hideOperand(Instruction *instr, int i) {
  Value *v = instr->getOperand(i);
  // Only put in the copy if we haven't done it already.
  if (!getBSCopyValue(v)) {
    Instruction *dummyCopy = makeCopy(v, instr);
    instr->setOperand(i, dummyCopy);
  }
}
void hideOperands(Instruction *instr) {
  for (unsigned i = 0; i < instr->getNumOperands(); i++) {
    hideOperand(instr, i);
  }
}

void hideUses(Instruction *instr, User *skip) {
  Instruction *dummyCopy = nullptr;
  for (auto is = instr->use_begin(), ie = instr->use_end(); is != ie;) {
    Use &use = *is++; // Grab and advance, so we can delete
    if (use.getUser() != skip && !getBSCopyValue(use.getUser())) {
      if (!dummyCopy) {
        dummyCopy = makeCopy(instr, getNextInsertionPt(instr));
      }
      //errs() << "Hiding " << instr->getName() << " as "
      //       << dummyCopy->getName() << " in " << *use.getUser() << "\n";
      use.set(dummyCopy);
    }
  }
}

//
void enforceBranchOn(BasicBlock *next, ICmpInst *icmp, int idx) {
  if (!icmp) return;
  // In order to keep LLVM from optimizing our stuff away we
  // insert dummy copies of the operands and a compiler barrier in the
  // target.
  hideOperands(icmp);
  Instruction *front = &*next->getFirstInsertionPt();
  if (!isInstrBarrier(front)) makeBarrier(front);
}

void enforceAddrDeps(Use *use, std::vector<Instruction *> &trail) {
  Instruction *end = cast<Instruction>(use->getUser());
  //errs() << "enforcing for: " << *end << "\n";
  for (auto is = trail.begin(), ie = trail.end(); is != ie; is++) {
    // Hide all the uses except for the other ones in the dep chain
    // XXX: maybe we need to hide more, in case the function gets
    // inlined, which could provide more information.
    // Think about, at least.
    Instruction *next = is+1 != ie ? *(is+1) : end;
    hideUses(*is, next);
  }
}

////////////// Program analysis that we use

BasicBlock *getPathPred(PathCache *cache, PathID path, BasicBlock *block) {
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
  // XXX: make this platform configured; on some platforms maybe an
  // atomic cmpxchg does /not/ behave like it branches on the old value
  if (isa<AtomicCmpXchgInst>(load) || isa<AtomicRMWInst>(load)) {
    if (icmpOut) *icmpOut = nullptr;
    if (outIdx) *outIdx = 0;
    return true;
  }

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

typedef SmallPtrSet<Value *, 4> PendingPhis;

// Look for address dependencies on a read.
bool addrDepsOnSearch(Value *pointer, Value *load,
                      PathCache *cache, PathCache::SkipSet &reachable,
                      PendingPhis &phis,
                      std::vector<std::vector<Instruction *> > *trails) {
  Instruction *instr = dyn_cast<Instruction>(pointer);
  if (!instr) return false;

  unsigned init_size = trails ? trails->size() : 0;
  auto extend_trails = [&] (bool ret) {
    if (!trails || !ret) return ret;
    for (unsigned i = init_size; i < trails->size(); i++) {
      (*trails)[i].push_back(instr);
    }
    return true;
  };


  if (pointer == load || phis.count(pointer)) {
    // Push an empty trail on the back to fill up
    if (trails) trails->push_back({});
    return extend_trails(true);
  } else if (getBSCopyValue(pointer)) {
    bool succ = addrDepsOnSearch(getRealValue(pointer),
                                 load, cache, reachable, phis, trails);
    return extend_trails(succ);
  }

  // We trace through GEP, BitCast, IntToPtr.
  // I wish we weren't recursive. Maybe we should restrict how we
  // trace through GEPs?
  // TODO: less heavily restrict what we use?
  // XXX: We need to do Loads now, right??? Is the way we do it right??
  if (isa<GetElementPtrInst>(instr) || isa<BitCastInst>(instr) ||
      isa<SExtInst>(instr) || isa<IntToPtrInst>(instr) ||
      isa<LoadInst>(instr)) {
    for (auto v : instr->operand_values()) {
      if (addrDepsOnSearch(v, load, cache, reachable, phis, trails)) {
        return extend_trails(true);
      }
    }
  }
  // We need to trace down *every* phi node path, except ones
  // that weren't reachable anyways.
  if (PHINode *phi = dyn_cast<PHINode>(instr)) {
    for (unsigned i = 0; i < phi->getNumIncomingValues(); i++) {
      // Don't trace down through blocks that aren't reachable
      if (!reachable.count(phi->getIncomingBlock(i))) continue;
      phis.insert(phi);
      bool succ = addrDepsOnSearch(phi->getIncomingValue(i),
                                   load, cache, reachable, phis, trails);
      phis.erase(phi);
      if (!succ) return false;
    }
    return extend_trails(true);
  }

  return false;
}

bool addrDepsOn(Use *use, Value *load,
                PathCache *cache, BasicBlock *bindSite,
                PathID path,
                std::vector<std::vector<Instruction *> > *trails) {
  Instruction *pointer = dyn_cast<Instruction>(use->get());
  Instruction *load_instr = dyn_cast<Instruction>(load);
  if (!pointer || !load_instr) return false;

  PendingPhis phis;
  // XXX: The path we are given can include a prefix we don't actually care
  // about.
  PathCache::SkipSet reachable = cache->pathReachable(bindSite, path);

#ifdef DEBUG_SPEW
  errs() << "from: " << load_instr->getParent()->getName() << " ";
  if (bindSite) errs() << "bound: " << bindSite->getName() << " ";
  errs() << "reachable: {";
  for (BasicBlock *block : reachable) {
    errs() << block->getName() << ", ";
  }
  errs() << "}\n";
#endif

  return addrDepsOnSearch(pointer, load_instr, cache,
                           reachable, phis, trails);
}

}


////////////// non-SMT specific compilation

CutStrength RealizeRMC::isPathCut(const RMCEdge &edge,
                                  PathID pathid,
                                  bool enforceSoft,
                                  bool justCheckCtrl) {
  Path path = pc_.extractPath(pathid);
  if (path.size() <= 1) return HardCut;

  bool hasSoftCut = false;
  Value *outgoingDep = edge.src->outgoingDep;

  // Paths are backwards.
  for (auto i = path.begin(), e = path.end(); i != e; ++i) {
    bool isFront = i == path.begin(), isBack = i == e-1;
    BasicBlock *bb = *i;

    auto cut_i = cuts_.find(bb);
    if (cut_i != cuts_.end()) {
      const BlockCut &cut = cut_i->second;
      bool cutHits = !(isFront && cut.isFront) && !(isBack && !cut.isFront);
      if (cutHits) {
        // sync cuts
        if (cut.type == CutSync) {
          return HardCut;
        }
        // lwsync cuts
        if (cut.type == CutLwsync && edge.edgeType < PushEdge) {
          return HardCut;
        }
        // ctrlisync cuts
        if (edge.edgeType == ExecutionEdge &&
            cut.type == CutCtrlIsync &&
            cut.read == outgoingDep) {
          return SoftCut;
        }
      }
    }

    if (isBack) continue;
    // If the destination is a write, and this is an execution edge,
    // and the source is a read, then we can just take advantage of a
    // control dependency to get a soft cut.  Also, if we are just
    // checking to make sure there is a control dep, we don't care
    // about what the dest does..
    if (edge.edgeType != ExecutionEdge || !outgoingDep) continue;
    if (!(edge.dst->type == ActionSimpleWrites ||
          edge.dst->type == ActionSimpleRMW ||
          justCheckCtrl)) continue;
    if (hasSoftCut) continue;

    // Is there a branch on the load?
    int idx;
    ICmpInst *icmp;
    hasSoftCut = branchesOn(bb, outgoingDep, &icmp, &idx);

    if (hasSoftCut && enforceSoft) {
      BasicBlock *next = *(i+1);
      enforceBranchOn(next, icmp, idx);
    }
  }

  if (hasSoftCut) return SoftCut;

  // Try a data cut
  // See if we have a data dep in a very basic way.
  // FIXME: Should be able to handle writes also!
  std::vector<std::vector<Instruction *> > trails;
  auto trailp = enforceSoft ? &trails : nullptr;
  if (edge.src->outgoingDep && edge.dst->incomingDep &&
      addrDepsOn(edge.dst->incomingDep, edge.src->outgoingDep,
                 &pc_, edge.bindSite, pathid, trailp)) {
    if (enforceSoft) {
      for (auto & trail : trails) {
        enforceAddrDeps(edge.dst->incomingDep, trail);
      }
    }
    return DataCut;
  }

  return NoCut;
}

CutStrength RealizeRMC::isEdgeCut(const RMCEdge &edge,
                                  bool enforceSoft, bool justCheckCtrl) {
  CutStrength strength = HardCut;

  PathCache::SkipSet skip;
  if (edge.bindSite) skip.insert(edge.bindSite);
  PathList paths =
    pc_.findAllSimplePaths(&skip, edge.src->outBlock, edge.dst->bb);
  //pc_.dumpPaths(paths);
  for (auto & path : paths) {
    CutStrength pathStrength = isPathCut(edge, path,
                                         enforceSoft, justCheckCtrl);
    if (pathStrength < strength) strength = pathStrength;
  }

  return strength;
}

bool RealizeRMC::isCut(const RMCEdge &edge) {
  RMCEdge selfEdge = RMCEdge{edge.edgeType, edge.src, edge.src, edge.bindSite};

  switch (isEdgeCut(edge)) {
  case HardCut: return true;
  case NoCut: return false;
  case DataCut:
    // We need to make sure we have a cut from src->src but it *isn't*
    // enough to just check ctrl!
    if (isEdgeCut(selfEdge, false, false)
        > NoCut) {
      isEdgeCut(edge, true, false);
      isEdgeCut(selfEdge, true, false);
      return true;
    } else {
      return false;
    }
  case SoftCut:
    if (isEdgeCut(selfEdge, false, true)
        > NoCut) {
      isEdgeCut(edge, true, true);
      isEdgeCut(selfEdge, true, true);
      return true;
    } else {
      return false;
    }
  default: assert(0 && "bogus case"); abort();
  }
}

void RealizeRMC::cutEdge(RMCEdge &edge) {
  if (isCut(edge)) return;

  // As a first pass, we just insert lwsyncs at the start of the destination.
  // (Or syncs if it is a push edge)
  BasicBlock *bb = edge.dst->bb;
  Instruction *i_point = &*bb->getFirstInsertionPt();
  if (edge.edgeType == PushEdge) {
    makeSync(i_point);
  } else {
    makeLwsync(i_point);
  }
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

////////////// SMT specific compilation

// Remove edges that have no effect (after transitive closure
void removeUselessEdges(std::vector<Action> &actions) {
  for (auto & src : actions) {
    ActionType st = src.type;

    // Move the set out and filter by building a new one to avoid
    // iterator invalidation woes.
    auto newExec = std::move(src.transEdges[ExecutionEdge]);
    src.transEdges[ExecutionEdge].clear();
    for (auto & entry : newExec) {
      ActionType dt = entry.first->type;
      if (!(st == ActionPush || dt == ActionPush ||
            st == ActionNop || dt == ActionNop ||
            st == ActionSimpleWrites)) {
        src.transEdges[ExecutionEdge].insert(std::move(entry));
      }
    }

    auto newVis = std::move(src.transEdges[VisibilityEdge]);
    src.transEdges[VisibilityEdge].clear();
    for (auto & entry : newVis) {
      ActionType dt = entry.first->type;
      if (!(st == ActionPush || dt == ActionPush ||
            st == ActionNop || dt == ActionNop ||
            /* R->R has same force as execution, and we made execution
             * versions of all the vis edges. */
            (st == ActionSimpleRead && dt == ActionSimpleRead) ||
            (st == ActionSimpleWrites && dt == ActionSimpleRead))) {
        src.transEdges[VisibilityEdge].insert(std::move(entry));
      }
    }

    // Push edges are never useless, because even if they are a nop
    // they interact with visibility and execution edges in a critical way.
    // (And visibility and execution edges that we might not see.)
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
  if (term->getNumSuccessors() > 1) return &*cut.dst->getFirstInsertionPt();
  if (cut.type == CutCtrl && isInstrIsync(getPrevInstr(term))) {
    return getPrevInstr(term);
  }
  return term;
}

AtomicOrdering strengthenOrder(AtomicOrdering order, AtomicOrdering strength) {
  if (order == AtomicOrdering::SequentiallyConsistent) return order;
  if (order == AtomicOrdering::Acquire && strength == AtomicOrdering::Release)
    return AtomicOrdering::AcquireRelease;
  if (order == AtomicOrdering::Release && strength == AtomicOrdering::Acquire)
    return AtomicOrdering::AcquireRelease;
  return strength;
}

void strengthenBlockOrders(BasicBlock *block, AtomicOrdering strength) {
  for (auto & i : *block) {
    if (StoreInst *store = dyn_cast<StoreInst>(&i)) {
      store->setAtomic(strengthenOrder(store->getOrdering(), strength));
    }
    if (LoadInst *load = dyn_cast<LoadInst>(&i)) {
      load->setAtomic(strengthenOrder(load->getOrdering(), strength));
    }
    if (AtomicRMWInst *rmw = dyn_cast<AtomicRMWInst>(&i)) {
      rmw->setSynchScope(CrossThread);
      rmw->setOrdering(strengthenOrder(rmw->getOrdering(), strength));
    }
    if (AtomicCmpXchgInst *cas = dyn_cast<AtomicCmpXchgInst>(&i)) {
      cas->setSynchScope(CrossThread);
      cas->setSuccessOrdering(
        strengthenOrder(cas->getSuccessOrdering(), strength));
      // Failure orderings are just loads, so making them Release
      // doesn't make any sense.
      if (strength == AtomicOrdering::Acquire) {
        cas->setFailureOrdering(
          strengthenOrder(cas->getFailureOrdering(), strength));
      }
    }
  }
}

void RealizeRMC::insertCut(const EdgeCut &cut) {
  //errs() << cut.type << ": "
  //       << cut.src->getName() << " -> "
  //       << (cut.dst ? cut.dst->getName() : "n/a") << "\n";

  switch (cut.type) {
  case CutSync:
    makeSync(getCutInstr(cut));
    break;
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
  case CutData:
  {
    std::vector<std::vector<Instruction *> > trails;
    Use *end = bb2action_[cut.dst]->incomingDep;
    bool deps = addrDepsOn(end, cut.read, &pc_,
                           cut.bindSite, cut.path, &trails);
    assert_(deps);

    for (auto & trail : trails) {
      enforceAddrDeps(end, trail);
    }
    break;
  }
  case CutRelease:
    strengthenBlockOrders(cut.src, AtomicOrdering::Release);
    break;
  case CutAcquire:
    strengthenBlockOrders(cut.src, AtomicOrdering::Acquire);
    break;
  default:
    assert(false && "Unimplemented insertCut case");
  }

}

////////////// Shared compilation

void RealizeRMC::cutPushes() {
  // We just insert pushes wherever we see one, for now.
  // We could also have a notion of push edges derived from
  // the edges to and from a push action.
  for (auto action : pushes_) {
    assert(action->isPush);
    BasicBlock *bb = action->bb;
    makeSync(&*bb->getFirstInsertionPt());
    cuts_[bb] = BlockCut(CutSync, true);
    // XXX: since we can't actually handle cuts on the front and the
    // back and because the sync is the only thing in the block and so
    // cuts at both the front and back, we insert a bogus BlockCut in
    // the next block.
    cuts_[getSingleSuccessor(action->outBlock)] = BlockCut(CutSync, true);
  }
}


bool RealizeRMC::run() {
  findActions();
  findEdges();

  if (actions_.empty() && edges_.empty()) return false;

  // This is kind of silly, but we depend on blocks having names, so
  // give a bogus name to any unnamed edges.
  for (auto & block : func_) {
    if (!block.hasName()) {
      block.setName("<unnamed>");
    }
  }

#ifdef DEBUG_SPEW
  errs() << "Stuff to do for: " << func_.getName() << "\n";
  for (auto & edge : edges_) {
    errs() << "Found an edge: " << edge << "\n";
  }
#endif

  // Analyze the instructions in actions to see what they do.
  // Also change their memory accesses to have an atomic ordering.
  for (auto & action : actions_) {
    analyzeAction(action);
  }
  //errs() << "Func body after setup:\n" << func_ << "\n";

  buildActionGraph(actions_, numNormalActions_, domTree_);

  cutPushes();

  if (!useSMT_) {
    cutEdges();
  } else {
    removeUselessEdges(actions_);
    auto cuts = smtAnalyze();
    //errs() << "Applying SMT results:\n";
    for (auto & cut : cuts) {
      insertCut(cut);
    }
  }
  //errs() << "========================================\n";
  //errs() << "Func body at end:\n" << func_ << "\n";

  return true;
}

#if USE_Z3
cl::opt<bool> UseSMT("rmc-use-smt",
                     cl::desc("Use an SMT solver to realize RMC"));
#else
const bool UseSMT = false;
#endif

// The actual pass. It has a bogus setup routine and otherwise
// calls out to RealizeRMC.
class RealizeRMCPass : public FunctionPass {
public:
  static char ID;
  RealizeRMCPass() : FunctionPass(ID) { }
  ~RealizeRMCPass() { }

  virtual bool doInitialization(Module &M) override {
    // Pull the platform out of the target triple and then sort of bogusly
    // stick it in a global variable
    std::string triple = M.getTargetTriple();
    if (triple.find("x86") == 0) {
      target = TargetX86;
    } else if (triple.find("aarch64") == 0) {
      target = TargetARMv8;
    } else if (triple.find("armv8") == 0) {
      target = TargetARMv8;
    } else if (triple.find("arm") == 0) {
      target = TargetARM;
    } else if (triple.find("powerpc") == 0) {
      target = TargetPOWER;
    } else {
      assert(false && "not given a supported target");
    }
    return false;
  }
  virtual bool runOnFunction(Function &F) override {
    DominatorTree &dom = getAnalysis<DominatorTreeWrapperPass>().getDomTree();
    LoopInfo &li = getLoopInfo(*this);
    RealizeRMC rmc(F, this, dom, li, UseSMT, target);
    return rmc.run();
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequiredID(BreakCriticalEdgesID);
    AU.addRequired<DominatorTreeWrapperPass>();
    AU.addRequired<LOOPINFO_PASS_NAME>();
  }
};

char RealizeRMCPass::ID = 0;

namespace llvm { void initializeRealizeRMCPassPass(PassRegistry&); }

// Create the pass init routine, registering the dependencies. We
// can't use RegisterPass because then we wind up with the deps not
// being initialized. I'm not totally sure why this wasn't a problem
// when I was just using opt instead of trying to have it plugged into
// clang, but...
INITIALIZE_PASS_BEGIN(RealizeRMCPass, "realize-rmc", "Compile RMC annotations",
                      false, false)
INITIALIZE_PASS_DEPENDENCY(BreakCriticalEdges)
// Hack to cause macro expansion of the name first...
#define INITIALIZE_PASS_DEPENDENCY_X(x) INITIALIZE_PASS_DEPENDENCY(x)
INITIALIZE_PASS_DEPENDENCY_X(LOOPINFO_PASS_NAME)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_END(RealizeRMCPass, "realize-rmc", "Compile RMC annotations",
                    false, false)

// Dummy class so we can trigger our pass initialization with a static
// initializer. We can't use RegisterPass because we need to be able
// to specify dependencies.
struct RMCInit {
  RMCInit() { initializeRealizeRMCPassPass(*PassRegistry::getPassRegistry()); }
} init;

cl::opt<bool> DoRMC("rmc-pass",
                    cl::desc("Enable the RMC pass in the pass manager"));

static void registerRMCPass(const PassManagerBuilder &,
                            legacy::PassManagerBase &PM) {
  if (DoRMC) { PM.add(new RealizeRMCPass()); }
}
// LoopOptimizerEnd seems to be a fairly reasonable place to stick
// this.  We want it after inlining and some basic optimizations, but
// our markers inhibit code motion and so we want to let the optimizer
// have a go afterwards.  LoopOptimizerEnd seems like the earliest
// place we can put it and have this work.  An atrocious hack that
// might be worth considering is putting it at EP_Peephole, which gets
// called multiple times, and only doing it at the "right" one.
static RegisterStandardPasses
    RegisterRMC(PassManagerBuilder::EP_LoopOptimizerEnd,
                registerRMCPass);

// A very simple pass that deletes all of the dummy copies that RMC
// inserted.  My hope is that this can be safely inserted at the very
// end of compilation in order to remove the register allocation and
// instruction selection penalties from the dummy copies.
//
// Is this guaranteed to not get broken by post-IR peepholing and the
// like? FSVO "guarantee"?
// Nope: on POWER with -O=3, it optimizes out the deps in dep1 and dep5
// Actually, for POWER, it gets broken by pre-IR optimizations that
// are enabled in a POWER specific way as part of the backend...
class CleanupCopiesPass : public BasicBlockPass {
public:
  static char ID;
  CleanupCopiesPass() : BasicBlockPass(ID) { }
  ~CleanupCopiesPass() { }

  virtual bool runOnBasicBlock(BasicBlock &BB) override {
    bool changed = false;
    for (auto is = BB.begin(), ie = BB.end(); is != ie; ) {
      Instruction *i = &*is++;
      if (Value *v = getBSCopyValue(i)) {
        i->replaceAllUsesWith(v);
        i->eraseFromParent();
        changed = true;
      }
    }
    return changed;
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
  }
};


char CleanupCopiesPass::ID = 0;
RegisterPass<CleanupCopiesPass> Y("cleanup-copies", "Remove some RMC crud at the end");

cl::opt<bool> DoCleanupCopies("rmc-cleanup-copies",
                              cl::desc("Enable the RMC copy cleanup phase"));

static void registerCleanupPass(const PassManagerBuilder &,
                               legacy::PassManagerBase &PM) {
  if (DoCleanupCopies) { PM.add(new CleanupCopiesPass()); }
}
static RegisterStandardPasses
    RegisterCleanup(PassManagerBuilder::EP_OptimizerLast,
                    registerCleanupPass);


// A super bogus pass that deletes all functions except one
class DropFunsPass : public ModulePass {
public:
  static char ID;
  DropFunsPass() : ModulePass(ID) { }
  ~DropFunsPass() { }

  virtual bool runOnModule(Module &M) override {
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

  void getAnalysisUsage(AnalysisUsage &AU) const override {}
};


char DropFunsPass::ID = 0;
RegisterPass<DropFunsPass> Z("drop-funs", "Drop all but specified functions ");
