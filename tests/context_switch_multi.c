#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>

//#define DEBUG 1

#ifdef DEBUG
#define dprintf printf
#else
#define dprintf(...) do{}while(0)
#endif

#define PAGE_SIZE 4096

#define READ 0
#define WRITE 1

#define PIPE_PERIOD 1
#define FAULT_PERIOD 19
#define AVX_POKE_PERIOD 13
#define XSAVE_MODIFIED_OPTIMIZATION_PERIOD 11
#define MEMSIZE (128 * 1024)

#ifdef __i386__
#define REX_PREFIX
#else
#define REX_PREFIX "0x48, "
#endif

static inline void avx_poke(void)
{
#define ALIGN32 __attribute__ ((__aligned__(32)))
        char ymm_save[32] ALIGN32;

	/*
	 * should take AVX out of the init state
	 */
        asm volatile("vmovaps %%ymm0, %0" : "=m" (ymm_save) : : "memory"); \
        asm volatile("vmovaps %0, %%ymm0" : : "m" (ymm_save));
}

#define ALIGN32 __attribute__ ((__aligned__(32)))


static inline uint64_t xgetbv(uint32_t index)
{
        uint32_t eax, edx;

	// Whoops, XGETBV[1] is not supported on BDW
	if (index != 0)
		return -1;

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
	struct xsave_struct *xsave_buf;


	assert(pipe(a.fd1) == 0);
	assert(pipe(a.fd2) == 0);

	mem = mmap(NULL, MEMSIZE, PROT_READ|PROT_WRITE,
			       MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	assert(mem != MAP_FAILED);

	new_task(child, &a);

	xsave_buf = mmap(NULL, PAGE_SIZE, PROT_READ|PROT_WRITE,
					       MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	printf("a XCR0: 0x%jx XINUSE: 0x%jx\n", xgetbv(0), xgetbv(1));
	avx_poke();
	printf("b XCR0: 0x%jx XINUSE: 0x%jx\n", xgetbv(0), xgetbv(1));
	// save off all the extended state
	xsave(xsave_buf, 0xffff);
	// and now take it all out of the init state
	xsave_buf->xsave_hdr.xstate_bv = xgetbv(0);
	xrstor(xsave_buf, 0xffff);
	printf("c XCR0: 0x%jx XINUSE: 0x%jx\n", xgetbv(0), xgetbv(1));

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
			ret = madvise(mem, MEMSIZE, MADV_DONTNEED);
			assert(!ret);
			memset(mem, 'x', MEMSIZE);
		}
		if (!(*iterations % AVX_POKE_PERIOD)) {
			avx_poke();
		}
		if (!(*iterations % XSAVE_MODIFIED_OPTIMIZATION_PERIOD)) {
			dprintf("&xsave_bss: %p\n", xsave_buf);
			dprintf("1 XCR0: 0x%jx XINUSE: 0x%jx XSTATE_BV: 0x%jx\n", xgetbv(0), xgetbv(1), xsave_buf->xsave_hdr.xstate_bv);
			// — If RFBM[i] = 0, state component i is tracked as modified; XMODIFIED[i] is set to 1.
			// poke the modified exception:
			xrstor(xsave_buf, 0x0);
			dprintf("2 XCR0: 0x%jx XINUSE: 0x%jx XSTATE_BV: 0x%jx\n", xgetbv(0), xgetbv(1), xsave_buf->xsave_hdr.xstate_bv);
		}

		// iret vs sysexit?
		(*iterations) += 1;
	}
}
