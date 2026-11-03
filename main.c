#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// entry of the file system
struct Node {
    char name[256];
    int is_dir;
    struct Node *parent;
    struct Node **children;
    int child_count;
    int child_capacity;
};

// frees a node and everything below it
void free_tree(struct Node *node) {
    for (int i = 0; i < node->child_count; i++) {
        free_tree(node->children[i]);
    }
    free(node->children);
    free(node);
}

struct Node *find_child(struct Node *dir, const char *name) {
    for (int i = 0; i < dir->child_count; i++) {
        if (strcmp(dir->children[i]->name, name) == 0) {
            return dir->children[i];
        }
    }
    return NULL;
}

struct Node *resolve_path(struct Node *cwd, struct Node *root, const char *path) {
    char copy[1100];
    strcpy(copy, path);

    struct Node *cur;
    if (path[0] == '/') {
        cur = root;
    } else {
        cur = cwd;
    }

    char *part = strtok(copy, "/");
    while (part != NULL) {
        if (strcmp(part, ".") == 0) {
            // stay
        } else if (strcmp(part, "..") == 0) {
            if (cur->parent != NULL) {
                cur = cur->parent;
            }
        } else {
            if (!cur->is_dir) {
                return NULL;
            }
            struct Node *next = find_child(cur, part);
            if (next == NULL) {
                return NULL;
            }
            cur = next;
        }
        part = strtok(NULL, "/");
    }
    return cur;
}

void print_path(struct Node *node) {
    if (node->parent == NULL) {
        return;
    }
    print_path(node->parent);
    printf("/%s", node->name);
}

void print_pwd(struct Node *node) {
    if (node->parent == NULL) {
        printf("/");
    } else {
        print_path(node);
    }
}

int main(void) {
    char line[1100];

    // create the root directory 
    struct Node *root = malloc(sizeof(struct Node));
    if (root == NULL) {
        return 1;
    }
    strcpy(root->name, "/");
    root->is_dir = 1;
    root->parent = NULL;
    root->children = NULL;
    root->child_count = 0;
    root->child_capacity = 0;

    // current working directory starts at the root
    struct Node *cwd = root;

    while (1) {
        printf("user@boun:");
	print_pwd(cwd);
	printf("$ ");
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
	    print_pwd(cwd);
            printf("\n");
        } else if (strcmp(words[0], "echo") == 0) {
            for (int i = 1; i < count; i++) {
                if (i > 1) {
                    printf(" ");
                }
                printf("%s", words[i]);
            }
            printf("\n");
        } else if (strcmp(words[0], "mkdir") == 0) {
            if (count != 2) {
                printf("bushell: error\n");
            } else {
                // does this name already exist in the current directory?
                int exists = 0;
                for (int i = 0; i < cwd->child_count; i++) {
                    if (strcmp(cwd->children[i]->name, words[1]) == 0) {
                        exists = 1;
                    }
                }

                if (exists) {
                    printf("bushell: error\n");
                } else {
                    // grow the children list if it is full
                    if (cwd->child_count == cwd->child_capacity) {
                        if (cwd->child_capacity == 0) {
                            cwd->child_capacity = 2;
                        } else {
                            cwd->child_capacity = cwd->child_capacity * 2;
                        }
                        cwd->children = realloc(cwd->children, cwd->child_capacity * sizeof(struct Node *));
                        if (cwd->children == NULL) {
                            return 1;
                        }
                    }

                    // create the new directory and add it to the list
                    struct Node *n = malloc(sizeof(struct Node));
                    if (n == NULL) {
                        return 1;
                    }
                    strcpy(n->name, words[1]);
                    n->is_dir = 1;
                    n->parent = cwd;
                    n->children = NULL;
                    n->child_count = 0;
                    n->child_capacity = 0;
                    cwd->children[cwd->child_count] = n;
                    cwd->child_count++;
                }
            }
	 } else if (strcmp(words[0], "cd") == 0) {
   		 if (count != 2) {
        		printf("bushell:error\n");
    		} else {
        		struct Node *target = resolve_path(cwd, root, words[1]);
        		if (target == NULL || !target->is_dir) {
            			printf("bushell:error\n");
        		} else {
            			cwd = target;
        		}
    		}

        } else if (strcmp(words[0], "ls") == 0) {
            for (int i = 0; i < cwd->child_count; i++) {
                printf("%s", cwd->children[i]->name);
                if (cwd->children[i]->is_dir) {
                    printf("/");
                }
                printf("\n");
            }
	
        } else {
            printf("bushell: error\n");
        }
    }

    free_tree(root);
    return 0;
}