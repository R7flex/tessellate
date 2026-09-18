#include "Utils.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Metadata.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/Local.h"

#include <cstdlib>
#include <ctime>

using namespace llvm;

static cl::opt<bool> TessVerbose(
    "tess-verbose", cl::init(false), cl::Hidden,
    cl::desc("Log pass visits (or set TESS_VERBOSE=1)"));

static cl::opt<bool> TessOnlyAnnotated(
    "tess-only-annotated", cl::init(false), cl::Hidden,
    cl::desc("Only obfuscate functions with tess_obf / tess_protect"));

static cl::opt<bool> TessProtectEntry(
    "tess-protect-entry", cl::init(true), cl::Hidden,
    cl::desc("Always obfuscate DllMain / tess_protect entries"));

static cl::opt<unsigned> TessMinInst(
    "tess-min-inst", cl::init(3), cl::Hidden,
    cl::desc("Skip functions smaller than this (unless protected)"));

static cl::opt<unsigned> TessBcfProb(
    "tess-bcf-prob", cl::init(20), cl::Hidden,
    cl::desc("Bogus CF insertion probability percent (0-100)"));

static cl::opt<unsigned> TessSubProb(
    "tess-sub-prob", cl::init(55), cl::Hidden,
    cl::desc("Instruction substitution probability percent (0-100)"));

static cl::opt<unsigned> TessBcfMax(
    "tess-bcf-max", cl::init(8), cl::Hidden,
    cl::desc("Max bogus CF insertions per function"));

