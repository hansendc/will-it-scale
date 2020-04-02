#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>

#define PAGE_SIZE 4096

#define READ 0
#define WRITE 1

#define PIPE_PERIOD 1
#define FAULT_PERIOD 1
#define MEMSIZE (128 * 1024 * 1024)

#ifdef __i386__
#define REX_PREFIX
#else
#define REX_PREFIX "0x48, "
#endif

static inline uint64_t xgetbv(uint32_t index)
{
        uint32_t eax, edx;

        asm volatile(".byte 0x0f,0x01,0xd0" /* xgetbv */
                     : "=a" (eax), "=d" (edx)
                     : "c" (index));
        return eax + ((uint64_t)edx << 32);
}

struct xsave_hdr_struct {
        uint64_t xstate_bv;
        uint64_t reserved1[2];
        uint64_t reserved2[5];
} __attribute__((packed));

struct bndregs_struct {
        uint64_t bndregs[8];
} __attribute__((packed));

struct bndcsr_struct {
        uint64_t cfg_reg_u;
        uint64_t status_reg;
} __attribute__((packed));

struct xsave_struct {
        uint8_t fpu_sse[512];
        struct xsave_hdr_struct xsave_hdr;
        uint8_t ymm[256];
        uint8_t lwp[128];
        struct bndregs_struct bndregs;
        struct bndcsr_struct bndcsr;
} __attribute__((packed));

static void xrstor(struct xsave_struct *fx, uint64_t mask)
{
        uint32_t lmask = mask;
        uint32_t hmask = mask >> 32;

        asm volatile(".byte " REX_PREFIX "0x0f,0xae,0x2f\n\t"
                     : : "D" (fx), "m" (*fx), "a" (lmask), "d" (hmask)
                     :   "memory");
}

static void xsave(void *_fx, uint64_t mask)
{
        uint32_t lmask = mask;
        uint32_t hmask = mask >> 32;
        unsigned char *fx = _fx;

        asm volatile(".byte " REX_PREFIX "0x0f,0xae,0x27\n\t"
                     : : "D" (fx), "m" (*fx), "a" (lmask), "d" (hmask)
                     :   "memory");
}

char *testcase_description = "Context switch via pipes";

extern void new_task(void *(func)(void *), void *arg);

struct args {
	int fd1[2];
	int fd2[2];
};

static void *child(void *arg)
{
	struct args *a = arg;
	char c;
	int ret;

	while (1) {
		do {
			ret = read(a->fd1[READ], &c, 1);
		} while (ret != 1 && errno == EINTR);
		assert(ret == 1);

		do {
			ret = write(a->fd2[WRITE], &c, 1);
		} while (ret != 1 && errno == EINTR);
		assert(ret == 1);
	}

	return NULL;
}

void testcase(unsigned long long *iterations)
{
	struct args a;
	char c;
	int ret;
	char *mem;
	struct xsave_struct *xsave_buf = mmap(NULL, PAGE_SIZE, PROT_READ|PROT_WRITE,
					       MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);

	assert(pipe(a.fd1) == 0);
	assert(pipe(a.fd2) == 0);

	new_task(child, &a);

	mem = mmap(NULL, MEMSIZE, PROT_READ|PROT_WRITE,
			       MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	assert(mem != MAP_FAILED);

	while (1) {
		if (!(*iterations % PIPE_PERIOD)) {
			do {
				ret = write(a.fd1[WRITE], &c, 1);
			} while (ret != 1 && errno == EINTR);
			assert(ret == 1);
			do {
				ret = read(a.fd2[READ], &c, 1);
			} while (ret != 1 && errno == EINTR);
			assert(ret == 1);
		}
		if (!(*iterations % FAULT_PERIOD)) {
			memset(mem, 'x', MEMSIZE);
			ret = madvise(mem, MEMSIZE, MADV_DONTNEED);
			assert(!ret);
		}
		{
			printf("&xsave_bss: %p\n", xsave_buf);
			printf("1 XCR0: 0x%jx\n", xgetbv(0));
			// — If RFBM[i] = 0, state component i is tracked as modified; XMODIFIED[i] is set to 1.
			// poke the modified exception:
			xrstor(xsave_buf, 0x0);
			printf("2 XCR0: 0x%jx\n", xgetbv(0));
			munmap(xsave_buf, PAGE_SIZE);
		}

		// iret vs sysexit?
		(*iterations) += 1;
	}
}
