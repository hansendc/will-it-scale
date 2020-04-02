#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <assert.h>
#include <pthread.h>

#define MEMSIZE (128 * 1024 * 1024)

char *testcase_description = "Anonymous memory page fault then MADV_DONTNEED";

void threadwork(void *arg)
{
	unsigned long pgsize = getpagesize();
	unsigned long long *iterations = arg;

	while (1) {
		unsigned long i;

		char *c = mmap(NULL, MEMSIZE, PROT_READ|PROT_WRITE,
			       MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
		assert(c != MAP_FAILED);

		for (i = 0; i < MEMSIZE; i += pgsize) {
			c[i] = 0;
			madvise(&c[i], pgsize, MADV_DONTNEED);
			if (iterations)
				(*iterations)++;
		}

		munmap(c, MEMSIZE);
	}
}

void testcase(unsigned long long *iterations)
{
	pthread_attr_t attr;
	pthread_t tid;

        pthread_attr_init(&attr);
	pthread_create(&tid, &attr, threadwork, iterations);

	threadwork(iterations);
}
