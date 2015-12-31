#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/user.h>
#include <sys/wait.h>

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "shared/util.h"
#include "shared/list.h"
#include "ptracer.h"

/*
 * TODO: encapsulate register stuff into a header.
 */
#if defined(__i386__)
#define __INST_PTR_MEMBER  eip
#elif defined(__x86_64__)
#define __INST_PTR_MEMBER  rip
#else
#error Unsupported architecture
#endif

#define INST_PTR_TYPE \
	typeof( ((struct user_regs_struct *)0)->__INST_PTR_MEMBER )

#define set_inst_ptr(regp, val) \
	do { (regp)->__INST_PTR_MEMBER = (INST_PTR_TYPE)(val); } while (0)

#define get_inst_ptr(regp) \
	({ \
		INST_PTR_TYPE __ret = (regp)->__INST_PTR_MEMBER; \
		__ret; \
	})



#define breakpoint_entry(entry) \
	list_entry(entry, struct ptracer_breakpoint, node)

static int
__ptrace_waitpid(pid_t pid, int *out_status, int options)
{
	pid_t perr;

	perr = waitpid(pid, out_status, options);

	if (perr == (pid_t)-1)
		return -1;

	/* 0 - no children changed status (WNOHANG option)
	 * 1 - sccess
	 */
	return (perr != (pid_t)0);
}

static inline int
__ptrace_cont_and_wait(pid_t pid, int *out_status, int options)
{
	int err;

	err = ptrace_cont(pid);

	if (err != 0)
		return -1;

	return  __ptrace_waitpid(pid, out_status, options);
}


struct ptracer_ctx *
ptracer_new(pid_t pid)
{
	struct ptracer_ctx *ctx;

	ctx = malloc(sizeof(*ctx));

	if (ctx == NULL)
		return NULL;

	ptracer_init(ctx, pid);
	return ctx;
}

void
ptracer_destroy(struct ptracer_ctx *ctx)
{
	ptracer_fini(ctx);
	free(ctx);
}


void
ptracer_init(struct ptracer_ctx *ctx, pid_t pid)
{
	memset(ctx, 0, sizeof(*ctx));
	ctx->pid = pid;
	list_head_init(&(ctx->breakpoints));
}

void
ptracer_fini(struct ptracer_ctx *ctx)
{
	struct list_head *next;
	struct list_head *entry;

	list_for_each_safe(entry, next, &(ctx->breakpoints)) {
		struct ptracer_breakpoint *node;

		list_del(entry);
		node = breakpoint_entry(entry);
		free(node);
	}
}


static int
breakpoint_enable(struct ptracer_ctx *ctx,
	struct ptracer_breakpoint *node)
{
	int err;
	unsigned long data;

	err = ptracer_peektext(ctx, node->addr, &data);

	if (err != 0)
		return err;

	node->orig_data = data;
	*(uint8_t *)&data = 0xCC;

	return ptracer_poketext(ctx, node->addr, data);
}

static inline int
breakpoint_disable(struct ptracer_ctx *ctx,
	struct ptracer_breakpoint *node)
{
	return ptracer_poketext(ctx, node->addr, node->orig_data);
}

static int
breakpoint_resume(struct ptracer_ctx *ctx,
	struct ptracer_breakpoint *node)
{
	int err;
	int wait_status;

	/* TODO: On ptracer_* error, check errno for ESRCH versus
     * other possible returns. ESRCH means the process has
     * exited and we should stop.
     */

	err = ptracer_getregs(ctx, &(ctx->regs));

	if (err != 0) {
		/* ptrace error */
		return -1;
	}

	/* Disable the breakpoint, rewind the IP back to the original
     * instruction and single-step the process.  This executes the
     * original instruction that was replaced by the breakpoint.
     */

	set_inst_ptr(&(ctx->regs), (INST_PTR_TYPE)node->addr);

	err = ptracer_setregs(ctx, &(ctx->regs));

	if (err != 0) {
		/* ptrace error */
		return -1;
	}

	err = breakpoint_disable(ctx, node);

	if (err != 0) {
		/* ptrace error */
		return -1;
	}

	err = ptracer_singlestep_waitpid(ctx, &wait_status, 0);

	/* Note different return value check. */
	if (err < 0) {
		/* ptrace or waitpid error */
		return -1;
	}

	ctx->process_status = wait_status;

	if (WIFEXITED(wait_status))
		return 0; /* process exited. */

	/* Re-enable the breakpoint and let the process run. */
	err = breakpoint_enable(ctx, node);

	if (err != 0) {
		/* ptrace error */
		return -1;
	}

	err = __ptrace_cont_and_wait(ctx->pid, &wait_status, 0);

	if (err < 0) {
		/* ptrace or waitpid error */
		return -1;
	}

	ctx->process_status = wait_status;

	if (WIFEXITED(wait_status))
		return 0;

	if (WIFSTOPPED(wait_status))
		return 1;

	return -1;
}


