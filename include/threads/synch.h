#ifndef THREADS_SYNCH_H
#define THREADS_SYNCH_H

#include <list.h>
#include <stdbool.h>

/* A counting semaphore. 값과 대기쓰레드가 있음 */
struct semaphore
{
    unsigned value;      /* Current value. unsigned = unsigned int와 같다. */
    struct list waiters; /* List of waiting threads. 대기 쓰레드가 들어가는 리스트, 헤드와 테일이 있음*/
};

void sema_init(struct semaphore *, unsigned value);
void sema_down(struct semaphore *);
bool sema_try_down(struct semaphore *);
void sema_up(struct semaphore *);
void sema_self_test(void);

/* Lock. */
struct lock
{
    struct thread *holder;      /* Thread holding lock (for debugging). */
    struct semaphore semaphore; /* Binary semaphore controlling access. */
};

void lock_init(struct lock *);
void lock_acquire(struct lock *);
bool lock_try_acquire(struct lock *);
void lock_release(struct lock *);
bool lock_held_by_current_thread(const struct lock *);

/* Condition variable. */
struct condition
{
    struct list waiters; /* List of waiting threads. */
};

void cond_init(struct condition *);
void cond_wait(struct condition *, struct lock *);
void cond_signal(struct condition *, struct lock *);
void cond_broadcast(struct condition *, struct lock *);

/* Optimization barrier.
 * 컴파일러의 최적화를 방해해서, 특정 코드가 컴파일 되도록 하는 함수.
 * The compiler will not reorder operations across an
 * optimization barrier.  See "Optimization Barriers" in the
 * reference guide for more information.*/
#define barrier() asm volatile("" : : : "memory")

#endif /* threads/synch.h */
