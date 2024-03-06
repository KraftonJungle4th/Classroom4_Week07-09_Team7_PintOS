#include "include/lib/kernel/list.h"
#include "threads/thread.h"
#include <stdbool.h>
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "devices/timer.h"
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "intrinsic.h"

#include "devices/timer.h"

#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details.

   struct thread의 'magic' 멤버에 대한 랜덤 값입니다.
   스택 오버플로우를 감지하는 데 사용됩니다.
   자세한 내용은 thread.h 맨 위의 큰 주석을 참조하십시오. */
#define THREAD_MAGIC 0xcd6abf4b

/* Random value for basic thread
   Do not modify this value.

   기본 쓰레드에 대한 랜덤 값입니다.
   이 값을 수정하지 마십시오. */
#define THREAD_BASIC 0xd42df210

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running.

   THREAD_READY 상태의 프로세스 목록입니다.
   즉, 실행 준비가 된 프로세스이지만 실제로 실행되지는 않습니다. */
static struct list ready_list;

/* List of processes in THREAD_SLEEP state, that is, processes
   that are sleeping.

   THREAD_SLEEP 상태의 프로세스 목록입니다.
   즉, 슬립 상태인 프로세스입니다. */
static struct list sleep_list;

/* Idle thread.
   유휴 상태의 쓰레드*/
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main().
   초기화 쓰레드, init.c의 main()함수를 실행시키는 쓰레드임*/
static struct thread *initial_thread;

/* Lock used by allocate_tid().
   allocate_tid()에 의해 사용되는 Lock */
static struct lock tid_lock;

/* Thread destruction requests
   쓰레드 파괴에 대한 요청 */
static struct list destruction_req;

/* Statistics.
   통계 */
static long long idle_ticks;   /* # of timer ticks spent idle. 유휴 상태에서 보낸 타이머 틱 수 */
static long long kernel_ticks; /* # of timer ticks in kernel threads. 커널 상태에서 보낸 타이머 틱 수*/
static long long user_ticks;   /* # of timer ticks in user programs. 유저 프로그램에서 보낸 타이머 틱 수*/

/* Scheduling.

   스케쥴링*/
#define TIME_SLICE 4		  /* # of timer ticks to give each thread. 스레드에 할당된 타이머 틱 수 */
static unsigned thread_ticks; /* # of timer ticks since last yield. 마지막 양보 이후의 타이머 틱 수 */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs".

   만약 false(기본 설정)이면, RR 스케쥴러 사용
   만야 true면 멀티 레벨 피드백 큐 스케쥴러(MLFQ) 사용
   커널의 커맨드라인 옵션인 "-o mlfqs"에 의해 조정된다*/
bool thread_mlfqs;

static void kernel_thread(thread_func *, void *aux);

