#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/user.h>
#include <sys/wait.h>

#include <errno.h>
#include <signal.h>
#include <stddef.h>

#include "shared/list.h"
#include "ptracer.h"

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
 *        ptracer_ctx->current_state = SIG_STOPPED
 *     else
 *        ptracer_ctx->current_state = PTRACE_STOPPED
 *
 *  TODO: Functions should probably set current_state to DEAD
 *        when waitpid() or ptrace() report ESRCH. Note that
 *        ptrace() returns ESRCH when the process is not being
 *        traced or the process is not stopped.  This makes
 *        these checks more complicated.
 */

/* waitpid wrappers */

/**
 * ptracer interface wrapper for waitpid(2).
 *
 * This function also changes current_state based on the
 * waitpid return.
 *
 * If the child has exited (WIFEXITED or WIFSIGNALED),
 *   PTRACER_PROC_STATE_DEAD is set.
 *
 * If the child has stopped (WIFSTOPPED) by SIGSTOP,
 *   PTRACER_PROC_STATE_SIG_STOPPED is set.
 *
 * If the child has stopped (WIFSTOPPED) with anything
 *   else, PTRACER_PROC_STATE_PTRACE_STOPPED is set.
 *
 * @param[in] ctx - ptracer context structure
 * @param[out] out_status - child integer return status
 * @param[in] options - waitpid options parameter
 *
 * @return < 0 on waitpid(2) failure with error returned in errno
 * @return 0 if no children changed status (when given WNOHANG)
 * @return > 0 on successful waiting
 */
int
ptracer_waitpid(struct ptracer_ctx *ctx,
    int *out_status, int options)
{
    int ret;
    int status;

    /* Always grab status so we can set the current state. */
    ret = ptrace_waitpid(ctx->pid, &status, options);

    /* Error or no children changed status.
     * No need to store status in this case, just return. */
    if (ret <= 0)
        return ret;

    /* > 0, we got a return from the child. */

    if (WIFEXITED(status) || WIFSIGNALED(status)) {
        ctx->current_state = PTRACER_PROC_STATE_DEAD;
        goto out;
    }

    if (WIFSTOPPED(status)) {
        if (WSTOPSIG(status) == SIGSTOP)
            ctx->current_state = PTRACER_PROC_STATE_SIG_STOPPED;
        else
            ctx->current_state = PTRACER_PROC_STATE_PTRACE_STOPPED;

        goto out;
    }

    /* No state change otherwise.  Realistically we could set
     * PTRACER_PROC_STATE_RUNNING since the only other option
     * is WIFCONTINUED().
     *
     * TODO: If we change to useing waitid() instead. we can
     *       explicitly only wait on WEXITED and WSTOPPED
     *       events and ignore the continued option. This
     *       means we only have to check WIFSTOPPED and just
     *       set _DEAD otherwise (since terminated is the
     *       only other option).
     */

out:

    if (out_status != NULL)
        *out_status = status;

    ctx->process_status = status;

    return ret;
}

/**
 * Wrapper function for waitpid(2).
 *
 * @param[in] pid - process id to wait on
 * @param[out] out_status - child integer return status
 * @param[in] options - waitpid options parameter
 *
 * @return < 0 on waitpid(2) failure with error returned in errno
 * @return 0 if no children changed status (when given WNOHANG)
 * @return > 0 on successful waiting
 */
int
ptrace_waitpid(pid_t pid, int *out_status, int options)
{
    pid_t perr;

    perr = waitpid(pid, out_status, options);

    /* TODO: should we re-try waiting if EINTR is returned?
     *       From the man page:
     *
     *  EINTR - WNOHANG was not set and an unblocked signal
     *          or a SIGCHLD was caught.
     *
     *  We're probably okay not handling this since we are
     *  the tracer not the parent process. SIGCHLD should
     *  only populate to parent process not the tracer as
     *  an unhandled signal.
     */

    if (perr == (pid_t)-1)
        return -1;

    /* 0 - no children changed status (WNOHANG options)
     * 1 - success
     */
    return (perr != (pid_t)0);
}

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

