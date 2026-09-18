#include "Passes.h"
#include "Utils.h"

#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

static cl::opt<bool> EnableFla(
    "tess-fla", cl::init(false), cl::Hidden,
    cl::desc("Enable control-flow flattening"));

static cl::opt<bool> EnableBcf(
    "tess-bcf", cl::init(false), cl::Hidden,
    cl::desc("Enable bogus control flow"));

static cl::opt<bool> EnableSub(
    "tess-sub", cl::init(false), cl::Hidden,
    cl::desc("Enable instruction substitution"));

static cl::opt<bool> EnableTrim(
    "tess-trim", cl::init(false), cl::Hidden,
    cl::desc("Enable IR size-trim helpers"));

namespace tess {
namespace {

void addEnabledPasses(ModulePassManager &MPM) {
  if (EnableSub)
    MPM.addPass(SubstitutionPass());
  if (EnableBcf)
    MPM.addPass(BogusCFPass());
  if (EnableFla)
    MPM.addPass(FlatteningPass());
  if (EnableTrim)
    MPM.addPass(SizeTrimPass());
}

}

void registerTessPasses(PassBuilder &PB) {
  PB.registerPipelineParsingCallback(
      [](StringRef Name, ModulePassManager &MPM,
         ArrayRef<PassBuilder::PipelineElement>) {
        if (Name == "tess-obf") {
          if (!EnableFla && !EnableBcf && !EnableSub && !EnableTrim) {
            EnableSub = true;
            EnableBcf = true;
            EnableFla = true;
            EnableTrim = true;
          }
          addEnabledPasses(MPM);
          return true;
        }
        if (Name == "tess-fla") {
          EnableFla = true;
          MPM.addPass(FlatteningPass());
          return true;
        }
        if (Name == "tess-bcf") {
          EnableBcf = true;
          MPM.addPass(BogusCFPass());
          return true;
        }
        if (Name == "tess-sub") {
          EnableSub = true;
          MPM.addPass(SubstitutionPass());
          return true;
        }
        if (Name == "tess-trim") {
          EnableTrim = true;
          MPM.addPass(SizeTrimPass());
          return true;
        }
        return false;
      });

  PB.registerPipelineEarlySimplificationEPCallback(
      [](ModulePassManager &MPM, OptimizationLevel) {
        if (!EnableFla && !EnableBcf && !EnableSub && !EnableTrim)
          return;
        addEnabledPasses(MPM);
      });
}

}

llvm::PassPluginLibraryInfo getTessObfPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "LLVMObfuscationx", "0.2.0",
          [](PassBuilder &PB) { tess::registerTessPasses(PB); }};
}

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return getTessObfPluginInfo();
}
