/*
 * INTEL CONFIDENTIAL
 * Copyright (c) 2015, Intel Corporation All Rights Reserved.
 *
 * The source code contained or described herein and all documents related to the
 * source code ("Material") are owned by Intel Corporation or its suppliers or
 * licensors. Title to the Material remains with Intel Corporation or its suppliers
 * and licensors. The Material contains trade secrets and proprietary and
 * confidential information of Intel or its suppliers and licensors. The Material
 * is protected by worldwide copyright and trade secret laws and treaty provisions.
 * No part of the Material may be used, copied, reproduced, modified, published,
 * uploaded, posted, transmitted, distributed, or disclosed in any way without
 * Intel's prior express written permission.
 *
 * No license under any patent, copyright, trade secret or other intellectual
 * property right is granted to or conferred upon you by disclosure or delivery of
 * the Materials, either expressly, by implication, inducement, estoppel or
 * otherwise. Any license under such intellectual property rights must be express
 * and approved by Intel in writing.
 *
 */

#include "ext_utility.h"
#include "nodes.h"
#include "optimization.h"
#include "optimization_x86.h"
#include <sstream>
#include "utils.h"

namespace art {

  std::string GetMethodName(const HGraph* graph) {
    DCHECK(graph != nullptr);
     //neeraj - resolve build error
     return graph->GetDexFile().PrettyMethod(graph->GetMethodIdx());
  }

  void SplitStringIntoSet(const std::string& s, char delim, std::unordered_set<std::string>& tokens) {
    std::stringstream stream(s);
    std::string item;
    while (std::getline(stream, item, delim)) {
      if (item.size() > 1) {
        tokens.insert(item);
      }
    }
  }

  std::ostream& operator<<(std::ostream& os, HInstruction* instruction) {
    os << GetTypeId(instruction->GetType()) << instruction->GetId() << " ";
    os << instruction->DebugName();
    switch (instruction->GetKind()) {
      case HInstruction::kIntConstant:
        os << ' ' << instruction->AsIntConstant()->GetValue();
        break;
      case HInstruction::kLongConstant:
        os << ' ' << instruction->AsLongConstant()->GetValue();
        break;
      case HInstruction::kFloatConstant:
        os << ' ' << instruction->AsFloatConstant()->GetValue();
        break;
      case HInstruction::kDoubleConstant:
        os << ' ' << instruction->AsDoubleConstant()->GetValue();
        break;
      default:
        break;
    }
    if (instruction->InputCount() > 0) {
      os << " [ ";
      for (HInputIterator inputs(instruction); !inputs.Done(); inputs.Advance()) {
        os << GetTypeId(inputs.Current()->GetType()) << inputs.Current()->GetId() << ' ';
      }
      os << ']';
    }
    return os;
  }

  //neeraj - added cases for kCondB, kCondBE, kCondA & kCondAE
  IfCondition NegateCondition(IfCondition cond) {
    switch (cond) {
      case kCondNE:
        return kCondEQ;
      case kCondEQ:
        return kCondNE;
      case kCondLT:
        return kCondGE;
      case kCondGE:
        return kCondLT;
      case kCondGT:
        return kCondLE;
      case kCondLE:
        return kCondGT;
      case kCondB:
        return kCondAE;
      case kCondBE:
        return kCondA;
      case kCondA:
        return kCondBE;
      case kCondAE:
        return kCondB;
      default:
        LOG(FATAL) << "Unknown if condition";
    }

    // Unreachable code.
    return kCondEQ;
  }

  //neeraj - added cases for kCondB, kCondBE, kCondA & kCondAE
  IfCondition FlipConditionForOperandSwap(IfCondition cond) {
    switch (cond) {
      case kCondEQ:
      case kCondNE:
        // Do nothing, a == b <==> b == a.
        // Same for a != b.
        return cond;
      case kCondLT:
        return kCondGT;
      case kCondLE:
        return kCondGE;
      case kCondGT:
        return kCondLT;
      case kCondGE:
        return kCondLE;
      case kCondB:
        return kCondA;
      case kCondBE:
        return kCondAE;
      case kCondA:
        return kCondB;
      case kCondAE:
        return kCondBE;
      default:
        LOG(FATAL) << "Unknown if condition";
    }

    // Unreachable code.
    return kCondEQ;
  }

