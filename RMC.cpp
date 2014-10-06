// BUG: this whole thing depends on the specifics of how the clang version I
// am using emits llvm bitcode for the hacky RMC protocol.
// We rely on how basic blocks get named, on the labels forcing things
// into their own basic blocks, and probably will rely on this block
// having one predecessor and one successor. We could probably even
// force those to be empty without too much work by adding more labels...



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

#include <llvm/Support/raw_ostream.h>

#include <ostream>
#include <fstream>
#include <sstream>
#include <iostream>
#include <map>
#include <set>

using namespace llvm;

namespace {
#if 0 // I hate you, emacs. Is there a better way to work around this?
}
#endif

/* To do bogus data dependencies, we will emit inline assembly
  instructions. This is sort of tasteless; we should add an intrinsic;
  but it means we don't need to modify llvm. This is what the
  instruction should look like (probably the constraints aren't
  right?):

  %2 = call i32 asm sideeffect "eor $0, $0;", "=r,0"(i32 %1) #2, !srcloc !11

  Also for ctrlisb we do:
  call void asm sideeffect "cmp $0, $0;beq 1f;1: isb", "r,~{memory}"(i32 %1) #2, !srcloc !4

  And for dmb:
  call void asm sideeffect "dmb", "~{memory}"() #2, !srcloc !5


  We can probably do a bogus inline asm on x86 to prevent reordering:
  %2 = call i32 asm sideeffect "", "=r,r,0,~{dirflag},~{fpsr},~{flags}"(i32 %1, i32 %0) #3, !srcloc !9

  What about this makes the right value come out:
    =r specifies an output parameter and the 0 says that that input parameter
    is paired with param 0.

  Hm. Does this *actually* work?? What does "sideeffect" actually mean
  for asm and do we need it.

  --
  Should we emit dmb directly, or try to use llvm's fences?
*/


//// Some auxillary data structures
enum RMCEdgeType {
  VisbilityEdge,
  ExecutionEdge
};

raw_ostream& operator<<(raw_ostream& os, const RMCEdgeType& t) {
  switch (t) {
    case VisbilityEdge:
      os << "v";
      break;
    case ExecutionEdge:
      os << "x";
      break;
    default:
      os << "?";
      break;
  }
  return os;
}


///////////////////////////////////////////////////////////////////////////
// Information for a node in the RMC graph.
enum ActionType {
  ActionComplex,
  ActionSimpleRead,
  ActionSimpleWrites, // needs to be paired with a dep
  ActionSimpleRMW
};
struct Action {
  Action(BasicBlock *p_bb) :
    bb(p_bb),
    type(ActionComplex),
    stores(0), loads(0), RMWs(0), calls(0), soleLoad(nullptr) {}
  void operator=(const Action &) LLVM_DELETED_FUNCTION;
  Action(const Action &) LLVM_DELETED_FUNCTION;
  Action(Action &&) = default; // move constructor!

  BasicBlock *bb;

  // Some basic info about what sort of instructions live in the action
  ActionType type;
  int stores;
  int loads;
  int RMWs;
  int calls;
  LoadInst *soleLoad;

  // Edges in the graph.
  // XXX: Would we be better off storing this some other way?
  // a <ptr, type> pair?
  // And should we store v edges in x
  SmallPtrSet<Action *, 2> execEdges;
  SmallPtrSet<Action *, 2> visEdges;
};

// Info about an RMC edge
struct RMCEdge {
  RMCEdgeType edgeType;
  Action *src;
  Action *dst;

  bool operator<(const RMCEdge& rhs) const {
    return std::tie(edgeType, src, dst)
      < std::tie(rhs.edgeType, rhs.src, rhs.dst);
  }

  void print(raw_ostream &os) const {
    // substr(5) is to drop "_rmc_" from the front
    os << src->bb->getName().substr(5) << " -" << edgeType <<
      "-> " << dst->bb->getName().substr(5);
  }
};

raw_ostream& operator<<(raw_ostream& os, const RMCEdge& e) {
  e.print(os);
  return os;
}

// Cuts in the graph
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


///////////////////////////////////////////////////////////////////////////
// Code to find all simple paths between two basic blocks.
// Could generalize more to graphs if we wanted, but I don't right
// now.
typedef std::vector<BasicBlock *> Path;
typedef SmallVector<Path, 2> PathList;
typedef SmallPtrSet<BasicBlock *, 8> GreySet;

