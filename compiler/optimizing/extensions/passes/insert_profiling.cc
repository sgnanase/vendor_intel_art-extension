/*
 * Copyright (C) 2015 Intel Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "insert_profiling.h"

#include "art_method.h"
#include "driver/compiler_driver.h"
#include "driver/compiler_options.h"
#include "ext_utility.h"
#include "ext_profiling.h"
#include "gc_root-inl.h"
#include "graph_x86.h"
#include "jit/profiling_info.h"
#include "thread-inl.h"

namespace art {

void HInsertProfiling::Run() {
  // Is there anything to do?
  CompilerOptions::ProfilingCounts profiling_counts =
      driver_->GetCompilerOptions().GetProfilingCounts();
  bool use_profiles = driver_->GetCompilerOptions().UseExactProfiles();
  if (profiling_counts == CompilerOptions::kProfilingNone && !use_profiles) {
    return;  // nothing to do.
  }

  PRINT_PASS_OSTREAM_MESSAGE(this, "Start " << GetMethodName(graph_));
  if (use_profiles) {
    std::vector<std::unique_ptr<ExactProfiler>>* eps = driver_->GetExactProfilers();
    if (eps != nullptr) {
      // We need the right ExactProfiler for this method.
      ExactProfiler* ep = ExactProfiler::FindExactProfiler(*eps, graph_);
      if (ep) {
        if (locked_) {
          Locks::mutator_lock_->AssertSharedHeld(Thread::Current());
          InsertProfilingInformationFromProfile(ep);
        } else {
          ReaderMutexLock mu(Thread::Current(), *Locks::mutator_lock_);
          InsertProfilingInformationFromProfile(ep);
        }
      } else {
        LOG(WARNING) << "Couldn't find correct profile for " << GetMethodName(graph_);;
      }
    }
    if (profiling_counts == CompilerOptions::kProfilingNone) {
      // Nothing else to do.
      PRINT_PASS_OSTREAM_MESSAGE(this, "End " << GetMethodName(graph_));
      return;
    }
  }

  ArenaAllocator* arena = graph_->GetArena();

  // Walk the blocks, and insert profiling code.  Do this for original blocks
  // formed from the Dex code only.
  // Also remember all the virtual/interface invokes and dex_pcs.
  std::set<uint32_t> dex_pcs_seen;
  HX86ReturnExecutionCountTable* count_table = nullptr;
  int32_t max_profiled_block = -1;
  std::map<uint32_t, HInvoke*> virtual_invoke_map;
  for (HBasicBlock* block : graph_->GetBlocks()) {
    if (block == nullptr) {
      continue;
    }

    int block_id = block->GetBlockId();

    if (block == graph_->GetExitBlock()) {
      // We don't profile the exit block.
      continue;
    }

    for (HInstructionIterator inst_it(block->GetInstructions()); !inst_it.Done(); inst_it.Advance()) {
      HInstruction* insn = inst_it.Current();
      switch(insn->GetKind()) {
        case HInstruction::kInvokeUnresolved:
          switch (insn->AsInvokeUnresolved()->GetOriginalInvokeType()) {
            case kVirtual:
            case kInterface:
              // Will fall through to code below to add instrumentation.
              break;
            default:
              // Ignore this invoke.
              continue;
          }
          FALLTHROUGH_INTENDED;
        case HInstruction::kInvokeVirtual:
        case HInstruction::kInvokeInterface:
          virtual_invoke_map.insert(
              std::pair<uint32_t, HInvoke*>(insn->GetDexPc(), insn->AsInvoke()));
          break;
        default:
          break;
      }
    }

    // We know we have to profile block 0 and 1, and they will have a duplicate dex_pc.
    bool is_entry_block = block == graph_->GetEntryBlock();
    DCHECK_EQ(is_entry_block, block_id == 0);
    if (block_id < 2) {
      if (is_entry_block) {
        // Need to address the profile area for this method.
        bool is_64_bit = Is64BitInstructionSet(graph_->GetInstructionSet());
        count_table = new(arena) HX86ReturnExecutionCountTable(is_64_bit,
                                                               graph_->GetCurrentMethod(),
                                                               0);
        PRINT_PASS_OSTREAM_MESSAGE(this, "Insert return count table into block " << block_id);
        block->InsertInstructionBefore(count_table, block->GetLastInstruction());
      }

      DCHECK(count_table != nullptr);
      HInstruction* increment =
        new(arena) HX86IncrementExecutionCount(block_id, count_table, block->GetDexPc());
      if (is_entry_block) {
        block->InsertInstructionAfter(increment, count_table);
      } else {
        HInstruction* insn = block->GetFirstInstruction();
        DCHECK(!insn->IsLoadException());
        if (insn->IsSuspendCheck()) {
          insn = insn->GetNext();
        }
        block->InsertInstructionBefore(increment, insn);
      }
      max_profiled_block = std::max(max_profiled_block, block_id);
      PRINT_PASS_OSTREAM_MESSAGE(this, "Insert increment into block " << block_id);
      if (is_entry_block && profiling_counts == CompilerOptions::kProfilingMethod) {
        // All done.
        break;
      }
      dex_pcs_seen.insert(0);
      continue;
    }

    // Have we seen this block before?
    uint32_t dex_pc = block->GetDexPc();
    if (dex_pc == kNoDexPc) {
      // Give up now.
      PRINT_PASS_OSTREAM_MESSAGE(this, "Exiting: seen kNoDexPc in block " << block_id);
      break;
    }

    if (dex_pcs_seen.find(dex_pc) != dex_pcs_seen.end()) {
      // We have a duplicate block.  Is this the special case of a try block?
      HInstruction* insn = block->GetFirstInstruction();
      if (insn->IsSuspendCheck()) {
        insn = insn->GetNext();
      }
      if (insn->IsTryBoundary()) {
        PRINT_PASS_OSTREAM_MESSAGE(this, "Seen TryBoundary in block " << block_id);
        continue;
      }
      PRINT_PASS_OSTREAM_MESSAGE(this, "Saw duplicate dex_pc in block " << block_id);
      break;
    }

    HInstruction* increment =
        new(arena) HX86IncrementExecutionCount(block_id, count_table, block->GetDexPc());
    // We have to be careful about catch blocks, as there are assumptions made
    // about instruction ordering.
    HInstruction* insn = block->GetFirstInstruction();
    if (insn != nullptr) {
      if (insn->IsLoadException()) {
        // We can't insert the increment until after the ClearException.
        while (insn != nullptr && !insn->IsClearException()) {
          insn = insn->GetNext();
        }
      } else if (insn->IsSuspendCheck()) {
        insn = insn->GetNext();
      }
      if (insn != nullptr) {
        block->InsertInstructionBefore(increment, insn);
        max_profiled_block = std::max(max_profiled_block, block_id);
        PRINT_PASS_OSTREAM_MESSAGE(this, "Insert increment into block " << block_id);
        dex_pcs_seen.insert(dex_pc);
      }
    }
  }

  HGraph_X86* graph = GRAPH_TO_GRAPH_X86(graph_);
  graph->SetNumProfiledBlocks(max_profiled_block+1);

  if (!virtual_invoke_map.empty()) {
    // Generate the profiling information for virtual invokes.
    int index = 0;
    ArenaVector<uint16_t>& dex_pcs = graph->GetProfiledInvokesDexPcs();
    dex_pcs.reserve(virtual_invoke_map.size());
    for (auto& it : virtual_invoke_map) {
      uint32_t dex_pc = it.first;
      HInvoke* insn = it.second;
      HX86ProfileInvoke* prof_insn = 
          new(arena) HX86ProfileInvoke(index,
                                       graph_->GetCurrentMethod(),
                                       insn->InputAt(0),
                                       dex_pc);
      insn->GetBlock()->InsertInstructionBefore(prof_insn, insn);
      dex_pcs.push_back(dex_pc);
      index++;
    }
  }

  PRINT_PASS_OSTREAM_MESSAGE(this, "End " << GetMethodName(graph_));
}

static void UnableToCreateProfilingInfo(ArtMethod* method)
    SHARED_REQUIRES(Locks::mutator_lock_) {
  VLOG(exact_profiler) << "Unable to create profile information for method "
                       << PrettyMethod(method, true);
}

static void MatchedClass(ArtMethod* method,
                         mirror::Class* klass,
                         PersistentClassIndex class_index,
                         uint32_t dex_pc)
    SHARED_REQUIRES(Locks::mutator_lock_) {
  VLOG(exact_profiler) << "Found invoke class for method "
                       << PrettyMethod(method, true)
                       << " index" << class_index
                       << " at dex pc 0x" << std::hex << dex_pc
                       << ", class = " << PrettyDescriptor(klass);
}

static void NoMatchingInvoke(ArtMethod* method, uint32_t dex_pc)
    SHARED_REQUIRES(Locks::mutator_lock_) {
  VLOG(exact_profiler) << "Unable to find matching invoke site for method "
                       << PrettyMethod(method, true) << " for dex pc 0x"
                       << std::hex << dex_pc;
}

static void NoMatchingClass(ArtMethod* method,
                            PersistentClassIndex class_index,
                            uint32_t dex_pc)
    SHARED_REQUIRES(Locks::mutator_lock_) {
  VLOG(exact_profiler) << "Unable to find matching class index "
                       << class_index << " for method "
                       << PrettyMethod(method, true) << " for dex pc 0x"
                       << std::hex << dex_pc;
}

void HInsertProfiling::InsertProfilingInformationFromProfile(ExactProfiler* ep)
    NO_THREAD_SAFETY_ANALYSIS {
  DCHECK(ep != nullptr);
  OneMethod* method_info = ep->FindMethodCounts(graph_);
  if (method_info == nullptr) {
    return;
  }

  // Associate counts with the existing blocks, if we match and
  // have executed the method at least once.
  // Note: We don't add an increment to the exit block.
  if (graph_->GetBlocks().size() - 1 == method_info->num_blocks &&
      method_info->counts[0] != 0) {
    // Enter the block count information for each block.
    for (uint32_t i = 0; i < method_info->num_blocks; i++) {
      HBasicBlock* block = graph_->GetBlocks()[i];
      block->SetBlockCount(method_info->counts[i]);
    }
    HGraph_X86* graph = GRAPH_TO_GRAPH_X86(graph_);
    graph->SetProfileCountKind(
        method_info->num_blocks == 1 ? HGraph_X86::kMethodCount : HGraph_X86::kBasicBlockCounts);
  }

  if (method_info->num_method_invokes != 0) {
    ArtMethod* method = graph_->GetArtMethod();
    ProfilingInfo* jit_info = ExactProfiler::FindProfileForMethod(method);
    if (jit_info != nullptr) {
      // We have created this before.
      return;
    }
    {
      jit_info = ProfilingInfo::Create(method);
      if (UNLIKELY(jit_info == nullptr)) {
        UnableToCreateProfilingInfo(method);
        return;
      }
      ExactProfiler::SetProfileForMethod(method, jit_info);
    }
    DCHECK(jit_info != nullptr);
    for (uint32_t i = 0; i < method_info->num_method_invokes; i++) {
      OneCallSite* call_site = method_info->CallSiteAt(i);
      if (call_site->targets[0].class_index.IsNull()) {
        // No information for this site.
        continue;
      }
      // Walk the call site.
      InlineCache* jit_ic = jit_info->GetInlineCache(call_site->dex_pc);
      if (UNLIKELY(jit_ic == nullptr)) {
        NoMatchingInvoke(method, call_site->dex_pc);
        continue;
      }
      size_t jit_class_index = 0;
      for (int m = 0; m < OneCallSite::kNumInvokeTargets; m++) {
        OneInvoke& invoke = call_site->targets[m];
        if (invoke.class_index.IsNull()) {
          break;
        }
        // Don't bother with cold counts.
        if (invoke.count < kHotMethodCount) {
          continue;
        }
        mirror::Class* klass = ep->FindClass(driver_, invoke.class_index);
        if (UNLIKELY(klass == nullptr)) {
          if (UNLIKELY(VLOG_IS_ON(exact_profiler))) {
            NoMatchingClass(method, invoke.class_index, call_site->dex_pc);
          }
        } else {
          if (UNLIKELY(VLOG_IS_ON(exact_profiler))) {
            MatchedClass(method, klass, invoke.class_index, call_site->dex_pc);
          }
          // Have we already entered this class (may come from duplicated OAT files)?
          bool seen = false;
          for (size_t j = 0; j < jit_class_index; j++) {
            if (jit_ic->GetTypeAt(j) == klass) {
              seen = true;
              break;
            }
          }
          if (!seen) {
            jit_ic->SetClassIndex(jit_class_index++, klass, invoke.count);
          }
        }
      }
    }
  }
}

}
