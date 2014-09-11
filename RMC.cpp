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

class RMCPass : public FunctionPass {
private:
public:
  static char ID;
  RMCPass() : FunctionPass(ID) {}
  ~RMCPass() { }
  virtual bool runOnFunction(Function &F);
};

bool RMCPass::runOnFunction(Function &F) {
  errs() << "Hello: ";
  errs().write_escaped(F.getName()) << "\n";
  return false;
}

char RMCPass::ID = 0;
RegisterPass<RMCPass> X("rmc-pass", "RMC");
}