int
ptracer_set_breakpoint(struct ptracer_ctx *ctx,
	unsigned long addr, ptracer_breakpoint_callback cb)
{
	struct ptracer_breakpoint *node;

	node = calloc(1, sizeof(*node));

	if (node == NULL)
		return -1;

	node->callback = cb;
	node->addr = addr;

	list_add(&(node->node), &(ctx->breakpoints));

	if (ctx->started)
		return breakpoint_enable(ctx, node);

	return 0;
}

int
ptracer_clobber_addr(struct ptracer_ctx *ctx,
	unsigned long addr, size_t length)
{
	int err;
	size_t rem_len;

	unsigned long data;
	unsigned long left_addr;

	left_addr = addr;

	rem_len = length / sizeof(data);

	if (rem_len != 0) {
		size_t i;

		memset(&data, 0x90, sizeof(data));

		for (i = 0; i < rem_len; ++i) {
			err = ptracer_poketext(ctx, left_addr, data);

			if (err != 0) {
				/* ptrace error */
				return -1;
			}

			left_addr += (i + 1) * sizeof(data);
		}
	}

	rem_len = length % sizeof(data);

	/* Done clobbering. */
	if (rem_len == 0)
		return 0;

	err = ptracer_peektext(ctx, left_addr, &data);

	if (err != 0) {
		/* ptrace error */
		return -1;
	}

	/* Data is sequential, so this works properly
	 * regardless of endianess. */
	memset(&data, 0x90, rem_len);

	return ptracer_poketext(ctx, left_addr, data);
}


static struct ptracer_breakpoint *
find_breakpoint_node(struct list_head *head, INST_PTR_TYPE ip)
{
	struct list_head *entry;

	list_for_each(entry, head) {
		struct ptracer_breakpoint *bp;

		bp = breakpoint_entry(entry);

		if (bp->addr == (unsigned long)ip)
			return bp;
	}

	return NULL;
}

int
ptracer_run(struct ptracer_ctx *ctx)
{
	int err;
	int wait_status;
	struct list_head *entry;

	ctx->started = 1;

	/* Set the breakpoints. */
	list_for_each(entry, &(ctx->breakpoints)) {
		err = breakpoint_enable(ctx, breakpoint_entry(entry));

		if (err != 0) {
			/* ptrace error */
			return err;
		}
	}

	ctx->current_breakpoint = NULL;

	if (ctx->run_callback != NULL)
		ctx->run_callback(ctx);

	err = __ptrace_cont_and_wait(ctx->pid, &wait_status, 0);

	if (err < 0) {
		/* ptrace or waitpid error */
		return -1;
	}

	ctx->process_status = wait_status;

	if (WIFEXITED(wait_status))
		return 0;

	if (!WIFSTOPPED(wait_status))
		return -1;

	for (;;) {
		struct ptracer_breakpoint *node;

		/* Very likely that a trap occured. */
		err = ptracer_getregs(ctx, &(ctx->regs));

		if (err != 0) {
			/* ptrace error */
			return -1;
		}

		/* Find the trapped breakpoint.
         * Subtract one from IP since a trap is a single
		 * byte and the breakpoint is registered by the
		 * original IP.
		 */
		node = find_breakpoint_node(&(ctx->breakpoints),
					get_inst_ptr(&(ctx->regs)) - 1);

		if (node == NULL) {
			/* Seems we did not hit a trap for out of our breakpoints. */
			err = __ptrace_cont_and_wait(ctx->pid, &wait_status, 0);

			if (err < 0) {
				/* ptrace or waitpid error */
				return -1;
			}

			ctx->process_status = wait_status;

			if (WIFEXITED(wait_status))
				return 0;

			if (!WIFSTOPPED(wait_status))
				return 1;

			continue;
		}

		/* Call the found breakpoint callback. */
		ctx->current_breakpoint = node;

		if (node->callback != (ptracer_breakpoint_callback)0)
			node->callback(ctx);

		/* Resume execution and wait on next event. */
		err = breakpoint_resume(ctx, node);

		if (err < 0) {
			/* ptrace or waitpid error */
			return -1;
		}

		/* Child exited */
		if (err == 0)
			return 0;

		/* Child stopped properly, continue loop. */
	}

	/* Should never get here. */
	return -1;
}







/* ptrace call wrappers */

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

int
ptrace_singlestep_waitpid(pid_t pid, int *out_status, int options)
{
	if (ptrace_singlestep(pid) != 0)
		return -1;

	return __ptrace_waitpid(pid, out_status, options);
}

int
ptrace_syscall_waitpid(pid_t pid, int *out_status, int options)
{
	if (ptrace_syscall(pid) != 0)
		return -1;

	return __ptrace_waitpid(pid, out_status, options);
}

int
ptrace_stop_waitpid(pid_t pid, int *out_status, int options)
{
	if (ptrace_stop(pid) != 0)
		return -1;

	return __ptrace_waitpid(pid, out_status, options);
}

int
ptrace_attach_waitpid(pid_t pid, int *out_status, int options)
{
	if (ptrace_attach(pid) != 0)
		return -1;

	return __ptrace_waitpid(pid, out_status, options);
}
