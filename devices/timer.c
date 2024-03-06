#include "devices/timer.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include "threads/interrupt.h"
#include "threads/io.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* See [8254] for hardware details of the 8254 timer chip. */

#if TIMER_FREQ < 19
#error 8254 timer requires TIMER_FREQ >= 19
#endif
#if TIMER_FREQ > 1000
#error TIMER_FREQ <= 1000 recommended
#endif

/* Number of timer ticks since OS booted.
  타이머의 누계가 저장된다. */
static int64_t ticks;

/* Number of loops per timer tick.
   Initialized by timer_calibrate().
   타이머틱 당 루프갯수 */
static unsigned loops_per_tick;

static intr_handler_func timer_interrupt;
static bool too_many_loops(unsigned loops);
static void busy_wait(int64_t loops);
static void real_time_sleep(int64_t num, int32_t denom);

/* Sets up the 8254 Programmable Interval Timer (PIT) to
   interrupt PIT_FREQ times per second, and registers the
   corresponding interrupt. */
void timer_init(void)
{
    /* 8254 input frequency divided by TIMER_FREQ, rounded to
       nearest. */
    uint16_t count = (1193180 + TIMER_FREQ / 2) / TIMER_FREQ;

    outb(0x43, 0x34); /* CW: counter 0, LSB then MSB, mode 2, binary. */
    outb(0x40, count & 0xff);
    outb(0x40, count >> 8);

    intr_register_ext(0x20, timer_interrupt, "8254 Timer");
}

/* Calibrates loops_per_tick, used to implement brief delays.
   짧은 딜레이를 구현하는 loops_per_tick을 보정이 아니고
   테스트용 프로그램을 돌리기위한 루프 그냥 생성? */
void timer_calibrate(void)
{
    unsigned high_bit, test_bit;

    ASSERT(intr_get_level() == INTR_ON);
    printf("Calibrating timer...  ");

    /* Approximate loops_per_tick as the largest power-of-two still less than
       one timer tick. 1u = unsigned int 인 1 */
    loops_per_tick = 1u << 10;
    while (!too_many_loops(loops_per_tick << 1))
    {
        loops_per_tick <<= 1;
        ASSERT(loops_per_tick != 0);
    }

    /* Refine the next 8 bits of loops_per_tick. */
    high_bit = loops_per_tick;
    for (test_bit = high_bit >> 1; test_bit != high_bit >> 10; test_bit >>= 1)
        if (!too_many_loops(high_bit | test_bit))
            loops_per_tick |= test_bit;

    printf("%'" PRIu64 " loops/s.\n", (uint64_t)loops_per_tick * TIMER_FREQ);
}

/* Returns the number of timer ticks since the OS booted.
    타이머의현재틱 (ticks) 반환*/
int64_t
timer_ticks(void)
{
    enum intr_level old_level = intr_disable();
    // 타이머 조정을 위해 또다른 침범(인터럽트) 방지
    int64_t t = ticks;
    intr_set_level(old_level);
    // 인터럽트 작업이 끝난 후, 이전 상태를 복원한다. 이는 여전히 OS단계일 수도
    // 응용계층으로 올라갈 수도 있다.

    barrier();
    return t;
}

/* Returns the number of timer ticks elapsed since THEN, which
   should be a value once returned by timer_ticks().
   return (타이머시작된이후로의 누계틱 - 인수) 을 함 (구간틱)*/
int64_t
timer_elapsed(int64_t then)
{
    return timer_ticks() - then;
}

/* Suspends execution for approximately TICKS timer ticks.
  얘를 조정하라. 지금은 busy waits = spin lock 이다.
  스레드가 자는거네! */
void timer_sleep(int64_t ticks)
{
    int64_t start = timer_ticks(); //  타이머 현재 틱을 이 함수인 timer_sleep이 실행된
    // 이후즉시의 start값으로 받음

    ASSERT(intr_get_level() == INTR_ON);
    while (timer_elapsed(start) < ticks) //  잘 시간이 남아 있으므로
        thread_yield();
    // //  다시 뒤로 들어가라.
}

/* 타이머가 스레드들을 스턴시키는 함수 */
void timer_stun(int64_t ticks)
{
    printf("thread 스턴!\n");
    int64_t start = timer_ticks();
    ASSERT(intr_get_level() == INTR_ON);

    // while문으로 구현해서 CPU를 계속 점유하던 걸, 간단한 코드로 잠깐만 쓰게!
    if (timer_elapsed(start) < ticks)
        thread_stun(start + ticks);
}

