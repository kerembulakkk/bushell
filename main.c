#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum NodeType { NODE_FILE, NODE_DIR, NODE_SYMLINK };

struct Inode {
	char *content;
	size_t size;
	size_t ref_count;
};

// entry of the file system
struct Node {
	char *name;
	enum NodeType type;
	char *target;
	struct Node *parent;
	struct Node **children;
	struct Inode *inode;
	int child_count;
	int child_capacity;
};

struct Buf {
	char *data;
	size_t len;
	size_t cap;
};

int buf_add(struct Buf *b, const char *s) {
	size_t n = strlen(s);
	if (b->len + n + 1 > b->cap) {
		size_t newcap = b->cap;
		if (newcap == 0) {
			newcap = 64;
		}
		while (b->len + n + 1 > newcap) {
			newcap = newcap * 2;
		}
		char *bigger = realloc(b->data, newcap);
		if (bigger == NULL) {
			return 0;
		}
		b->data = bigger;
		b->cap = newcap;
	}
	memcpy(b->data + b->len, s, n + 1);
	b->len += n;
	return 1;
}

void path_to_buf(struct Node *node, struct Buf *b) {
	if (node->parent == NULL) {
		return;
	}
	path_to_buf(node->parent, b);
	buf_add(b, "/");
	buf_add(b, node->name);
}

void pwd_to_buf(struct Node *node, struct Buf *b) {
	if (node->parent == NULL) {
		buf_add(b, "/");
	} else {
		path_to_buf(node, b);
	}
}

void release_inode(struct Inode *in) {
	in->ref_count--;
	if (in->ref_count == 0) {
		free(in->content);
		free(in);
	}
}

void free_node(struct Node *node) {
	free(node->children);
	if (node->inode != NULL) {
		release_inode(node->inode);
	}
	free(node->name);
	free(node->target);
	free(node);
}

