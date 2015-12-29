#ifndef H_PID_MAPS
#define H_PID_MAPS

#include <sys/types.h>
#include "region.h"

int process_pid_maps(pid_t pid, struct region_list *list);

#endif /* H_PID_MAPS */
