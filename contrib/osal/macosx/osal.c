/*
 * This software is dual-licensed under GPLv3 and a commercial
 * license. See the file LICENSE.md distributed with this software for
 * full license information.
 */

#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <osal.h>

#include <errno.h>
#include <stdio.h>

#define USECS_PER_SEC 1000000

// simulate clock_nanosleep
int clock_nanosleep_relative(const struct timespec *req)
{
   struct timespec rem = *req;
   int ret;

   // EINTR
   while ((ret = nanosleep(&rem, &rem)) == -1 && errno == EINTR)
   {
      printf("sleep interrupted, left %ld secs %ld nsecs\n", rem.tv_sec, rem.tv_nsec);
   }

   return ret;
}

/* Returns time from some unspecified moment in past,
 * strictly increasing, used for time intervals measurement. */
void osal_get_monotonic_time(ec_timet *ts)
{
   /* Use clock_gettime to prevent possible live-lock.
    * Gettimeofday uses CLOCK_REALTIME that can get NTP timeadjust.
    * If this function preempts timeadjust and it uses vpage it live-locks.
    * Also when using XENOMAI, only clock_gettime is RT safe */
   clock_gettime(CLOCK_MONOTONIC, ts);
}

int osal_monotonic_sleep(ec_timet *ts)
{
   int result;
   result = clock_nanosleep_relative(ts);
   return result == 0 ? 0 : -1;
}

int osal_usleep(uint32 tv_usec)
{
   struct timespec ts;
   ts.tv_sec = tv_usec / USECS_PER_SEC;
   ts.tv_nsec = (tv_usec % USECS_PER_SEC) * 1000;
   /* usleep is deprecated, use nanosleep instead */
   return nanosleep(&ts, NULL);
}

int osal_gettimeofday(struct timeval *tv, struct timezone *tz)
{
   struct timespec ts;
   int return_value;
   (void)tz; /* Not used */

   /* Use clock_gettime to prevent possible live-lock.
    * Gettimeofday uses CLOCK_REALTIME that can get NTP timeadjust.
    * If this function preempts timeadjust and it uses vpage it live-locks.
    * Also when using XENOMAI, only clock_gettime is RT safe */
   return_value = clock_gettime(CLOCK_MONOTONIC, &ts);
   tv->tv_sec = ts.tv_sec;
   tv->tv_usec = ts.tv_nsec / 1000;
   return return_value;
}

ec_timet osal_current_time(void)
{
   struct timeval current_time;
   ec_timet return_value;

   osal_gettimeofday(&current_time, 0);
   return_value.tv_sec = current_time.tv_sec;
   return_value.tv_nsec = current_time.tv_usec / 1000;
   return return_value;
}

void osal_time_diff(ec_timet *start, ec_timet *end, ec_timet *diff)
{
   if (end->tv_nsec < start->tv_nsec)
   {
      diff->tv_sec = end->tv_sec - start->tv_sec - 1;
      diff->tv_nsec = end->tv_nsec + 1000000000 - start->tv_nsec;
   }
   else
   {
      diff->tv_sec = end->tv_sec - start->tv_sec;
      diff->tv_nsec = end->tv_nsec - start->tv_nsec;
   }
}

void osal_timer_start(osal_timert *self, uint32 timeout_usec)
{
   struct timeval start_time;
   struct timeval timeout;
   struct timeval stop_time;

   osal_gettimeofday(&start_time, 0);
   timeout.tv_sec = timeout_usec / USECS_PER_SEC;
   timeout.tv_usec = timeout_usec % USECS_PER_SEC;
   timeradd(&start_time, &timeout, &stop_time);

   self->stop_time.tv_sec = stop_time.tv_sec;
   self->stop_time.tv_nsec = stop_time.tv_usec * 1000;
}

boolean osal_timer_is_expired(osal_timert *self)
{
   struct timeval current_time;
   struct timeval stop_time;
   int is_not_yet_expired;

   osal_gettimeofday(&current_time, 0);
   stop_time.tv_sec = self->stop_time.tv_sec;
   stop_time.tv_usec = self->stop_time.tv_nsec / 1000;
   is_not_yet_expired = timercmp(&current_time, &stop_time, <);

   return is_not_yet_expired == FALSE;
}

void *osal_malloc(size_t size)
{
   return malloc(size);
}

void osal_free(void *ptr)
{
   free(ptr);
}

int osal_thread_create(void *thandle, int stacksize, void *func, void *param)
{
   int ret;
   pthread_attr_t attr;
   pthread_t *threadp;

   threadp = thandle;
   pthread_attr_init(&attr);
   pthread_attr_setstacksize(&attr, stacksize);
   ret = pthread_create(threadp, &attr, func, param);
   if (ret < 0)
   {
      printf("[osal_thread_create] failed to create thread\n");
      return 0;
   }
   return 1;
}

int osal_thread_create_rt(void *thandle, int stacksize, void *func, void *param)
{
   int ret;
   pthread_attr_t attr;
   struct sched_param schparam;
   pthread_t *threadp;

   threadp = thandle;
   pthread_attr_init(&attr);
   pthread_attr_setstacksize(&attr, stacksize);
   ret = pthread_create(threadp, &attr, func, param);
   pthread_attr_destroy(&attr);
   if (ret < 0)
   {
      printf("[osal_thread_create_rt] failed to create thread\n");
      return 0;
   }
   memset(&schparam, 0, sizeof(schparam));
   schparam.sched_priority = 40;
   ret = pthread_setschedparam(*threadp, SCHED_FIFO, &schparam);
   if (ret < 0)
   {
      printf("[pthread_create] failed to set priority\n");
      return 0;
   }

   return 1;
}

void *osal_mutex_create(void)
{
   pthread_mutexattr_t mutexattr;
   osal_mutext *mutex;
   mutex = (osal_mutext *)osal_malloc(sizeof(osal_mutext));
   if (mutex)
   {
      pthread_mutexattr_init(&mutexattr);
      pthread_mutexattr_setprotocol(&mutexattr, PTHREAD_PRIO_INHERIT);
      pthread_mutex_init(mutex, &mutexattr);
   }
   return (void *)mutex;
}

void osal_mutex_destroy(void *mutex)
{
   pthread_mutex_destroy((osal_mutext *)mutex);
   osal_free(mutex);
}

void osal_mutex_lock(void *mutex)
{
   pthread_mutex_lock((osal_mutext *)mutex);
}

void osal_mutex_unlock(void *mutex)
{
   pthread_mutex_unlock((osal_mutext *)mutex);
}
