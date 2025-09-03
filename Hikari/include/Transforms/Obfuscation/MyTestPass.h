#ifndef _MY_TEST_PASS_H_
#define _MY_TEST_PASS_H_
#include "llvm/Pass.h"
#include "llvm/IR/LegacyPassManager.h"
using namespace std;
using namespace llvm;

// Namespace
namespace llvm {
#if LLVM_VERSION_MAJOR >= 13
	class MyTestIRPass : public PassInfoMixin<IndirectBranchPass>{ 
        public:
            PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
            static bool isRequired() { return true; }
	};
#endif
}
#endif
