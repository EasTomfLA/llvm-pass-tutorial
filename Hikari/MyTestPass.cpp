#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Value.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include "Transforms/Obfuscation/Obfuscation.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
using namespace llvm;
using namespace std;
namespace llvm {
#if LLVM_VERSION_MAJOR >= 13

bool mergeBlocks(llvm::BasicBlock* father_block, llvm::BasicBlock* son_block) {
    // 1. 移除目标块的终结指令（如果需要）
    if (llvm::Instruction* term = father_block->getTerminator()) {
        term->eraseFromParent();
    }
    
    // 2. 移动源块的所有指令到目标块
    while (!son_block->empty()) {
        llvm::Instruction& inst = son_block->front();
        inst.removeFromParent();
        father_block->getInstList().push_back(&inst);
    }
    
    // 3. 替换对源块的所有引用
    son_block->replaceAllUsesWith(son_block);
    
    // 4. 删除空的源块
    son_block->eraseFromParent();
    
    return true;
}

void protectBasicBlocksWithName(
    llvm::Module& M, 
    const std::vector<llvm::BasicBlock*>& blocks,
    const std::string& tableName = "ProtectedBasicBlockTable"
) {
    if (blocks.empty()) return;
    
    std::vector<llvm::Constant*> blockAddresses;
    blockAddresses.reserve(blocks.size());
    
    // 为每个块创建BlockAddress
    for (llvm::BasicBlock* block : blocks) {
        if (block && block->getParent()) {
            llvm::BlockAddress* addr = llvm::BlockAddress::get(
                block->getParent(), block
            );
            blockAddresses.push_back(addr);
        }
    }
    
    if (blockAddresses.empty()) return;
    
    // 创建保护表
    llvm::ArrayType* AT = llvm::ArrayType::get(
        llvm::Type::getInt8PtrTy(M.getContext()), 
        blockAddresses.size()
    );
    
    llvm::Constant* BlockAddressArray = llvm::ConstantArray::get(
        AT, 
        llvm::ArrayRef<llvm::Constant*>(blockAddresses)
    );
    
    llvm::GlobalVariable* Table = new llvm::GlobalVariable(
        M, AT, false, 
        llvm::GlobalValue::LinkageTypes::InternalLinkage,
        BlockAddressArray, 
        tableName
    );
    
    // 防止优化器删除
    appendToCompilerUsed(M, {Table});
}


PreservedAnalyses MyTestIRPass::run(Module &M, ModuleAnalysisManager& AM) {
    errs() << "Running MyTestIRPass On Module " << M.getName() << "\n";

    LLVMContext &Context = M.getContext();


    //setup jump_function
    IRBuilder<> module_IRB(Context);
    FunctionType *VOID_FT_NO_ARG = FunctionType::get(Type::getVoidTy(Context), false);
    Function *JF = Function::Create(VOID_FT_NO_ARG, Function::ExternalLinkage, "Jump_function", M);

    BasicBlock *BB = BasicBlock::Create(Context, "entry", JF);
    module_IRB.SetInsertPoint(BB);
    std::string AsmStr = R"(stp x0, x1, [sp, #-0x10]!
        ldr w0, [x30, w0, uxtw #2]
        add x30, x30, w0, uxtw
        ldp x0, x1, [sp], #0x10)";
    std::string Constraints = "";
    
    InlineAsm *IA = InlineAsm::get(
        VOID_FT_NO_ARG,
        AsmStr,
        Constraints,
        true,    // 有副作用
        false,   // 不对齐栈
        InlineAsm::AD_ATT,
        false    
    );
    module_IRB.CreateCall(IA);
    module_IRB.CreateRetVoid();


    vector<BasicBlock* > modify_origin_BBs;
    for (auto F = M.begin(); F != M.end(); F++) {
        errs() << "Running MyTestIRPass On Function" << F->getName() << "\n";
        if (&*F == JF || F->hasFnAttribute(Attribute::AlwaysInline) || F->isDeclaration() ||
        F->isIntrinsic()) {
            continue;
        }
        F->addFnAttr(llvm::Attribute::OptimizeNone);

        //collect target block
        int block_index_count = 0;
        vector<BasicBlock* > target_origin_BBs;
        for (auto origin_BB = F->begin(); origin_BB != F->end(); origin_BB++, block_index_count++) {
            if (&*origin_BB == &(F->getEntryBlock())){
                errs() << "Block " << std::to_string(block_index_count) << " is Entry" << "\n";
                continue;
            }
            if (auto *BI = dyn_cast<BranchInst>(origin_BB->getTerminator())){
                if (BI->isUnconditional()) {
                    errs() << "adding Block " << std::to_string(block_index_count) << " to target\n";
                    target_origin_BBs.push_back(&*origin_BB);
                }
                else 
                    errs() << "Block " << std::to_string(block_index_count) << " is conditional" << "\n";
            }
            else{
                errs() << "Block " << std::to_string(block_index_count) << " is not with BI-Terminator" << "\n";
            }
        }

        
        //init jump_block
        errs() << "init jump_block\n";
        BasicBlock *JBB = BasicBlock::Create(Context, "jump_block", &*F);
        IRBuilder<> JBB_IRB(JBB);
        JBB_IRB.CreateCall(JF);
        JBB->moveAfter(&F->getEntryBlock());
        BlockAddress* JBA = BlockAddress::get(JBB);
        //setup protect_block
        // BasicBlock *PBB = BasicBlock::Create(Context, "protect_block", &*F);
        // IRBuilder<> PBB_IRB(PBB);

        // llvm::Value* SwitchValue = PBB_IRB.getInt32(0);
        // llvm::SwitchInst* Switch = PBB_IRB.CreateSwitch(
        //     SwitchValue, JBB, target_origin_BBs.size());
        // for (size_t i = 0; i < target_origin_BBs.size(); ++i) {
        //     llvm::ConstantInt* CaseVal = PBB_IRB.getInt32(i + 1);  // 1, 2, 3, ...
        //     Switch->addCase(CaseVal, target_origin_BBs[i]);
        // }
        // PBB_IRB.CreateUnreachable();


        // llvm::BlockAddress* PhantomBA = llvm::BlockAddress::get(PBB);
        // llvm::GlobalVariable* Anchor = new llvm::GlobalVariable(
        //     M,
        //     llvm::Type::getInt8PtrTy(Context),
        //     true,  // 常量
        //     llvm::GlobalValue::InternalLinkage,
        //     llvm::ConstantExpr::getBitCast(PhantomBA, llvm::Type::getInt8PtrTy(Context)),
        //     F->getName() + ".phantom_anchor"
        // );
        // Anchor->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::None);
        

        

        // BasicBlock *JODBB = BasicBlock::Create(Context, "jump_offset_data_block", &*F);
        // JODBB->moveAfter(JBB);
        // needed_protected_blocks.push_back(JODBB);


        // //llvm::Type* intPtrType = llvm::Type::getIntNTy(Context,
        //     M.getDataLayout().getPointerSizeInBits());
        //llvm::Constant* JBA_CONST = llvm::ConstantExpr::getPtrToInt(JBA, int32Type);
        // llvm::Constant* JBA_CONST = llvm::ConstantExpr::getPtrToInt(JBA, intPtrType);
        // IRBuilder<> JODBB_IRB(JODBB);


        int modify_blockIndex = 0;
        int sindex = 0;
        for (auto origin_BB = target_origin_BBs.begin(); origin_BB != target_origin_BBs.end(); origin_BB++) {
            BasicBlock *BBPtr = *origin_BB;

            //add block offset data
            BlockAddress* TBA = BlockAddress::get(BBPtr);
            std::string data_str = ".long ($0 - $1)";  // 32位数据
            llvm::InlineAsm* data_asm = llvm::InlineAsm::get(
                VOID_FT_NO_ARG,
                data_str,                    // 汇编字符串
                "i,i",                       // 约束：i = 立即数
                true,                      // hasSideEffects = true（确保不被优化掉）
                false,                     // isAlignStack = false
                llvm::InlineAsm::AD_ATT    // 汇编方言
            );
            JBB_IRB.CreateCall(data_asm, {TBA, JBA});

            //add jump function arg to block
            auto *BI = dyn_cast<BranchInst>(BBPtr->getTerminator());
            IRBuilder<> modifyBlock_IRB(BI);
            std::string setIndexAsmStr = "mov w0, #" + std::to_string(modify_blockIndex);
            InlineAsm *setIndexIA = InlineAsm::get(
                VOID_FT_NO_ARG,
                setIndexAsmStr,
                "",
                true,    // 有副作用
                false,   // 不对齐栈
                InlineAsm::AD_ATT,
                false    
            );
            modifyBlock_IRB.CreateCall(setIndexIA);

            //set dest to jump_function
            Instruction* terminator = BBPtr->getTerminator();
            terminator->setSuccessor(0, JBB);
            //errs() << "Create Ret for " << F->getName() << "\n";
            modify_blockIndex++;
            modify_origin_BBs.push_back(BBPtr);
        }

        //end Jump block
        JBB_IRB.CreateUnreachable();


        errs() << "finish MyTestIRPass On Function" << F->getName() << "\n";
    }


    //setup protect_function
    // Function *PF = Function::Create(VOID_FT_NO_ARG, Function::ExternalLinkage, "protect_function", M);

    // BasicBlock *PF_BB = BasicBlock::Create(Context, "entry", PF);
    // module_IRB.SetInsertPoint(PF_BB);
    // llvm::Value* SwitchValue = module_IRB.getInt32(0);
    // llvm::SwitchInst* Switch = module_IRB.CreateSwitch(
    //     SwitchValue, PF_BB, modify_origin_BBs.size());
    // for (size_t i = 0; i < modify_origin_BBs.size(); ++i) {
    //     llvm::ConstantInt* CaseVal = module_IRB.getInt32(i + 1);  // 1, 2, 3, ...
    //     Switch->addCase(CaseVal, modify_origin_BBs[i]);
    // }
    // module_IRB.CreateRetVoid();


    errs() << "finish MyTestIRPass On Module" << M.getName() << "\n";
    return PreservedAnalyses::all();
}
#endif
}
