#ifndef PCPS4_MSABI_H
#define PCPS4_MSABI_H

/*
 * ABI bridge between native Windows x86-64 code and the PcPs4 runtime.
 *
 * A loaded Windows PE is compiled for the Microsoft x64 calling convention
 * (integer args in RCX/RDX/R8/R9, 32-byte caller-allocated shadow space,
 * RSI/RDI/XMM6-15 callee-saved). PcPs4 itself is built for the PS4's System V
 * AMD64 ABI (args in RDI/RSI/RDX/RCX/R8/R9, red zone, a different callee-saved
 * set). A Windows binary therefore cannot call a PcPs4 function pointer
 * directly: the arguments land in the wrong registers and the two sides
 * disagree about which registers must be preserved, so state is corrupted.
 *
 * PCPS4_MS_ABI marks a function as using the Microsoft x64 convention. Any
 * pointer a Windows PE will call through - an import (IAT) entry, a COM vtable
 * slot, or the image entry point - must be PCPS4_MS_ABI so the compiler emits
 * the convention the PE expects and translates it back to System V when it
 * forwards into the real PcPs4 implementation.
 *
 * On non-x86-64 hosts (e.g. an arm64 workstation running the unit tests) the
 * attribute expands to nothing: there is a single calling convention, so the
 * bridge is a straight call and the tests still exercise the forwarding logic.
 */
#if defined(__x86_64__) || defined(_M_X64)
#  define PCPS4_MS_ABI __attribute__((ms_abi))
#else
#  define PCPS4_MS_ABI
#endif

#ifdef __cplusplus

/*
 * PCPS4_MSW(fn) yields a `const void *` to a compiler-generated Microsoft-x64
 * entry point that forwards to the System V function `fn`. The compiler writes
 * the register/stack shuffle and the shadow-space handling, so the bridge is
 * correct by construction for any signature (it is not a hand-written, fixed
 * arity thunk). `fn` stays an ordinary System V function that the rest of
 * PcPs4 can keep calling normally.
 */
template <typename Fn, Fn fn>
struct PcPs4MsWrap;

template <typename R, typename... A, R (*fn)(A...)>
struct PcPs4MsWrap<R (*)(A...), fn> {
    static R PCPS4_MS_ABI thunk(A... args) { return fn(args...); }
};

#define PCPS4_MSW(fn) \
    (reinterpret_cast<const void *>(&PcPs4MsWrap<decltype(&fn), &fn>::thunk))

#endif /* __cplusplus */

#endif /* PCPS4_MSABI_H */
