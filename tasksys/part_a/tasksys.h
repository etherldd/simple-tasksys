#ifndef _TASKSYS_H
#define _TASKSYS_H

//#define DEBUG2

#include "itasksys.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <deque>
#include <iostream>
#include <chrono>
#include <unistd.h>
#include <assert.h>
#include <vector>
/*
 * TaskSystemSerial: This class is the student's implementation of a
 * serial task execution engine.  See definition of ITaskSystem in
 * itasksys.h for documentation of the ITaskSystem interface.
 */
class TaskSystemSerial: public ITaskSystem {
    public:
        TaskSystemSerial(int num_threads);
        ~TaskSystemSerial();
        const char* name();
        void run(IRunnable* runnable, int num_total_tasks);
        TaskID runAsyncWithDeps(IRunnable* runnable, int num_total_tasks,
                                const std::vector<TaskID>& deps);
        void sync();
};

/*
 * TaskSystemParallelSpawn: This class is the student's implementation of a
 * parallel task execution engine that spawns threads in every run()
 * call.  See definition of ITaskSystem in itasksys.h for documentation
 * of the ITaskSystem interface.
 */
class TaskSystemParallelSpawn: public ITaskSystem {
    private:
        int num_threads; //max
    public:
        TaskSystemParallelSpawn(int num_threads);
        ~TaskSystemParallelSpawn();
        const char* name();
        void run(IRunnable* runnable, int num_total_tasks);
        TaskID runAsyncWithDeps(IRunnable* runnable, int num_total_tasks,
                                const std::vector<TaskID>& deps);
        void sync();
        void thread_run(int i, int num_total_tasks, IRunnable* runnable);
};

/*
 * TaskSystemParallelThreadPoolSpinning: This class is the student's
 * implementation of a parallel task execution engine that uses a
 * thread pool. See definition of ITaskSystem in itasksys.h for
 * documentation of the ITaskSystem interface.
 */
class TaskSystemParallelThreadPoolSpinning: public ITaskSystem {
    private:
        //thread pool related:
        int          num_threads; 
        std::thread* thread_pool;
        bool         stop;
        std::vector<std::deque<int>> pthread_pool_deques;
        std::vector<std::mutex*> granularity_lucks;

        //task related:
        int cur_num_total_tasks;
        int done_num;
        std::mutex mut_update_done;
        IRunnable*  cur_runable;

        //helper func:
    public:
        TaskSystemParallelThreadPoolSpinning(int num_threads);
        ~TaskSystemParallelThreadPoolSpinning();
        const char* name();
        void run(IRunnable* runnable, int num_total_tasks) __attribute__ ((optimize(0)));
        //void run(IRunnable* runnable, int num_total_tasks);    
        TaskID runAsyncWithDeps(IRunnable* runnable, int num_total_tasks,
                                const std::vector<TaskID>& deps);
        void sync();
        void thread_run(int i) __attribute__ ((optimize(0)));
        //void thread_run(int i);
#ifdef DEBUG
        std::mutex DEBUG_PRINT;
#endif
};

/*
 * TaskSystemParallelThreadPoolSleeping: This class is the student's
 * optimized implementation of a parallel task execution engine that uses
 * a thread pool. See definition of ITaskSystem in
 * itasksys.h for documentation of the ITaskSystem interface.
 */
class TaskSystemParallelThreadPoolSleeping: public ITaskSystem {
    private:
        bool                         stop;
        /*thread pool related:*/
        int                          num_threads; 
        std::vector<std::thread>     thread_pool;
        std::vector<std::deque<int>> pthread_pool_deques;
        std::vector<std::mutex*>     granularity_lucks;

        /*task related:*/
        int             cur_num_total_tasks;
        int             done_num;
        IRunnable*      cur_runable;
        std::mutex      mut_done_num;

        /*sleep related:*/
        std::mutex                  main_thread_cv_mut;
        std::condition_variable     main_thread_cv;
        std::vector<std::mutex*>    worker_threads_cv_muts;
        std::vector<std::condition_variable*> worker_threads_cvs;
        std::vector<int>            deques_is_wait;     //???????????std::vector<bool> will fail???????????
        bool                        check_corresponding_deque_not_empty(int index);
        
    public:
        TaskSystemParallelThreadPoolSleeping(int num_threads);
        ~TaskSystemParallelThreadPoolSleeping();
        const char* name();
        void run(IRunnable* runnable, int num_total_tasks);
        //void run(IRunnable* runnable, int num_total_tasks) __attribute__ ((optimize(0)));
        TaskID runAsyncWithDeps(IRunnable* runnable, int num_total_tasks,
                                const std::vector<TaskID>& deps);
        void sync();
        void thread_run(int i);
        //void thread_run(int i) __attribute__ ((optimize(0)));
#ifdef DEBUG
        std::mutex DEBUG_PRINT;
#endif
};

#endif
