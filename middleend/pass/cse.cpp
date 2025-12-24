#include <middleend/pass/cse.h>
#include <middleend/module/ir_block.h>
#include <middleend/module/ir_instruction.h>
#include <middleend/module/ir_operand.h>
#include <middleend/pass/analysis/analysis_manager.h>
#include <middleend/pass/analysis/cfg.h>
#include <middleend/pass/analysis/dominfo.h>
#include <middleend/visitor/utils/operand_replace_visitor.h>
#include <middleend/visitor/utils/use_def_visitor.h>
#include <transfer.h>
#include <unordered_map>
#include <unordered_set>
#include <sstream>
#include <algorithm>

namespace ME
{
    namespace
    {
        bool isCommutativeOp(Operator op)
        {
            switch (op)
            {
                case Operator::ADD:
                case Operator::MUL:
                case Operator::FADD:
                case Operator::FMUL:
                case Operator::BITAND:
                case Operator::BITXOR: return true;
                default: return false;
            }
        }

        bool isCSECandidate(Instruction* inst)
        {
            switch (inst->opcode)
            {
                case Operator::ADD:
                case Operator::SUB:
                case Operator::MUL:
                case Operator::DIV:
                case Operator::MOD:
                case Operator::FADD:
                case Operator::FSUB:
                case Operator::FMUL:
                case Operator::FDIV:
                case Operator::BITAND:
                case Operator::BITXOR:
                case Operator::SHL:
                case Operator::ASHR:
                case Operator::LSHR:
                case Operator::ICMP:
                case Operator::FCMP:
                case Operator::GETELEMENTPTR:
                case Operator::ZEXT:
                case Operator::SITOFP:
                case Operator::FPTOSI: return true;
                default: return false;
            }
        }

        std::string operandKey(Operand* op)
        {
            if (!op) return "null";
            std::stringstream ss;
            switch (op->getType())
            {
                case OperandType::REG:
                    ss << "r" << static_cast<RegOperand*>(op)->regNum;
                    break;
                case OperandType::IMMEI32:
                    ss << "i32:" << static_cast<ImmeI32Operand*>(op)->value;
                    break;
                case OperandType::IMMEF32:
                    ss << "f32:" << FLOAT_TO_INT_BITS(static_cast<ImmeF32Operand*>(op)->value);
                    break;
                case OperandType::GLOBAL:
                    ss << "g:" << static_cast<GlobalOperand*>(op)->name;
                    break;
                case OperandType::LABEL:
                    ss << "l:" << static_cast<LabelOperand*>(op)->lnum;
                    break;
                default: ss << "u"; break;
            }
            return ss.str();
        }

        std::string makeExprKey(Instruction* inst)
        {
            std::stringstream ss;
            ss << static_cast<int>(inst->opcode);

            switch (inst->opcode)
            {
                case Operator::ADD:
                case Operator::SUB:
                case Operator::MUL:
                case Operator::DIV:
                case Operator::MOD:
                case Operator::FADD:
                case Operator::FSUB:
                case Operator::FMUL:
                case Operator::FDIV:
                case Operator::BITAND:
                case Operator::BITXOR:
                case Operator::SHL:
                case Operator::ASHR:
                case Operator::LSHR:
                {
                    auto* arith = static_cast<ArithmeticInst*>(inst);
                    std::string lhsKey = operandKey(arith->lhs);
                    std::string rhsKey = operandKey(arith->rhs);
                    if (isCommutativeOp(inst->opcode) && lhsKey > rhsKey) std::swap(lhsKey, rhsKey);
                    ss << "|dt:" << static_cast<int>(arith->dt) << "|lhs:" << lhsKey << "|rhs:" << rhsKey;
                    return ss.str();
                }
                case Operator::ICMP:
                {
                    auto* icmp = static_cast<IcmpInst*>(inst);
                    ss << "|dt:" << static_cast<int>(icmp->dt) << "|cond:" << static_cast<int>(icmp->cond)
                       << "|lhs:" << operandKey(icmp->lhs) << "|rhs:" << operandKey(icmp->rhs);
                    return ss.str();
                }
                case Operator::FCMP:
                {
                    auto* fcmp = static_cast<FcmpInst*>(inst);
                    ss << "|dt:" << static_cast<int>(fcmp->dt) << "|cond:" << static_cast<int>(fcmp->cond)
                       << "|lhs:" << operandKey(fcmp->lhs) << "|rhs:" << operandKey(fcmp->rhs);
                    return ss.str();
                }
                case Operator::GETELEMENTPTR:
                {
                    auto* gep = static_cast<GEPInst*>(inst);
                    ss << "|dt:" << static_cast<int>(gep->dt) << "|it:" << static_cast<int>(gep->idxType)
                       << "|base:" << operandKey(gep->basePtr) << "|dims:";
                    for (int dim : gep->dims) ss << dim << ",";
                    ss << "|idx:";
                    for (auto* idx : gep->idxs) ss << operandKey(idx) << ",";
                    return ss.str();
                }
                case Operator::ZEXT:
                {
                    auto* zext = static_cast<ZextInst*>(inst);
                    ss << "|from:" << static_cast<int>(zext->from) << "|to:" << static_cast<int>(zext->to)
                       << "|src:" << operandKey(zext->src);
                    return ss.str();
                }
                case Operator::SITOFP:
                {
                    auto* sitofp = static_cast<SI2FPInst*>(inst);
                    ss << "|src:" << operandKey(sitofp->src);
                    return ss.str();
                }
                case Operator::FPTOSI:
                {
                    auto* fptosi = static_cast<FP2SIInst*>(inst);
                    ss << "|src:" << operandKey(fptosi->src);
                    return ss.str();
                }
                default: return "";
            }
        }

    }  // namespace

