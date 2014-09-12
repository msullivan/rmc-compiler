// BUG: this whole thing depends on the specifics of how the clang version I
// am using emits llvm bitcode for the hacky RMC protocol.

#include <llvm/Pass.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Instructions.h>

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

//// Some auxillary data structures
enum RMCEdgeType {
  VisbilityEdge,
  ExecutionEdge
};

raw_ostream& operator<<(raw_ostream& os, const RMCEdgeType& t) {
  switch (t) {
    case VisbilityEdge:
      os << "visibility";
      break;
    case ExecutionEdge:
      os << "execution";
      break;
    default:
      os << "BOGUS";
      break;
  }
  return os;
}


struct RMCEdge {
  RMCEdgeType edgeType;
  BasicBlock *src;
  BasicBlock *dst;

  bool operator<(const RMCEdge& rhs) const {
    return std::tie(edgeType, src, dst)
      < std::tie(rhs.edgeType, rhs.src, rhs.dst);
  }

  void print(raw_ostream &os) const {
    os << "Edge type: " << edgeType << ", src: "
       << src->getName() << ", dest: " << dst->getName();
  }
};

raw_ostream& operator<<(raw_ostream& os, const RMCEdge& e) {
  e.print(os);
  return os;
}

//// Actual code for the pass
class RMCPass : public FunctionPass {
private:
public:
  static char ID;
  RMCPass() : FunctionPass(ID) {}
  ~RMCPass() { }
  std::vector<RMCEdge> findEdges(Function &F);
  virtual bool runOnFunction(Function &F);
};

std::vector<RMCEdge> RMCPass::findEdges(Function &F) {
  std::vector<RMCEdge> edges;
  bool usesRMC = false;

  for (auto & block : F) {
    for (BasicBlock::iterator is = block.begin(), ie = block.end(); is != ie;) {
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

      usesRMC = true;

      // Pull out what the operands have to be.
      // We just assert if something is wrong, which is not great UX.
      bool isVisibility = cast<ConstantInt>(load->getOperand(0))
        ->getValue().getBoolValue();
      RMCEdgeType edgeType = isVisibility ? VisbilityEdge : ExecutionEdge;
      BasicBlock *src = cast<BlockAddress>(load->getOperand(1))
        ->getBasicBlock();
      BasicBlock *dst = cast<BlockAddress>(load->getOperand(2))
        ->getBasicBlock();

      edges.push_back({edgeType, src, dst});

      // Delete the bogus call.
      i->eraseFromParent();
    }
  }

  // If this function uses RMC, look for a bogus "indirectgoto" block
  // and remove it.
  if (usesRMC) {
    for (auto & block : F) {
      if (block.getName() == "indirectgoto" &&
          pred_begin(&block) == pred_end(&block)) {
        DeleteDeadBlock(&block);
        break;
      }
    }
  }

  return edges;
}

bool RMCPass::runOnFunction(Function &F) {
  auto edges = findEdges(F);

  for (auto & edge : edges) {
    errs() << "Found one: " << edge << "\n";
  }

  return !edges.empty();
}

char RMCPass::ID = 0;
RegisterPass<RMCPass> X("rmc-pass", "RMC");
}
