#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* Random value for basic thread
   Do not modify this value. */
#define THREAD_BASIC 0xd42df210

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running.
   쓰레드_레디 상태? 자료구조 안의 wait이랑은 또 다른건가? */
static struct list ready_list;
static struct list stun_list;

/* Idle thread. 놈팽이쓰레드포인터 */
// static struct thread *idle_th_ptr
;
static struct thread *idle_th_ptr;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Thread destruction requests */
static struct list destruction_req;

/* Statistics. 상태정보들 */
static long long idle_ticks;   /* 정적변수 ### of timer ticks spent idle. */
static long long kernel_ticks; /* 정적변수 ### of timer ticks in kernel threads. */
static long long user_ticks;   /* 정적변수 ### of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4          /* 스케줄링을 위한 간격 ### of timer ticks to give each thread. */
static unsigned thread_ticks; /* ### of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread(thread_func *, void *aux);

static void idle(void *aux UNUSED);
static struct thread *next_thread_to_run(void);
struct thread *judge_thread_to_run(void);
static void init_thread(struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule(void);
static tid_t allocate_tid(void);

/* Returns true if T appears to point to a valid thread. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* Returns the running thread.
 * Read the CPU's stack pointer `rsp', and then round that
 * down to the start of a page.  Since `struct thread' is
 * always at the beginning of a page and the stack pointer is
 * somewhere in the middle, this locates the curent thread. */
#define running_thread() ((struct thread *)(pg_round_down(rrsp())))

// Global descriptor table for the thread_start.
// Because the gdt will be setup after the thread_init, we should
// setup temporal gdt first.
static uint64_t gdt[3] = {0, 0x00af9a000000ffff, 0x00cf92000000ffff};

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes.

   스레딩 시스템을 초기화함으로써 현재 실행 중인 코드를 스레드로
   변환합니다. 일반적으로 이 작업은 수행될 수 없지만, loader.S가 스택의
   하단을 페이지 경계에 맞추어 놓았기 때문에 이 경우에만 가능합니다.
   또한 실행 큐와 tid 잠금을 초기화합니다. 이 함수를 호출한 후에는
   thread_create()를 사용하여 어떤 스레드도 생성하기 전에 페이지 할당자를
   반드시 초기화해야 합니다. 이 함수가 완료될 때까지 thread_current()를
   호출하는 것은 안전하지 않습니다.*/
