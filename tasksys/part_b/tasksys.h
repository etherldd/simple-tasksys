#ifndef _TASKSYS_H
#define _TASKSYS_H


//#define DEBUG2

#include "itasksys.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <deque>
#include <set>
#include <map>
#include <iostream>
#include <chrono>
#include <unistd.h>
#include <assert.h>
#include <vector>
/*
 * TaskSystem: This class is the student's
 * optimized implementation of a parallel task execution engine that uses
 * a thread pool. See definition of ITaskSystem in
 * itasksys.h for documentation of the ITaskSystem interface.
 */
typedef struct {
    int cur_tid;
    int Taskid;
} DequeElemType;

typedef int TaskTotalNumType;
typedef int TaskDoneNumType;

class TaskSystem: public ITaskSystem {
    private:
/*if ~TaskSystem : stop = true*/
        bool                                   stop;

/*thread pool related:*/
        int                                    num_threads; 
        std::vector<std::thread>               thread_pool;
        std::vector<std::deque<DequeElemType>> thread_pool_deques;
        std::vector<std::mutex*>               thread_pool_deques_locks;
#ifdef DEBUG2
        std::vector<std::deque<DequeElemType>> thread_pool_deques_finished;
#endif

/*task related(used): for mutiply task -> runAsyncWithDeps*/

    //for running tasks:
        std::set<TaskID>                      running_tasks;   //below's keys
        std::mutex                            running_tasks_mut;
    //for waiting tasks:
        std::mutex                            waiting_task_mut;
        std::map<TaskID, std::vector<TaskID>*>waiting_tasks;
    //for all tasks:   
        std::mutex                            all_tasks_mut;
        std::map<TaskID, IRunnable*>          task_runables;
        std::map<TaskID, TaskTotalNumType>    task_total_nums; 
        std::map<TaskID, TaskDoneNumType>     task_done_nums;  
        std::map<TaskID, std::mutex*>         task_done_nums_pmuts;
        bool                                  judge_is_done(TaskID id);
        bool                                  judge_is_allDone();
    //for randomly allocate elem:
        std::mutex                            allocate_deque_id_mut;
        int                                   allocate_deque_id;
        std::mutex                            allocate_task_id_mut;
        unsigned int                          allocate_task_id; 

/*sleep related:*/
        std::mutex                            main_thread_cv_mut;
        std::condition_variable               main_thread_cv;
        std::vector<std::mutex*>              worker_threads_cv_muts;
        std::vector<std::condition_variable*> worker_threads_cvs;

        //helper for solve deadluck
        std::vector<int>                      deques_is_wait;
        bool                                  check_corresponding_deque_not_empty(int index);

/*sync related:*/
        bool                                  is_sync;
    public:
        TaskSystem(int num_threads);
        ~TaskSystem();
        const char*                          name();
        void                                 run(IRunnable* runnable, int num_total_tasks);
        void                                 allocate_task(int taskid, int total, bool is_mask=false, int selfid=0);
        TaskID                               runAsyncWithDeps(IRunnable* runnable, int num_total_tasks,
                                                                    const std::vector<TaskID>& deps);
        void                                 sync();
        //void                                thread_run(int i);
        void                                 thread_run(int i) __attribute__ ((optimize(0)));
#ifdef DEBUG
        std::mutex DEBUG_PRINT;
#endif
};





#endif
