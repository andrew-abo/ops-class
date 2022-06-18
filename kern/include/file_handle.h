#ifndef _FILE_HANDLE_H_
#define _FILE_HANDLE_H_

#include <types.h>
#include <vnode.h>
#include <synch.h>

// File abstraction for file syscalls.
struct file_handle {
    char *name;  // String identifier for this handle.
    off_t offset;  // Byte offset from beginning of file for next operation.
    struct vnode *vn;
    struct lock *file_lock;  // Protects handle and vnode.
    int ref_count;  // Number of table entries pointing here.
    int flags;  // O_RDONLY, O_WRONLY, O_RDWR
};

struct file_handle *create_file_handle(const char *);
void destroy_file_handle(struct file_handle *);

#endif
