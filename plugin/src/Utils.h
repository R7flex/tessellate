#pragma once

#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"

#include <cstdint>

namespace llvm {
class BasicBlock;
class Instruction;
class Value;
}

namespace tess {

class Random {
public:
  explicit Random(uint64_t Seed = 0);
  void reseed(uint64_t Seed);
  uint32_t next();
  uint32_t nextRange(uint32_t MinIncl, uint32_t MaxIncl);
  bool chance(unsigned Percent);

private:
  uint64_t State;
};

bool shouldSkipFunction(const llvm::Function &F);
bool wantsObfuscation(const llvm::Function &F);
bool isProtectedEntry(const llvm::Function &F);
bool hasExceptionHandling(const llvm::Function &F);
bool isFlattenable(const llvm::Function &F);
unsigned countInstructions(const llvm::Function &F);
void demoteRegisters(llvm::Function &F);
void verboseLog(llvm::StringRef passName, const llvm::Function &F);
bool verboseEnabled();

struct ObfConfig {
  unsigned MinInstructions = 3;
  unsigned BcfProbability = 35;
  unsigned SubProbability = 55;
  unsigned BcfMaxPerFunction = 8;
  bool ProtectEntry = true;
  bool OnlyAnnotated = false;
};

ObfConfig &config();

}
