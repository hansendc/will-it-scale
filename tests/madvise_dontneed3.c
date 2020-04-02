#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <assert.h>

#define MEMSIZE (128 * 1024 * 1024)

char *testcase_description = "Anonymous memory page fault then MADV_DONTNEED";

void eat_icache(void)
{
        // the nopl is 7-bytes
        // 64k / 7 = 9362
        asm(".rept 9362 ; nopl 0x7eeeeeee(%rax) ; .endr");
}

void testcase(unsigned long long *iterations)
{
	unsigned long pgsize = getpagesize();
	unsigned long eat_icache_addr = (unsigned long)&eat_icache;

	eat_icache_addr &= ~0xfff;

	while (1) {
		unsigned long i;

		for (i = 0; i < MEMSIZE; i += pgsize) {
			//eat_icache();
			madvise(eat_icache_addr, 65536, MADV_DONTNEED);
			(*iterations)++;
		}
	}
}
