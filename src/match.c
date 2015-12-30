#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "match.h"


bool
match_flags_set_integer(const char *value, union match_flags *flags)
{
	int neg;
	int64_t sval;
	uint64_t val;
	char *endptr = NULL;

	errno = 0;
	val = (uint64_t)strtoull(value, &endptr, 0);

	if ((errno != 0) || (*endptr != '\0'))
		return false;

	sval = *(int8_t *)&val;

	/* Determine if we were given a negative value. */
	neg = (sval < 0LL);

	/* For negative values, we know the given number
	 * is < 0, so only check if the parsed result is
	 * smaller than the signed type minimum. An upper
	 * bound check would be redundant.
	 */

	if (val <= UINT8_MAX) {
		if (neg)
			flags->i8 = !(sval < INT8_MIN);
		else
			flags->i8 = 1;
	}

	if (val <= UINT16_MAX) {
		if (neg)
			flags->i16 = !(sval < INT16_MIN);
		else
			flags->i16 = 1;
	}

	if (val <= UINT32_MAX) {
		if (neg)
			flags->i32 = !(sval < INT32_MIN);
		else
			flags->i32 = 1;
	}

	/* A value can always be a 64 bit integer. */
	flags->i64 = 1;

	return true;
}

bool
match_flags_set_floating(const char *value, union match_flags *flags)
{
	float f;
	double d;
	char *endptr;

	/* Only if the value can be parsed explicitly as a float,
	 * is this value capable of being an f32. */

	errno = 0;
	endptr = NULL;
	f = strtof(value, &endptr);

	/* This would result in a parse falure for double as well
	 * so just fail out. */
	if (endptr != '\0')
		return false;

	if (errno == 0) {
		flags->f32 = 1;
		flags->f64 = 1;
		return true;
	}

	/* If float parsing failed, attempt double. */

	errno = 0;
	endptr = NULL;
	d = strtod(value, &endptr);

	if ((errno != 0) && (*endptr != '\0'))
		return false;

	flags->f64 = 1;

	return true;
}

