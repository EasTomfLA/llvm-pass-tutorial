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


PreservedAnalyses MyTestIRPass::run(Module &M, ModuleAnalysisManager& AM) {
    errs() << "Running MyTestIRPass On Module " << M.getName() << "\n";

    LLVMContext &Context = M.getContext();


    //setup jump_function
    IRBuilder<> module_IRB(Context);
    FunctionType *VOID_FT_NO_ARG = FunctionType::get(Type::getVoidTy(Context), false);
    Function *JF = Function::Create(VOID_FT_NO_ARG, Function::ExternalLinkage, "Jump_function", M);

    BasicBlock *JF_BB = BasicBlock::Create(Context, "entry", JF);
    module_IRB.SetInsertPoint(JF_BB);
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

    
    CallInst* jf_ci = module_IRB.CreateCall(IA);
    module_IRB.CreateRetVoid();

    BlockAddress* JF_BA = BlockAddress::get(JF_BB);


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
                    bool is_PHI = false;
                    BasicBlock* successor_block = BI->getSuccessor(0);
                    for (auto &Phi : successor_block->phis()) {
                        for (unsigned i = 0; i < Phi.getNumIncomingValues(); ++i) {
                            if (Phi.getIncomingBlock(i) == &*origin_BB) {
                                errs() << "Block " << std::to_string(block_index_count) << " relate with PHI, ingore" << "\n";
                                is_PHI = true;
                                break;
                            }
                        }
                        if (is_PHI) break;
                    }
                    if (!is_PHI){
                        errs() << "adding Block " << std::to_string(block_index_count) << " to target\n";
                        target_origin_BBs.push_back(&*origin_BB);
                    }
                }
                else 
                    errs() << "Block " << std::to_string(block_index_count) << " is conditional" << "\n";
            }
            else{
                errs() << "Block " << std::to_string(block_index_count) << " is not with BI-Terminator" << "\n";
            }
        }
        if (target_origin_BBs.empty())
            continue;

        // BasicBlock *OBB = BasicBlock::Create(Context, "offset_block", &*F);
        // BlockAddress* OBA = BlockAddress::get(OBB);
        // IRBuilder<> OBB_IRB(OBB);

        
        // target_origin_BBs.push_back(OBB);

        //init jump_block
        errs() << "init jump_block\n";

        std::string jb_AsmStr = "bl #$0";
        
        InlineAsm *jb_IA = InlineAsm::get(
            VOID_FT_NO_ARG,
            jb_AsmStr,
            "i",
            true,    // 有副作用
            false,   // 不对齐栈
            InlineAsm::AD_ATT,
            false    
        );

        std::string jb_empty_AsmStr = "";
        
        InlineAsm *jb_empty_IA = InlineAsm::get(
            VOID_FT_NO_ARG,
            jb_empty_AsmStr,
            "",
            true,    // 有副作用
            false,   // 不对齐栈
            InlineAsm::AD_ATT,
            false    
        );

        //BasicBlock *LJF_BB = BasicBlock::Create(Context, "local_jf", &*F);
        // IRBuilder<> LJF_BB_IRB(LJF_BB);
        // std::string LJF_BB_AsmStr = R"(stp x0, x1, [sp, #-0x10]!
        //     ldr w0, [x30, w0, uxtw #2]
        //     add x30, x30, w0, uxtw
        //     ldp x0, x1, [sp], #0x10)";
        // std::string LJF_BB_Constraints = "";
        
        // InlineAsm *LJF_IA = InlineAsm::get(
        //     VOID_FT_NO_ARG,
        //     LJF_BB_AsmStr,
        //     LJF_BB_Constraints,
        //     true,    // 有副作用
        //     false,   // 不对齐栈
        //     InlineAsm::AD_ATT,
        //     false    
        // );

        // LJF_BB_IRB.CreateCall(LJF_IA);
        // LJF_BB_IRB.CreateRetVoid();

        BasicBlock *LJF_BB = BasicBlock::Create(Context, "local_jf", &*F);
        IRBuilder<> LJF_BB_IRB(LJF_BB);
        LJF_BB_IRB.CreateUnreachable();


        BasicBlock *JBB = BasicBlock::Create(Context, "jump_block", &*F);
        IRBuilder<> JBB_IRB(JBB);
        JBB_IRB.CreateCall(jb_IA, {JF_BA});

        JBB->moveAfter(&F->getEntryBlock());
        // OBB->moveAfter(JBB);
        LJF_BB->moveAfter(JBB);
        //JBB_IRB.CreateUnreachable();
        BlockAddress* JBA = BlockAddress::get(JBB);


        // llvm::Value* SwitchValue = JBB_IRB.getInt32(0);
        // llvm::SwitchInst* Switch = JBB_IRB.CreateSwitch(SwitchValue, JBB, target_origin_BBs.size());
        // for (size_t i = 0; i < target_origin_BBs.size(); ++i) {
        //     llvm::ConstantInt* CaseVal = JBB_IRB.getInt32(i + 1);  // 1, 2, 3, ...
        //     Switch->addCase(CaseVal, target_origin_BBs[i]);
        // }





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



        llvm::Constant* HEAD_OFFSET = JBB_IRB.getInt32(4);
        int modify_blockIndex = 0;
        int sindex = 0;
        vector<BasicBlock*> wait_add_Ref_blocks;
        set<BasicBlock*> modify_block_successors_set;
        for (auto origin_BB = target_origin_BBs.begin(); origin_BB != target_origin_BBs.end(); origin_BB++) {
            BasicBlock *BBPtr = *origin_BB;
            // if (BBPtr == OBB) continue;

            //add block offset data to table
            BlockAddress* TBA = BlockAddress::get(BBPtr);
            std::string data_str = ".long ($0 - $1 - $2)";  // 32位数据
            llvm::InlineAsm* data_asm = llvm::InlineAsm::get(
                VOID_FT_NO_ARG,
                data_str,                    // 汇编字符串
                "i,i,i",                       // 约束：i = 立即数
                true,                      // hasSideEffects = true（确保不被优化掉）
                false,                     // isAlignStack = false
                llvm::InlineAsm::AD_ATT    // 汇编方言
            );
            JBB_IRB.CreateCall(data_asm, {TBA, JBA, HEAD_OFFSET});


            //add index data after block
            BasicBlock *index_data_BB = BasicBlock::Create(Context, "", &*F);
            index_data_BB->moveAfter(BBPtr);
            IRBuilder<> index_data_BB_IRB(index_data_BB);
            BlockAddress* index_data_BA = BlockAddress::get(index_data_BB);
            std::string index_data_str = ".long $0";  // 32位数据
            llvm::InlineAsm* index_data_IA = llvm::InlineAsm::get(
                VOID_FT_NO_ARG,
                index_data_str,                    // 汇编字符串
                "i",                       // 约束：i = 立即数
                true,                      // hasSideEffects = true（确保不被优化掉）
                false,                     // isAlignStack = false
                llvm::InlineAsm::AD_ATT    // 汇编方言
            );

            llvm::Constant* TABLE_INDEX = JBB_IRB.getInt32(modify_blockIndex);
            index_data_BB_IRB.CreateCall(index_data_IA, {TABLE_INDEX});
            index_data_BB_IRB.CreateUnreachable();
            wait_add_Ref_blocks.push_back(index_data_BB);

            
            //add jump function arg to block V1
            auto *BI = dyn_cast<BranchInst>(BBPtr->getTerminator());
            llvm::Constant* TABLE_OFFSET = JBB_IRB.getInt32(modify_blockIndex);
            IRBuilder<> modifyBlock_IRB(BI);
            std::string setIndexAsmStr = "mov w0, #$0";
            InlineAsm *setIndexIA = InlineAsm::get(
                VOID_FT_NO_ARG,
                setIndexAsmStr,
                "i,~{w0}",
                true,    // 有副作用
                false,   // 不对齐栈
                InlineAsm::AD_ATT,
                false    
            );

            modifyBlock_IRB.CreateCall(setIndexIA, {TABLE_OFFSET});


            //add jump function arg to block V2
            // auto *BI = dyn_cast<BranchInst>(BBPtr->getTerminator());
            // IRBuilder<> modifyBlock_IRB(BI);
            // llvm::Constant* index_data_CONST = llvm::ConstantExpr::getPtrToInt(index_data_BA, modifyBlock_IRB.getInt32Ty());
            // LoadInst *LoadIndexData = modifyBlock_IRB.CreateLoad(modifyBlock_IRB.getInt32Ty(), index_data_CONST);




            //set dest to jump_function
            Instruction* terminator = BBPtr->getTerminator();
            BasicBlock* successor_block = terminator->getSuccessor(0);

            // if (!successor_block->hasNPredecessorsOrMore(2)){
            //     if (modify_block_successors_set.find(successor_block) == modify_block_successors_set.end()){
            //         modify_block_successors_set.insert(terminator->getSuccessor(0));
            //     }
            // }

            if (modify_block_successors_set.find(successor_block) == modify_block_successors_set.end()){
                modify_block_successors_set.insert(terminator->getSuccessor(0));
            }


            terminator->setSuccessor(0, JBB);
            //errs() << "Create Ret for " << F->getName() << "\n";
            modify_blockIndex++;
            modify_origin_BBs.push_back(BBPtr);
        }

        //end Jump block
        for (auto it = modify_block_successors_set.begin(); it != modify_block_successors_set.end(); it++){
            wait_add_Ref_blocks.push_back(*it);

        }
        JBB_IRB.CreateCallBr(jb_empty_IA, LJF_BB, wait_add_Ref_blocks);
        //OBB_IRB.CreateUnreachable();

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