/**
 * ptracer interface wrapper for PTRACE_SINGLESTEP
 * followed by waitpid(2).
 *
 * This function also changes current_state based on the
 * waitpid return.
 *
 * If the child has exited (WIFEXITED or WIFSIGNALED),
 *   PTRACER_PROC_STATE_DEAD is set.
 *
 * If the child has stopped (WIFSTOPPED) by SIGSTOP,
 *   PTRACER_PROC_STATE_SIG_STOPPED is set.
 *
 * If the child has stopped (WIFSTOPPED) with anything
 *   else, PTRACER_PROC_STATE_PTRACE_STOPPED is set.
 *
 * @param[in] ctx - ptracer context structure
 * @param[out] out_status - child integer return status
 * @param[in] options - waitpid options parameter
 *
 * @return < 0 on ptrace(2) failure with error returned in errno
 * @return < 0 on waitpid(2) failure with error returned in errno
 * @return 0 if no children changed status (when given WNOHANG)
 * @return > 0 on successful waiting
 */
int
ptracer_singlestep_waitpid(struct ptracer_ctx *ctx,
    int *out_status, int options)
{
    if (ptracer_singlestep(ctx) != 0)
        return -1;

    return ptracer_waitpid(ctx, out_status, options);
}

/**
 * Wrapper function for PTRACE_SINGLESTEP followed by waitpid(2).
 *
 * @param[in] pid - process id to PTRACE_SINGLESTEP and wait on
 * @param[out] out_status - child integer return status
 * @param[in] options - waitpid options parameter
 *
 * @return < 0 on ptrace(2) failure with error returned in errno
 * @return < 0 on waitpid(2) failure with error returned in errno
 * @return 0 if no children changed status (when given WNOHANG)
 * @return > 0 on successful waiting
 */
int
ptrace_singlestep_waitpid(pid_t pid, int *out_status, int options)
{
    if (ptrace_singlestep(pid) != 0)
        return -1;

    return ptrace_waitpid(pid, out_status, options);
}

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

/**
 * ptracer interface wrapper for PTRACE_SYSCALL
 * followed by waitpid(2).
 *
 * This function also changes current_state based on the
 * waitpid return.
 *
 * If the child has exited (WIFEXITED or WIFSIGNALED),
 *   PTRACER_PROC_STATE_DEAD is set.
 *
 * If the child has stopped (WIFSTOPPED) by SIGSTOP,
 *   PTRACER_PROC_STATE_SIG_STOPPED is set.
 *
 * If the child has stopped (WIFSTOPPED) with anything
 *   else, PTRACER_PROC_STATE_PTRACE_STOPPED is set.
 *
 * @param[in] ctx - ptracer context structure
 * @param[out] out_status - child integer return status
 * @param[in] options - waitpid options parameter
 *
 * @return < 0 on ptrace(2) failure with error returned in errno
 * @return < 0 on waitpid(2) failure with error returned in errno
 * @return 0 if no children changed status (when given WNOHANG)
 * @return > 0 on successful waiting
 */
int
ptracer_syscall_waitpid(struct ptracer_ctx *ctx,
    int *out_status, int options)
{
    if (ptracer_syscall(ctx) != 0)
        return -1;

    return ptracer_waitpid(ctx, out_status, options);
}

/**
 * Wrapper function for PTRACE_SYSCALL followed by waitpid(2).
 *
 * @param[in] pid - process id to PTRACE_SYSCALL and wait on
 * @param[out] out_status - child integer return status
 * @param[in] options - waitpid options parameter
 *
 * @return < 0 on ptrace(2) failure with error returned in errno
 * @return < 0 on waitpid(2) failure with error returned in errno
 * @return 0 if no children changed status (when given WNOHANG)
 * @return > 0 on successful waiting
 */
int
ptrace_syscall_waitpid(pid_t pid, int *out_status, int options)
{
    if (ptrace_syscall(pid) != 0)
        return -1;

    return ptrace_waitpid(pid, out_status, options);
}

/* PTRACE_GETREGS */

/**
 * ptracer interface wrapper for PTRACE_GETREGS.
 *
 * @param[in] ctx - ptracer context structure
 * @param[out] out_regs - storage location for the registers
 *
 * @return 0 on success
 * @return not 0 on failure with error returned in errno
 */
int
ptracer_getregs(struct ptracer_ctx *ctx, struct user_regs_struct *out_regs)
{
    return ptrace_getregs(ctx->pid, out_regs);
}

/**
 * Wrapper function for PTRACE_GETREGS.
 *
 * @param[in] pid - process id to singlestep
 * @param[out] out_regs - storage location for the registers
 *
 * @return 0 on success
 * @return not 0 on failure with error returned in errno
 */