namespace tess {
namespace {

ObfConfig GConfig;

bool hasStringAttribute(const Function &F, StringRef Key) {
  return F.hasFnAttribute(Key);
}

bool annotationStringContains(const Constant *C, StringRef Needle) {
  if (!C)
    return false;
  if (auto *GV = dyn_cast<GlobalVariable>(C->stripPointerCasts())) {
    if (auto *CDS = dyn_cast_or_null<ConstantDataSequential>(GV->getInitializer())) {
      if (CDS->isString() && CDS->getAsString().contains(Needle))
        return true;
    }
  }
  return false;
}

bool hasAnnotation(const Function &F, StringRef Needle) {
  if (auto *MD = F.getMetadata("annotation")) {
    for (unsigned I = 0, E = MD->getNumOperands(); I != E; ++I) {
      if (auto *S = dyn_cast<MDString>(MD->getOperand(I))) {
        if (S->getString().contains(Needle))
          return true;
      } else if (auto *Tup = dyn_cast<MDNode>(MD->getOperand(I))) {
        for (unsigned J = 0, JE = Tup->getNumOperands(); J != JE; ++J) {
          if (auto *S2 = dyn_cast<MDString>(Tup->getOperand(J))) {
            if (S2->getString().contains(Needle))
              return true;
          }
        }
      }
    }
  }

  const Module *M = F.getParent();
  if (!M)
    return false;
  const GlobalVariable *GA = M->getGlobalVariable("llvm.global.annotations");
  if (!GA || !GA->hasInitializer())
    return false;
  const auto *CA = dyn_cast<ConstantArray>(GA->getInitializer());
  if (!CA)
    return false;
  for (unsigned I = 0, E = CA->getNumOperands(); I != E; ++I) {
    const auto *Struct = dyn_cast<ConstantStruct>(CA->getOperand(I));
    if (!Struct || Struct->getNumOperands() < 2)
      continue;
    const Value *Annotated =
        Struct->getOperand(0)->stripPointerCasts();
    if (Annotated != &F)
      continue;
    if (annotationStringContains(dyn_cast<Constant>(Struct->getOperand(1)),
                                 Needle))
      return true;
  }
  return false;
}

bool valueEscapes(const Instruction &Inst) {
  const BasicBlock *BB = Inst.getParent();
  for (const User *U : Inst.users()) {
    const Instruction *UI = cast<Instruction>(U);
    if (UI->getParent() != BB || isa<PHINode>(UI))
      return true;
  }
  return false;
}

}

Random::Random(uint64_t Seed) { reseed(Seed ? Seed : 0xC0FFEEULL ^ (uint64_t)std::time(nullptr)); }

void Random::reseed(uint64_t Seed) {
  State = Seed ? Seed : 0xDEADBEEFCAFEBABEULL;
}

uint32_t Random::next() {
  State ^= State >> 12;
  State ^= State << 25;
  State ^= State >> 27;
  return (uint32_t)((State * 0x2545F4914F6CDD1DULL) >> 32);
}

uint32_t Random::nextRange(uint32_t MinIncl, uint32_t MaxIncl) {
  if (MaxIncl <= MinIncl)
    return MinIncl;
  return MinIncl + (next() % (MaxIncl - MinIncl + 1));
}

bool Random::chance(unsigned Percent) {
  if (Percent >= 100)
    return true;
  if (Percent == 0)
    return false;
  return (next() % 100) < Percent;
}

ObfConfig &config() {
  GConfig.MinInstructions = TessMinInst;
  GConfig.BcfProbability = TessBcfProb;
  GConfig.SubProbability = TessSubProb;
  GConfig.BcfMaxPerFunction = TessBcfMax;
  GConfig.ProtectEntry = TessProtectEntry;
  GConfig.OnlyAnnotated = TessOnlyAnnotated;
  return GConfig;
}

bool verboseEnabled() {
  if (TessVerbose)
    return true;
  if (const char *Env = std::getenv("TESS_VERBOSE"))
    return Env[0] == '1' || Env[0] == 'y' || Env[0] == 'Y';
  return false;
}

bool isRuntimeHelperName(StringRef N) {
  static constexpr const char *KDeny[] = {
      "memcpy",         "memmove",        "memset",           "memcmp",
      "memchr",         "strlen",         "wcslen",           "atexit",
      "_fltused",       "__chkstk",       "__alloca_probe",   "__C_specific_handler",
      "_CxxThrowException", "__std_terminate", "_purecall",
  };
  for (const char *D : KDeny) {
    if (N == D)
      return true;
  }
  if (N.starts_with("__tess") || N.starts_with("tess."))
    return true;
  if (N.starts_with("__security_") || N.starts_with("_RTC_") ||
      N.starts_with("__GS") || N.starts_with("_chkstk") || N.starts_with("__chkstk"))
    return true;
  return false;
}

bool shouldSkipFunction(const Function &F) {
  if (F.isDeclaration() || F.isIntrinsic())
    return true;
  if (F.hasFnAttribute(Attribute::OptimizeNone))
    return true;
  if (hasStringAttribute(F, "tess_skip") || hasAnnotation(F, "tess_skip")) {
    if (verboseEnabled())
      errs() << "[tess:skip] " << F.getName() << "\n";
    return true;
  }
  if (isRuntimeHelperName(F.getName()))
    return true;
  return false;
}

bool isProtectedEntry(const Function &F) {
  if (hasStringAttribute(F, "tess_protect") || hasAnnotation(F, "tess_protect"))
    return true;
  StringRef N = F.getName();
  if (N == "DllMain" || N == "DllEntryPoint" || N == "DriverEntry")
    return true;
  if (N.contains("DllMain"))
    return true;
  return false;
}

bool wantsObfuscation(const Function &F) {
  if (shouldSkipFunction(F))
    return false;

  const ObfConfig &C = config();
  const bool Protected = isProtectedEntry(F);
  if (Protected && C.ProtectEntry)
    return true;

  if (C.OnlyAnnotated)
    return hasStringAttribute(F, "tess_obf") || hasAnnotation(F, "tess_obf") ||
           Protected;

  if (hasStringAttribute(F, "tess_obf") || hasAnnotation(F, "tess_obf"))
    return true;

  if (!Protected && countInstructions(F) < C.MinInstructions)
    return false;

  return true;
}

bool hasExceptionHandling(const Function &F) {
  if (F.hasPersonalityFn())
    return true;
  for (const BasicBlock &BB : F) {
    if (BB.isEHPad())
      return true;
    for (const Instruction &I : BB) {
      if (isa<InvokeInst>(I) || isa<ResumeInst>(I) || isa<CatchReturnInst>(I) ||
          isa<CleanupReturnInst>(I) || isa<LandingPadInst>(I) ||
          isa<CatchPadInst>(I) || isa<CleanupPadInst>(I))
        return true;
    }
  }
  return false;
}

bool isFlattenable(const Function &F) {
  if (shouldSkipFunction(F))
    return false;
  if (hasExceptionHandling(F))
    return false;
  if (F.size() < 2)
    return false;
  if (F.hasFnAttribute(Attribute::Naked))
    return false;
  return true;
}

unsigned countInstructions(const Function &F) {
  unsigned N = 0;
  for (const Instruction &I : instructions(F)) {
    (void)I;
    ++N;
  }
  return N;
}

void demoteRegisters(Function &F) {
  SmallVector<PHINode *, 16> Phis;
  for (BasicBlock &BB : F)
    for (Instruction &I : BB)
      if (auto *PN = dyn_cast<PHINode>(&I))
        Phis.push_back(PN);
  for (PHINode *PN : Phis)
    DemotePHIToStack(PN);

  SmallVector<Instruction *, 32> Escaped;
  for (BasicBlock &BB : F) {
    for (Instruction &I : BB) {
      if (isa<AllocaInst>(I) || isa<PHINode>(I))
        continue;
      if (I.getType()->isVoidTy())
        continue;
      if (valueEscapes(I))
        Escaped.push_back(&I);
    }
  }
  for (Instruction *I : Escaped)
    DemoteRegToStack(*I, false);
}

void verboseLog(StringRef passName, const Function &F) {
  if (!verboseEnabled())
    return;
  errs() << "[tess:" << passName << "] " << F.getName()
         << (isProtectedEntry(F) ? " (protected)" : "") << "\n";
}

}