    void CSEPass::runOnFunction(Function& function)
    {
        bool changed = false;

        // Block-local CSE
        {
            std::unordered_map<Instruction*, size_t> inst2block;
            UserCollector                            userCollector;
            for (auto& [id, block] : function.blocks)
            {
                for (auto* inst : block->insts) inst2block[inst] = id;
            }
            for (auto& [id, block] : function.blocks)
            {
                for (auto* inst : block->insts)
                {
                    userCollector.currentInst = inst;
                    apply(userCollector, *inst);
                }
            }

            auto hasExternalUse = [&](size_t defReg, size_t blockId) -> bool {
                auto it = userCollector.userMap.find(defReg);
                if (it == userCollector.userMap.end()) return false;
                for (auto* useInst : it->second)
                {
                    auto bit = inst2block.find(useInst);
                    if (bit != inst2block.end() && bit->second != blockId) return true;
                }
                return false;
            };

            for (auto& [id, block] : function.blocks)
            {
                std::unordered_map<size_t, Operand*> replaceRegs;
                OperandReplaceVisitor                replacer(replaceRegs);
                std::unordered_map<std::string, Operand*> exprMap;
                auto                                      it = block->insts.begin();
                while (it != block->insts.end())
                {
                    Instruction* inst = *it;
                    if (!replaceRegs.empty()) apply(replacer, *inst);

                    if (!isCSECandidate(inst))
                    {
                        ++it;
                        continue;
                    }

                    DefCollector defCollector;
                    apply(defCollector, *inst);
                    size_t defReg = defCollector.getResult();
                    if (defReg == 0)
                    {
                        ++it;
                        continue;
                    }

                    std::string key = makeExprKey(inst);
                    if (key.empty())
                    {
                        ++it;
                        continue;
                    }

                    auto found = exprMap.find(key);
                    if (found != exprMap.end())
                    {
                        bool externalUse = hasExternalUse(defReg, id);
                        replaceRegs[defReg] = found->second;
                        if (!externalUse)
                        {
                            delete inst;
                            it      = block->insts.erase(it);
                            changed = true;
                            continue;
                        }
                        ++it;
                        continue;
                    }

                    exprMap.emplace(std::move(key), getRegOperand(defReg));
                    ++it;
                }
            }
        }

        // Dominator tree CSE for scalar expressions
        {
            Analysis::AM.invalidate(function);
            auto* cfg = Analysis::AM.get<Analysis::CFG>(function);
            auto* dom = Analysis::AM.get<Analysis::DomInfo>(function);
            if (cfg && dom && !cfg->id2block.empty())
            {
                const auto& domTree = dom->getDomTree();
                std::unordered_map<std::string, Operand*> exprMap;
                std::unordered_map<size_t, Operand*>      replaceRegs;
                std::unordered_set<Instruction*>          eraseSet;
                OperandReplaceVisitor                     replacer(replaceRegs);
                std::unordered_set<size_t>                visited;

                std::function<void(size_t)> dfs = [&](size_t blockId) {
                    if (visited.count(blockId)) return;
                    visited.insert(blockId);
                    Block* block = function.getBlock(blockId);
                    if (!block) return;

                    std::vector<std::tuple<std::string, bool, Operand*>> localStack;

                    for (auto* inst : block->insts)
                    {
                        if (!replaceRegs.empty()) apply(replacer, *inst);
                        if (!isCSECandidate(inst)) continue;

                        DefCollector defCollector;
                        apply(defCollector, *inst);
                        size_t defReg = defCollector.getResult();
                        if (defReg == 0) continue;

                        std::string key = makeExprKey(inst);
                        if (key.empty()) continue;

                        auto found = exprMap.find(key);
                        if (found != exprMap.end())
                        {
                            replaceRegs[defReg] = found->second;
                            eraseSet.insert(inst);
                            changed = true;
                            continue;
                        }

                        auto it = exprMap.find(key);
                        bool hadOld = it != exprMap.end();
                        Operand* oldVal = hadOld ? it->second : nullptr;
                        exprMap[key] = getRegOperand(defReg);
                        localStack.emplace_back(key, hadOld, oldVal);
                    }

                    if (blockId < domTree.size())
                    {
                        for (int child : domTree[blockId])
                        {
                            if (child == static_cast<int>(blockId)) continue;
                            dfs(static_cast<size_t>(child));
                        }
                    }

                    for (auto it = localStack.rbegin(); it != localStack.rend(); ++it)
                    {
                        const std::string& key = std::get<0>(*it);
                        bool               hadOld = std::get<1>(*it);
                        Operand*           oldVal = std::get<2>(*it);
                        if (hadOld)
                            exprMap[key] = oldVal;
                        else
                            exprMap.erase(key);
                    }
                };

                dfs(0);

                if (!eraseSet.empty())
                {
                    for (auto& [id, block] : function.blocks)
                    {
                        auto it = block->insts.begin();
                        while (it != block->insts.end())
                        {
                            Instruction* inst = *it;
                            if (eraseSet.count(inst))
                            {
                                delete inst;
                                it = block->insts.erase(it);
                                continue;
                            }
                            ++it;
                        }
                    }
                }

                if (!replaceRegs.empty())
                {
                    OperandReplaceVisitor finalReplacer(replaceRegs);
                    for (auto& [id, block] : function.blocks)
                    {
                        for (auto* inst : block->insts) apply(finalReplacer, *inst);
                    }
                }
            }
        }

        // Implied CSE: disabled (can break CFG/phi consistency in later passes)

        if (changed) Analysis::AM.invalidate(function);
    }
}  // namespace ME
