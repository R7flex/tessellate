#include "Passes.h"
#include "Utils.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace tess {
namespace {

bool stripGlobalCtors(Module &M) {
  bool Changed = false;
  for (const char *Name : {"llvm.global_ctors", "llvm.global_dtors"}) {
    if (GlobalVariable *GV = M.getGlobalVariable(Name)) {
      GV->eraseFromParent();
      Changed = true;
      if (verboseEnabled())
        errs() << "[tess:trim] stripped " << Name << "\n";
    }
  }
  return Changed;
}

bool removeDeadInternals(Module &M, unsigned &RemovedFns, unsigned &RemovedGVs) {
  bool Changed = false;

  SmallVector<Function *, 16> DeadFns;
  for (Function &F : M) {
    if (F.isDeclaration() || !F.hasLocalLinkage())
      continue;
    if (isProtectedEntry(F))
      continue;
    if (F.use_empty())
      DeadFns.push_back(&F);
  }
  for (Function *F : DeadFns) {
    F->eraseFromParent();
    ++RemovedFns;
    Changed = true;
  }

  SmallVector<GlobalVariable *, 32> DeadGVs;
  for (GlobalVariable &GV : M.globals()) {
    if (!GV.hasLocalLinkage() || GV.isDeclaration())
      continue;
    if (!GV.use_empty())
      continue;
    if (GV.getName().starts_with("llvm."))
      continue;
    DeadGVs.push_back(&GV);
  }
  for (GlobalVariable *GV : DeadGVs) {
    GV->eraseFromParent();
    ++RemovedGVs;
    Changed = true;
  }

  return Changed;
}

}

PreservedAnalyses SizeTrimPass::run(Module &M, ModuleAnalysisManager &) {
  unsigned RemovedFns = 0, RemovedGVs = 0;
  bool Changed = false;
  Changed |= stripGlobalCtors(M);
  Changed |= removeDeadInternals(M, RemovedFns, RemovedGVs);

  unsigned InternalFns = 0;
  for (Function &F : M)
    if (F.hasInternalLinkage() && !F.isDeclaration())
      ++InternalFns;

  if (verboseEnabled() || Changed) {
    errs() << "[tess:trim] module=" << M.getName()
           << " internal_fns=" << InternalFns
           << " removed_fns=" << RemovedFns
           << " removed_gvs=" << RemovedGVs << "\n";
  }

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

}
