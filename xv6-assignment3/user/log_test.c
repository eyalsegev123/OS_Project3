#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/riscv.h"

#define MAX_CHILDREN 4

// Compose a 32-bit header: [high 16 bits = index][low 16 bits = length]
#define HEADER(index, length) (((index & 0xFFFF) << 16) | (length & 0xFFFF))
#define GET_INDEX(hdr) (((hdr) >> 16) & 0xFFFF)
#define GET_LENGTH(hdr) ((hdr) & 0xFFFF)

// Write one message to the shared buffer
void child_writer(char *shared_buffer, int index, const char *msg) {
  int len = strlen(msg);
  char *addr = shared_buffer;

  //for each child we scan the buffer from the beginning until we find a free segment
  while ((uint64)addr + len + 4 <= (uint64)shared_buffer + PGSIZE) {
    // Checking for zero header
    int old = __sync_val_compare_and_swap((int*)addr, 0, HEADER(index, len));
    if (old == 0) {
      // Claimed space successfully
      memmove(addr + 4, msg, len);
      break;
    }

    // Skip to next possible header (aligned)
    addr += 4 + GET_LENGTH(old);
    addr = (char*)(((uint64)addr + 3) & ~3);
  }

  exit(0);
}

// Live-reading messages as they arrive
void parent_reader(char *shared_buffer, char **last_read_ptr, int *messages_read) {
  char *addr = *last_read_ptr;

  while ((uint64)addr + 4 <= (uint64)shared_buffer + PGSIZE) {
    int hdr = *(int*)addr;
    if (hdr == 0) break;

    int index = GET_INDEX(hdr);
    int len = GET_LENGTH(hdr);

    if ((uint64)(addr + 4 + len) > (uint64)shared_buffer + PGSIZE) break;

    char msg[256] = {0};
    memmove(msg, addr + 4, len);
    msg[len] = '\0';

    //handling edge case - the parent read an incomplete message.
    int actual_msg_len = strlen(msg);
    if (actual_msg_len != len) 
        return;  // Exit without updating last_read_ptr and message_read

    printf("Child %d: %s\n", index, msg);
    (*messages_read)++;

    addr += 4 + len;
    addr = (char*)(((uint64)addr + 3) & ~3);
  }

  *last_read_ptr = addr;
}

int main() {
  // Allocate shared buffer
  char *shared_buf = malloc(PGSIZE);
  if (!shared_buf) {
    printf("malloc failed\n");
    exit(1);
  }
  //Initialize 0 to the shared buffer
  memset(shared_buf, 0, PGSIZE);

  // Fork children
  for (int i = 0; i < MAX_CHILDREN; i++) {
    int pid = fork();
    if (pid == 0) {
      // Child process: build and write message
      char msg[64];
      snprintf(msg, sizeof(msg), "Hello from child %d!", i);
      sleep(i + 1);  // Optional: staggered writes
      child_writer(shared_buf, i, msg);
    } else if (pid < 0) {
      printf("Fork failed for child %d\n", i);
      exit(1);
    }
  }

  // Parent process: read live from shared buffer
  char *last_read = shared_buf;
  int messages_read = 0;
  int children_finished = 0;

  printf("Parent: Waiting for children to write messages...\n");

  while (children_finished < MAX_CHILDREN) {
    parent_reader(shared_buf, &last_read, &messages_read);
    
    // Check if any child has exited
    int status;
    int pid = waitpid(-1, &status, WNOHANG);  // Non-blocking wait
    if (pid > 0) {
      children_finished++;
      printf("Parent: Child %d finished (%d/%d complete)\n", 
             pid, children_finished, MAX_CHILDREN);
    }
    
    sleep(1);
  }

  free(shared_buf);
  exit(0);
}
