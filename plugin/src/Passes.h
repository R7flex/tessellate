#pragma once

#include "llvm/IR/PassManager.h"

namespace llvm {
class PassBuilder;
}

namespace tess {

struct FlatteningPass : public llvm::PassInfoMixin<FlatteningPass> {
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);
  static bool isRequired() { return true; }
};

struct BogusCFPass : public llvm::PassInfoMixin<BogusCFPass> {
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);
  static bool isRequired() { return true; }
};

struct SubstitutionPass : public llvm::PassInfoMixin<SubstitutionPass> {
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);
  static bool isRequired() { return true; }
};

struct SizeTrimPass : public llvm::PassInfoMixin<SizeTrimPass> {
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);
  static bool isRequired() { return true; }
};

void registerTessPasses(llvm::PassBuilder &PB);

} // namespace tess
