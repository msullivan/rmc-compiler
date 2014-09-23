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
#include <llvm/IR/CFG.h>

#include <llvm/Transforms/Utils/BasicBlockUtils.h>
#include <llvm/IR/InstIterator.h>
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

// Information for a node in the RMC graph.
struct Block {
  Block(BasicBlock *p_bb) : bb(p_bb), stores(0), loads(0), RMWs(0), calls(0) {}
  void operator=(const Block &) LLVM_DELETED_FUNCTION;
  Block(const Block &) LLVM_DELETED_FUNCTION;
  Block(Block &&) = default; // move constructor!

  BasicBlock *bb;

  // Some basic info about what sort of instructions live in the block
  int stores;
  int loads;
  int RMWs;
  int calls;

  // Edges in the graph.
  // XXX: Would we be better off storing this some other way?
  // a <ptr, type> pair?
  // And should we store v edges in x
  SmallPtrSet<Block *, 2> execEdges;
  SmallPtrSet<Block *, 2> visEdges;
};

// Code to find all paths. Maybe we should optimize this some more,
// but it's fundamentally exponential so whatever, really.
// Finds all non-looping paths between two basic blocks.
// Could generalize more to graphs if we wanted, but I don't right
// now.
typedef std::vector<BasicBlock *> Path;
typedef SmallVector<Path, 2> PathList;
typedef SmallPtrSet<BasicBlock *, 8> GreySet;

PathList findAllPaths_(GreySet *grey, BasicBlock *src, BasicBlock *dst) {
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
    PathList subpaths = findAllPaths_(grey, *i, dst);
    std::move(subpaths.begin(), subpaths.end(), back_inserter(paths));
  }

  // Add our step to all of the vectors
  for (auto & path : paths) {
    path.push_back(src);
  }

  // Remove it from the set of things we've seen. We might come
  // through here again. Maybe we should memoize the results to avoid
  // redoing the search, but I don't think that really saves us all
  // that much.
  grey->erase(src);

  return paths;
}

PathList findAllPaths(BasicBlock *src, BasicBlock *dst) {
  GreySet grey;
  return findAllPaths_(&grey, src, dst);
}

void dumpPaths(const PathList &paths) {
  for (auto & path : paths) {
    for (auto block : path) {
      errs() << block->getName() << " <- ";
    }
    errs() << "\n";
  }
}

//// Actual code for the pass
class RMCPass : public FunctionPass {
private:
  std::vector<Block> blocks_;
  DenseMap<BasicBlock *, Block *> bb2block_;

public:
  static char ID;
  RMCPass() : FunctionPass(ID) {

  }
  ~RMCPass() { }
  std::vector<RMCEdge> findEdges(Function &F);
  void buildGraph(std::vector<RMCEdge> &edges, Function &F);
  virtual bool runOnFunction(Function &F);
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

void analyzeBlock(Block &info) {
  for (auto & i : *info.bb) {
    if (isa<LoadInst>(i)) {
      info.loads++;
    } else if (isa<StoreInst>(i)) {
      info.stores++;
    // What else counts as a call? I'm counting fences I guess.
    } else if (isa<CallInst>(i) || isa<FenceInst>(i)) {
      info.calls++;
    } else if (isa<AtomicCmpXchgInst>(i) || isa<AtomicRMWInst>(i)) {
      info.RMWs++;
    }
  }
}

void RMCPass::buildGraph(std::vector<RMCEdge> &edges, Function &F) {
  // First, collect all the basic blocks with edges attached to them
  SmallPtrSet<BasicBlock *, 8> basicBlocks;
  for (auto & edge : edges) {
    basicBlocks.insert(edge.src);
    basicBlocks.insert(edge.dst);
  }

  // Now, make the vector of blocks and a mapping from BasicBlock *.
  blocks_.reserve(basicBlocks.size());
  for (auto bb : basicBlocks) {
    blocks_.emplace_back(bb);
    bb2block_[bb] = &blocks_.back();
  }

  // Analyze the instructions in blocks to see what sort of actions
  // they peform.
  for (auto & block : blocks_) {
    analyzeBlock(block);
  }

  // Build our list of edges into a more explicit graph
  for (auto & edge : edges) {
    Block *src = bb2block_[edge.src];
    Block *dst = bb2block_[edge.dst];
    if (edge.edgeType == VisbilityEdge) {
      src->visEdges.insert(dst);
    } else {
      src->execEdges.insert(dst);
    }
  }
}

bool RMCPass::runOnFunction(Function &F) {
  auto edges = findEdges(F);

  for (auto & edge : edges) {
    errs() << "Found an edge: " << edge << "\n";
  }

  buildGraph(edges, F);
  bool changed = !edges.empty();

  // Clear our data structures to save memory, make things clean for
  // future runs.
  blocks_.clear();
  bb2block_.clear();

  return changed;
}

char RMCPass::ID = 0;
RegisterPass<RMCPass> X("rmc-pass", "RMC");
}
