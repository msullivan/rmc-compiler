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

#include <llvm/ADT/ArrayRef.h>

#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <llvm/Support/raw_ostream.h>

#include <ostream>
#include <fstream>
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


// Info about an RMC edge
struct RMCEdge {
  RMCEdgeType edgeType;
  BasicBlock *src;
  BasicBlock *dst;

  bool operator<(const RMCEdge& rhs) const {
    return std::tie(edgeType, src, dst)
      < std::tie(rhs.edgeType, rhs.src, rhs.dst);
  }

  void print(raw_ostream &os) const {
    // substr(5) is to drop "_rmc_" from the front
    os << src->getName().substr(5) << " -" << edgeType <<
      "-> " << dst->getName().substr(5);
  }
};

raw_ostream& operator<<(raw_ostream& os, const RMCEdge& e) {
  e.print(os);
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

enum CutType {
  CutNone,
  CutCtrlIsync, // needs to be paired with a dep
  CutLwsync,
  CutSync
};
struct EdgeCut {
  EdgeCut() : type(CutNone), front(false), read(nullptr) {}
  CutType type;
  bool front;
  Value *read;
};


///////////////////////////////////////////////////////////////////////////
// Code to find all simple paths between two basic blocks.
// Could generalize more to graphs if we wanted, but I don't right
// now.
typedef std::vector<BasicBlock *> Path;
typedef SmallVector<Path, 2> PathList;
typedef SmallPtrSet<BasicBlock *, 8> GreySet;

PathList findAllSimplePaths_(GreySet *grey, BasicBlock *src, BasicBlock *dst) {
  PathList paths;
  if (grey->count(src)) return paths;
  if (src == dst) {
    Path path;
    path.push_back(dst);
    paths.push_back(std::move(path));
    return paths;
  }

  grey->insert(src);

  // Go search all the successors
  for (auto i = succ_begin(src), e = succ_end(src); i != e; i++) {
    PathList subpaths = findAllSimplePaths_(grey, *i, dst);
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
  return findAllSimplePaths_(&grey, src, dst);
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
// XXX: This is the wrong way to do this!
bool targetARM = true;
bool targetx86 = false;

// Some llvm nonsense. I should probably find a way to clean this up.
// do we put ~{dirflag},~{fpsr},~{flags} for the x86 ones? don't think so.
Instruction *makeSync(Value *dummy) {
  LLVMContext &C = dummy->getContext();
  FunctionType *f_ty = FunctionType::get(FunctionType::getVoidTy(C), false);
  InlineAsm *a;
  if (targetARM) {
    a = InlineAsm::get(f_ty, "dmb", "~{memory}", true);
  } else if (targetx86) {
    a = InlineAsm::get(f_ty, "msync", "~{memory}", true);
  } else {
    assert(false && "invalid target");
  }
  return CallInst::Create(a, None, "sync");
}
Instruction *makeLwsync(Value *dummy) {
  LLVMContext &C = dummy->getContext();
  FunctionType *f_ty = FunctionType::get(FunctionType::getVoidTy(C), false);
  InlineAsm *a;
  if (targetARM) {
    a = InlineAsm::get(f_ty, "dmb", "~{memory}", true);
  } else if (targetx86) {
    a = InlineAsm::get(f_ty, "", "~{memory}", true);
  } else {
    assert(false && "invalid target");
  }
  return CallInst::Create(a, None, "lwsync");
}
Instruction *makeCtrlIsync(Value *v) {
  LLVMContext &C = v->getContext();
  FunctionType *f_ty =
    FunctionType::get(FunctionType::getVoidTy(C), v->getType(), false);
  InlineAsm *a;
  if (targetARM) {
    a = InlineAsm::get(f_ty, "cmp $0, $0;beq 1f;1: isb", "r,~{memory}", true);
  } else if (targetx86) {
    a = InlineAsm::get(f_ty, "", "r,~{memory}", true);
  } else {
    assert(false && "invalid target");
  }
  return CallInst::Create(a, None, "ctrlisync");
}
// We also need to add a thing for fake data deps, which is more annoying.

///////////////////////////////////////////////////////////////////////////
//// Actual code for the pass

class RMCPass : public FunctionPass {
private:
  std::vector<Action> actions_;
  DenseMap<BasicBlock *, Action *> bb2action_;
  DenseMap<BasicBlock *, EdgeCut> cuts_;

public:
  static char ID;
  RMCPass() : FunctionPass(ID) {

  }
  ~RMCPass() { }
  std::vector<RMCEdge> findEdges(Function &F);
  void buildGraph(std::vector<RMCEdge> &edges, Function &F);
  virtual bool runOnFunction(Function &F);

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequiredID(BreakCriticalEdgesID);
    AU.setPreservesCFG();
  }

  // Clear our data structures to save memory, make things clean for
  // future runs.
  void clear() {
    actions_.clear();
    bb2action_.clear();
    cuts_.clear();
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

std::vector<RMCEdge> RMCPass::findEdges(Function &F) {
  std::vector<RMCEdge> edges;

  for (inst_iterator is = inst_begin(F), ie = inst_end(F); is != ie;) {
    // Grab the instruction and advance the iterator at the start, since
    // we might delete the instruction.
    Instruction *i = &*is;
    is++;

    CallInst *load = dyn_cast<CallInst>(i);
    if (!load) continue;
    Function *target = load->getCalledFunction();
    // We look for calls to the bogus function
    // __rmc_edge_register, pull out the information about them,
    // and delete the calls.
    if (!target || target->getName() != "__rmc_edge_register") continue;

    // Pull out what the operands have to be.
    // We just assert if something is wrong, which is not great UX.
    bool isVisibility = cast<ConstantInt>(load->getOperand(0))
      ->getValue().getBoolValue();
    RMCEdgeType edgeType = isVisibility ? VisbilityEdge : ExecutionEdge;
    StringRef srcName = getStringArg(load->getOperand(1));
    StringRef dstName = getStringArg(load->getOperand(2));

    // Since multiple blocks can have the same tag, we search for
    // them by name.
    // We could make this more efficient by building maps but I don't think
    // it is going to matter.
    for (auto & src : F) {
      if (!nameMatches(src.getName(), srcName)) continue;
      for (auto & dst : F) {
        if (!nameMatches(dst.getName(), dstName)) continue;

        edges.push_back({edgeType, &src, &dst});
      }
    }

    // Delete the bogus call.
    i->eraseFromParent();
  }

  return edges;
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

void RMCPass::buildGraph(std::vector<RMCEdge> &edges, Function &F) {
  // First, collect all the basic blocks with edges attached to them
  SmallPtrSet<BasicBlock *, 8> basicBlocks;
  for (auto & edge : edges) {
    basicBlocks.insert(edge.src);
    basicBlocks.insert(edge.dst);
  }

  // Now, make the vector of actions and a mapping from BasicBlock *.
  actions_.reserve(basicBlocks.size());
  for (auto bb : basicBlocks) {
    actions_.emplace_back(bb);
    bb2action_[bb] = &actions_.back();
  }

  // Analyze the instructions in actions to see what they do.
  for (auto & action : actions_) {
    analyzeAction(action);
  }

  // Build our list of edges into a more explicit graph
  for (auto & edge : edges) {
    Action *src = bb2action_[edge.src];
    Action *dst = bb2action_[edge.dst];
    if (edge.edgeType == VisbilityEdge) {
      src->visEdges.insert(dst);
    } else {
      src->execEdges.insert(dst);
    }
  }
}

bool RMCPass::runOnFunction(Function &F) {
  auto edges = findEdges(F);
  if (edges.empty()) return false;

  for (auto & edge : edges) {
    errs() << "Found an edge: " << edge << "\n";
  }

  buildGraph(edges, F);

  clear();
  return true;
}

char RMCPass::ID = 0;
RegisterPass<RMCPass> X("rmc-pass", "RMC");
}
