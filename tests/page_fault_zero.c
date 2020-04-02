#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <assert.h>

#define MEMSIZE (128 * 1024 * 1024)

char *testcase_description = "Anonymous memory read fault";

#define barrier() __asm__ __volatile__("": : :"memory")
__attribute__((noinline)) char read_ptr(char *ptr)
{
        /*
         * Keep GCC from optimizing this away somehow
         */
        barrier();
        return *ptr;
}


void testcase(unsigned long long *iterations, unsigned long nr)
{
	unsigned long pgsize = getpagesize();

	while (1) {
		unsigned long i;

		char *c = mmap(NULL, MEMSIZE, PROT_READ|PROT_WRITE,
			       MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
		assert(c != MAP_FAILED);

		madvise(c, MEMSIZE, MADV_NOHUGEPAGE);

		for (i = 0; i < MEMSIZE; i += 4096) {
			read_ptr(&c[i]);
			(*iterations)++;
		}

		munmap(c, MEMSIZE);
	}
}
