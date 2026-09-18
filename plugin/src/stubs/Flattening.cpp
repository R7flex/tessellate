#include "Passes.h"
#include "Utils.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

static cl::opt<bool> FlaEntry(
    "tess-fla-entry", cl::init(false), cl::Hidden,
    cl::desc("Also flatten DllMain / protected entries (risky for manual-map)"));

namespace tess {
namespace {

void expandOneSwitch(SwitchInst *SI) {
  Function *F = SI->getFunction();
  BasicBlock *BB = SI->getParent();
  Value *Cond = SI->getCondition();
  BasicBlock *Default = SI->getDefaultDest();

  struct CaseRec {
    ConstantInt *Val;
    BasicBlock *Succ;
  };
  SmallVector<CaseRec, 16> Cases;
  for (auto &C : SI->cases())
    Cases.push_back({C.getCaseValue(), C.getCaseSuccessor()});

  SI->eraseFromParent();

  if (Cases.empty()) {
    IRBuilder<> B(BB);
    B.CreateBr(Default);
    return;
  }

  BasicBlock *Fail = Default;
  for (int I = (int)Cases.size() - 1; I >= 0; --I) {
    BasicBlock *CmpBB =
        (I == 0) ? BB
                 : BasicBlock::Create(F->getContext(), "tess.sw", F, Fail);
    IRBuilder<> B(CmpBB);
    Value *Eq = B.CreateICmpEQ(Cond, Cases[I].Val);
    B.CreateCondBr(Eq, Cases[I].Succ, Fail);
    Fail = CmpBB;
  }
}

void expandSwitches(Function &F) {
  SmallVector<SwitchInst *, 8> Work;
  for (BasicBlock &BB : F)
    if (auto *SI = dyn_cast<SwitchInst>(BB.getTerminator()))
      Work.push_back(SI);
  for (SwitchInst *SI : Work)
    expandOneSwitch(SI);
}

bool flattenFunction(Function &F, Random &R) {
  if (!isFlattenable(F))
    return false;

  expandSwitches(F);
  demoteRegisters(F);

  BasicBlock *Entry = &F.getEntryBlock();
  auto It = Entry->begin();
  while (It != Entry->end() && isa<AllocaInst>(&*It))
    ++It;
  if (It == Entry->end())
    return false;

  BasicBlock *First = Entry->splitBasicBlock(It, "tess.fla.first");

  SmallVector<BasicBlock *, 32> OrigBBs;
  for (BasicBlock &BB : F)
    if (&BB != Entry)
      OrigBBs.push_back(&BB);

  if (OrigBBs.size() < 2)
    return false;

  DenseMap<BasicBlock *, uint32_t> CaseMap;
  DenseSet<uint32_t> Used;
  for (BasicBlock *BB : OrigBBs) {
    uint32_t C;
    do {
      C = R.next();
    } while (!Used.insert(C).second);
    CaseMap[BB] = C;
  }

  Instruction *EntryTerm = Entry->getTerminator();
  IRBuilder<> EntryB(EntryTerm);
  AllocaInst *SwVar = EntryB.CreateAlloca(EntryB.getInt32Ty(), nullptr, "tess.fla.sw");
  EntryB.CreateStore(EntryB.getInt32(CaseMap[First]), SwVar);
  EntryTerm->eraseFromParent();

  BasicBlock *LoopHdr =
      BasicBlock::Create(F.getContext(), "tess.fla.hdr", &F, First);
  BasicBlock *LoopEnd =
      BasicBlock::Create(F.getContext(), "tess.fla.end", &F, First);
  IRBuilder<>(Entry).CreateBr(LoopHdr);

  IRBuilder<> HdrB(LoopHdr);
  LoadInst *Ld = HdrB.CreateLoad(HdrB.getInt32Ty(), SwVar);
  SwitchInst *Disp = HdrB.CreateSwitch(Ld, LoopEnd, OrigBBs.size());

  for (BasicBlock *BB : OrigBBs) {
    BB->moveBefore(LoopEnd);
    Disp->addCase(HdrB.getInt32(CaseMap[BB]), BB);

    Instruction *Term = BB->getTerminator();
    if (!Term)
      continue;

    if (auto *Br = dyn_cast<BranchInst>(Term)) {
      IRBuilder<> TB(Term);
      if (Br->isUnconditional()) {
        BasicBlock *Succ = Br->getSuccessor(0);
        auto Found = CaseMap.find(Succ);
        if (Found == CaseMap.end())
          continue;
        TB.CreateStore(TB.getInt32(Found->second), SwVar);
        TB.CreateBr(LoopEnd);
        Term->eraseFromParent();
      } else {
        BasicBlock *TSucc = Br->getSuccessor(0);
        BasicBlock *FSucc = Br->getSuccessor(1);
        if (!CaseMap.count(TSucc) || !CaseMap.count(FSucc))
          continue;
        Value *Sel = TB.CreateSelect(Br->getCondition(),
                                     TB.getInt32(CaseMap[TSucc]),
                                     TB.getInt32(CaseMap[FSucc]));
        TB.CreateStore(Sel, SwVar);
        TB.CreateBr(LoopEnd);
        Term->eraseFromParent();
      }
    }
  }

  IRBuilder<>(LoopEnd).CreateBr(LoopHdr);
  return true;
}

}

PreservedAnalyses FlatteningPass::run(Module &M, ModuleAnalysisManager &) {
  bool Changed = false;
  Random R;
  for (Function &F : M) {
    if (!wantsObfuscation(F) || !isFlattenable(F))
      continue;
    if (isProtectedEntry(F) && !FlaEntry)
      continue;
    if (!isProtectedEntry(F) && countInstructions(F) > 4000)
      continue;
    verboseLog("fla", F);
    if (flattenFunction(F, R))
      Changed = true;
  }
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

}
