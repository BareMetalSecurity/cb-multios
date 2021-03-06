/* Copyright 2015 Peter Goodman (peter@trailofbits.com), all rights reserved. */

#define LIBCGC_IMPL
#include "libcgc.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/types.h>
#include <unistd.h>
#include <err.h>

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) < (b)) ? (b) : (a))

#ifndef SIGPWR
# define SIGPWR 0
#endif

enum {
    k2GiB = 2147483648
};

static uintptr_t gMemBegin = 0;
static uintptr_t gMemEnd = 0;
static uint8_t gMappedPages[(k2GiB / PAGE_SIZE) / 8] = {0};

/* Reserve a large slab of memory that we'll use for doling out allocations.
 * The CGC allocator, for the most part, is a bump pointer allocator, returning
 * pages from a contiguous range. There are some edge cases, e.g. the stack,
 * high memory pressure, etc.
 */
static void init_memory(void) {
  errno = 0;
  cgc_size_t alloc_size = k2GiB;
  int errno_val = ENOMEM;
  void *mem = NULL;
  for (; errno_val && alloc_size; alloc_size /= 2) {
    mem = mmap(NULL, alloc_size, PROT_NONE,
               MAP_PRIVATE | MAP_ANONYMOUS,  /* TODO(pag): MAP_32BIT? */
               -1, 0);
    errno_val = errno;
    errno = 0;
  }

  if (!mem || !alloc_size || errno_val) {
    exit(EXIT_FAILURE);
  }

  gMemBegin = (uintptr_t) mem;
  gMemEnd = gMemBegin + alloc_size;
}

static int test_page(uintptr_t addr) {
  const cgc_size_t page = (addr - gMemBegin) / PAGE_SIZE;
  const cgc_size_t byte = page / 8;
  const cgc_size_t bit = page % 8;
  return 0 != (gMappedPages[byte] & (1U << bit));
}

static void set_page(uintptr_t addr) {
  const cgc_size_t page = (addr - gMemBegin) / PAGE_SIZE;
  const cgc_size_t byte = page / 8;
  const cgc_size_t bit = page % 8;
  gMappedPages[byte] |= 1U << bit;
}

static void clear_page(uintptr_t addr) {
  const cgc_size_t page = (addr - gMemBegin) / PAGE_SIZE;
  const cgc_size_t byte = page / 8;
  const cgc_size_t bit = page % 8;
  gMappedPages[byte] &= ~(1U << bit);
}


/* Returns `1` if a page is readable, otherwise `0`.
 *
 * TODO(pag): Use `select` instead for portability? */
static int page_is_readable(const void *ptr) {
  const uintptr_t addr = (uintptr_t) ptr;
  if (!addr) return 0;
  if (gMemBegin <= addr && addr < gMemEnd) {
    return test_page(addr);
  }

  errno = 0;
  sigaction(SIGPWR, ptr, NULL);  /* SIGPWR is rarely used. */
  const int errno_val = errno;
  errno = 0;
  return EFAULT != errno_val;
}

/* Returns `1` if a page is writable, otherwise `0`.
 *
 * TODO(pag): Use `fstat` instead for portability? */
static int page_is_writable(void *ptr) {
  const uintptr_t addr = (uintptr_t) ptr;
  if (!addr) return 0;
  if (gMemBegin <= addr && addr < gMemEnd) {
    return test_page(addr);
  }
  uint8_t mem[sizeof(struct sigaction)];
  memcpy(&(mem[0]), ptr, sizeof(struct sigaction));
  errno = 0;
  sigaction(SIGPWR, NULL, ptr);  /* SIGPWR is rarely used. */
  const int errno_val = errno;
  errno = 0;
  if (EFAULT != errno_val) {
    memcpy(ptr, &(mem[0]), sizeof(struct sigaction));
    return 1;
  } else {
    return 0;
  }
}

/* Returns the number of readable bytes pointed to by `ptr`, up to a maximum
 * of `size` bytes. */
static cgc_size_t num_readable_bytes(const void *ptr, cgc_size_t size) {
  const uintptr_t addr = (uintptr_t) ptr;
  const uintptr_t end_addr = addr + size;
  uintptr_t page_addr = addr & ~(((uintptr_t) PAGE_SIZE) - 1);
  cgc_size_t count = 0;
  cgc_size_t disp = addr - page_addr;
  for (; page_addr < end_addr; page_addr += PAGE_SIZE) {
    if (!page_is_readable((const void *) page_addr)) {
      break;
    }
    count += PAGE_SIZE - disp;
    disp = 0;
  }
  return MIN(count, size);
}

