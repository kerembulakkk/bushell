#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// One entry of the file system (for now: only the root directory exists)
struct Node {
    char name[256];
    int is_dir;
    struct Node *parent;
};

int main(void) {
    char line[1100];

    // Create the root directory "/" in memory
    struct Node *root = malloc(sizeof(struct Node));
    if (root == NULL) {
        return 1;
    }
    strcpy(root->name, "/");
    root->is_dir = 1;
    root->parent = NULL;

    // The current working directory starts at the root
    struct Node *cwd = root;

    while (1) {
        printf("user@boun:/$ ");
        if (fgets(line, sizeof(line), stdin) == NULL) {
            break;
        }
        line[strcspn(line, "\n")] = '\0';

        // Split the line into words
        char *words[600];
        int count = 0;

        char *word = strtok(line, " ");
        while (word != NULL) {
            words[count] = word;
            count++;
            word = strtok(NULL, " ");
        }

        if (count == 0) {
            continue;
        }
        if (strcmp(words[0], "exit") == 0) {
            break;
        }

        if (strcmp(words[0], "pwd") == 0) {
            printf("%s\n", cwd->name);
        } else if (strcmp(words[0], "echo") == 0) {
            for (int i = 1; i < count; i++) {
                if (i > 1) {
                    printf(" ");
                }
                printf("%s", words[i]);
            }
            printf("\n");
        } else {
            printf("bushell: error\n");
        }
    }

    free(root);
    return 0;
}
