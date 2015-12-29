
/* Format:
 *
 *  address        perms          offset    dev              inode    pathname
 *  -------------  -------------  --------  ---------------  -------  --------
 *  <start>-<end>  <r><w><x><p>   <offset>  <major>:<minor>  <inode>  <path>
 *
 *  address:
 *
 *    start    unsigned long, map start address
 *    end      unsigned long, map end address
 *
 *  perms:
 *
 *    r        byte, read permission or '-' for no read
 *    w        byte, write permissions or '-' for no write
 *    x        byte, exec permissions or '-' for no exec
 *    p        byte, private mapping or '-' for non-copy-on-write (shared)
 *
 *  offset:
 *
 *    offset   unsigned long, offset into the file if from a file
 *
 *  dev:
 *
 *    major    unsigned int, major device number
 *    minor    unsigned int, minor device number
 *
 *  inode:
 *
 *    inode    unsigned long, file inode number if from a file
 *
 *  pathaname:
 *
 *    path     string, path to the file mapped from, psuedo-path, or nothing
 *
 * Possible pseudo-paths:
 *
 *   [stack]         main thread stack
 *   [stack:<tid>]   thread stack
 *   [vdso]          virtual dynamic shared object (for the link editor)
 *   [heap]          process heap
 */

#define P_READ  0x1
#define P_WRITE 0x2
#define P_EXEC  0x4
#define P_PRIV  0x8
struct mapping {
	struct {
		unsigned long start;
		unsigned long end;
	} address;

	unsigned int perms;

	unsigned long offset;

	struct {
		unsigned int major;
		unsigned int minor;
	} dev;

	unsigned long inode;

	char pathname[1];
}

static inline int
parse_line(const char *line, struct mapping *mapping)
{
	int ret;
	unsigned char r, w, x, p;

	ret = sscanf(line,
			"%lu-%lu %c%c%c%c %lu %u:%u %lu %s",
			&(mapping->address.start), &(mapping->address.end),
			&r, &w, &x, &p,
			&(mapping->offset),
			&(mapping->dev.major), &(mapping->dev.minor),
			&(mapping->inode),
			&(mapping->pathname));

	mapping->perms = ((r == 'r') * P_READ)
				   | ((w == 'w') * P_WRITE)
				   | ((x == 'x') * P_EXEC)
				   | ((p == 'p') * P_PRIV);

	return ret;
}