/* Returns the number of writable bytes pointed to by `ptr`, up to a maximum
 * of `size` bytes. */
static cgc_size_t num_writable_bytes(void *ptr, cgc_size_t size) {
  const uintptr_t addr = (uintptr_t) ptr;
  const uintptr_t end_addr = addr + size;
  uintptr_t page_addr = addr & ~(((uintptr_t) PAGE_SIZE) - 1);
  cgc_size_t count = 0;
  cgc_size_t disp = addr - page_addr;
  for (; page_addr < end_addr; page_addr += PAGE_SIZE) {
    if (!page_is_readable((const void *) page_addr) ||
        !page_is_writable((void *) page_addr)) {
      break;
    }
    count += PAGE_SIZE - disp;
    disp = 0;
  }
  return MIN(count, size);
}

/* Terminates the process. */
void _terminate(unsigned int status) {
  exit(status);
  __builtin_unreachable();
}

#define OBJECT_IS_READABLE(ptr) \
  (sizeof(*(ptr)) == num_readable_bytes((ptr), sizeof(*(ptr))))

#define OBJECT_IS_WRITABLE(ptr) \
  (sizeof(*(ptr)) == num_writable_bytes((ptr), sizeof(*(ptr))))

/* Updates a byte counter and returns the corresponding status code. */
static int update_byte_count(cgc_size_t *counter, cgc_size_t count) {
  if (!counter) return 0;
  if (!OBJECT_IS_WRITABLE(counter)) {
    return CGC_EFAULT;
  } else {
    *counter = count;
    return 0;
  }
}

/* Transmits data from one CGC process to another. */
int transmit(int fd, const void *buf, cgc_size_t count, cgc_size_t *tx_bytes) {
  if (!count) {
    return update_byte_count(tx_bytes, 0);
  } else if (0 > fd) {
    return CGC_EBADF;
  }

  const cgc_size_t max_count = num_readable_bytes(buf, count);
  if (!max_count) {
    return CGC_EFAULT;
  } else if (max_count < count) {
    count = max_count & ~2047;
  } else {
    count = max_count;
  }

  errno = 0;
  const cgc_ssize_t ret = write(fd, buf, count);
  const int errno_val = errno;
  errno = 0;

  if (EFAULT == errno_val) {
    return CGC_EFAULT;
  } else if (EBADF == errno_val) {
    return CGC_EBADF;
  } else if (errno_val) {
    return CGC_EPIPE;  /* Guess... */
  } else {
    return update_byte_count(tx_bytes, (cgc_size_t) ret);
  }
}

/* Receives data from another CGC process. */
int receive(int fd, void *buf, cgc_size_t count, cgc_size_t *rx_bytes) {
  if (!count) {
    return update_byte_count(rx_bytes, 0);
  } else if (0 > fd) {
    return CGC_EBADF;
  }

  const cgc_size_t max_count = num_writable_bytes(buf, count);
  if (!max_count) {
    return CGC_EFAULT;
  }

  errno = 0;
  const cgc_ssize_t ret = read(fd, buf, max_count);
  const int errno_val = errno;
  errno = 0;

  if (EFAULT == errno_val) {
    return CGC_EFAULT;
  } else if (EBADF == errno_val) {
    return CGC_EBADF;
  } else if (errno_val) {
    return CGC_EPIPE;  /* Guess... */
  } else {
    return update_byte_count(rx_bytes, (cgc_size_t) ret);
  }
}

/* Tries to validate a timeout. */
static int check_timeout(const struct cgc_timeval *timeout) {
  if (!timeout) {
    return 0;
  } else if (!OBJECT_IS_READABLE(timeout)) {
    return CGC_EFAULT;
  } else if (0 > timeout->tv_sec || 0 > timeout->tv_usec) {
    return CGC_EINVAL;
  } else {
    return 0;
  }
}

enum {
    // Maximum number of binaries running for one challenge
    kPracticalMaxNumCBs = 10,
    
    // STD(IN/OUT/ERR) + a socketpair for every binary
    // All fds used by the binaries should be less than this
    kExpectedMaxFDs = 3 + (2 * kPracticalMaxNumCBs)
};

