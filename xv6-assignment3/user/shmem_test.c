#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/riscv.h"
#include "user/user.h"

// Flag to disable unmapping in child (set via command line)
int disable_unmap = 0;

void print_process_size(char* label) {
    // Use sbrk(0) to get current process size
    printf("%s: %d bytes\n", label, sbrk(0));
}

int main(int argc, char *argv[]) {
    printf("=== Shared Memory Test ===\n");
    
    // Check for disable_unmap flag
    if (argc > 1 && strcmp(argv[1], "no_unmap") == 0) {
        disable_unmap = 1;
        printf("Running test WITHOUT child unmapping\n");
    }
    
    // Allocate some memory in parent that we'll share
    char* parent_data = malloc(100);
    if (!parent_data) {
        printf("Failed to allocate memory in parent\n");
        exit(1);
    }
    strcpy(parent_data, "Initial data");
    
    print_process_size("Parent before fork");
    
    int pid = fork();
    
    if (pid == 0) {
        // CHILD PROCESS
        printf("\n=== CHILD PROCESS ===\n");
        
        print_process_size("Child before mapping");
        
        // Map shared memory from parent to child
        // Arguments: (src_proc_pid, dst_proc_pid, src_va, size)
        uint64 shared_addr = map_shared_pages(getppid(), getpid(), (uint64)parent_data, 100);
        
        if (shared_addr == (uint64)-1) {
            printf("Child: Failed to map shared pages\n");
            exit(1);
        }
        
        printf("Child: Successfully mapped shared memory at %p\n", (void*)shared_addr);
        print_process_size("Child after mapping");
        
        // Write "Hello daddy" to shared memory
        char* shared_ptr = (char*)shared_addr;
        strcpy(shared_ptr, "Hello daddy");
        printf("Child: Wrote 'Hello daddy' to shared memory\n");
        
        // Test malloc before unmapping
        char* test_malloc1 = malloc(50);
        if (test_malloc1) {
            strcpy(test_malloc1, "Child malloc before unmap");
            printf("Child: malloc() before unmap works\n");
        }
        
        if (!disable_unmap) {
            // Unmap shared memory
            printf("Child: Unmapping shared memory...\n");
            if (unmap_shared_pages(getpid(), shared_addr, 100) == 0) {
                printf("Child: Successfully unmapped shared memory\n");
                print_process_size("Child after unmapping");
            } else {
                printf("Child: Failed to unmap shared memory\n");
                exit(1);
            }
            
            // Test malloc after unmapping - should work normally now
            char* test_malloc2 = malloc(75);
            if (test_malloc2) {
                strcpy(test_malloc2, "Child malloc after unmap");
                printf("Child: malloc() after unmap works properly\n");
                print_process_size("Child after malloc");
                free(test_malloc2);
            } else {
                printf("Child: malloc() failed after unmap\n");
            }
            
            if (test_malloc1) free(test_malloc1);
        } else {
            printf("Child: Skipping unmap to test kernel cleanup\n");
        }
        
        printf("Child: Exiting...\n");
        exit(0);
        
    } else if (pid > 0) {
        // PARENT PROCESS
        printf("\n=== PARENT PROCESS ===\n");
        
        // Wait for child to write to shared memory
        sleep(1);
        
        // Read from our memory - should now contain "Hello daddy"
        printf("Parent: Reading from shared memory: '%s'\n", parent_data);
        
        // Wait for child to complete
        int status;
        wait(&status);
        printf("Parent: Child exited with status %d\n", status);
        
        printf("\n=== AFTER CHILD EXIT ===\n");
        
        if (!disable_unmap) {
            printf("Parent: Child unmapped properly\n");
        } else {
            printf("Parent: Testing kernel cleanup (child exited without unmapping)\n");
        }
        
        // Parent should still be able to access the data
        printf("Parent: Data still accessible: '%s'\n", parent_data);
        
        // Clean up in parent
        printf("Parent: Cleaning up...\n");
        print_process_size("Parent before cleanup");
        
        free(parent_data);
        print_process_size("Parent after cleanup");
        
        printf("=== Test completed successfully! ===\n");
        
    } else {
        printf("Fork failed\n");
        exit(1);
    }
    
    return 0;
}