  bool GetFPConstantValue(HConstant* constant, double& value) {
    if (constant->IsFloatConstant()) {
      HFloatConstant* fp_cst = constant->AsFloatConstant();
      value = fp_cst->GetValue();
      return true;
    } else if (constant->IsDoubleConstant()) {
      HDoubleConstant* double_cst = constant->AsDoubleConstant();
      value = double_cst->GetValue();
      return true;
    }

    // If not int or long, bail.
    return false;
  }

  bool GetIntConstantValue(HConstant* constant, int64_t& value) {
    if (constant->IsIntConstant()) {
      HIntConstant* int_cst = constant->AsIntConstant();
      value = int_cst->GetValue();
      return true;
    } else if (constant->IsLongConstant()) {
      HLongConstant* long_cst = constant->AsLongConstant();
      value = long_cst->GetValue();
      return true;
    }

    // If not int or long, bail.
    return false;
  }

  HInstruction* GetCompareInstruction(HInstruction* instruction) {
    // Look at the first instruction, is it a compare?
    HInstruction* first = instruction->InputAt(0);

    if (first->IsCompare()) {
      return first;
    }

    // Not the first one, what about the second one?
    HInstruction* second = instruction->InputAt(1);

    if (second->IsCompare()) {
      return second;
    }

    // None of the two, so return the original one.
    return instruction;
  }

  void DeconstructFP(double value,
                     bool is_double,
                     bool with_implicit_one,
                     int32_t& sign,
                     int32_t& power,
                     int64_t& mantissa) {
    int32_t exponent_length;
    int32_t mantissa_length;
    int64_t as_int;
    const int64_t one = 1;

    if (is_double) {
      union {
        double d;
        int64_t l;
      } conversion;
      conversion.d = value;
      as_int = conversion.l;
      exponent_length = 11;
      mantissa_length = 52;
    } else {
      union {
        float f;
        int32_t i;
      } conversion;
      conversion.f = static_cast<float>(value);
      // Extend to 64 bit.
      as_int = conversion.i | (int64_t)0;
      exponent_length = 8;
      mantissa_length = 23;
    }

    // Get the actual output values.
    // Sign is the leftmost (31th or 63th) bit.
    sign = static_cast<int32_t>((as_int >> (is_double ? 63 : 31)) & 1);
    // Mantissa bits are the last ones.
    mantissa = as_int & ((one << mantissa_length) - one);
    // Exponent bits are between them.
    int32_t exponent = static_cast<int32_t>
                       ((as_int >> mantissa_length) &
                       ((one << exponent_length) - one));
    // For non-zero values, power is exponent - 01111...11.
    if (value == 0.0) {
      power = 0;
    } else {
      power = exponent - ((1 << (exponent_length - 1)) - 1);
    }

    // For non-zero value, add an implicit 1 to mantissa.
    if (with_implicit_one && (value != 0.0)) {
      mantissa |= ((int64_t)1 << mantissa_length);
    }
  }

  int32_t CountEndZerosInMantissa(int64_t mantissa, bool is_double) {
    const int32_t limit = (is_double ? 52 : 23);
    int32_t ret = 0;
    while ((ret < limit) && ((mantissa & 1) == 0)) {
      ret++;
      mantissa >>= 1;
    }
    return ret;
  }

  void RemoveEnvAsUser(HInstruction* instruction) {
    for (HEnvironment* environment = instruction->GetEnvironment();
         environment != nullptr;
         environment = environment->GetParent()) {
      for (size_t i = 0, e = environment->Size(); i < e; ++i) {
        if (environment->GetInstructionAt(i) != nullptr) {
          environment->RemoveAsUserOfInput(i);
        }
      }
    }
  }

