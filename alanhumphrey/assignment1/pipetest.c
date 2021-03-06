/*********************************************************************
 *
 * Alan Humphrey
 * cs6960, fall 2017
 * File:  pipetest.c
 *
 *
 *********************************************************************/

#ifdef LINUX
  #include <stdio.h>
  #include <stdlib.h>
  #include <stdint.h>
  #include <string.h>
  #include <unistd.h>
  #include <wait.h>
#else
  #include "types.h"
  #include "stat.h"
  #include "user.h"
#endif

#ifdef LINUX
  typedef unsigned int uint;
#endif

#define STDOUT     1
#define STDERR     2
#define READ       0
#define WRITE      1
#define BUF_SIZE   512
#define NUM_RANDS  2048 // 131072 1MB
#define NUM_BYTES  NUM_RANDS * sizeof(uint) // 1MB
#define NUM_TESTS  10



// ============================================================================
// Taken from https://en.wikipedia.org/wiki/Xorshift
// The state word must be initialized to non-zero
//   Used for random array of bytes - pushed through the pipe
uint
xorshift32( uint state[static 1] )
{
  uint x = state[0];
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  state[0] = x;
  return x;
}


// ============================================================================
// Pulled and slightly adapted from xv6 usertests.c
//   Used for random read/write length
uint
rand_uint( uint randstate )
{
  randstate = randstate * 1664525 + 1013904223;
  return randstate;
}


// ============================================================================
// Simple single-process test
int
single_process_test( uint const * const in, uint * out, const int iteration )
{
  int fds[2];  // fds[0] READ,  fds[1] WRITE

  if (pipe(fds) == -1) {
#ifdef LINUX
    printf("pipe creation failed \n");
#else
    printf(STDERR, "pipe creation failed \n");
#endif
    return -1;
  }

  uint bytes_written = 0;
  uint bytes_read    = 0;

  for (uint i = 0; i < NUM_RANDS; ++i) {

    bytes_written = write(fds[WRITE], (void*)(in  + i), sizeof(uint));
    if (bytes_written > 0) {
      bytes_read = read( fds[READ], (void*)(out + i), sizeof(uint));
    }

    if (bytes_read != bytes_written) {
#ifdef LINUX
      printf("%u bytes written but only %u bytes read (test %u/%d) \n", bytes_written, bytes_read, iteration, NUM_TESTS);
#else
      printf(STDERR, "%d bytes written but only %d bytes read (test %d/%d) \n", bytes_written, bytes_read, iteration, NUM_TESTS);
#endif
      return -1;
    }
  }

  // close the pipe
  close(fds[READ]);
  close(fds[WRITE]);

  // compare bytes (4 at a time (sizeof(uint)) pushed through the pipe against the original generated bytes
  for (uint i = 0; i < NUM_RANDS; ++i) {

    if (in[i] != out[i]) {
#ifdef LINUX
    printf("\nin_bytes[%u] != out_bytes[%u] (test %u/%d) \n", i, i, iteration, NUM_TESTS);
#else
    printf(STDERR, "\nin_bytes[%d] != out_bytes[%d] (test %d/%d) \n", i, i, iteration, NUM_TESTS);
#endif
    return -1;
    }
  }

  return 0;
}


