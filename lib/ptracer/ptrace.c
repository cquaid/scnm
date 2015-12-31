#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/user.h>

#include <signal.h>
#include <stddef.h>

#include "shared/list.h"

/**
 * @file ptrace.c
 *
 * This file contains ptrace(2) wrapper functions.
 *
 * While most ptracer_* functions can be simple macro
 * expansions to the equivalent ptrace_* functions,
 * some cases need additional assignments and checks
 * because of the multiple ways a process can be stopped
 * and returned to the tracer.
 *
 * There are two ways of stopping the process:
 *   1 - SIGSTOP (entire thread group)
 *
 *   2 - Various ptrace commands and traps (single thread and
 *       is a special [kernel-internal only] process state).
 *
 * SIGSTOP stops can only be resumed by a SIGCONT.
 * "ptrace stops" can only be resumed by PTRACE_CONT.
 *
 * The following are how a "ptrace stop" can be generated:
 *   1 - PTRACE_ATTACH will stop the process after attaching (waitpid ret)
 *
 *   2 - Using PTRACE_SYSCALL will set the ptrace stop state when entering
 *       or exiting a syscall function (depending on where it was resumed).
 *
 *   3 - Using PTRACE_SINGLESTEP will set the ptrace stop state once
 *       a single instruction has been executed.
 *
 *   4 - Hitting a breakpoint (int 3 / 0xCC), returns a SIGTRAP from waitpid
 *       as the signal that stopped the tracee.  While this is not a "ptrace
 *       stop" proper, it can be resumed by a PTRACE_CONT without issue.
 *
 *   5 - Various PTRACE_O_* settings that can be set by PTRACE_SETOPTIONS
 *       can set stop points.  These options always signal with SIGTRAP
 *       bitwise-or'd with some other identifying PTRACE_EVENT_* value.
 *
 * The SIGSTOP state is only created by the SIGSTOP signal.
 *
 * Since ptrace does not have a tracer-exposed method of stopping a tracee,
 * the ptracer_stop functions use SIGSTOP.  This means we need to change
 * the functionality of the ptracer_cont functions when SIGSTOPed.
 *
 * Note there is no ptrace_stop equivalent since this requires storing state
 * within the ptracer context structure.
 *
 * Outlines:
 *
 *   ptracer_stop:
 *     send SIGSTOP
 *     expect state = STOPPED
 *
 *   ptracer_cont:
 *     if (ptracer_ctx->current_state == STOPPED)
 *        send SIGCONT
 *     else
 *        send PTRACE_CONT
 *     ptracer_ctx->current_state = RUNNING
 *
 *   ptracer_syscall|ptracer_singlestep:
 *     call ptrace_syscall or ptrace_singlestep
 *     expect state = PTRACE_STOPPED
 *     ptracer_ctx->current_state = RUNNING
 *
 *   ptracer_attach:
 *     call ptrace_attach
 *     expect state = PTRACE_STOPPED
 *
 *   ptracer_waitpid:
 *     call waitpid
 *     if exited or killed
 *        ptracer_ctx->current_state = DEAD
 *     else if signaled with SIGTRAP (with any high order mask)
 *        ptracer_ctx->current_state = PTRACE_STOPPED
 *     else if signaled with SIGSTOP
 *        ptracer_ctx->current_state = STOPPED
 *     else
 *        ptracer_ctx->current_state = STOPPED
 *
 *  TODO: Functions should probably set current_state to DEAD
 *        when waitpid() or ptrace() report ESRCH.
 */

/* PTRACE_PEEKTEXT */

/**
 * ptracer interface wrapper for PTRACE_PEEKTEXT.
 *
 * @param[in] ctx - ptracer context structure
 * @param[in] addr - address to peek
 * @param[out] out - bytes read from address packed into a ulong
 *
 * @return 0 on success
 * @return not 0 on failure with error returned in errno
 */
int
ptracer_peektext(struct ptracer_ctx *ctx,
	unsigned long addr, unsigned long *out)
{
	return ptrace_peektext(ctx->pid, addr, out);
}

/**
 * Wrapper for PTRACE_PEEKTEXT.
 *
 * @param[in] pid - process id to peek from
 * @param[in] addr - address to peek
 * @param[out] out - bytes read from address packed into a ulong
 *
 * @return 0 on success
 * @return not 0 on failure with error returned in errno
 */
