//
// Created by LeadroyaL on 2018/10/10.
//

#include "Transforms/Obfuscation/Obfuscation.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

#if LLVM_VERSION_MAJOR >= 13
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"

namespace llvm {

llvm::PassPluginLibraryInfo getOllvmPluginInfo() {
    return {
    LLVM_PLUGIN_API_VERSION, "Hikari", LLVM_VERSION_STRING,
        [](PassBuilder &PB) {
            // cryptoutils->prng_seed();
            PB.registerPipelineStartEPCallback(
                [](llvm::ModulePassManager &PM,
                    llvm::OptimizationLevel Level) {
                    // PM.addPass(AntiClassDumpPass()); /*only apple*/
                    // PM.addPass(FunctionCallObfuscatePass()); /*only apple*/
                    //PM.addPass(IndirectBranchPass());
                    PM.addPass(MyTestIRPass());

                    //llvm::FunctionPassManager FPM;
                    //FPM.addPass(BogusControlFlowPass());
                    // FPM.addPass(FlatteningPass());
                    // FPM.addPass(SplitBasicBlockPass());
                    // FPM.addPass(SubstitutionPass());
                    //PM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));
                });

            PB.registerOptimizerLastEPCallback(
                [](llvm::ModulePassManager &PM,
                    llvm::OptimizationLevel Level) {
                    //PM.addPass(MyTestIRPass());
                    //PM.addPass(StringEncryptionPass());
                    //PM.addPass(FunctionWrapperPass());
                });




    }};
}

extern "C" LLVM_ATTRIBUTE_WEAK llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
    return getOllvmPluginInfo();
}

}
#else
using namespace llvm;

static void registerHikariModulePass(const PassManagerBuilder &,
                              legacy::PassManagerBase &PM) {
//    PM.add(createFunctionWrapperPass(true)); /*broken*/
    PM.add(createStringEncryptionPass(true));

}

static void registerHikariFunctionPass(const PassManagerBuilder &,
                              legacy::PassManagerBase &PM) {
    PM.add(createBogusControlFlowPass(true));
#if LLVM_VERSION_MAJOR >= 9
    PM.add(createLowerSwitchPass());
#endif
    PM.add(createFlatteningPass(true));
    PM.add(createFunctionCallObfuscatePass(true));
    PM.add(createIndirectBranchPass(true));
    PM.add(createSplitBasicBlockPass(true));
    PM.add(createSubstitutionPass(true));
}

static RegisterStandardPasses
        RegisterMyPass(PassManagerBuilder::EP_EnabledOnOptLevel0,
                       registerHikariModulePass);
static RegisterStandardPasses
        RegisterMyPass0(PassManagerBuilder::EP_OptimizerLast,
                        registerHikariModulePass);
static RegisterStandardPasses
        RegisterMyPass1(PassManagerBuilder::EP_EarlyAsPossible,
                        registerHikariFunctionPass);
#endif