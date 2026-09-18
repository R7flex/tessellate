#include "Passes.h"
#include "Utils.h"

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"

#include <algorithm>

using namespace llvm;

namespace tess {
namespace {

bool substituteInst(Instruction *I, Random &R) {
  auto *Bin = dyn_cast<BinaryOperator>(I);
  if (!Bin)
    return false;
  if (!Bin->getType()->isIntegerTy())
    return false;

  IRBuilder<> B(Bin);
  Value *A = Bin->getOperand(0);
  Value *BOp = Bin->getOperand(1);
  Value *Res = nullptr;

  switch (Bin->getOpcode()) {
  case Instruction::Add: {
    if (R.chance(50)) {
      Value *Nb = B.CreateNot(BOp);
      Value *T = B.CreateSub(A, Nb);
      Res = B.CreateSub(T, ConstantInt::get(Bin->getType(), 1));
    } else {
      Value *X = B.CreateXor(A, BOp);
      Value *An = B.CreateAnd(A, BOp);
      Value *Sh = B.CreateShl(An, ConstantInt::get(Bin->getType(), 1));
      Res = B.CreateAdd(X, Sh);
    }
    break;
  }
  case Instruction::Sub: {
    Value *Nb = B.CreateNot(BOp);
    Value *T = B.CreateAdd(A, Nb);
    Res = B.CreateAdd(T, ConstantInt::get(Bin->getType(), 1));
    break;
  }
  case Instruction::Xor: {
    Value *O = B.CreateOr(A, BOp);
    Value *An = B.CreateAnd(A, BOp);
    Res = B.CreateSub(O, An);
    break;
  }
  case Instruction::And: {
    Value *Na = B.CreateNot(A);
    Value *Nb = B.CreateNot(BOp);
    Value *O = B.CreateOr(Na, Nb);
    Res = B.CreateNot(O);
    break;
  }
  case Instruction::Or: {
    Value *X = B.CreateXor(A, BOp);
    Value *An = B.CreateAnd(A, BOp);
    Res = B.CreateXor(X, An);
    break;
  }
  default:
    return false;
  }

  if (!Res)
    return false;
  Bin->replaceAllUsesWith(Res);
  Bin->eraseFromParent();
  return true;
}

bool runSub(Function &F, Random &R) {
  if (shouldSkipFunction(F))
    return false;

  unsigned Prob = config().SubProbability;
  if (isProtectedEntry(F))
    Prob = std::min(95u, Prob + 20);

  SmallVector<Instruction *, 64> Work;
  for (BasicBlock &BB : F)
    for (Instruction &I : BB)
      if (auto *BO = dyn_cast<BinaryOperator>(&I))
        if (BO->getType()->isIntegerTy())
          Work.push_back(BO);

  bool Changed = false;
  unsigned Budget = isProtectedEntry(F) ? 64 : 32;
  unsigned Done = 0;
  for (Instruction *I : Work) {
    if (Done >= Budget)
      break;
    if (!R.chance(Prob))
      continue;
    if (substituteInst(I, R)) {
      Changed = true;
      ++Done;
    }
  }
  return Changed;
}

}

PreservedAnalyses SubstitutionPass::run(Module &M, ModuleAnalysisManager &) {
  bool Changed = false;
  Random R;
  for (Function &F : M) {
    if (!wantsObfuscation(F))
      continue;
    verboseLog("sub", F);
    if (runSub(F, R))
      Changed = true;
  }
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

}
