#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/riscv.h"
#include "user/user.h"

// Flag to disable unmapping in child
int disable_unmap = 0;

// Print current process size
void print_process_size(const char* label) {
    printf("%s: %d bytes\n", label, sbrk(0));
}

int main(int argc, char *argv[]) {
    // Step 6: Check for flag
    if (argc > 1 && strcmp(argv[1], "no_unmap") == 0) {
        disable_unmap = 1;
        printf("Child will skip unmapping (DISABLE_UNMAP flag enabled)\n");
    }

    // Step 1: Parent allocates memory
    char* parent_data = malloc(100);
    if (!parent_data) {
        printf("Parent: malloc failed\n");
        exit(1);
    }

    strcpy(parent_data, "Initial data");

    print_process_size("Parent initial size");

    int pid = fork();
    if (pid == 0) {
        // === CHILD PROCESS ===
        print_process_size("Child before mapping");

        // Step 1: Map memory from parent
        uint64 shared_addr = map_shared_pages(getppid(), getpid(), (uint64)parent_data, 100);
        if (shared_addr == (uint64)-1) {
            printf("Child: Failed to map shared pages\n");
            exit(1);
        }

        print_process_size("Child after mapping");

        // Step 2: Write to shared memory
        strcpy((char*)shared_addr, "Hello daddy");

        if (!disable_unmap) {
            // Step 3: Unmap
            if (unmap_shared_pages(getpid(), shared_addr, 100) == 0) {
                printf("Child: Successfully unmapped shared memory\n");
                print_process_size("Child after unmapping");

                // Step 4: Test malloc
                char* test_alloc = malloc(100);
                if (test_alloc) {
                    strcpy(test_alloc, "Child malloc after unmap");
                    printf("Child: malloc after unmapping works\n");
                    print_process_size("Child after malloc");
                } else {
                    printf("Child: malloc failed after unmapping\n");
                }
            } else {
                printf("Child: Failed to unmap shared memory\n");
                exit(1);
            }
        } else {
            // Step 6: Test cleanup case
            printf("Child: Skipping unmap to test kernel cleanup\n");
        }

        printf("Child: Exiting...\n");
        exit(0);
    }

    // === PARENT PROCESS ===
    sleep(10);  // Wait for child to write

    printf("\n=== PARENT PROCESS ===\n");
    // Step 2: Read what child wrote
    printf("Parent reads: %s\n", parent_data);

    // Step 5/6: Wait and print result
    wait(0);

    if (disable_unmap) {
        printf("Parent: After child exited without unmapping, still reads: %s\n", parent_data);
    }

    print_process_size("Parent after child exit");
    printf("Test completed\n");

    exit(0);
}