int
ptrace_getregs(pid_t pid, struct user_regs_struct *out_regs)
{
    return (ptrace(PTRACE_GETREGS, pid, 0, (void *)out_regs) == -1);
}

/* PTRACE_GETFPREGS */

/**
 * ptracer interface wrapper for PTRACE_GETFPREGS.
 *
 * @param[in] ctx - ptracer context structure
 * @param[out] out_regs - storage location for the registers
 *
 * @return 0 on success
 * @return not 0 on failure with error returned in errno
 */
int
ptracer_getfpregs(struct ptracer_ctx *ctx,
    struct user_fpregs_struct *out_regs)
{
    return ptrace_getfpregs(ctx->pid, out_regs);
}

/**
 * Wrapper function for PTRACE_GETFPREGS.
 *
 * @param[in] pid - process id to singlestep
 * @param[out] out_regs - storage location for the registers
 *
 * @return 0 on success
 * @return not 0 on failure with error returned in errno
 */
int
ptrace_getfpregs(pid_t pid, struct user_fpregs_struct *out_regs)
{
    return (ptrace(PTRACE_GETFPREGS, pid, 0, (void *)out_regs) == -1);
}

/* PTRACE_GETREGS and PTRACE_GETFPREGS */

/**
 * ptracer interface wrapper for PTRACE_GETREGS and PTRACE_GETFPREGS.
 *
 * @param[in] ctx - ptracer context structure
 * @param[out] out_regs - storage location for the registers
 * @param[out] out_fpregs - storage location for the floating point registers.
 *
 * @return 0 on success
 * @return not 0 on failure with error returned in errno
 */
int
ptracer_get_all_regs(struct ptracer_ctx *ctx,
    struct user_regs_struct *out_regs,
    struct user_fpregs_struct *out_fpregs)
{
    return ptrace_get_all_regs(ctx->pid, out_regs, out_fpregs);
}

/**
 * Wrapper function for PTRACE_GETREGS and PTRACE_GETFPREGS.
 *
 * @param[in] pid - process id to singlestep
 * @param[out] out_regs - storage location for the registers
 * @param[out] out_fpregs - storage location for the floating point registers.
 *
 * @return 0 on success
 * @return not 0 on failure with error returned in errno
 */
int
ptrace_get_all_regs(pid_t pid, struct user_regs_struct *out_regs,
    struct user_fpregs_struct *out_fpregs)
{
    if (ptrace_getregs(pid, out_regs) != 0)
        return 1;

    return ptrace_getfpregs(pid, out_fpregs);
}

/* PTRACE_SETREGS */
/**
 * ptracer interface wrapper for PTRACE_SETREGS.
 *
 * @param[in] ctx - ptracer context structure
 * @param[in] regs - pointer to registers to restore
 *
 * @return 0 on success
 * @return not 0 on failure with error returned in errno
 */
int
ptracer_setregs(struct ptracer_ctx *ctx,
    struct user_regs_struct *regs)
{
    return ptrace_setregs(ctx->pid, regs);
}

/**
 * Wrapper function for PTRACE_SETREGS.
 *
 * @param[in] pid - process id to singlestep
 * @param[in] regs - pointer to registers to restore
 *
 * @return 0 on success
 * @return not 0 on failure with error returned in errno
 */
int
ptrace_setregs(pid_t pid, struct user_regs_struct *regs)
{
    return (ptrace(PTRACE_SETREGS, pid, 0, (void *)regs) == -1);
}

/* PTRACE_SETFPREGS */

/**
 * ptracer interface wrapper for PTRACE_SETFPREGS.
 *
 * @param[in] ctx - ptracer context structure
 * @param[in] regs - pointer to floating point registers to restore
 *
 * @return 0 on success
 * @return not 0 on failure with error returned in errno
 */
int
ptracer_setfpregs(struct ptracer_ctx *ctx,
    struct user_fpregs_struct *regs)
{
    return ptrace_setfpregs(ctx->pid, regs);
}

/**
 * Wrapper function for PTRACE_SETFPREGS.
 *
 * @param[in] pid - process id to singlestep
 * @param[in] regs - pointer to floating point registers to restore
 *
 * @return 0 on success
 * @return not 0 on failure with error returned in errno
 */