/* Marshal a CGC fd set into an OS fd set. */
static int copy_cgc_fd_set(const cgc_fd_set *cgc_fds, fd_set *os_fds, int *num_fds) {
  for (unsigned fd = 0; fd < CGC__NFDBITS; ++fd) {
    if (CGC_FD_ISSET(fd, cgc_fds)) {
      // Shouldn't be using an fd greater than the allowed values
      if (fd >= kExpectedMaxFDs) {
          return CGC_EBADF;
      }
      
      if (fd > NFDBITS) {
        continue;  /* OS set size is too small. */
      }
      FD_SET(fd, os_fds);
      ++*num_fds;
    }
  }
  return 0;
}

/* Marshal an OS fd set into a CGC fd set. */
static void copy_os_fd_set(const fd_set *os_fds, cgc_fd_set *cgc_fds) {
  for (unsigned fd = 0; fd < MIN(NFDBITS, CGC__NFDBITS); ++fd) {
    if (FD_ISSET(fd, os_fds)) {
      CGC_FD_SET(fd, cgc_fds);
    }
  }
}
int cgc_fdwait(int nfds, cgc_fd_set *readfds, cgc_fd_set *writefds,
               const struct cgc_timeval *timeout, int *readyfds) {

  int ret = check_timeout(timeout);
  int actual_num_fds = 0;
  struct timeval max_wait_time = {0, 0};
  fd_set read_fds;
  fd_set write_fds;

  if (ret) {
    return ret;
  } else if (0 > nfds || CGC__NFDBITS < nfds) {
    return EINVAL;
  }

  FD_ZERO(&read_fds);
  FD_ZERO(&write_fds);

  if (readfds) {
    if (!OBJECT_IS_WRITABLE(readfds)) {  /* Opportunistic. */
      return CGC_EFAULT;
    } else if (0 != (ret = copy_cgc_fd_set(readfds, &read_fds, &actual_num_fds))) {
      return ret;
    }
  }

  if (writefds) {
    if (!OBJECT_IS_WRITABLE(writefds)) {  /* Opportunistic. */
      return CGC_EFAULT;
    } else if (0 != (ret = copy_cgc_fd_set(writefds, &write_fds, &actual_num_fds))) {
      return ret;
    }
  }

  if (actual_num_fds != nfds) {
    return EINVAL;  /* Not actually specified, but oh well. */
  }

  if (readfds)  CGC_FD_ZERO(readfds);
  if (writefds) CGC_FD_ZERO(writefds);

  if (timeout) {
    max_wait_time.tv_sec = timeout->tv_sec;
    max_wait_time.tv_usec = timeout->tv_usec;
  }

  errno = 0;
  int num_selected_fds = select(
          nfds,
          (readfds ? &read_fds : NULL),
          (writefds ? &write_fds : NULL),
          NULL,
          (timeout ? &max_wait_time : NULL));
  const int errno_val = errno;
  errno = 0;

  if (errno_val) {
    if (ENOMEM == errno_val) {
      return CGC_ENOMEM;
    } else if (EBADF == errno_val) {
      return CGC_EBADF;
    } else {
      return CGC_EINVAL;
    }
  }

  if (readfds) {
    copy_os_fd_set(&read_fds, readfds);
  }

  if (writefds) {
    copy_os_fd_set(&write_fds, writefds);
  }

  if (readyfds) {
    if (!OBJECT_IS_WRITABLE(readyfds)) {
      return CGC_EFAULT;
    }
    *readyfds = num_selected_fds;
  }

  return 0;
}