PathList findAllSimplePaths(GreySet *grey, BasicBlock *src, BasicBlock *dst) {
  PathList paths;
  if (src == dst) {
    Path path;
    path.push_back(dst);
    paths.push_back(std::move(path));
    return paths;
  }
  if (grey->count(src)) return paths;

  grey->insert(src);

  // We consider all exits from a function to loop back to the start
  // edge, so we need to handle that unfortunate case.
  if (isa<ReturnInst>(src->getTerminator())) {
    BasicBlock *entry = &src->getParent()->getEntryBlock();
    paths = findAllSimplePaths(grey, entry, dst);
  }
  // Go search all the normal successors
  for (auto i = succ_begin(src), e = succ_end(src); i != e; i++) {
    PathList subpaths = findAllSimplePaths(grey, *i, dst);
    std::move(subpaths.begin(), subpaths.end(), back_inserter(paths));
  }

  // Add our step to all of the vectors
  for (auto & path : paths) {
    path.push_back(src);
  }

  // Remove it from the set of things we've seen. We might come
  // through here again.
  // We can't really do any sort of memoization, since in a cyclic
  // graph the possible simple paths depend not just on what node we
  // are on, but our previous path (to avoid looping).
  grey->erase(src);

  return paths;
}

PathList findAllSimplePaths(BasicBlock *src, BasicBlock *dst) {
  GreySet grey;
  return findAllSimplePaths(&grey, src, dst);
}