/* Suspends execution for approximately MS milliseconds. */
void timer_msleep(int64_t ms)
{
    real_time_sleep(ms, 1000);
}

/* Suspends execution for approximately US microseconds. */
void timer_usleep(int64_t us)
{
    real_time_sleep(us, 1000 * 1000);
}

/* Suspends execution for approximately NS nanoseconds. */
void timer_nsleep(int64_t ns)
{
    real_time_sleep(ns, 1000 * 1000 * 1000);
}

/* Prints timer statistics. */
void timer_print_stats(void)
{
    printf("Timer: %" PRId64 " ticks\n", timer_ticks());
}

/* Timer interrupt handler.
   타이머가 쓰레드를 밀치고 들어오는 타이머인터럽트 핸들러의 시작부분. 끝은 timer_sleep에 있다.*/
static void
timer_interrupt(struct intr_frame *args UNUSED)
// intr_frame : 전체 실행문맥을 저장할 수 있는 것. (thread.c)
{
    ticks++;
    thread_tick(); // 현재 쓰레드에 1 추가 (근데 단위가 signed long int64)
    timer_check_wakeup(ticks);
}

/* Returns true if LOOPS iterations waits for more than one timer
   tick, otherwise false.
   루프반복이 한개의 타이머틱 보다 더 많게 대기하면 (2개이상?) true반환.
    이게 루프를 너무 많이 돈 것 = 루프는 틱당 한번만 돌아라? */
static bool
too_many_loops(unsigned loops)
{
    /* Wait for a timer tick. */
    int64_t start = ticks; // 현 sys 시간의 틱누계값을 start로 삼고,
    while (ticks == start) // while과 바로윗줄의 명령이 순식간에 실행되어서, ticks값이
                           // 차이가 없다면 아무것도 안하긴 하는데 일단 작동하게 해라. 쨋든 ticks가 증가하면
                           // while문 탈출
        barrier();

    /* Run LOOPS loops.
        특정 루프를(loops) 반복(LOOPS) 한다. */
    start = ticks;    // ticks를 다시 start로 삼고
    busy_wait(loops); //  loops를 인자로 busy_wait를 해라?

    /* If the tick count changed, we iterated too long. */
    barrier();
    return start != ticks;
}

/* Iterates through a simple loop LOOPS times, for implementing
   brief delays.
    짧은 딜레이의 구현을 위해, 간단한 루프를 몇 번 반복(LOOPS)합니다.
   Marked NO_INLINE because code alignment can significantly
   affect timings, so that if this function was inlined
   differently in different places the results would be difficult
   to predict.
    코드 정렬이 타이밍에 상당한 영향을 미칠 수 있기 때문에 NO_INLINE을
    표시했습니다. 따라서 이 함수를 다른 위치에 다르게 표시하면 결과를 예측하기가
    어렵습니다. */
static void NO_INLINE
busy_wait(int64_t loops)
{
    while (loops-- > 0) // loops가 0인지 측정하고 라인나갈때 loops를 하나뺀다.
        barrier();      // barrier는 아무것도 안하므로, 계속 빼다가 loops가 0이되면 탈출한다.
    // 이것은 단지 딜레이의 구현을 위해 쓰여졌다?
}

/* Sleep for approximately NUM/DENOM seconds.
    대략 숫자/분모 초 만큼 잔다.*/
static void
real_time_sleep(int64_t num, int32_t denom)
{
    /* Convert NUM/DENOM seconds into timer ticks, rounding down.
        버림하여, 숫자/분모 초를 타이머의 틱 단위로 바꾼다.

       (NUM / DENOM) s
       ---------------------- = NUM * TIMER_FREQ / DENOM ticks.
       1 s / TIMER_FREQ(타이머가 초당 인터럽트 하는 횟수) ticks

       */
    int64_t ticks = num * TIMER_FREQ / denom;
    // ticks를 할당한다.

    ASSERT(intr_get_level() == INTR_ON); //  인터럽트가 켜져야 진행된다.
    if (ticks > 0)
    {
        /* We're waiting for at least one full timer tick.  Use
           timer_sleep() because it will yield the CPU to other
           processes.
           timer_sleep()을 써라. 그래야 CPU를 다른 프로세스들에게 양보한다. */
        timer_sleep(ticks);
    }
    else
    {
        /* Otherwise, use a busy-wait loop for more accurate
           sub-tick tim to ing.  We scale the numerator and denominator
           down by 1000avoid the possibility of overflow. */
        ASSERT(denom % 1000 == 0);
        busy_wait(loops_per_tick * num / 1000 * TIMER_FREQ / (denom / 1000));
    }
}
