#include "userprog/syscall.h"
#include "filesys/filesys.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "threads/init.h"
#include "user/syscall.h"

void syscall_entry(void);
void syscall_handler(struct intr_frame *);
void halt(void);
void exit(int status);
// pid_t fork(const char *thread_name);
int exec(const char *cmd_line);
// int wait(pid_t pid);
bool create(const char *file, unsigned initial_size);
bool remove(const char *file);
int open(const char *file);
int filesize(int fd);
int read(int fd, void *buffer, unsigned size);
int write(int fd, const void *buffer, unsigned size);
void seek(int fd, unsigned position);
unsigned tell(int fd);
void close(int fd);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr; 사용자 모드와 커널 모드 간 전환 시 사용할 세그먼트 셀렉터 설정 */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target; 명령어 실행 시 호출될 함수의 주소 설정, syscall_entry의 주소 설정 */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags; system call 동안 마스킹될 EFLAGS 레지스터의 비트 설정 */

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
void syscall_handler(struct intr_frame *f UNUSED)
{
    // TODO: Your implementation goes here.
    // printf("system call!\n");
    // printf("system call number: %d \n", f->R.rax);
    check_address(f);

    // 인자 들어오는 순서 %rdi, %rsi, %rdx, %r10, %r8, %r9

    switch (f->R.rax)
    {
    case SYS_HALT:
        halt();
        break;
    case SYS_EXIT:
        exit(f->R.rdi);
        break;
    case SYS_FORK:
        break;
    case SYS_EXEC:
        f->R.rax = exec(f->R.rdi);
        break;
    case SYS_WAIT:
        break;
    case SYS_CREATE:
        f->R.rax = create(f->R.rdi, f->R.rsi);
        break;
    case SYS_REMOVE:
        remove(f->R.rdi);
        break;
    case SYS_OPEN:
        open(f->R.rdi);
        break;
    case SYS_FILESIZE:
        filesize(f->R.rdi);
        break;
    case SYS_READ:
        read(f->R.rdi, f->R.rsi, f->R.rdx);
        break;
    case SYS_WRITE:
        f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
        break;
    case SYS_SEEK:
        seek(f->R.rdi, f->R.rsi);
        break;
    case SYS_TELL:
        tell(f->R.rdi);
        break;
    case SYS_CLOSE:
        close(f->R.rdi);
        break;
    }
    // thread_exit();
}

void check_address(struct intr_frame *intr_f)
{
    // printf("checking if address is invalid.. \n");
    // printf("your requested address is: %x \n", intr_f->rsp);

    if (is_kernel_vaddr(intr_f->rsp))
    {
        exit(-1);
    }
    if (intr_f->rsp == NULL)
    {
        exit(-1);
    }
    if (pml4_get_page(thread_current()->pml4, intr_f->rsp) == NULL)
    {
        exit(-1);
    }
}

void exit(int status)
{
    printf("%s: exit(%d)\n", thread_current()->name, status);
    // thread_current()->exit_status = status;
    thread_exit();
}

void halt(void)
{
    power_off();
}

int exec(const char *file)
{
    return process_exec(file);
}

int wait(pid_t pid)
{
    return process_wait(pid);
}

bool create(const char *file, unsigned initial_size)
{
    if (filesys_create(file, initial_size) == true)
    {
        return true;
    }
    else
        return false;
}

bool remove(const char *file)
{
    if (filesys_remove(file))
    {
        return true;
    }
    else
        return false;
}

int open(const char *file)
{
    check_address(file);
    struct file *f = filesys_open(file);
    if (f == NULL)
    {
        return -1;
    }
    else
    {
        thread_current()->fdt[2] = f;
    }
}

int filesize(int fd)
{
}

int read(int fd, void *buffer, unsigned size)
{
}

int write(int fd, const void *buffer, unsigned size)
{
    if (fd == 1)
    {
        putbuf(buffer, size);
        return size;
    }
    else
    {
        return -1;
    }
}

void seek(int fd, unsigned position)
{
}

unsigned tell(int fd)
{
}

void close(int fd)
{
}