#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/riscv.h"

#define MAX_CHILDREN 4

// Compose a 32-bit header: [high 16 bits = index][low 16 bits = length]
#define HEADER(index, length) (((index & 0xFFFF) << 16) | (length & 0xFFFF))
#define GET_INDEX(hdr) (((hdr) >> 16) & 0xFFFF)
#define GET_LENGTH(hdr) ((hdr) & 0xFFFF)

// Simple integer to string conversion
void int_to_str(int num, char *str) {
    if (num == 0) {
        str[0] = '0';
        str[1] = '\0';
        return;
    }
    
    int i = 0;
    int temp = num;
    
    while (temp > 0) {
        temp /= 10;
        i++;
    }
    
    str[i] = '\0';
    i--;
    
    while (num > 0) {
        str[i] = '0' + (num % 10);
        num /= 10;
        i--;
    }
}

// Simple string concatenation
void str_concat(char *dest, const char *src1, const char *src2, const char *src3) {
    int i = 0;
    
    while (src1[i] != '\0') {
        dest[i] = src1[i];
        i++;
    }
    
    int j = 0;
    while (src2[j] != '\0') {
        dest[i] = src2[j];
        i++;
        j++;
    }
    
    j = 0;
    while (src3[j] != '\0') {
        dest[i] = src3[j];
        i++;
        j++;
    }
    
    dest[i] = '\0';
}

void pseudo_sleep(int cycles) {
  for (volatile int i = 0; i < cycles * 100000; i++);
}

// Child writes a message or a fallback header
void child_writer(char *shared_buffer, int index, const char *msg) {
  int len = strlen(msg);
  char *addr;
  for (int j = 0; j < index+100; j++) { // Each child tries to send index messages
    addr = shared_buffer;
    int wrote_message = 0;

    while ((uint64)addr + len + 4 <= (uint64)shared_buffer + PGSIZE) {
      int old = __sync_val_compare_and_swap((int*)addr, 0, HEADER(index, len));
      if (old == 0) {
        memmove(addr + 4, msg, len);
        wrote_message = 1;
        break;
      }
      addr += 4 + GET_LENGTH(old);
      addr = (char*)(((uint64)addr + 3) & ~3);
    }

    if (!wrote_message) {
      addr = shared_buffer;
      while ((uint64)addr + 4 <= (uint64)shared_buffer + PGSIZE) {
        int old = __sync_val_compare_and_swap((int*)addr, 0, HEADER(index, 0));
        if (old == 0) break;
        addr += 4 + GET_LENGTH(old);
        addr = (char*)(((uint64)addr + 3) & ~3);
      }
    }

    // Optional delay between messages
    pseudo_sleep(index + j);
  }

  exit(0);
}

// Parent reads messages or failure headers
void parent_reader(char *shared_buffer, char **last_read_ptr, int *messages_read) {
  char *addr = *last_read_ptr;

  while ((uint64)addr + 4 <= (uint64)shared_buffer + PGSIZE) {
    int hdr = *(int*)addr;
    if (hdr == 0) break;

    int index = GET_INDEX(hdr);
    int len = GET_LENGTH(hdr);

    if ((uint64)(addr + 4 + len) > (uint64)shared_buffer + PGSIZE) break;

    if (len == 0) {
      printf("Child %d: FAILED to write log (buffer full)\n", index);
    } else {
      char msg[256] = {0};
      memmove(msg, addr + 4, len);
      msg[len] = '\0';

      int actual_msg_len = strlen(msg);
      if (actual_msg_len != len) return;  // Incomplete message

      printf("Child %d: %s\n", index, msg);
    }

    (*messages_read)++;

    addr += 4 + len;
    addr = (char*)(((uint64)addr + 3) & ~3);
  }

  *last_read_ptr = addr;
}

int main() {
  // Step 1: Allocate shared buffer in parent
  char *shared_buf = malloc(PGSIZE);
  if (!shared_buf) {
    printf("malloc failed\n");
    exit(1);
  }
  memset(shared_buf, 0, PGSIZE);

  // Step 2: Fork children
  for (int i = 0; i < MAX_CHILDREN; i++) {
    int pid = fork();
    if (pid == 0) {
      
      // Build message manually (no snprintf in xv6)
      char msg[64];
      char index_str[16];
      int_to_str(i, index_str);
      str_concat(msg, "Hello from child ", index_str, "!");
      
      uint64 result = map_shared_pages(getppid(), getpid(), (uint64)shared_buf, PGSIZE);
      
      if (result == (uint64)-1) {
          printf("Mapping failed for child %d\n", i);
          exit(1);
      }
      
      child_writer((char*)result, i, msg);
    }
    else if (pid < 0) {
      printf("Fork failed for child %d\n", i);
      exit(1);
    }
  }

  // Step 3: Parent reads logs in real-time
  char *last_read = shared_buf;
  int messages_read = 0;

  while (messages_read < MAX_CHILDREN) {
    parent_reader(shared_buf, &last_read, &messages_read);
  }

  exit(0);
}