/* Perform a backing memory allocation. */
static int do_allocate(uintptr_t start, cgc_size_t size, void **addr) {
  void *ret_addr = (void *) start;
//  printf("do_allocate: size=%x\n", size);
  errno = 0;
  void *mmap_addr = mmap(ret_addr, size, PROT_READ | PROT_WRITE,
                         MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  const int errno_val = errno;
  errno = 0;

  if (errno_val) {
    if (ENOMEM == CGC_ENOMEM) {
      return CGC_ENOMEM;
    } else {
      return CGC_EINVAL;
    }
  } else if (mmap_addr != ret_addr) {
    exit(EXIT_FAILURE);  /* Not much to do about this :-/ */
  }

  for (uintptr_t end = start + size; start < end; start += PAGE_SIZE) {
    set_page(start);
  }
  if (addr) {
    if (!OBJECT_IS_WRITABLE(addr)) {
      return CGC_EFAULT;
    }
    *addr = ret_addr;
  }
  return 0;
}

#define PAGE_ALIGN(x) (((x) + (PAGE_SIZE - 1)) & ~(PAGE_SIZE - 1))

/* Going to ignore `is_executable`. It's not really used in the official CGC
 * challenges, and if it were used, then JITed code would likely be 32-bit, and
 * ideally, this code will also work on 64-bit.
 */
int allocate(cgc_size_t length, int is_executable, void **addr) {
  if (!length) {
    return CGC_EINVAL;
  } else if (!gMemBegin) {
    init_memory();
  }

//  printf("do_allocate: length=%x\n", length);
  length = PAGE_ALIGN(length);  /* Might overflow. */

  if (!length || length >= (gMemEnd - gMemBegin)) {
    return CGC_EINVAL;  /* Too big of a request! */
  }

  cgc_size_t run_length = 0;
  for (cgc_size_t start = gMemEnd - PAGE_SIZE;
       start >= gMemBegin;
       start -= PAGE_SIZE) {
    if (test_page(start)) {
      run_length = 0;
    } else {
      run_length += PAGE_SIZE;
      if (run_length >= length) {
        return do_allocate(start, length, addr);
      }
    }
  }
  return CGC_ENOMEM;
}

/* Deallocate some range of memory and mark the pages as free. */
int deallocate(void *addr, cgc_size_t length) {
  uintptr_t base = (uintptr_t) addr;
  if (!length || base != PAGE_ALIGN(base)) {
    return CGC_EINVAL;
  }

  if (!gMemBegin) {
    init_memory();
  }

  length = PAGE_ALIGN(length);
  uintptr_t limit = base + length;

  if (limit <= gMemBegin || gMemEnd <= base) {
    return CGC_EINVAL;
  } else {
    base = MAX(base, gMemBegin);
    limit = MIN(limit, gMemEnd);
  }

  errno = 0;
  void *mmap_addr = mmap(addr, length, PROT_NONE,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
  const int errno_val = errno;
  errno = 0;

  if (errno_val) {
    return CGC_EINVAL;
  } else if (addr != mmap_addr) {
    exit(EXIT_FAILURE);  /* Not much to do here. */
  }

  for (; base < limit; base += PAGE_SIZE) {
    clear_page(base);
  }

  return 0;
}

/* So this isn't really a random number generator. */
int cgc_random(void *buf, cgc_size_t count, cgc_size_t *rnd_bytes) {
  if (!count) {
    return update_byte_count(rnd_bytes, 0);
  } else if (count > SSIZE_MAX) {
    return CGC_EINVAL;
  } else if (!(count = num_writable_bytes(buf, count))) {
    return CGC_EFAULT;
  } else {
#if defined(APPLE)
    // TODO: Support seeds from the testing. arc4random_buf is easy but 
    //  not the right way to do it.
    arc4random_buf(buf, count);
#else
	FILE *rdev = fopen("/dev/urandom", "rb");
	fread(buf, count, 1, rdev);
	fclose(rdev);
#endif
    return update_byte_count(rnd_bytes, count);
  }
}

void *cgc_initialize_secret_page(void)
{
  const void * MAGIC_PAGE_ADDRESS = (void *)0x4347C000;
  const size_t MAGIC_PAGE_SIZE = 4096;

  void *mmap_addr = mmap(MAGIC_PAGE_ADDRESS, MAGIC_PAGE_SIZE, 
                         PROT_READ | PROT_WRITE,
                         MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, 
                         -1, 0);

  if (mmap_addr != MAGIC_PAGE_ADDRESS)
  {
    err(1, "[!] Failed to map the secret page");
  }

#if defined(APPLE)
    // TODO: Support seeds from the testing. arc4random_buf is easy but 
    //  not the right way to do it.
    arc4random_buf(mmap_addr, MAGIC_PAGE_SIZE);
#else
	FILE *rdev = fopen("/dev/urandom", "rb");
	fread(mmap_addr, MAGIC_PAGE_SIZE, 1, rdev);
	fclose(rdev);
#endif

  return mmap_addr;
}