int
ptrace_setfpregs(pid_t pid, struct user_fpregs_struct *regs)
{
    return (ptrace(PTRACE_SETFPREGS, pid, 0, (void *)regs) == -1);
}

/* PTRACE_SETREGS and PTRACE_SETFPREGS */

/**
 * ptracer interface wrapper for PTRACE_SETREGS and PTRACE_SETFPREGS.
 *
 * @param[in] ctx - ptracer context structure
 * @param[in] regs - pointer to registers to restore
 * @param[in] fpregs - pointer to floating point registers to restore
 *
 * @return 0 on success
 * @return not 0 on failure with error returned in errno
 */
int
ptracer_set_all_regs(struct ptracer_ctx *ctx,
    struct user_regs_struct *regs,
    struct user_fpregs_struct *fpregs)
{
    return ptrace_set_all_regs(ctx->pid, regs, fpregs);
}

/**
 * Wrapper function for PTRACE_SETREGS and PTRACE_SETFPREGS.
 *
 * @param[in] pid - process id to singlestep
 * @param[in] regs - pointer to registers to restore
 * @param[in] fpregs - pointer to floating point registers to restore
 *
 * @return 0 on success
 * @return not 0 on failure with error returned in errno
 */
int
ptrace_set_all_regs(pid_t pid, struct user_regs_struct *regs,
    struct user_fpregs_struct *fpregs)
{
    if (ptrace_setregs(pid, regs) != 0)
        return 1;

    return ptrace_setfpregs(pid, fpregs);
}

/* PTRACE_CONT */

/**
 * ptracer interface wrapper for PTRACE_CONT or SIGCONT.
 *
 * If the current_state is PTRACER_PROC_STATE_SIG_STOPPED,
 * this function sends the SIGCONT signal, otherwise
 * this function performs a PTRACE_CONT call.
 *
 * @param[in] ctx - ptracer context structure
 *
 * @note
 *  Sets ctx->expected_next_state to PTRACER_PROC_STATE_RUNNING.
 *
 * @return 0 on success
 * @return not 0 on failure with error returned in errno
 */
int
ptracer_cont(struct ptracer_ctx *ctx)
{
    ctx->expected_next_state = PTRACER_PROC_STATE_PTRACE_STOPPED;

    if (ctx->current_state == PTRACER_PROC_STATE_SIG_STOPPED)
        return (kill(ctx->pid, SIGCONT) == -1);

    return ptrace_syscall(ctx->pid);
}

/**
 * Wrapper function for PTRACE_CONT.
 *
 * @param[in] pid - process id to continue
 *
 * @return 0 on success
 * @return not 0 on failure with error returned in errno
 */
int
ptrace_cont(pid_t pid)
{
    return (ptrace(PTRACE_CONT, pid, 0, 0) == -1);
}

/* PTRACER_STOP */

/**
 * ptracer wrapper function for SIGSTOP.
 *
 * @param[in] ctx - ptracer context structure
 *
 * @note
 *  Sets ctx->expected_next_state to PTRACER_PROC_STATE_SIG_STOPPED.
 *  There is no ptrace_* equivalent function.
 *
 * @return 0 on success
 * @return not 0 on failure with error returned in errno
 */
int
ptracer_stop(struct ptracer_ctx *ctx)
{
    ctx->expected_next_state = PTRACER_PROC_STATE_SIG_STOPPED;

    /* TODO: do not allow PTRACE_CONT when stopped by SIGSTOP.
     * SIGSTOP must be continued with SIGCONT. */
    return (kill(ctx->pid, SIGSTOP) == -1);
}

/* There is no ptrace_stop() function as it is not a real ptrace call. */

/**
 * ptracer interface wrapper for SIGSTOP followed by waitpid(2).
 *
 * This function also changes current_state based on the
 * waitpid return.
 *
 * If the child has exited (WIFEXITED or WIFSIGNALED),
 *   PTRACER_PROC_STATE_DEAD is set.
 *
 * If the child has stopped (WIFSTOPPED) by SIGSTOP,
 *   PTRACER_PROC_STATE_SIG_STOPPED is set.
 *
 * If the child has stopped (WIFSTOPPED) with anything
 *   else, PTRACER_PROC_STATE_PTRACE_STOPPED is set.
 *
 * @param[in] ctx - ptracer context structure
 * @param[out] out_status - child integer return status
 * @param[in] options - waitpid options parameter
 *
 * @return < 0 on kill(2) failure with error returned in errno
 * @return < 0 on waitpid(2) failure with error returned in errno
 * @return 0 if no children changed status (when given WNOHANG)
 * @return > 0 on successful waiting
 */