static void idle(void *aux UNUSED);
static struct thread *next_thread_to_run(void);
static void init_thread(struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule(void);

static tid_t allocate_tid(void);
static bool less(const struct list_elem *a, const struct list_elem *b, void *aux);
// static bool larger(const struct list_elem *a, const struct list_elem *b, void *aux);

/* Returns true if T appears to point to a valid thread.
   만약 T가 유효한 쓰레드이면 true 반환*/
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* Returns the running thread.
 * Read the CPU's stack pointer `rsp', and then round that
 * down to the start of a page.  Since `struct thread' is
 * always at the beginning of a page and the stack pointer is
 * somewhere in the middle, this locates the curent thread.
 *
 * 실행중인 쓰레드를 반환함
 * CPU의 스택 포인터인 'rsp'를 읽고, 그 다음 페이지의 시작으로 반올림함
 * 'struct thread'는 항상 페이지의 시작에 있고,
 * 스택 포인터는 중간에 있기 때문에 현재 쓰레드를 찾을 수 있음
 * */
#define running_thread() ((struct thread *)(pg_round_down(rrsp())))

/* Global descriptor table for the thread_start.
 * Because the gdt will be setup after the thread_init, we should
 * setup temporal gdt first.
 *
 * thread_start를 위한 전역 descriptor 테이블
 * gdt는 thread_init 이후에 설정될 것이기 때문에, 먼저 임시 gdt를 설정해야 함
 * */
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

   현재 실행 중인 코드를 스레드로 변환하여 스레드 시스템을 초기화합니다.
   이것은 일반적으로 작동하지 않으며,
   이 경우에만 가능한 것은 loader.S가 스택의 맨 아래를 페이지 경계에 두기 위해 주의를 기울였기 때문입니다.

   또한 실행 큐와 tid lock을 초기화합니다.

   이 함수를 호출한 후에는 thread_create()로 스레드를 생성하기 전에 페이지 할당기를 초기화해야 합니다.

   이 함수가 끝날 때까지 thread_current()를 호출하는 것은 안전하지 않습니다.
   */

bool less(const struct list_elem *a, const struct list_elem *b, void *aux)
{
	struct thread *ta = list_entry(a, struct thread, elem);
	struct thread *tb = list_entry(b, struct thread, elem);

	return ta->wakeup_tick < tb->wakeup_tick;
}

bool larger(const struct list_elem *a, const struct list_elem *b, void *aux)
{
	struct thread *ta = list_entry(a, struct thread, elem);
	struct thread *tb = list_entry(b, struct thread, elem);

	return ta->priority > tb->priority; // >
}

void thread_init(void)
{
	ASSERT(intr_get_level() == INTR_OFF);

	/* Reload the temporal gdt for the kernel
	 * This gdt does not include the user context.
	 * The kernel will rebuild the gdt with user context, in gdt_init ().
	 *
	 * 커널을 위해 임시 gdt를 다시 로드합니다.
	 * 이 gdt에는 사용자 컨텍스트가 포함되어 있지 않습니다.
	 * 커널은 사용자 컨텍스트가 포함된 gdt를 gdt_init()에서 다시 만들 것입니다. */
	struct desc_ptr gdt_ds = {
		.size = sizeof(gdt) - 1,
		.address = (uint64_t)gdt};
	lgdt(&gdt_ds);

	/* Init the global thread context
	   전역 스레드 컨텍스트 초기화 */
	lock_init(&tid_lock);
	list_init(&ready_list);
	list_init(&sleep_list);
	list_init(&destruction_req);

	/* Set up a thread structure for the running thread.
	   실행중인 쓰레드를 위한 쓰레드 구조를 설정 */
	initial_thread = running_thread();
	init_thread(initial_thread, "main", PRI_DEFAULT);
	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread.

   인터럽트를 활성화하여 선점형 스레드 스케줄링을 시작합니다.
   또한 유휴 스레드를 생성합니다.
   */
void thread_start(void)
{
	/* Create the idle thread.
	   유휴 스레드를 생성합니다. */
	struct semaphore idle_started;
	sema_init(&idle_started, 0);
	thread_create("idle", PRI_MIN, idle, &idle_started);

	/* Start preemptive thread scheduling.
	   선점형 스레드 스케줄링을 시작합니다. */
	intr_enable();

	/* Wait for the idle thread to initialize idle_thread.
	   유휴 스레드가 idle_thread를 초기화 하기를 기다립니다. */
	sema_down(&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context.

   매 타이머 틱마다 타이머 인터럽트 핸들러에 의해 호출됩니다.
   따라서 이 함수는 외부 인터럽트 컨텍스트에서 실행됩니다.
	*/
void thread_tick(void)
{
	struct thread *t = thread_current();

	/* Update statistics.
	   상태정보 업데이트 */
	if (t == idle_thread)
		idle_ticks++;
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else
		kernel_ticks++;

	/* Enforce preemption.
	   선점 실행 */
	if (++thread_ticks >= TIME_SLICE)
		intr_yield_on_return();
}

/* Prints thread statistics.
   쓰레드 통계 출력 */
void thread_print_stats(void)
{
	printf("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
		   idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3.

   NAME이라는 이름의 새로운 커널 스레드를 생성하고, 초기 우선순위로 PRIORITY를 사용하여
   FUNCTION을 실행하고 AUX를 인수로 전달하고 준비 큐에 추가합니다.
   새 스레드의 스레드 식별자를 반환하거나 생성에 실패하면 TID_ERROR를 반환합니다.

   thread_start()가 호출된 경우 새 스레드는 thread_create()가 반환되기 전에 예약될 수 있습니다.
   심지어 thread_create()가 반환되기 전에 종료될 수도 있습니다.
   그 반대의 경우에는 새 스레드가 예약되기 전에 원래 스레드가 실행될 수 있습니다.
   순서를 보장해야 하는 경우 세마포어 또는 다른 형태의 동기화를 사용하십시오.

   제공된 코드는 새 스레드의 '우선순위' 멤버를 PRIORITY로 설정하지만 실제 우선순위 스케줄링은 구현되지 않았습니다.
   우선순위 스케줄링은 문제 1-3의 목표입니다.
   */
tid_t thread_create(const char *name, int priority,
					thread_func *function, void *aux)
{
	struct thread *t;
	tid_t tid;

	ASSERT(function != NULL);

	/* Allocate thread.
	   쓰레드 할당 */
	t = palloc_get_page(PAL_ZERO);
	if (t == NULL)
		return TID_ERROR;

	/* Initialize thread.
	   쓰레드 초기화 */
	init_thread(t, name, priority);
	tid = t->tid = allocate_tid();

	/* Call the kernel_thread if it scheduled.
	 * Note) rdi is 1st argument, and rsi is 2nd argument.
	 *
	 * 스케줄되면 kernel_thread를 호출합니다.
	 * 참고) rdi는 첫 번째 인수이고, rsi는 두 번째 인수입니다. */

	t->tf.rip = (uintptr_t)kernel_thread;
	t->tf.R.rdi = (uint64_t)function;
	t->tf.R.rsi = (uint64_t)aux;
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;

	/* Add to run queue.
	   실행 큐에 추가 */
	thread_unblock(t);
	struct thread *curr = running_thread();
	int cur_priority = curr->priority;

	if (list_entry(list_front(&ready_list), struct thread, elem)->priority > cur_priority)
	{
		thread_yield();
	}

	return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h.

   현재 스레드를 슬립 상태로 전환합니다.
   스레드가 thread_unblock()에 의해 깨어날 때까지 다시 예약되지 않습니다.

   이 함수는 인터럽트가 꺼진 상태에서 호출해야 합니다.
   보통 synch.h의 동기화 기본형 중 하나를 사용하는 것이 더 좋은 생각입니다.
   */
void thread_block(void)
{
	ASSERT(!intr_context());
	ASSERT(intr_get_level() == INTR_OFF);
	thread_current()->status = THREAD_BLOCKED;
	schedule();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data.

   스레드 T를 블록된 상태에서 실행 준비 상태로 전환합니다.
   T가 블록되지 않은 경우 오류입니다. (실행 중인 스레드를 준비 상태로 만들려면 thread_yield()를 사용하십시오.)

   이 함수는 실행 중인 스레드를 선점하지 않습니다. 이것은 중요할 수 있습니다:
   호출자가 직접 인터럽트를 비활성화했다면
   스레드를 원자적으로 블록 해제하고 다른 데이터를 업데이트할 수 있다고 예상할 수 있습니다.
   */
void thread_unblock(struct thread *t)
{
	enum intr_level old_level;

	ASSERT(is_thread(t));
	old_level = intr_disable();
	ASSERT(t->status == THREAD_BLOCKED);
	// list_push_back(&ready_list, &t->elem);
	list_insert_ordered(&ready_list, &t->elem, (list_less_func *)larger, NULL);
	t->status = THREAD_READY;

	intr_set_level(old_level);
}

/* Returns the name of the running thread.
   실행 중인 스레드의 이름을 반환합니다. */
const char *thread_name(void)
{
	return thread_current()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details.

   실행 중인 스레드를 반환합니다.
   이것은 running_thread()에 몇 가지 안전 검사가 추가된 것입니다.
   자세한 내용은 thread.h 맨 위의 큰 주석을 참조하십시오. */
struct thread *
thread_current(void)
{
	struct thread *t = running_thread();

	/* Make sure T is really a thread.
	   If either of these assertions fire, then your thread may
	   have overflowed its stack.  Each thread has less than 4 kB
	   of stack, so a few big automatic arrays or moderate
	   recursion can cause stack overflow.

	   T가 실제로 스레드인지 확인하십시오.
	   이 assertions 중 하나라도 발생하면 스레드가 스택을 오버플로했을 수 있습니다.
	   각 스레드는 4 kB 미만의 스택을 가지므로
	   큰 자동 배열이나 중간 정도의 재귀는 스택 오버플로를 일으킬 수 있습니다. */
	ASSERT(is_thread(t));
	ASSERT(t->status == THREAD_RUNNING);

	return t;
}

void thread_sleep(int64_t ticks)
{
	struct thread *curr = thread_current();
	enum intr_level old_level;

	old_level = intr_disable();
	if (curr != idle_thread)
	{
		curr->status = THREAD_BLOCKED;
		curr->wakeup_tick = ticks;
		list_insert_ordered(&sleep_list, &curr->elem, (list_less_func *)less, NULL);
	}
	schedule();
	intr_set_level(old_level);
}

void thread_wakeup(int64_t ticks)
{
	if (list_empty(&sleep_list))
		return;

	enum intr_level old_level;
	struct thread *to_wakeup = list_entry(list_front(&sleep_list), struct thread, elem);

	old_level = intr_disable();
	while (to_wakeup->wakeup_tick <= ticks)
	{
		list_pop_front(&sleep_list);
		list_insert_ordered(&ready_list, &to_wakeup->elem, (list_less_func *)larger, NULL);
		to_wakeup->status = THREAD_READY;
		if (list_empty(&sleep_list))
			return;
		to_wakeup = list_entry(list_front(&sleep_list), struct thread, elem);
	}

	intr_set_level(old_level);
}

/* Returns the running thread's tid.
   실행 중인 스레드의 tid를 반환합니다. */
tid_t thread_tid(void)
{
	return thread_current()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller.

   현재 스레드를 스케쥴 해제하고 파괴합니다.
   절대 호출자에게 반환되지 않습니다. */
void thread_exit(void)
{
	ASSERT(!intr_context());

#ifdef USERPROG
	process_exit();
#endif

	/* Just set our status to dying and schedule another process.
	   We will be destroyed during the call to schedule_tail().

	   그저 우리의 상태를 dying으로 설정하고 다른 프로세스를 스케줄합니다.
	   schedule_tail() 호출 중에 우리는 파괴될 것입니다. */

	intr_disable();
	do_schedule(THREAD_DYING);
	NOT_REACHED();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim.

   CPU를 양보합니다. 현재 스레드는 슬립 상태로 전환되지 않으며
   스케줄러의 변덕에 따라 즉시 다시 예약될 수 있습니다.*/
void thread_yield(void)
{
	struct thread *curr = thread_current();
	enum intr_level old_level;

	ASSERT(!intr_context());

	old_level = intr_disable();
	if (curr != idle_thread)
	{
		// list_push_back(&ready_list, &curr->elem);
		list_insert_ordered(&ready_list, &curr->elem, larger, NULL);
	}

	do_schedule(THREAD_READY);
	intr_set_level(old_level);
}

/* Sets the current thread's priority to NEW_PRIORITY.
   현재 스레드의 우선순위를 NEW_PRIORITY로 설정합니다. */
void thread_set_priority(int new_priority)
{
	struct thread *curr = thread_current();
	curr->original = curr->priority = new_priority;
	printf("new prio: %d\n", new_priority);
	if (!list_empty(&curr->donations))
	{
		curr->priority = list_entry(list_front(&curr->donations), struct thread, elem)
							 ->priority;
		printf("don pri:%d\n", curr->priority);
	}

	if (list_empty(&ready_list))
		return;

	list_sort(&ready_list, larger, NULL);

	struct thread *top_pri_th = list_entry(list_front(&ready_list), struct thread, elem);
	int top_priority = top_pri_th->priority;

	if (top_priority > curr->priority)
	{
		thread_yield();
	}
}

/* Returns the current thread's priority.
   현재 스레드의 우선순위를 반환합니다. */
int thread_get_priority(void)
{
	printf("tid: %d pri: %d\n", thread_current()->tid, thread_current()->priority);
	return thread_current()->priority;
}

/* Sets the current thread's nice value to NICE.
   현재 스레드의 nice 값을 NICE로 설정합니다. */
void thread_set_nice(int nice UNUSED)
{
	/* TODO: Your implementation goes here */
}

/* Returns the current thread's nice value.
   현재 스레드의 nice 값을 반환합니다. */
int thread_get_nice(void)
{
	/* TODO: Your implementation goes here */
	return 0;
}

/* Returns 100 times the system load average.
   시스템 로드 평균의 100배를 반환합니다. */
int thread_get_load_avg(void)
{
	/* TODO: Your implementation goes here */
	// load_avg = (59 / 60) * load_avg + (1 / 60) * ready_threads;
	return 0;
}

/* Returns 100 times the current thread's recent_cpu value.
   현재 스레드의 recent_cpu 값을 100배한 값을 반환합니다. */
int thread_get_recent_cpu(void)
{
	/* TODO: Your implementation goes here */
	return 0;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty.

   유휴 스레드. 다른 스레드가 실행 준비 상태가 아닐 때 실행됩니다.

   유휴 스레드는 초기에 thread_start()에 의해 준비 목록에 넣습니다.
   초기에 한 번 예약됩니다. 이때 idle_thread를 초기화하고,
   thread_start()가 계속할 수 있도록 전달된 세마포어를 "up"하고 즉시 블록됩니다.
   그 후로 유휴 스레드는 준비 목록에 나타나지 않습니다.
   준비 목록이 비어 있는 경우 next_thread_to_run()에 의해 특수한 경우로 반환됩니다. */
static void
idle(void *idle_started_ UNUSED)
{
	struct semaphore *idle_started = idle_started_;

	idle_thread = thread_current();
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
		   7.11.1 "HLT Instruction".

		   다시 인터럽트를 활성화하고 다음 인터럽트를 기다립니다.

		   'sti' 명령은 다음 명령의 완료까지 인터럽트를 비활성화하므로
		   이 두 명령은 원자적으로 실행됩니다. 이 원자성은 중요합니다.
		   그렇지 않으면 인터럽트가 다시 활성화되고 다음 인터럽트를 기다리는 동안
		   인터럽트가 처리될 수 있어서 최대 한 클럭 틱의 시간이 낭비될 수 있습니다.

		   [IA32-v2a] "HLT", [IA32-v2b] "STI", [IA32-v3a]
		   7.11.1 "HLT Instruction" 참조 */
		asm volatile("sti; hlt" : : : "memory");
	}
}

/* Function used as the basis for a kernel thread.
   커널 스레드의 기초로 사용되는 함수입니다. */
static void
kernel_thread(thread_func *function, void *aux)
{
	ASSERT(function != NULL);

	intr_enable(); /* The scheduler runs with interrupts off. 스케쥴러가 인터럽트 없이 실행됩니다. */
	function(aux); /* Execute the thread function. 스레드의 기능을 실행시킵니다. */
	thread_exit(); /* If function() returns, kill the thread. 만약 function()이 반환되면, 스레드를 죽입니다. */
}

/* Does basic initialization of T as a blocked thread named NAME.
   T를 NAME이라는 이름의 블록된 스레드로 기본 초기화합니다. */
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
	t->original = priority;
	// printf("init pri %d  ori %d\n", t->priority, t->original);
	t->magic = THREAD_MAGIC;
	list_init(&t->donations);
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread.

   스케줄링할 다음 스레드를 선택하고 반환합니다.
   실행 큐에서 스레드를 반환해야 합니다. 실행 큐가 비어 있지 않은 한
   (실행 중인 스레드가 계속 실행할 수 있다면 실행 큐에 있을 것입니다.)
   실행 큐가 비어 있는 경우 idle_thread를 반환합니다. */
static struct thread *
next_thread_to_run(void)
{
	if (list_empty(&ready_list))
		return idle_thread;
	else
		return list_entry(list_pop_front(&ready_list), struct thread, elem);
}

/* Use iretq to launch the thread
   스레드를 시작하기 위해 iretq를 사용합니다. */
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

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   새 스레드의 페이지 테이블을 활성화하여 스레드를 전환하고,
   이전 스레드가 죽는 중이라면 파괴합니다.

   이 함수가 호출될 때, 우리는 스레드 PREV에서 방금 전환했으며,
   새 스레드가 이미 실행 중이고 인터럽트가 아직 비활성화되어 있습니다.

   스레드 전환이 완료될 때까지 printf()를 호출하는 것은 안전하지 않습니다.
   실제로는 printf()를 함수의 끝에 추가해야 합니다. */
static void
thread_launch(struct thread *th)
{
	uint64_t tf_cur = (uint64_t)&running_thread()->tf;
	uint64_t tf = (uint64_t)&th->tf;
	ASSERT(intr_get_level() == INTR_OFF);

	/* The main switching logic.
	 * We first restore the whole execution context into the intr_frame
	 * and then switching to the next thread by calling do_iret.
	 * Note that, we SHOULD NOT use any stack from here
	 * until switching is done.
	 *
	 * switching 하는 main 로직입니다.
	 * 먼저 전체 실행 컨텍스트를 intr_frame에 복원한 다음 do_iret를 호출하여 다음 스레드로 전환합니다.
	 * 여기서 switching이 완료될 때까지는 스택을 사용해서는 안 됩니다. */

	__asm __volatile(
		/* Store registers that will be used. */
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
		"movw %%cs, 8(%%rax)\n"	 // cs
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
 * This function modify current thread's status to status and then
 * finds another thread to run and switches to it.
 * It's not safe to call printf() in the schedule().
 *
 * 새로운 프로세스를 스케쥴합니다. 진입 시 인터럽트가 꺼져 있어야 합니다.
 * 이 함수는 현재 스레드의 상태를 status로 수정한 다음 다른 스레드를 찾아 실행하고 전환합니다.
 * schedule()에서 printf()를 호출하는 것은 안전하지 않습니다. */
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
	struct thread *next = next_thread_to_run();

	ASSERT(intr_get_level() == INTR_OFF);
	ASSERT(curr->status != THREAD_RUNNING);
	ASSERT(is_thread(next));
	/* Mark us as running. */
	next->status = THREAD_RUNNING;

	/* Start new time slice. */
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
		   schedule().

		   만약 우리가 전환한 스레드가 죽는 중이라면, 그 스레드의 struct thread를 파괴합니다.
		   thread_exit()가 자신의 rug under을 빼앗지 않도록 이것은 늦게 발생해야 합니다.

		   여기서는 페이지가 현재 스택에 의해 사용되고 있기 때문에 페이지 파괴 요청을 큐잉만 합니다.
		   실제 파괴 로직은 schedule()의 시작 부분에서 호출될 것입니다. */

		if (curr && curr->status == THREAD_DYING && curr != initial_thread)
		{
			ASSERT(curr != next);
			list_push_back(&destruction_req, &curr->elem);
		}

		/* Before switching the thread, we first save the information of current running.
		 * 스레드를 바꾸기 전에, 먼저 현재 실행 중인 정보를 저장합니다. */
		thread_launch(next);
	}
}

/* Returns a tid to use for a new thread.
   새 스레드에 사용할 tid를 반환합니다. */
static tid_t
allocate_tid(void)
{
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire(&tid_lock);
	tid = next_tid++;
	lock_release(&tid_lock);

	return tid;
}