void dumpPaths(const PathList &paths) {
  for (auto & path : paths) {
    for (auto block : path) {
      errs() << block->getName() << " <- ";
    }
    errs() << "\n";
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
    a = makeAsm(f_ty, "msync # sync", "~{memory}", true);
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
Instruction *makeCtrlIsync(Value *v, Instruction *to_precede) {
  LLVMContext &C = v->getContext();
  FunctionType *f_ty =
    FunctionType::get(FunctionType::getVoidTy(C), v->getType(), false);
  InlineAsm *a = nullptr;
  if (target == TargetARM) {
    a = makeAsm(f_ty, "cmp $0, $0;beq 1f;1: isb @ ctrlisync",
                "r,~{memory}", true);
  } else if (target == TargetX86) {
    a = makeAsm(f_ty, "# ctrlisync", "r,~{memory}", true);
  }
  return CallInst::Create(a, v, "", to_precede);
}
Instruction *makeCopy(Value *v, Instruction *to_precede) {
  FunctionType *f_ty = FunctionType::get(v->getType(), v->getType(), false);
  InlineAsm *a = makeAsm(f_ty, "# copy", "=r,0", false); /* false?? */
  return CallInst::Create(a, v, "__rmc_bs_copy", to_precede);
}
// We also need to add a thing for fake data deps, which is more annoying.

///////////////////////////////////////////////////////////////////////////
//// Actual code for the pass

class RealizeRMC : public FunctionPass {
private:
  std::vector<Action> actions_;
  std::vector<RMCEdge> edges_;
  DenseMap<BasicBlock *, Action *> bb2action_;
  DenseMap<BasicBlock *, EdgeCut> cuts_;

  CutStrength isPathCut(Function &F, const RMCEdge &edge, const Path &path,
                        bool enforceSoft = false);
  CutStrength isEdgeCut(Function &F, const RMCEdge &edge,
                        bool enforceSoft = false);
  bool isCut(Function &F, const RMCEdge &edge);

  void findActions(Function &F);
  void findEdges(Function &F);
  void cutEdges(Function &F);
  void cutEdge(Function &F, RMCEdge &edge);

  // Clear our data structures to save memory, make things clean for
  // future runs.
  void clear() {
    actions_.clear();
    edges_.clear();
    bb2action_.clear();
    cuts_.clear();
  }

public:
  static char ID;
  RealizeRMC() : FunctionPass(ID) {
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
  ~RealizeRMC() { }
  virtual bool runOnFunction(Function &F);

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequiredID(BreakCriticalEdgesID);
    AU.setPreservesCFG();
  }

};

bool nameMatches(StringRef blockName, StringRef target) {
  StringRef name = "_rmc_" + target.str() + "_";
  if (!blockName.startswith(name)) return false;
  // Now make sure the rest is an int
  APInt dummy;
  return !blockName.drop_front(name.size()).getAsInteger(10, dummy);
}

StringRef getStringArg(Value *v) {
  const Value *g = cast<Constant>(v)->stripPointerCasts();
  const Constant *ptr = cast<GlobalVariable>(g)->getInitializer();
  StringRef str = cast<ConstantDataSequential>(ptr)->getAsString();
  // Drop the '\0' at the end
  return str.drop_back();
}

void RealizeRMC::findEdges(Function &F) {
  for (inst_iterator is = inst_begin(F), ie = inst_end(F); is != ie;) {
    // Grab the instruction and advance the iterator at the start, since
    // we might delete the instruction.
    Instruction *i = &*is;
    is++;

    CallInst *call = dyn_cast<CallInst>(i);
    if (!call) continue;
    Function *target = call->getCalledFunction();
    // We look for calls to the bogus function
    // __rmc_edge_register, pull out the information about them,
    // and delete the calls.
    if (!target || target->getName() != "__rmc_edge_register") continue;

    // Pull out what the operands have to be.
    // We just assert if something is wrong, which is not great UX.
    bool isVisibility = cast<ConstantInt>(call->getOperand(0))
      ->getValue().getBoolValue();
    RMCEdgeType edgeType = isVisibility ? VisbilityEdge : ExecutionEdge;
    StringRef srcName = getStringArg(call->getOperand(1));
    StringRef dstName = getStringArg(call->getOperand(2));

    // Since multiple blocks can have the same tag, we search for
    // them by name.
    // We could make this more efficient by building maps but I don't think
    // it is going to matter.
    for (auto & srcBB : F) {
      if (!nameMatches(srcBB.getName(), srcName)) continue;
      for (auto & dstBB : F) {
        if (!nameMatches(dstBB.getName(), dstName)) continue;
        Action *src = bb2action_[&srcBB];
        Action *dst = bb2action_[&dstBB];

        // Insert into the graph and the list of edges
        if (edgeType == VisbilityEdge) {
          src->visEdges.insert(dst);
        } else {
          src->execEdges.insert(dst);
        }

        edges_.push_back({edgeType, src, dst});
      }
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
  if (info.loads == 1 && info.stores+info.calls+info.RMWs == 0) {
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

void RealizeRMC::findActions(Function &F) {
  // First, collect all the basic blocks that are actions
  SmallPtrSet<BasicBlock *, 8> basicBlocks;
  for (auto & block : F) {
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

CutStrength RealizeRMC::isPathCut(Function &F,
                                  const RMCEdge &edge,
                                  const Path &path,
                                  bool enforceSoft) {
  if (path.size() <= 1) return HardCut;

  bool hasSoftCut = false;
  Instruction *soleLoad = edge.src->soleLoad;

  // Paths are backwards.
  for (auto i = path.rbegin(), e = path.rend(); i != e; i++) {
    bool isFront = i == path.rbegin(), isBack = i == e-1;
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
        return HardCut;
      }
    }

    if (isBack) continue;
    // If the destination is a write, and this is an execution edge,
    // and the source is a read, then we can just take advantage of
    // a control dependency to get a soft cut.
    if (edge.edgeType == VisbilityEdge || !soleLoad) continue;
    if (!(edge.dst->type == ActionSimpleWrites ||
          edge.dst->type == ActionSimpleRMW)) continue;
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

CutStrength RealizeRMC::isEdgeCut(Function &F, const RMCEdge &edge,
                                  bool enforceSoft) {
  CutStrength strength = HardCut;
  PathList paths = findAllSimplePaths(edge.src->bb, edge.dst->bb);
  for (auto & path : paths) {
    CutStrength pathStrength = isPathCut(F, edge, path, enforceSoft);
    if (pathStrength < strength) strength = pathStrength;
  }

  return strength;
}

bool RealizeRMC::isCut(Function &F, const RMCEdge &edge) {
  switch (isEdgeCut(F, edge)) {
    case HardCut: return true;
    case NoCut: return false;
    case SoftCut:
      if (isEdgeCut(F, RMCEdge{edge.edgeType, edge.src, edge.src}) > NoCut) {
        isEdgeCut(F, edge, true);
        isEdgeCut(F, RMCEdge{edge.edgeType, edge.src, edge.src}, true);
        return true;
      } else {
        return false;
      }
  }
}


void RealizeRMC::cutEdge(Function &F, RMCEdge &edge) {
  if (isCut(F, edge)) return;

  // As a first pass, we just insert lwsyncs at the start of the destination.
  BasicBlock *bb = edge.dst->bb;
  makeLwsync(&bb->front());
  // XXX: we need to make sure we can't ever fail to track a cut at one side
  // of a block because we inserted one at the other! Argh!
  cuts_[bb] = EdgeCut(CutLwsync, true);
}

void RealizeRMC::cutEdges(Function &F) {
  // Maybe we should actually use the graph structure we built?
  for (auto & edge : edges_) {
    cutEdge(F, edge);
  }
}

bool RealizeRMC::runOnFunction(Function &F) {
  findActions(F);
  findEdges(F);

  if (actions_.empty() && edges_.empty()) return false;

  for (auto & edge : edges_) {
    errs() << "Found an edge: " << edge << "\n";
  }

  // Analyze the instructions in actions to see what they do.
  for (auto & action : actions_) {
    analyzeAction(action);
  }

  cutEdges(F);

  clear();
  return true;
}

char RealizeRMC::ID = 0;
RegisterPass<RealizeRMC> X("realize-rmc", "Compile RMC annotations");
}