int
ptracer_stop_waitpid(struct ptracer_ctx *ctx,
    int *out_status, int options)
{
    if (ptracer_stop(ctx) != 0)
        return -1;

    return ptracer_waitpid(ctx, out_status, options);
}

/* PTRACE_ATTACH */

/**
 * ptracer wrapper function for PTRACE_ATTACH.
 *
 * @param[in] ctx - ptracer context structure to attach to
 *
 * @note
 *  Sets ctx->expected_next_state to PTRACER_PROC_STATE_PTRACE_STOPPED.
 *
 * @return 0 on success
 * @return not 0 on failure with error returned in errno
 */
int
ptracer_attach(struct ptracer_ctx *ctx)
{
    ctx->expected_next_state = PTRACER_PROC_STATE_PTRACE_STOPPED;
    return ptrace_attach(ctx->pid);
}

/**
 * Wrapper function for PTRACE_ATTACH.
 *
 * @param[in] pid - process id to attach to
 *
 * @return 0 on success
 * @return not 0 on failure with error returned in errno
 */
int
ptrace_attach(pid_t pid)
{
    return (ptrace(PTRACE_ATTACH, pid, 0, 0) == -1);
}

/**
 * ptracer interface wrapper for PTRACE_ATTACH
 * followed by waitpid(2).
 *
 * This function also changes current_state based on the
 * waitpid return.
 *
 * If the child has exited (WIFEXITED or WIFSIGNALED),
 *   PTRACER_PROC_STATE_DEAD is set.
 *
 * If the child has stopped (WIFSTOPPED) by SIGSTOP,
 *   PTRACER_PROC_STATE_SIG_STOPPED is set.
 *
 * If the child has stopped (WIFSTOPPED) with anything
 *   else, PTRACER_PROC_STATE_PTRACE_STOPPED is set.
 *
 * @param[in] ctx - ptracer context structure
 * @param[out] out_status - child integer return status
 * @param[in] options - waitpid options parameter
 *
 * @return < 0 on ptrace(2) failure with error returned in errno
 * @return < 0 on waitpid(2) failure with error returned in errno
 * @return 0 if no children changed status (when given WNOHANG)
 * @return > 0 on successful waiting
 */
int
ptracer_attach_waitpid(struct ptracer_ctx *ctx,
    int *out_status, int options)
{
    if (ptracer_attach(ctx) != 0)
        return -1;

    return ptracer_waitpid(ctx, out_status, options);
}

/**
 * Wrapper function for PTRACE_ATTACH followed by waitpid(2).
 *
 * @param[in] pid - process id to PTRACE_ATTACH and wait on
 * @param[out] out_status - child integer return status
 * @param[in] options - waitpid options parameter
 *
 * @return < 0 on ptrace(2) failure with error returned in errno
 * @return < 0 on waitpid(2) failure with error returned in errno
 * @return 0 if no children changed status (when given WNOHANG)
 * @return > 0 on successful waiting
 */
int
ptrace_attach_waitpid(pid_t pid, int *out_status, int options)
{
    if (ptrace_attach(pid) != 0)
        return -1;

    return ptrace_waitpid(pid, out_status, options);
}

/* PTRACE_DETATCH */

/**
 * ptracer wrapper function for PTRACE_DETACH.
 *
 * @param[in] ctx - ptracer context structure to detach from
 *
 * @note
 *  Sets ctx->current_state to PTRACER_PROC_STATE_DETACHED
 *
 * @return 0 on success
 * @return not 0 on failure with error returned in errno
 */
int
ptracer_detach(struct ptracer_ctx *ctx)
{
    ctx->current_state = PTRACER_PROC_STATE_DETACHED;
    return ptrace_detach(ctx->pid);
}

/**
 * Wrapper function for PTRACE_DETACH.
 *
 * @param[in] pid - process id to detach from
 *
 * @return 0 on success
 * @return not 0 on failure with error returned in errno
 */
int
ptrace_detach(pid_t pid)
{
    return (ptrace(PTRACE_DETACH, pid, 0, 0) == -1);
}

