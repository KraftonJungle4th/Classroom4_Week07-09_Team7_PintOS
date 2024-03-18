#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

#include "threads/init.h"
#include "filesys/filesys.h"
#include "filesys/file.h"

void syscall_entry(void);
void syscall_handler(struct intr_frame *);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void syscall_init(void)
{
    write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48 |
                            ((uint64_t)SEL_KCSEG) << 32);
    write_msr(MSR_LSTAR, (uint64_t)syscall_entry);

    /* The interrupt service rountine should not serve any interrupts
     * until the syscall_entry swaps the userland stack to the kernel
     * mode stack. Therefore, we masked the FLAG_FL. */
    write_msr(MSR_SYSCALL_MASK,
              FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* The main system call interface */
void syscall_handler(struct intr_frame *f) // UNUSED)
{
    // TODO: Your implementation goes here.
    int64_t call_num = f->R.rax;
    // printf("call nul %d\n", call_num);

    // printf("system call!\n");

    switch (call_num)
    {
    case SYS_HALT:
        // printf("halt\n");
        halt();
        break;
    case SYS_EXIT:
        exit(f->R.rdi);
        break;
    case SYS_FORK:
        break;
    case SYS_EXEC:
        break;
    case SYS_WAIT:
        break;
    case SYS_CREATE:
        f->R.rax = create(f->R.rdi, f->R.rsi);
        break;
    case SYS_REMOVE:
        break;
    case SYS_OPEN:
        f->R.rax = open(f->R.rdi);
        break;
    case SYS_FILESIZE:
        f->R.rax = filesize(f->R.rdi);
        break;
    case SYS_READ:
        f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
        break;
    case SYS_WRITE:
        f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
        break;
    case SYS_SEEK:
        seek(f->R.rdi, f->R.rsi);
        break;
    case SYS_TELL:
        f->R.rax = tell(f->R.rdi);
        break;
    case SYS_CLOSE:
        close(f->R.rdi);
        break;
    default:
        break;
        /* project 2 */
    }
    // thread_exit();
}

bool check_addr(void *addr) // 또는 addr
{
    if (addr == NULL || is_kernel_vaddr(addr) || pml4_get_page(thread_current()->pml4, addr) == NULL)
    {
        exit(-1);
    }
}

void halt(void)
{
    power_off();
}

void exit(int status)
{
    printf("%s: exit(%d)\n", thread_current()->name, status);
    thread_exit();
}

pid_t fork(const char *thread_name)
{
    // process_fork(thread_name, if);
}

bool create(const char *file, unsigned initial_size)
{
    check_addr(file);

    return filesys_create(file, initial_size);
}

bool remove(const char *file)
{
    return filesys_remove(file);
}

int open(const char *file)
{
    check_addr(file);

    struct thread *t = thread_current();
    struct file **table = t->fd_table;
    struct file *open_file = NULL;
    int index = -1;
    open_file = filesys_open(file);

    if (open_file == NULL)
        return index;
    for (int i = 2; i < 128; i++)
    {
        if (table[i] == NULL)
        {
            index = i;
            table[i] = open_file;
            break;
        }
    }
    return index;
}

int filesize(int fd)
{
    struct thread *t = thread_current();
    struct file **table = t->fd_table;
    struct file *open_file;
    int size;

    if (table[fd] != NULL)
    {
        open_file = table[fd];
        size = file_length(open_file);
    }

    return size;
}

int read(int fd, void *buffer, unsigned size)
{
    struct thread *t = thread_current();
    struct file **table = t->fd_table;
    struct file *open_file;
    int ret = -1;

    check_addr(buffer);

    if (fd > 128 || fd < 0)
        return -1;
    if (table[fd] == NULL)
        return -1;

    if (fd == 0)
    {
        ret = input_getc();
    }
    else
    {
        open_file = table[fd];
        ret = file_read(open_file, buffer, size);
    }

    return ret;
}

int write(int fd, const void *buffer, unsigned size)
{
    struct thread *t = thread_current();
    struct file **table = t->fd_table;
    struct file *write_file;
    int write_size = -1;

    check_addr(buffer);

    if (fd < 0 || fd > 128)
        return -1;
    if (fd == 1) // STD_OUT
    {
        putbuf(buffer, size);
    }
    else
    {
        if (table[fd] != NULL)
        {
            write_file = table[fd];
            write_size = file_write_at(write_file, buffer, size, 0);
        }
    }

    return write_size;
}

void seek(int fd, unsigned position)
{
    struct thread *t = thread_current();
    struct file **table = t->fd_table;
    struct file *open_file;

    if (fd < 0 || fd > 128)
        exit(-1); // return;

    if (table[fd] != NULL)
        open_file = table[fd];
    else
        exit(-1); // return;

    file_seek(open_file, position); // position > filesize 면 에러
}

unsigned tell(int fd)
{
    struct thread *t = thread_current();
    struct file **table = t->fd_table;
    struct file *open_file;

    if (fd < 0 || fd > 128)
        return -1;

    if (table[fd] != NULL)
        open_file = table[fd];
    else
        return -1;

    return file_tell(open_file);
}

void close(int fd)
{
    struct thread *t = thread_current();
    struct file **table = t->fd_table;
    struct file *open_file;

    if (fd == 1 || fd == 0 || fd > 128)
        exit(-1); // return;

    if (table[fd] != NULL)
    {
        open_file = table[fd];
        table[fd] = NULL;
    }
}
