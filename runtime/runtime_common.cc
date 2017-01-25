/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include "runtime_common.h"

#include <signal.h>

#include <cinttypes>

#include "android-base/stringprintf.h"

namespace art {

using android::base::StringPrintf;

const char* GetSignalName(int signal_number) {
  switch (signal_number) {
    case SIGABRT: return "SIGABRT";
    case SIGBUS: return "SIGBUS";
    case SIGFPE: return "SIGFPE";
    case SIGILL: return "SIGILL";
    case SIGPIPE: return "SIGPIPE";
    case SIGSEGV: return "SIGSEGV";
#if defined(SIGSTKFLT)
    case SIGSTKFLT: return "SIGSTKFLT";
#endif
    case SIGTRAP: return "SIGTRAP";
  }
  return "??";
}

const char* GetSignalCodeName(int signal_number, int signal_code) {
  // Try the signal-specific codes...
  switch (signal_number) {
    case SIGILL:
      switch (signal_code) {
        case ILL_ILLOPC: return "ILL_ILLOPC";
        case ILL_ILLOPN: return "ILL_ILLOPN";
        case ILL_ILLADR: return "ILL_ILLADR";
        case ILL_ILLTRP: return "ILL_ILLTRP";
        case ILL_PRVOPC: return "ILL_PRVOPC";
        case ILL_PRVREG: return "ILL_PRVREG";
        case ILL_COPROC: return "ILL_COPROC";
        case ILL_BADSTK: return "ILL_BADSTK";
      }
      break;
    case SIGBUS:
      switch (signal_code) {
        case BUS_ADRALN: return "BUS_ADRALN";
        case BUS_ADRERR: return "BUS_ADRERR";
        case BUS_OBJERR: return "BUS_OBJERR";
      }
      break;
    case SIGFPE:
      switch (signal_code) {
        case FPE_INTDIV: return "FPE_INTDIV";
        case FPE_INTOVF: return "FPE_INTOVF";
        case FPE_FLTDIV: return "FPE_FLTDIV";
        case FPE_FLTOVF: return "FPE_FLTOVF";
        case FPE_FLTUND: return "FPE_FLTUND";
        case FPE_FLTRES: return "FPE_FLTRES";
        case FPE_FLTINV: return "FPE_FLTINV";
        case FPE_FLTSUB: return "FPE_FLTSUB";
      }
      break;
    case SIGSEGV:
      switch (signal_code) {
        case SEGV_MAPERR: return "SEGV_MAPERR";
        case SEGV_ACCERR: return "SEGV_ACCERR";
#if defined(SEGV_BNDERR)
        case SEGV_BNDERR: return "SEGV_BNDERR";
#endif
      }
      break;
    case SIGTRAP:
      switch (signal_code) {
        case TRAP_BRKPT: return "TRAP_BRKPT";
        case TRAP_TRACE: return "TRAP_TRACE";
      }
      break;
  }
  // Then the other codes...
  switch (signal_code) {
    case SI_USER:     return "SI_USER";
#if defined(SI_KERNEL)
    case SI_KERNEL:   return "SI_KERNEL";
#endif
    case SI_QUEUE:    return "SI_QUEUE";
    case SI_TIMER:    return "SI_TIMER";
    case SI_MESGQ:    return "SI_MESGQ";
    case SI_ASYNCIO:  return "SI_ASYNCIO";
#if defined(SI_SIGIO)
    case SI_SIGIO:    return "SI_SIGIO";
#endif
#if defined(SI_TKILL)
    case SI_TKILL:    return "SI_TKILL";
#endif
  }
  // Then give up...
  return "?";
}

void UContext::Dump(std::ostream& os) const {
  // TODO: support non-x86 hosts.
#if defined(__APPLE__) && defined(__i386__)
  DumpRegister32(os, "eax", context->__ss.__eax);
  DumpRegister32(os, "ebx", context->__ss.__ebx);
  DumpRegister32(os, "ecx", context->__ss.__ecx);
  DumpRegister32(os, "edx", context->__ss.__edx);
  os << '\n';

  DumpRegister32(os, "edi", context->__ss.__edi);
  DumpRegister32(os, "esi", context->__ss.__esi);
  DumpRegister32(os, "ebp", context->__ss.__ebp);
  DumpRegister32(os, "esp", context->__ss.__esp);
  os << '\n';

  DumpRegister32(os, "eip", context->__ss.__eip);
  os << "                   ";
  DumpRegister32(os, "eflags", context->__ss.__eflags);
  DumpX86Flags(os, context->__ss.__eflags);
  os << '\n';

  DumpRegister32(os, "cs",  context->__ss.__cs);
  DumpRegister32(os, "ds",  context->__ss.__ds);
  DumpRegister32(os, "es",  context->__ss.__es);
  DumpRegister32(os, "fs",  context->__ss.__fs);
  os << '\n';
  DumpRegister32(os, "gs",  context->__ss.__gs);
  DumpRegister32(os, "ss",  context->__ss.__ss);
#elif defined(__linux__) && defined(__i386__)
  DumpRegister32(os, "eax", context.gregs[REG_EAX]);
  DumpRegister32(os, "ebx", context.gregs[REG_EBX]);
  DumpRegister32(os, "ecx", context.gregs[REG_ECX]);
  DumpRegister32(os, "edx", context.gregs[REG_EDX]);
  os << '\n';

  DumpRegister32(os, "edi", context.gregs[REG_EDI]);
  DumpRegister32(os, "esi", context.gregs[REG_ESI]);
  DumpRegister32(os, "ebp", context.gregs[REG_EBP]);
  DumpRegister32(os, "esp", context.gregs[REG_ESP]);
  os << '\n';

  DumpRegister32(os, "eip", context.gregs[REG_EIP]);
  os << "                   ";
  DumpRegister32(os, "eflags", context.gregs[REG_EFL]);
  DumpX86Flags(os, context.gregs[REG_EFL]);
  os << '\n';

  DumpRegister32(os, "cs",  context.gregs[REG_CS]);
  DumpRegister32(os, "ds",  context.gregs[REG_DS]);
  DumpRegister32(os, "es",  context.gregs[REG_ES]);
  DumpRegister32(os, "fs",  context.gregs[REG_FS]);
  os << '\n';
  DumpRegister32(os, "gs",  context.gregs[REG_GS]);
  DumpRegister32(os, "ss",  context.gregs[REG_SS]);
#elif defined(__linux__) && defined(__x86_64__)
  DumpRegister64(os, "rax", context.gregs[REG_RAX]);
  DumpRegister64(os, "rbx", context.gregs[REG_RBX]);
  DumpRegister64(os, "rcx", context.gregs[REG_RCX]);
  DumpRegister64(os, "rdx", context.gregs[REG_RDX]);
  os << '\n';

  DumpRegister64(os, "rdi", context.gregs[REG_RDI]);
  DumpRegister64(os, "rsi", context.gregs[REG_RSI]);
  DumpRegister64(os, "rbp", context.gregs[REG_RBP]);
  DumpRegister64(os, "rsp", context.gregs[REG_RSP]);
  os << '\n';

  DumpRegister64(os, "r8 ", context.gregs[REG_R8]);
  DumpRegister64(os, "r9 ", context.gregs[REG_R9]);
  DumpRegister64(os, "r10", context.gregs[REG_R10]);
  DumpRegister64(os, "r11", context.gregs[REG_R11]);
  os << '\n';

  DumpRegister64(os, "r12", context.gregs[REG_R12]);
  DumpRegister64(os, "r13", context.gregs[REG_R13]);
  DumpRegister64(os, "r14", context.gregs[REG_R14]);
  DumpRegister64(os, "r15", context.gregs[REG_R15]);
  os << '\n';

  DumpRegister64(os, "rip", context.gregs[REG_RIP]);
  os << "   ";
  DumpRegister32(os, "eflags", context.gregs[REG_EFL]);
  DumpX86Flags(os, context.gregs[REG_EFL]);
  os << '\n';

  DumpRegister32(os, "cs",  (context.gregs[REG_CSGSFS]) & 0x0FFFF);
  DumpRegister32(os, "gs",  (context.gregs[REG_CSGSFS] >> 16) & 0x0FFFF);
  DumpRegister32(os, "fs",  (context.gregs[REG_CSGSFS] >> 32) & 0x0FFFF);
  os << '\n';
#else
  os << "Unknown architecture/word size/OS in ucontext dump";
#endif
}

void UContext::DumpRegister32(std::ostream& os, const char* name, uint32_t value) const {
  os << StringPrintf(" %6s: 0x%08x", name, value);
}

void UContext::DumpRegister64(std::ostream& os, const char* name, uint64_t value) const {
  os << StringPrintf(" %6s: 0x%016" PRIx64, name, value);
}

void UContext::DumpX86Flags(std::ostream& os, uint32_t flags) const {
  os << " [";
  if ((flags & (1 << 0)) != 0) {
    os << " CF";
  }
  if ((flags & (1 << 2)) != 0) {
    os << " PF";
  }
  if ((flags & (1 << 4)) != 0) {
    os << " AF";
  }
  if ((flags & (1 << 6)) != 0) {
    os << " ZF";
  }
  if ((flags & (1 << 7)) != 0) {
    os << " SF";
  }
  if ((flags & (1 << 8)) != 0) {
    os << " TF";
  }
  if ((flags & (1 << 9)) != 0) {
    os << " IF";
  }
  if ((flags & (1 << 10)) != 0) {
    os << " DF";
  }
  if ((flags & (1 << 11)) != 0) {
    os << " OF";
  }
  os << " ]";
}

}  // namespace art
