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

enum ptracer_proc_state {
	PTRACER_PROC_STATE_DEAD = 0,
	PTRACER_PROC_STATE_RUNNING = 1,
	PTRACER_PROC_STATE_STOPPED = 2,
	PTRACER_PROC_STATE_PTRACE_STOPPED = 3
};

/* Ptracer context. */
struct ptracer_ctx {
	pid_t pid;

	int started;
	int process_status;

	enum ptracer_proc_state current_state;
	enum ptracer_proc_state expected_next_state;

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

#define ptracer_peektext(ctx, addr, out) \
	ptrace_peektext((ctx)->pid, addr, out)

extern int
ptrace_peektext(pid_t pid, unsigned long addr, unsigned long *out);

#define ptracer_poketext(ctx, addr, val) \
	ptrace_poketext((ctx)->pid, addr, val)

static inline int
ptrace_poketext(pid_t pid, unsigned long addr, unsigned long val)
{
	return (ptrace(PTRACE_POKETEXT, pid, (void *)addr, (void *)val) == -1);
}

#define ptracer_singlestep(ctx) \
	ptrace_singlestep((ctx)->pid)

static inline int
ptrace_singlestep(pid_t pid)
{
	return (ptrace(PTRACE_SINGLESTEP, pid, 0, 0) == -1);
}

#define ptracer_singlestep_waitpid(ctx, out_status, options) \
	ptrace_singlestep_waitpid((ctx)->pid, out_status, options)

extern int
ptrace_singlestep_waitpid(pid_t pid, int *out_status, int options);

#define ptracer_syscall(ctx) \
	ptrace_syscall((ctx)->pid)

static inline int
ptrace_syscall(pid_t pid)
{
	return (ptrace(PTRACE_SYSCALL, pid, 0, 0) == -1);
}

#define ptracer_syscall_waitpid(ctx, out_status, options) \
	ptrace_syscall_waitpid((ctx)->pid, out_status, options)

extern int
ptrace_syscall_waitpid(pid_t pid, int *out_status, int options);

#define ptracer_getregs(ctx, out_regs) \
	ptrace_getregs((ctx)->pid, out_regs)

static inline int
ptrace_getregs(pid_t pid, struct user_regs_struct *out_regs)
{
	return (ptrace(PTRACE_GETREGS, pid, 0, (void *)out_regs) == -1);
}

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

#define ptracer_setregs(ctx, regs) \
	ptrace_setregs((ctx)->pid, regs)

static inline int
ptrace_setregs(pid_t pid, struct user_regs_struct *regs)
{
	return (ptrace(PTRACE_SETREGS, pid, 0, (void *)regs) == -1);
}

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

#define ptracer_cont(ctx) \
	ptrace_cont((ctx)->pid)

static inline int
ptrace_cont(pid_t pid)
{
	/* TODO: Allow the signal return parameter? */
	return (ptrace(PTRACE_CONT, pid, 0, 0) == -1);
}

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

static inline int
ptrace_attach(pid_t pid)
{
	return (ptrace(PTRACE_ATTACH, pid, 0, 0) == -1);
}

#define ptracer_attach_waitpid(ctx, out_status, options) \
	ptrace_attach_waitpid((ctx)->pid, out_status, options)

extern int
ptrace_attach_waitpid(pid_t pid, int *out_status, int options);

#define ptracer_detach(ctx) \
	ptrace_detatch((ctx)->pid)

static inline int
ptrace_detach(pid_t pid)
{
	return (ptrace(PTRACE_DETACH, pid, 0, 0) == -1);
}

#endif /* H_LIB_PTRACER */
