#ifndef H_LIB_PTRACER
#define H_LIB_PTRACER

#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/user.h>

#include <signal.h>
#include <stddef.h>

#include "shared/list.h"

struct ptracer_ctx;
/* Used when stopped at a breakpoint */
typedef void (*ptracer_breakpoint_callback)(struct ptracer_ctx *);

/* Node for the breakpoint list.
 * Describes where the breakpoint was set,
 * what the original data was before being overwritten,
 * and what function to call when trapped.
 */
struct ptracer_breakpoint {
    struct list_head node;
    ptracer_breakpoint_callback callback;

    unsigned long addr;
    unsigned long orig_data;
};

/* Process state flags and checking functions. */

#define PTRACER_PROC_IS_DEAD(ctx) \
    ((ctx)->current_state == PTRACER_PROC_STATE_DEAD)

#define PTRACER_PROC_IS_DETACHED(ctx) \
    ((ctx)->current_state == PTRACE_PROC_STATE_DETACHED)

#define PTRACER_PROC_IS_RUNNING(ctx) \
    ((ctx)->current_state == PTRACER_PROC_STATE_RUNNING)

#define PTRACER_PROC_IS_SIG_STOPPED(ctx) \
    ((ctx)->current_state == PTRACER_PROC_STATE_SIG_STOPPED)

#define PTRACER_PROC_IS_PTRACE_STOPPED(ctx) \
    ((ctx)->current_state == PTRACER_PROC_STATE_PTRACE_STOPPED)

#define PTRACER_PROC_IS_STOPPED(ctx) \
    (((ctx)->current_state & PTRACER_PROC_STATE_SIG_STOPPED) != 0)


#define PTRACER_PROC_STATE_MASK \
    ( PTRACER_PROC_STATE_DEAD | PTRACER_PROC_STATE_DETATCHED \
     | PTRACER_PROC_STATE_RUNNING | PTRACER_PROC_STATE_SIG_STOPPED \
     | PTRACER_PROC_STATE_PTRACE_STOPPED )

#define PTRACER_PROC_STATE_PTRACE_STOPPED \
    (PTRACER_PROC_STATE_SIG_STOPPED | __PTRACER_PROC_STATE_PTRACE)

#define PTRACER_PROC_STATE_DEAD        (0x00)
#define PTRACER_PROC_STATE_DETACHED    (0x01)
#define PTRACER_PROC_STATE_RUNNING     (0x02)
#define PTRACER_PROC_STATE_SIG_STOPPED (0x04)
#define __PTRACER_PROC_STATE_PTRACE    (0x08)


/* Ptracer context. */
struct ptracer_ctx {
    pid_t pid;

    int started;
    int process_status;

    int current_state;
    int expected_next_state;

    struct list_head breakpoints;
    struct ptracer_breakpoint *current_breakpoint;

    ptracer_breakpoint_callback run_callback;

    struct user_regs_struct regs;
    struct user_fpregs_struct fpregs;
};


extern void ptracer_init(struct ptracer_ctx *ctx, pid_t pid);
extern void ptracer_fini(struct ptracer_ctx *ctx);

extern struct ptracer_ctx *ptracer_new(pid_t pid);
extern void ptracer_destroy(struct ptracer_ctx *ctx);


extern int ptracer_set_breakpoint(struct ptracer_ctx *ctx,
                unsigned long addr, ptracer_breakpoint_callback cb);

extern int ptracer_clobber_address(struct ptracer_ctx *ctx,
                unsigned long addr, size_t length);


extern int ptracer_run(struct ptracer_ctx *ctx);


static inline void
ptracer_set_run_callback(struct ptracer_ctx *ctx,
    ptracer_breakpoint_callback cb)
{
    ctx->run_callback = cb;
}


/* ptrace call wrappers */

extern int ptracer_peektext(struct ptracer_ctx *ctx, unsigned long addr,
                unsigned long *out);
extern int ptrace_peektext(pid_t pid, unsigned long addr,
                unsigned long *out);


extern int ptracer_poketext(struct ptracer_ctx *ctx, unsigned long addr,
                unsigned long val);
extern int ptrace_poketext(pid_t pid, unsigned long addr,
                unsigned long val);


extern int ptracer_singlestep(struct ptracer_ctx *ctx);
extern int ptrace_singlestep(pid_t pid);

extern int ptracer_singlestep_waitpid(struct ptracer_ctx *ctx,
                int *out_status, int options);
extern int ptrace_singlestep_waitpid(pid_t pid, int *out_status,
                int options);


extern int ptracer_syscall(struct ptracer_ctx *ctx);
extern int ptrace_syscall(pid_t pid);

extern int ptracer_syscall_waitpid(struct ptracer_ctx *ctx,
                int *out_status, int options);
extern int ptrace_syscall_waitpid(pid_t pid, int *out_status,
                int options);


extern int ptracer_getregs(struct ptracer_ctx *ctx,
                struct user_regs_struct *out_regs);
extern int ptrace_getregs(pid_t pid, struct user_regs_struct *out_regs);


extern int ptracer_getfpregs(struct ptracer_ctx *ctx,
                struct user_fpregs_struct *out_regs);
extern int ptrace_getfpregs(pid_t pid, struct user_fpregs_struct *out_regs);


extern int ptracer_get_all_regs(struct ptracer_ctx *ctx,
                struct user_regs_struct *out_regs,
                struct user_fpregs_struct *out_fpregs);
extern int ptrace_get_all_regs(pid_t pid, struct user_regs_struct *out_regs,
                struct user_fpregs_struct *out_fpregs);


extern int ptracer_setregs(struct ptracer_ctx *ctx,
                struct user_regs_struct *regs);
extern int ptrace_setregs(pid_t pid, struct user_regs_struct *regs);


extern int ptracer_setfpregs(struct ptracer_ctx *ctx,
                struct user_fpregs_struct *regs);
extern int ptrace_setfpregs(pid_t pid, struct user_fpregs_struct *regs);


extern int ptracer_set_all_regs(struct ptracer_ctx *ctx,
                struct user_regs_struct *regs,
                struct user_fpregs_struct *fpregs);
extern int ptrace_set_all_regs(pid_t pid, struct user_regs_struct *regs,
                struct user_fpregs_struct *fpregs);


extern int ptracer_cont(struct ptracer_ctx *ctx);
extern int ptrace_cont(pid_t pid);


/* This is not a real ptrace command.
 * There is no ptrace_* equivalent. */
extern int ptracer_stop(struct ptracer_ctx *ctx);

extern int ptracer_stop_waitpid(struct ptracer_ctx *ctx,
                int *out_status, int options);


extern int ptracer_attach(struct ptracer_ctx *ctx);
extern int ptrace_attach(pid_t pid);

extern int ptracer_attach_waitpid(struct ptracer_ctx *ctx, int *out_status,
                int options);
extern int ptrace_attach_waitpid(pid_t pid, int *out_status, int options);


extern int ptracer_detach(struct ptracer_ctx *ctx);
extern int ptrace_detach(pid_t pid);

extern int ptracer_waitpid(struct ptracer_ctx *ctx, int *out_status,
                int options);
extern int ptrace_waitpid(pid_t pid, int *out_status, int options);

#endif /* H_LIB_PTRACER */
