#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/riscv.h"
#include "user/user.h"


#define TEST_DISABLE_UNMAP 0  // Set to 1 to test cleanup on exit

void print_process_size(char* label) {
    printf("Process size %s: %d bytes\n", label, sbrk(0));
}

int main() {
    printf("=== Shared Memory Test ===\n");
    
    // Allocate some initial memory in parent to have a base size
    char* initial_mem = malloc(100);
    if (!initial_mem) {
        printf("Failed to allocate initial memory\n");
        exit(1);
    }
    
    print_process_size("in parent (initial)");
    
    int pid = fork();
    
    if (pid == 0) {
        // Child process
        printf("\n--- Child Process ---\n");
        print_process_size("in child (before mapping)");
        
        // Map shared pages from parent (we'll use address near the initial memory)
        uint64 parent_addr = (uint64)initial_mem;
        uint64 shared_addr = map_shared_pages(getppid(), parent_addr, PGSIZE);
        
        if (shared_addr == -1) {
            printf("Child: Failed to map shared pages\n");
            exit(1);
        }
        
        printf("Child: Mapped shared memory at address %p\n", (void*)shared_addr);
        print_process_size("in child (after mapping)");
        
        // Write message in shared memory
        char* shared_mem = (char*)shared_addr;
        strcpy(shared_mem, "Hello daddy");
        printf("Child: Wrote message to shared memory\n");
        
        // Test unmapping (unless disabled for testing cleanup)
        if (!TEST_DISABLE_UNMAP) {
            if (unmap_shared_pages(shared_addr, PGSIZE) == 0) {
                printf("Child: Successfully unmapped shared memory\n");
                print_process_size("in child (after unmapping)");
            } else {
                printf("Child: Failed to unmap shared memory\n");
            }
            
            // Test malloc after unmapping
            char* test_malloc = malloc(200);
            if (test_malloc) {
                printf("Child: malloc() works properly after unmapping\n");
                print_process_size("in child (after malloc)");
                free(test_malloc);
            } else {
                printf("Child: malloc() failed after unmapping\n");
            }
        } else {
            printf("Child: Skipping unmap (testing cleanup on exit)\n");
        }
        
        printf("Child: Exiting\n");
        exit(0);
        
    } else if (pid > 0) {
        // Parent process
        printf("\n--- Parent Process ---\n");
        
        // Wait a bit for child to write the message
        sleep(1);
        
        // Read from the shared memory area
        char* shared_mem = (char*)initial_mem;
        printf("Parent: Message from child: '%s'\n", shared_mem);
        
        // Wait for child to complete
        int status;
        wait(&status);
        printf("Parent: Child exited with status %d\n", status);
        
        if (TEST_DISABLE_UNMAP) {
            printf("Parent: Testing access after child exit (no unmap)\n");
            printf("Parent: Message still accessible: '%s'\n", shared_mem);
        }
        
        // Clean up our initial allocation
        free(initial_mem);
        print_process_size("in parent (final)");
        
    } else {
        printf("Fork failed\n");
        exit(1);
    }
    
    printf("=== Test Complete ===\n");
    return 0;
}