// ============================================================================
//
int
multi_process_test(uint * in, uint * out, const uint iteration)
{
  int fds[2];  // fds[0] READ,  fds[1] WRITE
  uint seed = 0u;

  if (pipe(fds) == -1) {
#ifdef LINUX
    printf("pipe creation failed \n");
#else
    printf(STDERR, "pipe creation failed \n");
#endif
    return -1;
  }


  // ============================================================================
  //    Child process - READ
  // ============================================================================
  if (fork() == 0) {

    close(fds[READ]);  // close the READ end

    int curr_bytes_written   = 0;
    uint total_bytes_written = 0;

    while (total_bytes_written < NUM_BYTES) {

      uint write_size = (rand_uint(seed) % BUF_SIZE) / sizeof(uint);
      uint remaining_bytes =  NUM_BYTES - total_bytes_written;
      write_size = (write_size > remaining_bytes) ? remaining_bytes : write_size;
      curr_bytes_written = write(fds[WRITE], (((char*)in) + total_bytes_written), write_size);

      if (curr_bytes_written < 0) {
#ifdef LINUX
        printf("pipe write failed \n");
#else
        printf(STDERR, "pipe write failed \n");
#endif
        return -1;
      }

      total_bytes_written += curr_bytes_written;
      seed++;
    }

    close(fds[WRITE]);

#ifdef LINUX
    exit(EXIT_SUCCESS);
#else
    exit();
#endif

  }


  // ============================================================================
  //    Parent process - READ
  // ============================================================================
  else {

    close(fds[WRITE]);  // close the WRITE end

    int curr_bytes_read   = 0;
    uint total_bytes_read = 0;

    while (total_bytes_read < NUM_BYTES) {

      uint remaining_bytes = NUM_BYTES - total_bytes_read;
      uint read_size = (rand_uint(seed) % BUF_SIZE);
      read_size = (read_size > remaining_bytes) ? remaining_bytes : read_size;
      curr_bytes_read = read(fds[READ], (((char*)out) + total_bytes_read), read_size);

      if (curr_bytes_read < 0) {
#ifdef LINUX
        printf("pipe read failed \n");
#else
        printf(STDERR, "pipe read failed \n");
#endif
        return -1;
      }

      total_bytes_read += curr_bytes_read;
      seed++;
    }

    close(fds[READ]);

#ifdef LINUX
    wait(NULL);
#else
    wait();
#endif

  }

  // compare bytes pushed through the pipe against the original generated bytes
  for (uint i = 0; i < NUM_RANDS; ++i) {

    if (in[i] != out[i]) {
#ifdef LINUX
      printf("\nin_bytes[%u] != out_bytes[%u] (test %u/%d) \n", i, i, iteration, NUM_TESTS);
#else
      printf(STDERR, "\nin_bytes[%d] != out_bytes[%d] (test %d/%d) \n", i, i, iteration, NUM_TESTS);
#endif
      return -1;
    }
  }

  return 0;

}



// ============================================================================
//
int
main( void )
{
  // ==========================================================================
  //    Setup: allocate memory for tests, populate bytes to compare against
  // ==========================================================================

  // this will be the random bytes pushed through the pipe
  uint * in_bytes  = (uint*)malloc(NUM_BYTES);

  // this will be what we write to.... check against bytes in "in_bytes"
  uint * out_bytes = (uint*)malloc(NUM_BYTES);

  // fill up the random bytes (generate "NUM_RANDS" unsigned ints)
  uint state[1] = { 1u };
  for (uint i = 0; i < NUM_RANDS; ++i) {
    in_bytes[i] = xorshift32(state);
  }


  // ==========================================================================
  //    Do set of simple, single-process tests
  // ==========================================================================

  uint sp_test_num = 1;
  for (; sp_test_num <= NUM_TESTS; ++sp_test_num) {

    // initialize each element in target to 0
    memset((void*)out_bytes, 0, NUM_BYTES);

    int ret_val = single_process_test(in_bytes, out_bytes, sp_test_num);

    if (ret_val == -1) {
#ifdef LINUX
  printf("single process pipe test %u/%u FAILED \n", sp_test_num, NUM_TESTS);
#else
  printf(STDERR, "single process pipe test %d/%d FAILED \n", sp_test_num, NUM_TESTS);
#endif
    }
    else {
#ifdef LINUX
      printf("single process pipe test %u/%u  PASSED \n", sp_test_num, NUM_TESTS);
#else
      printf(STDOUT, "single process pipe test %d/%d  PASSED \n", sp_test_num, NUM_TESTS);
#endif
    }
  }


  // ==========================================================================
  //    Now do set of multi-process, single-reader/single-writer tests
  // ==========================================================================

  uint mp_test_num = 1;
  for (; mp_test_num <= NUM_TESTS; ++mp_test_num) {

    // initialize each element in target to 0
    memset((void*)out_bytes, 0, NUM_BYTES);

    int ret_val = multi_process_test(in_bytes, out_bytes, mp_test_num);

    if (ret_val == -1) {
#ifdef LINUX
  printf(" multi process pipe test %u/%u FAILED \n", mp_test_num, NUM_TESTS);
#else
  printf(STDERR, " multi process pipe test %d/%d FAILED \n", mp_test_num, NUM_TESTS);
#endif
    }
    else {
#ifdef LINUX
      printf(" multi process pipe test %u/%u  PASSED \n", mp_test_num, NUM_TESTS);
#else
      printf(STDOUT, " multi process pipe test %d/%d  PASSED \n", mp_test_num, NUM_TESTS);
#endif
    }
  }

  // free up memory
  free(in_bytes);
  free(out_bytes);

#ifdef LINUX
      return(EXIT_SUCCESS);
#else
      return 0;
#endif

}