  void RemoveAsUser(HInstruction* instruction) {
    for (size_t i = 0; i < instruction->InputCount(); i++) {
      instruction->RemoveAsUserOfInput(i);
    }

    RemoveEnvAsUser(instruction);
  }

  void RemoveFromEnvironmentUsers(HInstruction* insn) {
    const HUseList<HEnvironment*>& env_uses = insn->GetEnvUses();
    for (auto it = env_uses.begin(), end = env_uses.end(); it != end; /* ++it below */) {
      HEnvironment* user = it->GetUser();
      size_t index = it->GetIndex();
      ++it;
      user->SetRawEnvAt(index, nullptr);
    }
  }

  char GetTypeId(Primitive::Type type) {
    // Note that Primitive::Descriptor would not work for us
    // because it does not handle reference types (that is kPrimNot).
    switch (type) {
      case Primitive::kPrimBoolean: return 'z';
      case Primitive::kPrimByte: return 'b';
      case Primitive::kPrimChar: return 'c';
      case Primitive::kPrimShort: return 's';
      case Primitive::kPrimInt: return 'i';
      case Primitive::kPrimLong: return 'j';
      case Primitive::kPrimFloat: return 'f';
      case Primitive::kPrimDouble: return 'd';
      case Primitive::kPrimNot: return 'l';
      case Primitive::kPrimVoid: return 'v';
    }
    LOG(FATAL) << "Unreachable";
    return 'v';
  }

  bool CanExpressionBeRemoved(HInstruction* instruction) {
    return !instruction->GetSideEffects().HasSideEffectsExcludingGC()
           && !instruction->CanThrow()
           && !instruction->IsParameterValue()
           && !instruction->HasUses()
           && (!instruction->IsInvoke()  // Can remove pure invokes only.
               || instruction->CanBeMoved());
  }

  void TryKillUseTree(HOptimization_X86* pass,
                      HInstruction* insn,
                      std::unordered_set<HInstruction*>* all_removed) {
    DCHECK(pass != nullptr);
    DCHECK(insn != nullptr);

    // If we cannot remove even the first instruction, we will not remove
    // anything. So let's do the fast check to avoid unneeded overhead.
    if (!CanExpressionBeRemoved(insn)) {
      return;
    }

    // We will swap these two sets between iterations.
    std::unordered_set<HInstruction*> set_1;
    std::unordered_set<HInstruction*> set_2;

    // Start from current iteration.
    set_1.insert(insn);

    for (auto current_step = &set_1, next_step = &set_2;
         !current_step->empty();
         current_step->clear(), std::swap(current_step, next_step)) {
      DCHECK(next_step->empty());

      for (auto instruction : *current_step) {
        HBasicBlock* block = instruction->GetBlock();
        if (block == nullptr) {
          // We have already removed this instruction.
          continue;
        }

        // We removed this instruction as input. Now consider it for complete removal.
        if (CanExpressionBeRemoved(instruction)) {
          PRINT_PASS_OSTREAM_MESSAGE(pass, "Removing " << instruction << " because it is no"
                                                       << " longer used by any other"
                                                       << " instruction or environment.");

          // After we eliminate this instruction, its inputs can become unused.
          for (size_t i = 0; i < instruction->InputCount(); ++i) {
            next_step->insert(instruction->InputAt(i));
          }

          for (HEnvironment* environment = instruction->GetEnvironment();
              environment != nullptr;
              environment = environment->GetParent()) {
            for (size_t i = 0, e = environment->Size(); i < e; ++i) {
              HInstruction* in_env = environment->GetInstructionAt(i);
              if (in_env != nullptr) {
                next_step->insert(in_env);
              }
            }
          }

          block->RemoveInstructionOrPhi(instruction);
          if (all_removed != nullptr) {
            all_removed->insert(instruction);
          }
        }
      }
    }
  }

}  // namespace art