int
ptrace_peektext(pid_t pid, unsigned long addr, unsigned long *out)
{
	long val;

	errno = 0;
	val = ptrace(PTRACE_PEEKTEXT, pid, (void *)addr, 0);

	if (errno != 0)
		return 1;

	*out = *(unsigned long *)&val;
	return 0;
}

/* PTRACE_POKETEXT */

/**
 * ptracer interface wrapper for PTRACE_POKETEXT.
 *
 * @param[in] ctx - ptracer context structure
 * @param[in] addr - address to poke
 * @param[in] out - bytes write to address packed into a ulong
 *
 * @return 0 on success
 * @return not 0 on failure with error returned in errno
 */
int
ptracer_poketext(struct ptracer_ctx *ctx,
	unsigned long addr, unsigned long out)
{
	return ptrace_poketext(ctx->pid, addr, out);
}

/**
 * Wrapper for PTRACE_POKETEXT.
 *
 * @param[in] pid - process id to poke
 * @param[in] addr - address to poke
 * @param[in] out - bytes write to address packed into a ulong
 *
 * @return 0 on success
 * @return not 0 on failure with error returned in errno
 */
int
ptrace_poketext(pid_t pid, unsigned long addr, unsigned long val)
{
	return (ptrace(PTRACE_POKETEXT, pid, (void *)addr, (void *)val) == -1);
}

/**
 * ptracer interface wrapper for PTRACE_POKETEXT.
 *
 * @param[in] ctx - ptracer context structure
 * @param[in] addr - address to poke
 * @param[in] out - bytes write to address packed into a ulong
 *
 * @return 0 on success
 * @return not 0 on failure with error returned in errno
 */
int
ptracer_poketext(struct ptracer_ctx *ctx,
	unsigned long addr, unsigned long out)
{
	return ptrace_poketext(ctx->pid, addr, out);
}

/* PTRACE_SINGLESTEP */

/**
 * ptracer interface wrapper for PTRACE_SINGLESTEP.
 *
 * @param[in] ctx - ptracer context structure
 *
 * @note
 *  Sets ctx->expected_next_state to PTRACER_PROC_STATE_PTRACE_STOPPED.
 *
 * @return 0 on success
 * @return not 0 on failure with error returned in errno
 */
int
ptracer_singlestep(struct ptracer_ctx *ctx)
{
	ctx->expected_next_state = PTRACER_PROC_STATE_PTRACE_STOPPED;
	return ptrace_singlestep(ctx->pid);
}

/**
 * Wrapper function for PTRACE_SINGLESTEP.
 *
 * @param[in] pid - process id to singlestep
 *
 * @return 0 on success
 * @return not 0 on failure with error returned in errno
 */
int
ptrace_singlestep(pid_t pid)
{
	return (ptrace(PTRACE_SINGLESTEP, pid, 0, 0) == -1);
}

#define ptracer_singlestep_waitpid(ctx, out_status, options) \
	ptrace_singlestep_waitpid((ctx)->pid, out_status, options)

extern int
ptrace_singlestep_waitpid(pid_t pid, int *out_status, int options);

/* PTRACE_SYSCALL */

/**
 * ptracer interface wrapper for PTRACE_SYSCALL.
 *
 * @param[in] ctx - ptracer context structure
 *
 * @note
 *  Sets ctx->expected_next_state to PTRACER_PROC_STATE_PTRACE_STOPPED.
 *
 * @return 0 on success
 * @return not 0 on failure with error returned in errno
 */
int
ptracer_syscall(struct ptracer_ctx *ctx)
{
	ctx->expected_next_state = PTRACER_PROC_STATE_PTRACE_STOPPED;
	return ptrace_syscall(ctx->pid);
}

/**
 * Wrapper function for PTRACE_SYSCALL.
 *
 * @param[in] pid - process id to singlestep
 *
 * @return 0 on success
 * @return not 0 on failure with error returned in errno
 */
int
ptrace_syscall(pid_t pid)
{
	return (ptrace(PTRACE_SYSCALL, pid, 0, 0) == -1);
}