void free_tree(struct Node *node) {
	for (int i = 0; i < node->child_count; i++) {
		free_tree(node->children[i]);
	}
	free_node(node);
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
			if (cur->type != NODE_DIR) {
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

struct Node *follow(struct Node *root, struct Node *node) {
	int depth = 0;
	while (node != NULL && node->type == NODE_SYMLINK) {
		if (depth >= 16) {
			return NULL;
		}
		node = resolve_path(root, root, node->target);
		depth++;
	}
	return node;
}

int attach_child(struct Node *dir, struct Node *node) {
	if (dir->child_count == dir->child_capacity) {
		int newcap;
		if (dir->child_capacity == 0) {
			newcap = 2;
		} else {
			newcap = dir->child_capacity * 2;
		}
		struct Node **bigger = realloc(dir->children, newcap * sizeof(struct Node *));
		if (bigger == NULL) {
			return 0;
		}
		dir->children = bigger;
		dir->child_capacity = newcap;
	}
	dir->children[dir->child_count] = node;
	dir->child_count++;
	node->parent = dir;
	return 1;
}

struct Node *new_node(const char *name, enum NodeType type) {
	struct Node *n = malloc(sizeof(struct Node));
	if (n == NULL) {
		return NULL;
	}
	n->name = malloc(strlen(name) + 1);
	if (n->name == NULL) {
		free(n);
		return NULL;
	}
	strcpy(n->name, name);
	n->type = type;
	n->parent = NULL;
	n->children = NULL;
	n->child_count = 0;
	n->child_capacity = 0;
	n->inode = NULL;
	n->target = NULL;
	return n;
}

struct Node *add_child(struct Node *dir, const char *name, enum NodeType type) {
	struct Node *n = new_node(name, type);
	if (n == NULL) {
		return NULL;
	}
	if (type == NODE_FILE) {
		n->inode = malloc(sizeof(struct Inode));
		if (n->inode == NULL) {
			free_node(n);
			return NULL;
		}
		n->inode->content = NULL;
		n->inode->size = 0;
		n->inode->ref_count = 1;
	}
	if (!attach_child(dir, n)) {
		free_node(n);
		return NULL;
	}
	return n;
}

struct Node *add_hardlink(struct Node *dir, const char *name, struct Node *src) {
	struct Node *n = new_node(name, NODE_FILE);
	if (n == NULL) {
		return NULL;
	}
	n->inode = src->inode;
	n->inode->ref_count++;
	if (!attach_child(dir, n)) {
		free_node(n);
		return NULL;
	}
	return n;
}

struct Node *add_symlink(struct Node *dir, const char *name, struct Node *target) {
	struct Buf path = {NULL, 0, 0};
	pwd_to_buf(target, &path);
	if (path.data == NULL) {
		return NULL;
	}
	struct Node *n = new_node(name, NODE_SYMLINK);
	if (n == NULL) {
		free(path.data);
		return NULL;
	}
	n->target = path.data;
	if (!attach_child(dir, n)) {
		free_node(n);
		return NULL;
	}
	return n;
}

int remove_child(struct Node *dir, struct Node *node) {
	for (int i = 0; i < dir->child_count; i++) {
		if (dir->children[i] == node) {
			for (int j = i; j < dir->child_count - 1; j++) {
				dir->children[j] = dir->children[j + 1];
			}
			dir->child_count--;
			return 1;
		}
	}
	return 0;
}

int split_path(const char *path, char *parent, char *name) {
	char copy[1100];
	strcpy(copy, path);

	int len = strlen(copy);
	while (len > 1 && copy[len - 1] == '/') {
		copy[len - 1] = '\0';
		len--;
	}

	char *slash = strrchr(copy, '/');
	if (slash == NULL) {
		strcpy(parent, ".");
		strcpy(name, copy);
	} else {
		strcpy(name, slash + 1);
		if (slash == copy) {
			strcpy(parent, "/");
		} else {
			*slash = '\0';
			strcpy(parent, copy);
		}
	}

	if (strlen(name) == 0 || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
		return 0;
	}
	return 1;
}

int compare_nodes(const void *a, const void *b) {
	struct Node *x = *(struct Node * const *)a;
	struct Node *y = *(struct Node * const *)b;
	return strcmp(x->name, y->name);
}

void tree_children(struct Node *dir, int depth, struct Buf *b) {
	if (dir->child_count > 1) {
		qsort(dir->children, dir->child_count, sizeof(struct Node *), compare_nodes);
	}
	for (int i = 0; i < dir->child_count; i++) {
		struct Node *c = dir->children[i];
		for (int d = 0; d < depth; d++) {
			buf_add(b, "  ");
		}
		buf_add(b, c->name);
		if (c->type == NODE_DIR) {
			buf_add(b, "/");
		} else if (c->type == NODE_SYMLINK) {
			buf_add(b, "@");
		}
		buf_add(b, "\n");
		if (c->type == NODE_DIR) {
			tree_children(c, depth + 1, b);
		}
	}
}

int write_file(struct Node *cwd, struct Node *root, const char *path,
               const char *text, size_t len, int append) {
	char parent_path[1100];
	char name[1100];
	if (!split_path(path, parent_path, name)) {
		return 0;
	}
	struct Node *dir = resolve_path(cwd, root, parent_path);
	if (dir == NULL || dir->type != NODE_DIR) {
		return 0;
	}
	struct Node *node = find_child(dir, name);
	if (node == NULL) {
		node = add_child(dir, name, NODE_FILE);
		if (node == NULL) {
			return 0;
		}
	} else {
		node = follow(root, node);
		if (node == NULL || node->type != NODE_FILE) {
			return 0;
		}
	}

	struct Inode *in = node->inode;
	if (!append) {
		free(in->content);
		in->content = NULL;
		in->size = 0;
	}
	if (len > 0) {
		char *bigger = realloc(in->content, in->size + len + 1);
		if (bigger == NULL) {
			return 0;
		}
		memcpy(bigger + in->size, text, len);
		in->size += len;
		bigger[in->size] = '\0';
		in->content = bigger;
	}
	return 1;
}

int tokenize(char *line, char *words[], int quoted[], int max) {
	char *r = line;
	char *w = line;
	int count = 0;

	while (*r != '\0') {
		while (*r == ' ' || *r == '\t') {
			r++;
		}
		if (*r == '\0') {
			break;
		}
		if (count >= max) {
			return -1;
		}
		words[count] = w;
		quoted[count] = 0;
		int in_quotes = 0;

		while (*r != '\0' && (in_quotes || (*r != ' ' && *r != '\t'))) {
			if (*r == '\\') {
				quoted[count] = 1;
				r++;
				if (*r == '\0') {
					return -1;
				}
				*w = *r;
				w++;
				r++;
			} else if (*r == '"') {
				quoted[count] = 1;
				in_quotes = !in_quotes;
				r++;
			} else {
				*w = *r;
				w++;
				r++;
			}
		}
		if (in_quotes) {
			return -1;
		}
		if (*r != '\0') {
			r++;
		}
		*w = '\0';
		w++;
		count++;
	}
	return count;
}

int is_inside(struct Node *node, struct Node *ancestor) {
	while (node != NULL) {
		if (node == ancestor) {
			return 1;
		}
		node = node->parent;
	}
	return 0;
}

int main(void) {
	char line[1100];

	// create the root directory 
	struct Node *root = malloc(sizeof(struct Node));
	if (root == NULL) {
		return 1;
	}
	root->name = malloc(2);
	if (root->name == NULL) {
		free(root);
		return 1;
	}
	strcpy(root->name, "/");
	root->type = NODE_DIR;
	root->target = NULL;
	root->parent = NULL;
	root->children = NULL;
	root->child_count = 0;
	root->child_capacity = 0;
	root->inode = NULL;

	// current working directory starts at the root
	struct Node *cwd = root;

	while (1) {
		struct Buf p = {NULL, 0, 0};
		pwd_to_buf(cwd, &p);
		printf("user@boun:%s$ ", p.data);
		free(p.data);
		if (fgets(line, sizeof(line), stdin) == NULL) {
			break;
		}
		line[strcspn(line, "\n")] = '\0';

		// Split the line into words
		char *words[600];
		int quoted[600];
		int count = tokenize(line, words, quoted, 600);
		if (count < 0) {
			printf("bushell: error\n");
			continue;
		}
		if (count == 0) {
			continue;
		}

		if (strcmp(words[0], "exit") == 0) {
			break;
		}

		int err = 0;
		struct Buf out = {NULL, 0, 0};

		int redir = 0;
		char *redir_target = NULL;
		for (int i = 0; i < count; i++) {
			if (!quoted[i] && (strcmp(words[i], ">") == 0 || strcmp(words[i], ">>") == 0)) {
				if (strcmp(words[i], ">") == 0) {
					redir = 1;
				} else {
					redir = 2;
				}
				if (i == 0 || i + 2 != count) {
					err = 1;
				} else {
					redir_target = words[i + 1];
					count = i;
				}
				break;
			}
		}

		if(err){}

		else if (strcmp(words[0], "pwd") == 0) {
			if ( count != 1) {
				err = 1;
			} else {
				pwd_to_buf(cwd, &out);
				buf_add(&out, "\n");
			}
		} else if (strcmp(words[0], "echo") == 0) {
			for (int i = 1; i < count; i++) {
				if (i > 1) {
					buf_add(&out, " ");
				}
				buf_add(&out, words[i]);
			}
			buf_add(&out, "\n");
		} else if (strcmp(words[0], "mkdir") == 0) {
			char parent_path[1100];
			char new_name[1100];
			if (count != 2 || !split_path(words[1], parent_path, new_name)) {
				err = 1;
			} else {
				struct Node *dir = resolve_path(cwd, root, parent_path);
				if (dir == NULL || dir->type != NODE_DIR || find_child(dir, new_name) != NULL) {
					err = 1;
				} else if (add_child(dir, new_name, NODE_DIR) == NULL) {
					err = 1;
				}
			}
		} else if (strcmp(words[0], "cd") == 0) {
			if (count != 2) {
				err = 1;
			} else {
				struct Node *target = follow(root, resolve_path(cwd, root, words[1]));
				if (target == NULL || target->type != NODE_DIR) {
					err = 1;
				} else {
					cwd = target;
				}
			}
		} else if (strcmp(words[0], "touch") == 0) {
			char parent_path[1100];
			char new_name[1100];
			if (count != 2 || !split_path(words[1], parent_path, new_name)) {
				err = 1;
			} else {
				struct Node *dir = resolve_path(cwd, root, parent_path);
				if (dir == NULL || dir->type != NODE_DIR) {
					err = 1;
				} else if (find_child(dir, new_name) == NULL) {
					if (add_child(dir, new_name, NODE_FILE) == NULL) {
						err = 1;
					}
				}
			}
		} else if (strcmp(words[0], "ls") == 0) {
			int show_all = 0;
			int ok = 1;
			if (count == 2) {
				if (strcmp(words[1], "-a") == 0) {
					show_all = 1;
				} else {
					ok = 0;
				}
			} else if (count > 2) {
				ok = 0;
			}

			if (!ok) {
				err = 1;
			} else {
				if (cwd->child_count > 1) {
					qsort(cwd->children, cwd->child_count, sizeof(struct Node *), compare_nodes);
				}
				int first = 1;
				if (show_all) {
					buf_add(&out, "./ ../");
					first = 0;
				}
				for (int i = 0; i < cwd->child_count; i++) {
					if (!show_all && cwd->children[i]->name[0] == '.') {
						continue;
					}
					if (!first) {
						buf_add(&out, " ");
					}
					first = 0;
					buf_add(&out, cwd->children[i]->name);
					if (cwd->children[i]->type == NODE_DIR) {
						buf_add(&out, "/");
					} else if (cwd->children[i]->type == NODE_SYMLINK) {
						buf_add(&out, "@");
					}
				}
				buf_add(&out, "\n");
			}

		} else if (strcmp(words[0], "cat") == 0) {
			if (count != 2) {
				err = 1;
			} else {
				struct Node *f = follow(root, resolve_path(cwd, root, words[1]));
				if (f == NULL || f->type != NODE_FILE) {
					err = 1;
				} else if (f->inode->size > 0) {
					buf_add(&out, f->inode->content);
				}
			}
		} else if (strcmp(words[0], "du") == 0) {
			if (count != 2) {
				err = 1;
			} else {
				struct Node *f = follow(root, resolve_path(cwd, root, words[1]));
				if (f == NULL || f->type != NODE_FILE) {
					err = 1;
				} else {
					char num[32];
					snprintf(num, sizeof(num), "%zu", f->inode->size);
					buf_add(&out, num);
					buf_add(&out, "\t");
					buf_add(&out, words[1]);
					buf_add(&out, "\n");
				}
			}
		} else if (strcmp(words[0], "rm") == 0) {
			if (count != 2) {
				err = 1;
			} else {
				struct Node *node = resolve_path(cwd, root, words[1]);
				if (node == NULL || node == root || node == cwd || node->child_count > 0) {
					err = 1;
				} else {
					remove_child(node->parent, node);
					free_node(node);
				}
			}
		} else if (strcmp(words[0], "cp") == 0) {
			if (count != 3) {
				err = 1;
			} else {
				struct Node *src = follow(root, resolve_path(cwd, root, words[1]));
				struct Node *dest = resolve_path(cwd, root, words[2]);
				struct Node *dest_f = follow(root, dest);
				if (src == NULL || src->type != NODE_FILE) {
					err = 1;
				} else if (dest != NULL && dest_f == NULL) {
					err = 1;
				} else if (dest_f != NULL && dest_f->inode == src->inode) {
					err = 1;
				} else {
					const char *text = "";
					if (src->inode->size > 0) {
						text = src->inode->content;
					}
					if (!write_file(cwd, root, words[2], text, src->inode->size, 0)) {
						err = 1;
					}
				}
			}
		} else if (strcmp(words[0], "tree") == 0) {
			if (count != 1) {
				err = 1;
			} else {
				pwd_to_buf(cwd, &out);
				buf_add(&out, "\n");
				tree_children(cwd, 1, &out);
			}
		} else if (strcmp(words[0], "mv") == 0) {
			if (count != 3) {
				err = 1;
			} else {
				struct Node *src = resolve_path(cwd, root, words[1]);
				struct Node *dest = resolve_path(cwd, root, words[2]);
				struct Node *newdir = NULL;
				char *newname = NULL;
				char parent_path[1100];
				char new_name[1100];

				if (src == NULL || src == root) {
					err = 1;
				} else if (dest != NULL) {
					if (dest->type != NODE_DIR || find_child(dest, src->name) != NULL || is_inside(dest, src)) {
						err = 1;
					} else {
						newdir = dest;
					}
				} else if (!split_path(words[2], parent_path, new_name)) {
					err = 1;
				} else {
					newdir = resolve_path(cwd, root, parent_path);
					if (newdir == NULL || newdir->type != NODE_DIR || is_inside(newdir, src)) {
						err = 1;
					} else {
						newname = malloc(strlen(new_name) + 1);
						if (newname == NULL) {
							err = 1;
						} else {
							strcpy(newname, new_name);
						}
					}
				}

				if (!err) {
					struct Node *old_parent = src->parent;
					if (!attach_child(newdir, src)) {
						err = 1;
						free(newname);
					} else {
						remove_child(old_parent, src);
						if (newname != NULL) {
							free(src->name);
							src->name = newname;
						}
					}
				}
			}
		} else if (strcmp(words[0], "ln") == 0) {
			char parent_path[1100];
			char new_name[1100];
			if (count == 3) {
				struct Node *src = resolve_path(cwd, root, words[1]);
				if (src == NULL || src->type != NODE_FILE || !split_path(words[2], parent_path, new_name)) {
					err = 1;
				} else {
					struct Node *dir = resolve_path(cwd, root, parent_path);
					if (dir == NULL || dir->type != NODE_DIR || find_child(dir, new_name) != NULL) {
						err = 1;
					} else if (add_hardlink(dir, new_name, src) == NULL) {
						err = 1;
					}
				}
			} else if (count == 4 && strcmp(words[1], "-s") == 0) {
				struct Node *t = resolve_path(cwd, root, words[2]);
				if (t == NULL || t->type != NODE_FILE || !split_path(words[3], parent_path, new_name)) {
					err = 1;
				} else {
					struct Node *dir = resolve_path(cwd, root, parent_path);
					if (dir == NULL || dir->type != NODE_DIR || find_child(dir, new_name) != NULL) {
						err = 1;
					} else if (add_symlink(dir, new_name, t) == NULL) {
						err = 1;
					}
				}

			} else {
				err = 1;
			}
		} else {
			err = 1;
		}
		if (err) {
			printf("bushell: error\n");
		} else if (redir != 0) {
			const char *text = "";
			if (out.len > 0) {
				text = out.data;
			}
			if (!write_file(cwd, root, redir_target, text, out.len, redir == 2)) {
				printf("bushell: error\n");
			}
		} else if (out.len > 0) {
			fwrite(out.data, 1, out.len, stdout);
		}
		free(out.data);
	}

	free_tree(root);
	return 0;
}