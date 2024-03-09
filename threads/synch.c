/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
   */

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

void donate_priority(struct list_elem *donor, struct thread *holder);

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   세마포어인 SEMA = VALUE로 초기화한다. 세마포어는 두가지 연산이 딸려오는
   비-음의 정수이다.

   - down or "P": wait for the value to become positive, then
   decrement it.

   - up or "V": increment the value (and wake up one waiting
   thread, if any). */
void sema_init(struct semaphore *sema, unsigned value)
{
    ASSERT(sema != NULL);

    sema->value = value;
    list_init(&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. This is
   sema_down function.

세마포어에 대한 "P" 또는 내림 연산입니다. SEMA의 값이 양수가 될 때까지 대기한
다음에 그 값을 원자적으로 감소시킵니다. 이 함수는 잠들 수 있으므로,
인터럽트 핸들러 내에서 호출되어서는 안 됩니다. 이 함수는 인터럽트가 비활성화된
상태에서 호출될 수 있지만, 만약 함수가 잠들면 다음에 스케줄된 스레드가
아마도 인터럽트를 다시 활성화할 것입니다. 이것은 sema_down 함수입니다. */
void sema_down(struct semaphore *sema)
{
    enum intr_level old_level;

    ASSERT(sema != NULL);
    ASSERT(!intr_context());

    old_level = intr_disable();
    while (sema->value == 0)
    {
        list_push_back(&sema->waiters, &thread_current()->elem);
        // list_insert_ordered(&sema->waiters, &thread_current()->elem, priority, NULL);
        thread_block(); // 실행 정지
    }
    sema->value--;
    intr_set_level(old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool sema_try_down(struct semaphore *sema)
{
    enum intr_level old_level;
    bool success;

    ASSERT(sema != NULL);

    old_level = intr_disable();
    if (sema->value > 0)
    {
        sema->value--;
        success = true;
    }
    else
        success = false;
    intr_set_level(old_level);

    return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void sema_up(struct semaphore *sema)
{
    enum intr_level old_level;

    ASSERT(sema != NULL);

    old_level = intr_disable();
    if (!list_empty(&sema->waiters))
    {
        struct thread *curr = thread_current();

        list_sort(&sema->waiters, priority, NULL);
        struct thread *popth = list_entry(list_pop_front(&sema->waiters),
                                          struct thread, elem);
        thread_unblock(popth);
    }
    sema->value++;

    thread_yield(); // thread_preempt();
    intr_set_level(old_level);
}

static void sema_test_helper(void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void sema_self_test(void)
{
    struct semaphore sema[2];
    int i;

    printf("Testing semaphores...");
    sema_init(&sema[0], 0);
    sema_init(&sema[1], 0);
    thread_create("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
    for (i = 0; i < 10; i++)
    {
        sema_up(&sema[0]);
        sema_down(&sema[1]);
    }
    printf("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper(void *sema_)
{
    struct semaphore *sema = sema_;
    int i;

    for (i = 0; i < 10; i++)
    {
        sema_down(&sema[0]);
        sema_up(&sema[1]);
    }
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.
LOCK을 초기화합니다. 한 번에 최대 한 개의 스레드만이 잠금을 보유할 수 있습니다.
    우리의 잠금은 "재귀적"이지 않습니다. 즉, 현재 잠금을 보유하고 있는 스레드가
    그 잠금을 다시 획득하려고 시도하는 것은 오류입니다.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock.
   잠금은 초기 값이 1인 세마포어의 특수화입니다. 잠금과 이러한 세마포어 사이의 차이점은
   두 가지입니다. 첫째, 세마포어는 값이 1보다 클 수 있지만, 잠금은 한 번에 단 하나의 스레드만이
   소유할 수 있습니다. 둘째, 세마포어는 소유자가 없습니다. 즉, 한 스레드가 세마포어를
   "down"할 수 있고 그 다음에 다른 스레드가 그것을 "up"할 수 있지만, 잠금의 경우 동일한
   스레드가 잠금을 획득하고 해제해야 합니다. 이러한 제한이 번거로울 때, 잠금 대신 세마포어를
   사용해야 한다는 좋은 신호입니다.
   */
void lock_init(struct lock *lock)
{
    ASSERT(lock != NULL);

    lock->holder = NULL;
    sema_init(&lock->semaphore, 1);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.
잠금키를 얻습니다. 획득이 당장 안되면 사용 가능할 때 까지 잠듭니다.
    이 잠금키는 현재쓰레드가 이미보유하고 있지 않아야 합니다. (이잠금키를 현재의쓰레드가
    보유하는 상황이 벌어져서는 안됩니다.)
   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep.
    이 함수는 잠에 들 수도 있어서, 인터럽트 핸들러 내에서는 절대 실행되서는 안됩니다.
    (인터럽트 핸들러는 항상 동작해야 하므로) 인터럽트 비활성화 상태에서는 호출할 수
    있지만, 잠에 들어야 할경우, 인터럽트가 다시 켜질겁니다.
   */
void lock_acquire(struct lock *lock)
{
    ASSERT(lock != NULL);
    ASSERT(!intr_context());
    ASSERT(!lock_held_by_current_thread(lock));

    if (lock->holder != NULL)
    {
        thread_current()->wait_on_lock = lock;
    }
    sema_down(&lock->semaphore);
    lock->holder = thread_current();
    if (thread_current()->wait_on_lock != NULL)
        thread_current()->wait_on_lock = NULL;
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool lock_try_acquire(struct lock *lock)
{
    bool success;

    ASSERT(lock != NULL);
    ASSERT(!lock_held_by_current_thread(lock));

    success = sema_try_down(&lock->semaphore);
    if (success)
        lock->holder = thread_current();
    return success;
}

/* Releases LOCK, which must be owned by the current thread.
   This is lock_release function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void lock_release(struct lock *lock)
{
    ASSERT(lock != NULL);
    ASSERT(lock_held_by_current_thread(lock));

    lock->holder = NULL;
    sema_up(&lock->semaphore);
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.)
   현재 쓰레드가 잠금키( lock)을 갖고 있다면 true를 반환, 아닌경우 false반환. */
bool lock_held_by_current_thread(const struct lock *lock)
{
    ASSERT(lock != NULL);

    return lock->holder == thread_current(); // 현재 쓰레드가 lo
}

/* One semaphore in a list. */
struct semaphore_elem
{
    struct list_elem elem;      /* List element. */
    struct semaphore semaphore; /* This semaphore. */

    int priority;
};

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void cond_init(struct condition *cond)
{
    ASSERT(cond != NULL);

    list_init(&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void cond_wait(struct condition *cond, struct lock *lock)
{
    struct semaphore_elem waiter;

    ASSERT(cond != NULL);
    ASSERT(lock != NULL);
    ASSERT(!intr_context());
    ASSERT(lock_held_by_current_thread(lock));

    sema_init(&waiter.semaphore, 0);
    waiter.priority = lock->holder->priority;
    list_push_back(&cond->waiters, &waiter.elem);
    // list_insert_ordered(&cond->waiters, &waiter.elem, priority, NULL);
    lock_release(lock);
    sema_down(&waiter.semaphore);
    lock_acquire(lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
bool cond_priority(const struct list_elem *a, const struct list_elem *b, void *aux)
{
    struct semaphore_elem *sa = list_entry(a, struct semaphore_elem, elem);
    struct semaphore_elem *sb = list_entry(b, struct semaphore_elem, elem);

    return sa->priority > sb->priority;
}

void cond_signal(struct condition *cond, struct lock *lock)
{
    ASSERT(cond != NULL);
    ASSERT(lock != NULL);
    ASSERT(!intr_context());
    ASSERT(lock_held_by_current_thread(lock));

    if (!list_empty(&cond->waiters))
    { // 맨 앞에 애한테 알림
        list_sort(&cond->waiters, cond_priority, NULL);
        // struct list_elem *e = list_entry(list_front(&cond->waiters), struct semaphore_elem, elem);
        sema_up(&list_entry(list_pop_front(&cond->waiters),
                            struct semaphore_elem, elem)
                     ->semaphore);
    }
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void cond_broadcast(struct condition *cond, struct lock *lock)
{
    ASSERT(cond != NULL);
    ASSERT(lock != NULL);

    while (!list_empty(&cond->waiters))
        cond_signal(cond, lock);
}