#define ptracer_syscall_waitpid(ctx, out_status, options) \
	ptrace_syscall_waitpid((ctx)->pid, out_status, options)

extern int
ptrace_syscall_waitpid(pid_t pid, int *out_status, int options);

/* PTRACE_GETREGS */

#define ptracer_getregs(ctx, out_regs) \
	ptrace_getregs((ctx)->pid, out_regs)

static inline int
ptrace_getregs(pid_t pid, struct user_regs_struct *out_regs)
{
	return (ptrace(PTRACE_GETREGS, pid, 0, (void *)out_regs) == -1);
}

/* PTRACE_GETFPREGS */

#define ptracer_getfpregs(ctx, out_regs) \
	ptrace_getregs((ctx)->pid, out_regs)

static inline int
ptrace_getfpregs(pid_t pid, struct user_fpregs_struct *out_regs)
{
	return (ptrace(PTRACE_GETFPREGS, pid, 0, (void *)out_regs) == -1);
}

#define ptracer_get_all_regs(ctx, out_regs, out_fpregs) \
	ptrace_get_all_regs((ctx)->pid, out_regs, out_fpregs)

static inline int
ptrace_get_all_regs(pid_t pid, struct user_regs_struct *out_regs,
	struct user_fpregs_struct *out_fpregs)
{
	if (ptrace_getregs(pid, out_regs) != 0)
		return 1;

	return ptrace_getfpregs(pid, out_fpregs);
}

/* PTRACE_SETREGS */

#define ptracer_setregs(ctx, regs) \
	ptrace_setregs((ctx)->pid, regs)

static inline int
ptrace_setregs(pid_t pid, struct user_regs_struct *regs)
{
	return (ptrace(PTRACE_SETREGS, pid, 0, (void *)regs) == -1);
}

/* PTRACE_SETFPREGS */

#define ptracer_setfpregs(ctx, regs) \
	ptrace_setfpregs((ctx)->pid, regs)

static inline int
ptrace_setfpregs(pid_t pid, struct user_fpregs_struct *regs)
{
	return (ptrace(PTRACE_SETFPREGS, pid, 0, (void *)regs) == -1);
}

#define ptracer_set_all_regs(ctx, regs, fpregs) \
	ptrace_set_all_regs((ctx)->pid, regs, fpregs)

static inline int
ptrace_set_all_regs(pid_t pid, struct user_regs_struct *regs,
	struct user_fpregs_struct *fpregs)
{
	if (ptrace_setregs(pid, regs) != 0)
		return 1;

	return ptrace_setfpregs(pid, fpregs);
}

/* PTRACE_CONT */

#define ptracer_cont(ctx) \
	ptrace_cont((ctx)->pid)

static inline int
ptrace_cont(pid_t pid)
{
	/* TODO: Allow the signal return parameter? */
	return (ptrace(PTRACE_CONT, pid, 0, 0) == -1);
}

/* PTRACE_STOP */

#define ptracer_stop(ctx) \
	ptrace_stop((ctx)->pid)

static inline int
ptrace_stop(pid_t pid)
{
	/* TODO: do not allow PTRACE_CONT when stopped by SIGSTOP.
     * SIGSTOP must be continued with SIGCONT. */
	return (kill(pid, SIGSTOP) == -1);
}

#define ptracer_stop_waitpid(ctx, out_status, options) \
	ptrace_stop_waitpid((ctx)->pid, out_status, options)

extern int
ptrace_stop_waitpid(pid_t pid, int *out_status, int options);

#define ptracer_attach(ctx) \
	ptrace_attach((ctx)->pid)

/* PTRACE_ATTACH */

static inline int
ptrace_attach(pid_t pid)
{
	return (ptrace(PTRACE_ATTACH, pid, 0, 0) == -1);
}

#define ptracer_attach_waitpid(ctx, out_status, options) \
	ptrace_attach_waitpid((ctx)->pid, out_status, options)

extern int
ptrace_attach_waitpid(pid_t pid, int *out_status, int options);

/* PTRACE_DETATCH */

#define ptracer_detach(ctx) \
	ptrace_detatch((ctx)->pid)

static inline int
ptrace_detach(pid_t pid)
{
	return (ptrace(PTRACE_DETACH, pid, 0, 0) == -1);
}

