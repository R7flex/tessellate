#include "Passes.h"
#include "Utils.h"

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"

#include <algorithm>

using namespace llvm;

namespace tess {
namespace {

Value *buildAlwaysTrue(IRBuilder<> &B, Random &R) {
  Type *I32 = Type::getInt32Ty(B.getContext());
  AllocaInst *A = B.CreateAlloca(I32, nullptr, "tess.op.x");
  B.CreateStore(ConstantInt::get(I32, (R.next() | 1u)), A);
  Value *XV = B.CreateLoad(I32, A);
  Value *Xp1 = B.CreateAdd(XV, ConstantInt::get(I32, 1));
  Value *Mul = B.CreateMul(XV, Xp1);
  Value *And = B.CreateAnd(Mul, ConstantInt::get(I32, 1));
  return B.CreateICmpEQ(And, ConstantInt::get(I32, 0), "tess.op.t");
}

BasicBlock *createJunkBlock(Function &F, BasicBlock *InsertBefore, Random &R) {
  BasicBlock *Junk =
      BasicBlock::Create(F.getContext(), "tess.bcf.junk", &F, InsertBefore);
  IRBuilder<> B(Junk);
  Type *I32 = Type::getInt32Ty(F.getContext());
  AllocaInst *Tmp = B.CreateAlloca(I32);
  B.CreateStore(ConstantInt::get(I32, R.next()), Tmp);
  Value *V = B.CreateLoad(I32, Tmp);
  V = B.CreateXor(V, ConstantInt::get(I32, R.next()));
  V = B.CreateAdd(V, ConstantInt::get(I32, 1));
  B.CreateStore(V, Tmp);
  B.CreateBr(Junk);
  return Junk;
}

bool bogusOnBlock(BasicBlock *BB, Function &F, Random &R) {
  Instruction *Term = BB->getTerminator();
  if (!Term || !isa<BranchInst>(Term))
    return false;
  if (BB->isEHPad())
    return false;
  if (std::distance(BB->begin(), BB->end()) < 3)
    return false;
  if (BB->getName().starts_with("tess."))
    return false;

  BasicBlock *Cont = BB->splitBasicBlock(Term, "tess.bcf.cont");
  Instruction *AutoBr = BB->getTerminator();
  IRBuilder<> B(AutoBr);
  Value *Cond = buildAlwaysTrue(B, R);
  BasicBlock *Junk = createJunkBlock(F, Cont, R);
  AutoBr->eraseFromParent();
  IRBuilder<> B2(BB);
  B2.CreateCondBr(Cond, Cont, Junk);
  return true;
}

bool runBogus(Function &F, Random &R) {
  if (shouldSkipFunction(F) || hasExceptionHandling(F))
    return false;
  if (isProtectedEntry(F))
    return false;

  ObfConfig &C = config();
  unsigned Prob = C.BcfProbability;
  unsigned MaxIns = C.BcfMaxPerFunction;
  if (F.size() < 3)
    return false;

  SmallVector<BasicBlock *, 32> Blocks;
  for (BasicBlock &BB : F) {
    if (&BB == &F.getEntryBlock())
      continue;
    Blocks.push_back(&BB);
  }

  bool Changed = false;
  unsigned Inserted = 0;
  for (BasicBlock *BB : Blocks) {
    if (Inserted >= MaxIns)
      break;
    if (!R.chance(Prob))
      continue;
    if (bogusOnBlock(BB, F, R)) {
      Changed = true;
      ++Inserted;
    }
  }
  return Changed;
}

}

PreservedAnalyses BogusCFPass::run(Module &M, ModuleAnalysisManager &) {
  bool Changed = false;
  Random R;
  for (Function &F : M) {
    if (!wantsObfuscation(F))
      continue;
    verboseLog("bcf", F);
    if (runBogus(F, R))
      Changed = true;
  }
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

}
