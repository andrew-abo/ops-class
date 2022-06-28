#ifndef _FILE_HANDLE_H_
#define _FILE_HANDLE_H_

#include <types.h>
#include <vnode.h>
#include <synch.h>

// File abstraction for file syscalls.
// A file_handle is a file context, which includes an offset from the start of
// the file where the next operation shall occur.  Multiple file_handles
// can reference the same physical file.
struct file_handle {
    char *name;  // String identifier for this handle.
    off_t offset;  // Byte offset from beginning of file for next operation.
    struct vnode *vn;
    struct lock *file_lock;  // Protects handle and vnode.
    int ref_count;  // Number of table entries pointing here.
    int flags;  // O_RDONLY, O_WRONLY, O_RDWR
};

struct file_handle *create_file_handle(const char *);
void close_file_handle(struct file_handle *);
void destroy_file_handle(struct file_handle *);
void lock_file_handle(struct file_handle *);
int open_file_handle(const char *, int, struct file_handle **);
void release_file_handle(struct file_handle *);
#endif