void thread_init(void)
{
    ASSERT(intr_get_level() == INTR_OFF);

    /* Reload the temporal gdt for the kernel
     * This gdt does not include the user context.
     * The kernel will rebuild the gdt with user context, in gdt_init (). */
    struct desc_ptr gdt_ds = {
        .size = sizeof(gdt) - 1,
        .address = (uint64_t)gdt};
    lgdt(&gdt_ds);

    /* Init the globla thread context */
    lock_init(&tid_lock);
    list_init(&ready_list);
    list_init(&stun_list);
    list_init(&destruction_req);

    /* Set up a thread structure for the running thread. */
    initial_thread = running_thread();
    init_thread(initial_thread, "main", PRI_DEFAULT);
    initial_thread->status = THREAD_RUNNING;
    initial_thread->tid = allocate_tid();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread.
   preemptiveSchedulingStart()라고 명명하고 싶다.
  인터럽트를 활성화하여, 강탈방식(선점방식)의 쓰레드스케줄링을 시작합니다.  */
void thread_start(void)
{
    /* Create the idle thread.
        idle_thread가 세마포어로 정의되어있네!
        우리가 지켜야하는 유일한 자원은 시간(ticks)이므로
        idle이 타이머 역할을*/
    struct semaphore idle_started;
    sema_init(&idle_started, 0);
    thread_create("idle", PRI_MIN, idle, &idle_started);
    // 순서대로 생성할쓰레드이름name (문자포인터), 우선순위 (정수),
    // 쓰레드함수 (쓰레드함수포인터), 보조함수 (void포인터)) 를 받아 생성한다.

    /* Start preemptive thread scheduling.
       강탈방식 쓰레드스케줄링을 시작합니다.*/
    intr_enable();

    /* Wait for the idle thread to initialize idle_thread. */
    sema_down(&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context.
    매 타이머의 틱마다, 타이머 인터럽트 핸들러에 의해 불려진다.
   그래서, 이 함수는 external문맥(외부신호인터럽트)에서 실행된다.
   */
void thread_tick(void)
{
    struct thread *t = thread_current(); // 현재 스레드가 어떤스레드인지 확인.

    /* Update statistics. 상태정보들 업데이트*/
    if (t == idle_th_ptr) //  idle쓰레드면
        idle_ticks++;     //  idle쓰레드의 틱을 증가
#ifdef USERPROG
    else if (t->pml4 != NULL)
        user_ticks++;
#endif
    else //
        kernel_ticks++;

    /* Enforce preemption. 타이머의 선점을 강제 */
    if (++thread_ticks >= TIME_SLICE)
        //  딱 이때에만! 인터럽트가 양보
        intr_yield_on_return();
}

/* Prints thread statistics. 상태정보들 출력 */
void thread_print_stats(void)
{
    printf("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
           idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.
   커널수준(OS접근가능)의 NAME이란 이름의 쓰레드를 생성한다. priority, 실행할 함수, 보조함수
   를 가지게 되고, ready큐에 더해진다. 이 스레드의 식별자 또는 TID_ERROR(생성 실패시)를 반환한다.
   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.
    강탈방식스케쥴링이 호출되면(thread_start()), 반환되기전에 호출하면서 만드는
    새로운 idle 쓰레드가 스케줄 될 수 있다. 심지어 반환되기도 전에 종료될 수 있다.
    반대로, 원래스레드는 새 스레드가 예약되기 전에 임의의 시간동안 실행될 수 있습니다.
    이와같은 순서를 확실하게 하고싶으면 세마포어나 다른 동기화방식을 쓰세요.
   The code provided sets the new thread's 'priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3.
   priority멤버변수가 새 스레드 생성시 PRIORITY로 정해졌지만, priority스케줄링이
   구현되어 있진 않습니다. 우선순위스케줄링은 1-3의 목표입니다.
   */
tid_t thread_create(const char *name, int priority,
                    thread_func *function, void *aux)
{
    struct thread *thPtr;
    // 스레드구조체 포인터
    tid_t tid;
    // 스레드ID

    ASSERT(function != NULL); // 스레드 function이 있어야 실행된다.

    /* Allocate thread.
    스레드 할당*/
    thPtr = palloc_get_page(PAL_ZERO);
    if (thPtr == NULL)
        return TID_ERROR;

    /* Initialize thread. */
    init_thread(thPtr, name, priority);
    tid = thPtr->tid = allocate_tid();

    /* Call the kernel_thread if it scheduled.
     * Note) rdi is 1st argument, and rsi is 2nd argument. */
    thPtr->tf.rip = (uintptr_t)kernel_thread;
    thPtr->tf.R.rdi = (uint64_t)function;
    thPtr->tf.R.rsi = (uint64_t)aux;
    thPtr->tf.ds = SEL_KDSEG;
    thPtr->tf.es = SEL_KDSEG;
    thPtr->tf.ss = SEL_KDSEG;
    thPtr->tf.cs = SEL_KCSEG;
    thPtr->tf.eflags = FLAG_IF;

    /* Add to run queue. */
    thread_unblock(thPtr);

    return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().
   스레드 블락은 현재스레드로부터 CPU의 자원을 뺏는것. 언블락에 의해 깰때까지 잔다.
   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h.
   이 함수는 인터럽트가 꺼진채 실행되어야 한다. synch.h의 동기화원시 하나를 활용
   하는 것이 더 좋은 아이디어이다.*/
void thread_block(void)
{
    ASSERT(!intr_context());                   // 지금 컨텍스트가 외부신호 인터럽트가 아님
    ASSERT(intr_get_level() == INTR_OFF);      //  현재 인터럽트 비활성화여야함.
    thread_current()->status = THREAD_BLOCKED; //  현재스레드의 상태를 스레드블락으로 만듦
    schedule();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)
    잠든, 막힌, 블락된 쓰레드 T를 실행준비상태로 변환한다. 쓰레드T가 블락 안되어있으면
    이거는 에러다. (실행중인 쓰레드가 ready상태로 되게, thread_yield()를 사용하라.)
   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data.
   이 함수는 실행중인 쓰레드를 강탈하진 않는다. 이 사실은 중요할 수 있는데 :
   호출자가 인터럽트를 비활성화 했으면, 이 사실이 원자적으로 쓰레드를 언블락하고
   다른 데이터를 업데이트할 수 있다.*/
void thread_unblock(struct thread *t)
{
    enum intr_level old_level;

    ASSERT(is_thread(t));

    old_level = intr_disable();
    ASSERT(t->status == THREAD_BLOCKED);
    list_push_back(&ready_list, &t->elem); // 스레드의 자료를 레디큐에 넣는다.
    t->status = THREAD_READY;              // 쓰레드를 레디상태로 만든다.
    intr_set_level(old_level);             // 예전 사용자/OS모드로 돌아간다.
}

/* 스레드a와 b의 wakeuptick이 a가 같거나 작으면 true반환*/
bool less(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
    struct thread *th_a = list_entry(a, struct thread, elem);
    struct thread *th_b = list_entry(b, struct thread, elem);

    return th_a->wakeup_tick <= th_b->wakeup_tick;
}

/* 스레드a와 b의 wakeuptick이 a가 같거나 작으면 true반환*/
bool priority_less(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED)
{
    struct thread *th_a = list_entry(a, struct thread, elem);
    struct thread *th_b = list_entry(b, struct thread, elem);

    return th_a->priority >= th_b->priority;
}

/* 스레드가 스턴되는 함수 */
void thread_stun(int64_t th_tick)
{
    struct thread *curr = thread_current();
    enum intr_level old_level;

    ASSERT(!intr_context()); // 지금 컨텍스트가 외부신호 인터럽트가 아님

    old_level = intr_disable();
    if (curr != idle_th_ptr) // 만약 지금 CPU로 들어온 쓰레드가 현재 CPU에서 노는
    {                        // 것으로 추상화 된 idle_thread가 아니면
        thread_current()->wakeup_tick = th_tick;
        list_insert_ordered(&stun_list, &curr->elem, less, NULL);
        thread_current()->status = THREAD_STUNNED; //  현재스레드의 상태를 스레드블락으로 만듦
    }

    schedule();
    intr_set_level(old_level);
}

/* 스레드가 wakeup하는 함수 */
void thread_wakeup(struct thread *t)
{
    enum intr_level old_level;

    ASSERT(is_thread(t));

    old_level = intr_disable();
    ASSERT(t->status == THREAD_STUNNED);
    list_push_back(&ready_list, &t->elem); // 스레드의 자료를 레디큐에 넣는다.

    t->status = THREAD_READY;  // 쓰레드를 레디상태로 만든다.
    intr_set_level(old_level); // 예전 사용자/OS모드로 돌아간다.
}

/* Returns the name of the running thread. */
const char *
thread_name(void)
{
    return thread_current()->name;
}

// static struct thread *
// next_priority_thread_to_run(void)
// {
//     if (list_empty(&ready_list))lis
//         return idle_th_ptr;
//     else
//         list_insert_ordered(&ready_list, &curr->elem, less, NULL);
//     return list_entry(list_pop_front(&ready_list), struct thread, elem);
//     // 쓰레드 포인터로 변환해주는 전처리기
// }

/* Returns the running thread. This is running_thread() plus a couple of sanity checks.
    현재 실행중인 쓰레드를 반환한다. 그걸 하는게 running_thread()이고 여기에 몇가지 상태체크가 있다.
   See the big comment at the top of thread.h for details.
   자세한사항은 thread.h의 위에있는 긴 주석을 봐라. */
struct thread *
thread_current(void)
{
    struct thread *t = running_thread();

    /* Make sure T is really a thread.
       If either of these assertions fire, then your thread may
       have overflowed its stack.  Each thread has less than 4 kB
       of stack, so a few big automatic arrays or moderate
       recursion can cause stack overflow. */
    ASSERT(is_thread(t));
    ASSERT(t->status == THREAD_RUNNING);

    return t;
}

/* Returns the running thread's tid. */
tid_t thread_tid(void)
{
    return thread_current()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void thread_exit(void)
{
    ASSERT(!intr_context());

#ifdef USERPROG
    process_exit();
#endif

    /* Just set our status to dying and schedule another process.
       We will be destroyed during the call to schedule_tail(). */
    intr_disable();
    do_schedule(THREAD_DYING);
    NOT_REACHED();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim.
   CPU를 양보합니다. 현재 스레드는 잠들지 않으며(ready큐에
   그냥 즉시 들어가겠다), 스케줄러의 재량에 따라 즉시 다시
   스케줄될 수 있습니다. */
void thread_yield(void)
{
    struct thread *curr = thread_current();
    enum intr_level old_level;

    ASSERT(!intr_context());

    old_level = intr_disable();
    if (curr != idle_th_ptr) // 만약 지금 CPU로 들어온 쓰레드가 현재 CPU에서 노는
    {                        // 것으로 추상화 된 idle_thread가 아니면
        list_push_back(&ready_list, &curr->elem);
    }
    // list의 뒤로 밀어라
    do_schedule(THREAD_READY);
    intr_set_level(old_level);
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void thread_set_priority(int new_priority)
{
    thread_current()->priority = new_priority;
}

/* Returns the current thread's priority. */
int thread_get_priority(void)
{
    return thread_current()->priority;
}

/* Sets the current thread's nice value to NICE. */
void thread_set_nice(int nice UNUSED)
{
    /* TODO: Your implementation goes here */
}

/* Returns the current thread's nice value. */
int thread_get_nice(void)
{
    /* TODO: Your implementation goes here */
    return 0;
}

/* Returns 100 times the system load average. */
int thread_get_load_avg(void)
{
    /* TODO: Your implementation goes here */
    return 0;
}

/* Returns 100 times the current thread's recent_cpu value. */
int thread_get_recent_cpu(void)
{
    /* TODO: Your implementation goes here */
    return 0;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start()에 의해, idle쓰레드는 처음으로 readylist에
   thread_start().  It will be scheduled once initially, at which
   들어간다. 처음에 한번 스케쥴링되고, idle쓰레드포인터를 초기화
   point it initializes idle_th_ptr
   , "up"s the semaphore passed
   하면서 바로 그 시점에, 전달된 세마포어를 "업"해서 thread_start()가
   to it to enable thread_start() to continue, and immediately
   계속 되도록 하고, 이후 그 즉시 차단한다.
   blocks.  After that, the idle thread never appears in the
   이후에 idle쓰레드는 준비목록에 더 이상 나타나지 않는다.
   ready list.  It is returned by next_thread_to_run() as a
    (왜? 자신이 키를 쥐고 있으니까?)
   special case when the ready list is empty.
   레디큐가 비어있는 경우를 제외하고는 */
static void
idle(void *idle_started_)
{
    struct semaphore *idle_started = idle_started_;

    idle_th_ptr = thread_current();
    sema_up(idle_started);

    for (;;)
    {
        /* Let someone else run. */
        intr_disable();
        thread_block();

        /* Re-enable interrupts and wait for the next one.

           The `sti' instruction disables interrupts until the
           completion of the next instruction, so these two
           instructions are executed atomically.  This atomicity is
           important; otherwise, an interrupt could be handled
           between re-enabling interrupts and waiting for the next
           one to occur, wasting as much as one clock tick worth of
           time.

           See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
           7.11.1 "HLT Instruction". */
        asm volatile("sti; hlt" : : : "memory");
    }
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread(thread_func *function, void *aux)
{
    ASSERT(function != NULL);

    intr_enable(); /* The scheduler runs with interrupts off. */
    function(aux); /* Execute the thread function. */
    thread_exit(); /* If function() returns, kill the thread. */
}

/* Does basic initialization of T as a blocked thread named
   NAME.
   NAME이라는 이름의 차단된 스레드로 T를 기본 초기화합니다. */
static void
init_thread(struct thread *t, const char *name, int priority)
{
    ASSERT(t != NULL);
    ASSERT(PRI_MIN <= priority && priority <= PRI_MAX);
    ASSERT(name != NULL);

    memset(t, 0, sizeof *t);
    t->status = THREAD_BLOCKED;
    strlcpy(t->name, name, sizeof t->name);
    t->tf.rsp = (uint64_t)t + PGSIZE - sizeof(void *);
    t->priority = priority;
    t->magic = THREAD_MAGIC;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_th_ptr. */
static struct thread *
next_thread_to_run(void)
{
    if (list_empty(&ready_list))
        return idle_th_ptr;
    else
        return list_entry(list_pop_front(&ready_list), struct thread, elem);
    // 쓰레드 포인터로 변환해주는 전처리기
}

static struct thread *
next_priority_thread_to_run(void)
{
    if (list_empty(&ready_list))
        return idle_th_ptr;
    else
    {
        list_sort(&ready_list, priority_less, NULL);
        return list_entry(list_pop_front(&ready_list), struct thread, elem);
    }
    // 쓰레드 포인터로 변환해주는 전처리기
}

// struct thread *
// judge_thread_to_run(void)
// {
//     if (list_empty(&stun_list))
//         return idle_th_ptr;
//     else
//         return list_entry(list_front(&stun_list), struct thread, elem);
// }

void timer_check_wakeup(int64_t ticks)
{
    if (list_empty(&stun_list))
        return;
    // struct thread *th = judge_thread_to_run();

    struct thread *th = list_entry(list_front(&stun_list), struct thread, elem);

    if (th->wakeup_tick >= ticks)
        thread_wakeup(th);
}

/* Use iretq to launch the thread
  Interrupt RETurn 인터럽트 반환 */
void do_iret(struct intr_frame *tf)
{
    __asm __volatile(
        "movq %0, %%rsp\n"
        "movq 0(%%rsp),%%r15\n"
        "movq 8(%%rsp),%%r14\n"
        "movq 16(%%rsp),%%r13\n"
        "movq 24(%%rsp),%%r12\n"
        "movq 32(%%rsp),%%r11\n"
        "movq 40(%%rsp),%%r10\n"
        "movq 48(%%rsp),%%r9\n"
        "movq 56(%%rsp),%%r8\n"
        "movq 64(%%rsp),%%rsi\n"
        "movq 72(%%rsp),%%rdi\n"
        "movq 80(%%rsp),%%rbp\n"
        "movq 88(%%rsp),%%rdx\n"
        "movq 96(%%rsp),%%rcx\n"
        "movq 104(%%rsp),%%rbx\n"
        "movq 112(%%rsp),%%rax\n"
        "addq $120,%%rsp\n"
        "movw 8(%%rsp),%%ds\n"
        "movw (%%rsp),%%es\n"
        "addq $32, %%rsp\n"
        "iretq"
        : : "g"((uint64_t)tf) : "memory");
}

/* Switching the thread by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.
    새 스레드의 페이지테이블을 활성화 하는것으로 쓰레드를 전환한다.
    이전 쓰레드가 죽어가는 쓰레드면 파괴한다.
   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.
    이 함수의 부름(호출?) 시점에, 우리는 이전 쓰레드를 이제막 전환했고, 새 스레드는
    이미 실행중이며, 인터럽트는 여전히 비활성화 되어있다.
   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.
   스레드전환이 끝나기 전에 printf를 호출하는건 안전치않다. 이 말은 함수의 끝에
   printf() 를 추가하라는 뜻이다.

   ready큐에서 이 쓰레드가 CPU로 들어가서 실행하는것! */
static void
thread_launch(struct thread *th)
{
    uint64_t tf_cur = (uint64_t)&running_thread()->tf;
    uint64_t tf = (uint64_t)&th->tf;
    ASSERT(intr_get_level() == INTR_OFF);

    /* The main switching logic.
    메인 문맥전환 로직
     * We first restore the whole execution context into the intr_frame
     먼저, 전체 실행문맥을 intr_frame에 저장한다.
     * and then switching to the next thread by calling do_iret.
     그리고 다음 스레드를 do_iret를 호출함으로써 switch한다.
     * Note that, we SHOULD NOT use any stack from here
     여기서는 문맥전환이 일어나기 까지 어떠한 stack도 써서는 안된다.
     * until switching is done. */
    __asm __volatile(
        /* Store registers that will be used.
         */

        "push %%rax\n"
        "push %%rbx\n"
        "push %%rcx\n"
        /* Fetch input once */
        "movq %0, %%rax\n"
        "movq %1, %%rcx\n"
        "movq %%r15, 0(%%rax)\n"
        "movq %%r14, 8(%%rax)\n"
        "movq %%r13, 16(%%rax)\n"
        "movq %%r12, 24(%%rax)\n"
        "movq %%r11, 32(%%rax)\n"
        "movq %%r10, 40(%%rax)\n"
        "movq %%r9, 48(%%rax)\n"
        "movq %%r8, 56(%%rax)\n"
        "movq %%rsi, 64(%%rax)\n"
        "movq %%rdi, 72(%%rax)\n"
        "movq %%rbp, 80(%%rax)\n"
        "movq %%rdx, 88(%%rax)\n"
        "pop %%rbx\n" // Saved rcx
        "movq %%rbx, 96(%%rax)\n"
        "pop %%rbx\n" // Saved rbx
        "movq %%rbx, 104(%%rax)\n"
        "pop %%rbx\n" // Saved rax
        "movq %%rbx, 112(%%rax)\n"
        "addq $120, %%rax\n"
        "movw %%es, (%%rax)\n"
        "movw %%ds, 8(%%rax)\n"
        "addq $32, %%rax\n"
        "call __next\n" // read the current rip.
        "__next:\n"
        "pop %%rbx\n"
        "addq $(out_iret -  __next), %%rbx\n"
        "movq %%rbx, 0(%%rax)\n" // rip
        "movw %%cs, 8(%%rax)\n"  // cs
        "pushfq\n"
        "popq %%rbx\n"
        "mov %%rbx, 16(%%rax)\n" // eflags
        "mov %%rsp, 24(%%rax)\n" // rsp
        "movw %%ss, 32(%%rax)\n"
        "mov %%rcx, %%rdi\n"
        "call do_iret\n"
        "out_iret:\n"
        : : "g"(tf_cur), "g"(tf) : "memory");
}

/* Schedules a new process. At entry, interrupts must be off.
    새로운 프로세스를 스케줄링하라. 처음에 interrupts가 꺼져있어야한다.
 * This function modify current thread's status to status and then
    이 함수는 현재 스레드의 상태를 다른 상태로 조정하고
 * finds another thread to run and switches to it.
    cpu에 돌릴 다른 스레드를 찾아 그것으로 바꾼다.
 * It's not safe to call printf() in the schedule().
    schedule내에서 printf를 찍는건 안전치않다. */
static void
do_schedule(int status)
{
    ASSERT(intr_get_level() == INTR_OFF);
    ASSERT(thread_current()->status == THREAD_RUNNING);
    while (!list_empty(&destruction_req))
    {
        struct thread *victim =
            list_entry(list_pop_front(&destruction_req), struct thread, elem);
        palloc_free_page(victim);
    }
    thread_current()->status = status;
    schedule();
}

static void
schedule(void)
{
    struct thread *curr = running_thread();
    struct thread *next = next_priority_thread_to_run();

    ASSERT(intr_get_level() == INTR_OFF);
    ASSERT(curr->status != THREAD_RUNNING);
    ASSERT(is_thread(next));
    /* Mark us as running. */
    next->status = THREAD_RUNNING;

    /* Start new time slice.
    새로운 쓰레드틱스??*/
    thread_ticks = 0;

#ifdef USERPROG
    /* Activate the new address space. */
    process_activate(next);
#endif

    if (curr != next)
    {
        /* If the thread we switched from is dying, destroy its struct
           thread. This must happen late so that thread_exit() doesn't
           pull out the rug under itself.
           We just queuing the page free reqeust here because the page is
           currently used by the stack.
           The real destruction logic will be called at the beginning of the
           schedule(). */
        if (curr && curr->status == THREAD_DYING && curr != initial_thread)
        {
            ASSERT(curr != next);
            list_push_back(&destruction_req, &curr->elem);
        }

        /* Before switching the thread, we first save the information
         * of current running. */
        thread_launch(next);
    }
}

/* Returns a tid to use for a new thread.
   정적함수 : 스레드id를 반환하는데 이 id는 새 스레드 용 임.
   스레드에 tid를 할당. */
static tid_t
allocate_tid(void)
{
    static tid_t next_tid = 1; // 정적변수. 증가시키면서 할당할 스레드ID값
    tid_t tid;

    lock_acquire(&tid_lock); // tid들이 경쟁적으로 가져갈 lock키의 주소를 얻음
    tid = next_tid++;        //  tid에 다음tid 할당
    lock_release(&tid_lock); //

    return tid;
}
