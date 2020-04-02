#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>

#define BUFLEN 4096

char *testcase_description = "Separate file lseek";

void testcase(unsigned long long *iterations)
{
	char buf[BUFLEN];
	char tmpfile[] = "/tmp/willitscale.XXXXXX";
	int fd = mkstemp(tmpfile);

	assert(fd >= 0);
//#	unlink(tmpfile);

	assert(write(fd, buf, sizeof(buf)) == sizeof(buf));

	while (1) {
		struct stat buf;
		access(tmpfile, R_OK);

		(*iterations)++;
	}
}
