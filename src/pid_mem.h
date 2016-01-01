#ifndef H_PID_MEM
#define H_PID_MEM

#include <sys/types.h>

#define PID_MEM_FLAGS_READ  (0x01)
#define PID_MEM_FLAGS_WRITE (0x02)
#define PID_MEM_FLAGS_MASK \
    (PID_MEM_FLAGS_READ | PID_MEM_FLAGS_WRITE)

extern int can_read_pid_mem(pid_t pid);
extern int can_write_pid_mem(pid_t pid);


extern ssize_t read_pid_mem(pid_t pid, void *buf, size_t size, off_t offset);
extern ssize_t read_pid_mem_fd(int fd, void *buf, size_t size, off_t offset);


extern ssize_t read_pid_mem_loop(pid_t pid, void *buf, size_t size,
                    off_t offset);
extern ssize_t read_pid_mem_loop_fd(int fd, void *buf, size_t size,
                    off_t offset);


extern ssize_t write_pid_mem(pid_t pid, void *buf, size_t size, off_t offset);
extern ssize_t write_pid_mem_fd(int fd, void *buf, size_t size, off_t offset);


extern ssize_t write_pid_mem_loop(pid_t pid, void *buf, size_t size,
                    off_t offset);
extern ssize_t write_pid_mem_loop_fd(int fd, void *buf, size_t size,
                    off_t offset);


extern int open_pid_mem(pid_t pid, int pid_mem_flags);


static inline int
close_pid_mem(int fd)
{
    return close(fd);
}

#endif /* H_PID_MEM */

/* vim: set et ts=4 sts=4 sw=4 syntax=c : */
