#include "tasksys.h"


IRunnable::~IRunnable() {}

ITaskSystem::ITaskSystem(int num_threads) {}
ITaskSystem::~ITaskSystem() {}

/*
 * ================================================================
 * Serial task system implementation
 * ================================================================
 */

const char* TaskSystemSerial::name() {
    return "Serial";
}


TaskSystemSerial::TaskSystemSerial(int num_threads): ITaskSystem(num_threads) {
    //
    // TODO: CS149 student implementations may decide to perform setup
    // operations (such as thread pool construction) here.
    // Implementations are free to add new class member variables
    // (requiring changes to tasksys.h).
    //
    /*thread pool related:*/
    this->stop = false;
    this->num_threads = num_threads;
    this->thread_pool = new std::thread[num_threads];
    this->pthread_pool_deques.resize(num_threads);
    this->worker_threads_cvs.resize(num_threads);
    this->worker_threads_cv_muts.resize(num_threads);
    this->mask_for_deques.resize(num_threads);
    this->mask_for_deques_muts.resize(num_threads);
    //granularity luck for num_threads lucks
    this->granularity_lucks.resize(num_threads);
    for (int i = 0; i < num_threads; i++) {
        this->granularity_lucks[i] = new std::mutex();
        this->worker_threads_cvs[i] = new std::condition_variable();
        this->worker_threads_cv_muts[i] = new std::mutex();
        this->mask_for_deques_muts[i] = new std::mutex();
        this->mask_for_deques[i] = false;
        this->thread_pool[i] = std::thread(&TaskSystemSerial::thread_run, this, i);
    }

    /*task related:*/
    this->cur_num_total_tasks = 0;
    this->done_num = 0;
    this->cur_runable = nullptr;
}

TaskSystemSerial::~TaskSystemSerial() {
    //
    // TODO: CS149 student implementations may decide to perform cleanup
    // operations (such as thread pool shutdown construction) here.
    // Implementations are free to add new class member variables
    // (requiring changes to tasksys.h).
    //
    //thread pool related:
    this->stop = true;
    for (int i = 0; i < this->num_threads; i++) {
        /*ensure corresponding thread has sleep*/
        this->worker_threads_cv_muts[i]->lock();
        this->worker_threads_cv_muts[i]->unlock();
        this->worker_threads_cvs[i]->notify_one();
        this->thread_pool[i].join();
        delete this->granularity_lucks[i];
        delete this->worker_threads_cvs[i];
        delete this->worker_threads_cv_muts[i];
        delete this->mask_for_deques_muts[i];
    }
    delete[] this->thread_pool;

    /*task related:*/
}

bool TaskSystemSerial::check_corresponding_deque_not_empty(int index) {
    this->granularity_lucks[index]->lock();
    bool res = mask_for_deques[index] ? false : !pthread_pool_deques[index].empty();
    this->granularity_lucks[index]->unlock();
    return res || this->stop;
}

void TaskSystemSerial::thread_run(int i) {
    const int cur_thread_id = i;
    std::unique_lock<std::mutex> lk(*(this->worker_threads_cv_muts[cur_thread_id]));
    while (true) {
        this->worker_threads_cvs[cur_thread_id]->wait(lk, 
              std::bind(&TaskSystemSerial::check_corresponding_deque_not_empty, this, cur_thread_id));
        if (this->stop) {
            return;
        }
        this->granularity_lucks[i]->lock();
    //critical section begin
        /*1. get cur_index and update deque*/
        int cur_index = pthread_pool_deques[i].front();
        /*update shared var: ith pthread_pool_deques*/
        pthread_pool_deques[i].pop_front();
    //critical section end
        this->granularity_lucks[i]->unlock();

        /*2. lanch cur_index task*/
        this->cur_runable->runTask(cur_index, this->cur_num_total_tasks);

        /*3. update shared var: done_num*/
        this->mut_done_num.lock();
    //critical section begin
        this->done_num++;
        if (this->cur_num_total_tasks == this->done_num) {
    //critical section end
            this->mut_done_num.unlock();
            /*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/
            //this lock is necessary to ensure the main thread has gone to sleep
            /*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/
            this->main_thread_cv_mut.lock();
            this->main_thread_cv_mut.unlock();
            /*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/
            //this lock is necessary to ensure the main thread has gone to sleep
            /*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/
            this->main_thread_cv.notify_one();
#ifdef DEBUG
            printf("thread %d notify main thread\n", cur_thread_id);
            fflush(stdout);
#endif
        } else {
            this->mut_done_num.unlock();
        }
#ifdef DEBUG
        this->DEBUG_PRINT.lock();
        printf("thread %d finish %d task\n", cur_thread_id, cur_index);
        fflush(stdout);
        this->DEBUG_PRINT.unlock();
#endif
    }
    lk.unlock();
}


void TaskSystemSerial::run(IRunnable* runnable, int num_total_tasks) {


    //
    // TODO: CS149 students will modify the implementation of this
    // method in Parts A and B.  The implementation provided below runs all
    // tasks sequentially on the calling thread.
    //

    for (int i = 0; i < num_total_tasks; i++) {
        runnable->runTask(i, num_total_tasks);
    }
}


TaskID TaskSystemSerial::runAsyncWithDeps(IRunnable* runnable, int num_total_tasks,
                                          const std::vector<TaskID>& deps) {
    //shared var luck
    std::unique_lock<std::mutex> lk(this->main_thread_cv_mut);

    //update
    this->cur_runable = runnable;
    this->cur_num_total_tasks = num_total_tasks;

    //alocate tasks
    for (int i = 0; i < num_total_tasks; i++) {
        int deque_index = i % (this->num_threads);
        /*update shared var: ith granularity_lucks*/
        this->granularity_lucks[deque_index]->lock();
        //critical section begin
        /*push_back task and notify corresponding thread if deque empty*/
        bool used_empty = pthread_pool_deques[deque_index].empty();
        if (used_empty) {
            this->mask_for_deques_muts[deque_index]->lock();
            this->mask_for_deques[deque_index] = true;
            this->mask_for_deques_muts[deque_index]->unlock();
        }
        pthread_pool_deques[deque_index].push_back(i);
        //critical section end
        this->granularity_lucks[deque_index]->unlock();
        if (used_empty) {
            /*ensure corresponding thread has sleep*/
            this->worker_threads_cv_muts[deque_index]->lock();
            this->worker_threads_cv_muts[deque_index]->unlock();
            this->mask_for_deques_muts[deque_index]->lock();
            this->mask_for_deques[deque_index] = false;
            this->mask_for_deques_muts[deque_index]->unlock();
            this->worker_threads_cvs[deque_index]->notify_one();
        }
    }
    /*wait until done*/
    this->main_thread_cv.wait(lk);
    this->done_num = 0;
    lk.unlock();
    return 0;
}

void TaskSystemSerial::sync() {
    return;
}

/*
 * ================================================================
 * Parallel Task System Implementation
 * ================================================================
 */

const char* TaskSystemParallelSpawn::name() {
    return "Parallel + Always Spawn";
}


TaskSystemParallelSpawn::TaskSystemParallelSpawn(int num_threads): ITaskSystem(num_threads) {
    //
    // TODO: CS149 student implementations may decide to perform setup
    // operations (such as thread pool construction) here.
    // Implementations are free to add new class member variables
    // (requiring changes to tasksys.h).
    //
    /*thread pool related:*/
    this->stop = false;
    this->num_threads = num_threads;
    this->thread_pool = new std::thread[num_threads];
    this->pthread_pool_deques.resize(num_threads);
    this->worker_threads_cvs.resize(num_threads);
    this->worker_threads_cv_muts.resize(num_threads);
    this->mask_for_deques.resize(num_threads);
    this->mask_for_deques_muts.resize(num_threads);
    //granularity luck for num_threads lucks
    this->granularity_lucks.resize(num_threads);
    for (int i = 0; i < num_threads; i++) {
        this->granularity_lucks[i] = new std::mutex();
        this->worker_threads_cvs[i] = new std::condition_variable();
        this->worker_threads_cv_muts[i] = new std::mutex();
        this->mask_for_deques_muts[i] = new std::mutex();
        this->mask_for_deques[i] = false;
        this->thread_pool[i] = std::thread(&TaskSystemParallelSpawn::thread_run, this, i);
    }

    /*task related:*/
    this->cur_num_total_tasks = 0;
    this->done_num = 0;
    this->cur_runable = nullptr;
}

TaskSystemParallelSpawn::~TaskSystemParallelSpawn() {
    //
    // TODO: CS149 student implementations may decide to perform cleanup
    // operations (such as thread pool shutdown construction) here.
    // Implementations are free to add new class member variables
    // (requiring changes to tasksys.h).
    //
    //thread pool related:
    this->stop = true;
    for (int i = 0; i < this->num_threads; i++) {
        /*ensure corresponding thread has sleep*/
        this->worker_threads_cv_muts[i]->lock();
        this->worker_threads_cv_muts[i]->unlock();
        this->worker_threads_cvs[i]->notify_one();
        this->thread_pool[i].join();
        delete this->granularity_lucks[i];
        delete this->worker_threads_cvs[i];
        delete this->worker_threads_cv_muts[i];
        delete this->mask_for_deques_muts[i];
    }
    delete[] this->thread_pool;

    /*task related:*/
}

bool TaskSystemParallelSpawn::check_corresponding_deque_not_empty(int index) {
    this->granularity_lucks[index]->lock();
    bool res = mask_for_deques[index] ? false : !pthread_pool_deques[index].empty();
    this->granularity_lucks[index]->unlock();
    return res || this->stop;
}

void TaskSystemParallelSpawn::thread_run(int i) {
    const int cur_thread_id = i;
    std::unique_lock<std::mutex> lk(*(this->worker_threads_cv_muts[cur_thread_id]));
    while (true) {
        this->worker_threads_cvs[cur_thread_id]->wait(lk, 
              std::bind(&TaskSystemParallelSpawn::check_corresponding_deque_not_empty, this, cur_thread_id));
        if (this->stop) {
            return;
        }
        this->granularity_lucks[i]->lock();
    //critical section begin
        /*1. get cur_index and update deque*/
        int cur_index = pthread_pool_deques[i].front();
        /*update shared var: ith pthread_pool_deques*/
        pthread_pool_deques[i].pop_front();
    //critical section end
        this->granularity_lucks[i]->unlock();

        /*2. lanch cur_index task*/
        this->cur_runable->runTask(cur_index, this->cur_num_total_tasks);

        /*3. update shared var: done_num*/
        this->mut_done_num.lock();
    //critical section begin
        this->done_num++;
        if (this->cur_num_total_tasks == this->done_num) {
    //critical section end
            this->mut_done_num.unlock();
            /*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/
            //this lock is necessary to ensure the main thread has gone to sleep
            /*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/
            this->main_thread_cv_mut.lock();
            this->main_thread_cv_mut.unlock();
            /*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/
            //this lock is necessary to ensure the main thread has gone to sleep
            /*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/
            this->main_thread_cv.notify_one();
#ifdef DEBUG
            printf("thread %d notify main thread\n", cur_thread_id);
            fflush(stdout);
#endif
        } else {
            this->mut_done_num.unlock();
        }
#ifdef DEBUG
        this->DEBUG_PRINT.lock();
        printf("thread %d finish %d task\n", cur_thread_id, cur_index);
        fflush(stdout);
        this->DEBUG_PRINT.unlock();
#endif
    }
    lk.unlock();
}

void TaskSystemParallelSpawn::run(IRunnable* runnable, int num_total_tasks) {


    //
    // TODO: CS149 students will modify the implementation of this
    // method in Parts A and B.  The implementation provided below runs all
    // tasks sequentially on the calling thread.
    //

    for (int i = 0; i < num_total_tasks; i++) {
        runnable->runTask(i, num_total_tasks);
    }
}


TaskID TaskSystemParallelSpawn::runAsyncWithDeps(IRunnable* runnable, int num_total_tasks,
                                                 const std::vector<TaskID>& deps) {
    // NOTE: CS149 students are not expected to implement TaskSystemParallelSpawn in Part B.
    //shared var luck
    std::unique_lock<std::mutex> lk(this->main_thread_cv_mut);

    //update
    this->cur_runable = runnable;
    this->cur_num_total_tasks = num_total_tasks;

    //alocate tasks
    for (int i = 0; i < num_total_tasks; i++) {
        int deque_index = i % (this->num_threads);
        /*update shared var: ith granularity_lucks*/
        this->granularity_lucks[deque_index]->lock();
        //critical section begin
        /*push_back task and notify corresponding thread if deque empty*/
        bool used_empty = pthread_pool_deques[deque_index].empty();
        if (used_empty) {
            this->mask_for_deques_muts[deque_index]->lock();
            this->mask_for_deques[deque_index] = true;
            this->mask_for_deques_muts[deque_index]->unlock();
        }
        pthread_pool_deques[deque_index].push_back(i);
        //critical section end
        this->granularity_lucks[deque_index]->unlock();
        if (used_empty) {
            /*ensure corresponding thread has sleep*/
            this->worker_threads_cv_muts[deque_index]->lock();
            this->worker_threads_cv_muts[deque_index]->unlock();
            this->mask_for_deques_muts[deque_index]->lock();
            this->mask_for_deques[deque_index] = false;
            this->mask_for_deques_muts[deque_index]->unlock();
            this->worker_threads_cvs[deque_index]->notify_one();
        }
    }
    /*wait until done*/
    this->main_thread_cv.wait(lk);
    this->done_num = 0;
    lk.unlock();
    return 0;
}

void TaskSystemParallelSpawn::sync() {
    // NOTE: CS149 students are not expected to implement TaskSystemParallelSpawn in Part B.
    return;
}

/*
 * ================================================================
 * Parallel Thread Pool Spinning Task System Implementation
 * ================================================================
 */

const char* TaskSystemParallelThreadPoolSpinning::name() {
    return "Parallel + Thread Pool + Spin";
}


TaskSystemParallelThreadPoolSpinning::TaskSystemParallelThreadPoolSpinning(int num_threads): ITaskSystem(num_threads) {
    //
    // TODO: CS149 student implementations may decide to perform setup
    // operations (such as thread pool construction) here.
    // Implementations are free to add new class member variables
    // (requiring changes to tasksys.h).
    //
    /*thread pool related:*/
    this->stop = false;
    this->num_threads = num_threads;
    this->thread_pool = new std::thread[num_threads];
    this->pthread_pool_deques.resize(num_threads);
    this->worker_threads_cvs.resize(num_threads);
    this->worker_threads_cv_muts.resize(num_threads);
    this->mask_for_deques.resize(num_threads);
    this->mask_for_deques_muts.resize(num_threads);
    //granularity luck for num_threads lucks
    this->granularity_lucks.resize(num_threads);
    for (int i = 0; i < num_threads; i++) {
        this->granularity_lucks[i] = new std::mutex();
        this->worker_threads_cvs[i] = new std::condition_variable();
        this->worker_threads_cv_muts[i] = new std::mutex();
        this->mask_for_deques_muts[i] = new std::mutex();
        this->mask_for_deques[i] = false;
        this->thread_pool[i] = std::thread(&TaskSystemParallelThreadPoolSpinning::thread_run, this, i);
    }

    /*task related:*/
    this->cur_num_total_tasks = 0;
    this->done_num = 0;
    this->cur_runable = nullptr;
}

TaskSystemParallelThreadPoolSpinning::~TaskSystemParallelThreadPoolSpinning() {
    //
    // TODO: CS149 student implementations may decide to perform cleanup
    // operations (such as thread pool shutdown construction) here.
    // Implementations are free to add new class member variables
    // (requiring changes to tasksys.h).
    //
    //thread pool related:
    this->stop = true;
    for (int i = 0; i < this->num_threads; i++) {
        /*ensure corresponding thread has sleep*/
        this->worker_threads_cv_muts[i]->lock();
        this->worker_threads_cv_muts[i]->unlock();
        this->worker_threads_cvs[i]->notify_one();
        this->thread_pool[i].join();
        delete this->granularity_lucks[i];
        delete this->worker_threads_cvs[i];
        delete this->worker_threads_cv_muts[i];
        delete this->mask_for_deques_muts[i];
    }
    delete[] this->thread_pool;

    /*task related:*/
}

bool TaskSystemParallelThreadPoolSpinning::check_corresponding_deque_not_empty(int index) {
    this->granularity_lucks[index]->lock();
    bool res = mask_for_deques[index] ? false : !pthread_pool_deques[index].empty();
    this->granularity_lucks[index]->unlock();
    return res || this->stop;
}

void TaskSystemParallelThreadPoolSpinning::thread_run(int i) {
    const int cur_thread_id = i;
    std::unique_lock<std::mutex> lk(*(this->worker_threads_cv_muts[cur_thread_id]));
    while (true) {
        this->worker_threads_cvs[cur_thread_id]->wait(lk, 
              std::bind(&TaskSystemParallelThreadPoolSpinning::check_corresponding_deque_not_empty, this, cur_thread_id));
        if (this->stop) {
            return;
        }
        this->granularity_lucks[i]->lock();
    //critical section begin
        /*1. get cur_index and update deque*/
        int cur_index = pthread_pool_deques[i].front();
        /*update shared var: ith pthread_pool_deques*/
        pthread_pool_deques[i].pop_front();
    //critical section end
        this->granularity_lucks[i]->unlock();

        /*2. lanch cur_index task*/
        this->cur_runable->runTask(cur_index, this->cur_num_total_tasks);

        /*3. update shared var: done_num*/
        this->mut_done_num.lock();
    //critical section begin
        this->done_num++;
        if (this->cur_num_total_tasks == this->done_num) {
    //critical section end
            this->mut_done_num.unlock();
            /*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/
            //this lock is necessary to ensure the main thread has gone to sleep
            /*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/
            this->main_thread_cv_mut.lock();
            this->main_thread_cv_mut.unlock();
            /*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/
            //this lock is necessary to ensure the main thread has gone to sleep
            /*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/
            this->main_thread_cv.notify_one();
#ifdef DEBUG
            printf("thread %d notify main thread\n", cur_thread_id);
            fflush(stdout);
#endif
        } else {
            this->mut_done_num.unlock();
        }
#ifdef DEBUG
        this->DEBUG_PRINT.lock();
        printf("thread %d finish %d task\n", cur_thread_id, cur_index);
        fflush(stdout);
        this->DEBUG_PRINT.unlock();
#endif
    }
    lk.unlock();
}

void TaskSystemParallelThreadPoolSpinning::run(IRunnable* runnable, int num_total_tasks) {


    //
    // TODO: CS149 students will modify the implementation of this
    // method in Parts A and B.  The implementation provided below runs all
    // tasks sequentially on the calling thread.
    //

    for (int i = 0; i < num_total_tasks; i++) {
        runnable->runTask(i, num_total_tasks);
    }
}

TaskID TaskSystemParallelThreadPoolSpinning::runAsyncWithDeps(IRunnable* runnable, int num_total_tasks,
                                                              const std::vector<TaskID>& deps) {
    // NOTE: CS149 students are not expected to implement TaskSystemParallelThreadPoolSpinning in Part B.
    //shared var luck
    std::unique_lock<std::mutex> lk(this->main_thread_cv_mut);

    //update
    this->cur_runable = runnable;
    this->cur_num_total_tasks = num_total_tasks;

    //alocate tasks
    for (int i = 0; i < num_total_tasks; i++) {
        int deque_index = i % (this->num_threads);
        /*update shared var: ith granularity_lucks*/
        this->granularity_lucks[deque_index]->lock();
        //critical section begin
        /*push_back task and notify corresponding thread if deque empty*/
        bool used_empty = pthread_pool_deques[deque_index].empty();
        if (used_empty) {
            this->mask_for_deques_muts[deque_index]->lock();
            this->mask_for_deques[deque_index] = true;
            this->mask_for_deques_muts[deque_index]->unlock();
        }
        pthread_pool_deques[deque_index].push_back(i);
        //critical section end
        this->granularity_lucks[deque_index]->unlock();
        if (used_empty) {
            /*ensure corresponding thread has sleep*/
            this->worker_threads_cv_muts[deque_index]->lock();
            this->worker_threads_cv_muts[deque_index]->unlock();
            this->mask_for_deques_muts[deque_index]->lock();
            this->mask_for_deques[deque_index] = false;
            this->mask_for_deques_muts[deque_index]->unlock();
            this->worker_threads_cvs[deque_index]->notify_one();
        }
    }
    /*wait until done*/
    this->main_thread_cv.wait(lk);
    this->done_num = 0;
    lk.unlock();
    return 0;
}

void TaskSystemParallelThreadPoolSpinning::sync() {
    // NOTE: CS149 students are not expected to implement TaskSystemParallelThreadPoolSpinning in Part B.
    return;
}

/*
 * ================================================================
 * Parallel Thread Pool Sleeping Task System Implementation
 * ================================================================
 */

const char* TaskSystemParallelThreadPoolSleeping::name() {
    return "Parallel + Thread Pool + Sleep";
}

TaskSystemParallelThreadPoolSleeping::TaskSystemParallelThreadPoolSleeping(int num_threads): ITaskSystem(num_threads) {
    //
    // TODO: CS149 student implementations may decide to perform setup
    // operations (such as thread pool construction) here.
    // Implementations are free to add new class member variables
    // (requiring changes to tasksys.h).
    //

    /*allocate index*/
    this->allocate_task_id = 0;
    this->allocate_deque_id = 0;
    /*thread pool related:*/
    this->stop = false;
    this->num_threads = num_threads;
    this->thread_pool.resize(num_threads);
    this->thread_pool_deques.resize(num_threads);

#ifdef DEBUG2
    this->thread_pool_deques_finished.resize(num_threads);
#endif

    this->thread_pool_deques_locks.resize(num_threads);    //granularity lock for num_threads locks
    this->worker_threads_cvs.resize(num_threads);
    this->worker_threads_cv_muts.resize(num_threads);
    this->deques_is_wait.resize(num_threads);
    // this->mask_for_deques.resize(num_threads);
    // this->mask_for_deques_muts.resize(num_threads);
    for (int i = 0; i < num_threads; i++) {
        this->thread_pool_deques_locks[i] = new std::mutex();
        this->worker_threads_cvs[i] = new std::condition_variable();
        this->worker_threads_cv_muts[i] = new std::mutex();
        // this->mask_for_deques_muts[i] = new std::mutex();
        // this->mask_for_deques[i] = false;
        this->deques_is_wait[i] = false;
        this->thread_pool[i] = std::thread(&TaskSystemParallelThreadPoolSleeping::thread_run, this, i);
    }
}

TaskSystemParallelThreadPoolSleeping::~TaskSystemParallelThreadPoolSleeping() {
    //
    // TODO: CS149 student implementations may decide to perform cleanup
    // operations (such as thread pool shutdown construction) here.
    // Implementations are free to add new class member variables
    // (requiring changes to tasksys.h).
    //
    //thread pool related:
    this->stop = true;
    for (int i = 0; i < this->num_threads; i++) {
        this->worker_threads_cv_muts[i]->lock();
        this->worker_threads_cv_muts[i]->unlock();
        this->worker_threads_cvs[i]->notify_one();
        this->thread_pool[i].join();
        delete this->thread_pool_deques_locks[i];
        delete this->worker_threads_cvs[i];
        delete this->worker_threads_cv_muts[i];
        // delete this->mask_for_deques_muts[i];
    }
    /*task related:*/
}

void TaskSystemParallelThreadPoolSleeping::run(IRunnable* runnable, int num_total_tasks) {


    //
    // TODO: CS149 students will modify the implementation of this
    // method in Parts A and B.  The implementation provided below runs all
    // tasks sequentially on the calling thread.
    //

    for (int i = 0; i < num_total_tasks; i++) {
        runnable->runTask(i, num_total_tasks);
    }
}

bool TaskSystemParallelThreadPoolSleeping::judge_is_allDone() {
    all_tasks_mut.lock();
    bool res = task_runables.empty();
    all_tasks_mut.unlock();
    return res;
}

bool TaskSystemParallelThreadPoolSleeping::check_corresponding_deque_not_empty(int index) {
    this->thread_pool_deques_locks[index]->lock();
    //bool res = mask_for_deques[index] ? false : !thread_pool_deques[index].empty();
    bool res = !thread_pool_deques[index].empty();
    bool res_s = res || this->stop;
    this->deques_is_wait[index] = res_s ? 0 : 1;
    this->thread_pool_deques_locks[index]->unlock();
    return res_s;
}

void TaskSystemParallelThreadPoolSleeping::thread_run(int i) {
    const int cur_thread_id = i;
    std::unique_lock<std::mutex> lk(*(this->worker_threads_cv_muts[cur_thread_id]));
    lk.unlock();
    while (true) {
        lk.lock();
        this->worker_threads_cvs[cur_thread_id]->wait(lk, 
              std::bind(&TaskSystemParallelThreadPoolSleeping::check_corresponding_deque_not_empty, this, cur_thread_id));
        lk.unlock();
        if (this->stop) {
            return;
        }
        /*1. take one small task from  deque*/
    //critical section begin
        this->thread_pool_deques_locks[cur_thread_id]->lock();
        DequeElemType DequeElem_top = thread_pool_deques[cur_thread_id].front();
        thread_pool_deques[cur_thread_id].pop_front();
        this->thread_pool_deques_locks[cur_thread_id]->unlock();
    //critical section end
        int cur_index = DequeElem_top.cur_tid;
        int cur_taskid = DequeElem_top.Taskid;

        /*2. lanch cur_index task*/
        this->all_tasks_mut.lock();
        int cur_total_num = this->task_total_nums.find(cur_taskid)->second;
        this->all_tasks_mut.unlock();
        this->task_runables.find(cur_taskid)->second->runTask(cur_index, cur_total_num);

        /*3. update shared var: done_num*/
    //critical section begin
        this->all_tasks_mut.lock();
        this->task_done_nums_pmuts.find(cur_taskid)->second->lock();
        int cur_done_num = ++(this->task_done_nums.find(cur_taskid)->second);
        this->task_done_nums_pmuts.find(cur_taskid)->second->unlock();
        this->all_tasks_mut.unlock();
    //critical section end
        if (cur_total_num == cur_done_num) {//finish current TASK:
#ifdef DEBUG2
            thread_pool_deques_finished[i].push_front(DequeElem_top);
            printf("thread %d has finished task %d!\n", i, cur_taskid);
#endif
            /*1.kick out cur_TASK*/
        //critical section begin
            running_tasks_mut.lock();
            this->running_tasks.erase(cur_taskid);
            running_tasks_mut.unlock();
        //critical section end
        /*---------------------------*/
        //critical section begin
            this->all_tasks_mut.lock();
            this->task_total_nums.erase(cur_taskid);
            this->task_done_nums.erase(cur_taskid);
            this->task_runables.erase(cur_taskid);
            delete this->task_done_nums_pmuts.find(cur_taskid)->second;
            this->task_done_nums_pmuts.erase(cur_taskid);
            this->all_tasks_mut.unlock();
        //critical section end
            if (judge_is_allDone() && is_sync) {
                this->main_thread_cv_mut.lock();
                this->main_thread_cv_mut.unlock();
                this->main_thread_cv.notify_one();
                continue; // go to sleep, wait for next task
            }
            /*2.wake up possible cur_TASK*/
            std::vector<TaskID> tobe_started;//could run after checking
            std::map<TaskID, std::vector<TaskID>*>::iterator iter;
            this->waiting_task_mut.lock();
            for (iter = waiting_tasks.begin(); iter != waiting_tasks.end(); iter++) {
                bool is_runnable = true;
                std::vector<TaskID>* cur_set = iter->second;
                for (TaskID iter2 : *cur_set) {
                    if (!judge_is_done(iter2)) {
                        is_runnable = false;
                        break;
                    }
                }
                if (is_runnable) {
                    tobe_started.push_back(iter->first);
                }
            }
            for (auto& index : tobe_started) {
            //critical section begin
                //waiting_task_mut.lock();
                delete waiting_tasks.find(index)->second;
                waiting_tasks.erase(index);
                //waiting_task_mut.unlock();
            //critical section end
            /*-------------------------------*/
            //critical section begin
                running_tasks_mut.lock();
                running_tasks.insert(index);
                running_tasks_mut.unlock();
            //critical section end
            }
            this->waiting_task_mut.unlock();
            for (auto& index : tobe_started) {
                this->all_tasks_mut.lock();
                int num_total_tasks = task_total_nums.find(index)->second;
                this->all_tasks_mut.unlock();
                /*allocate index_th task*/
                this->allocate_task(index, num_total_tasks, true, cur_thread_id);
            }
        } 
    }
    lk.unlock();
}

bool   TaskSystemParallelThreadPoolSleeping::judge_is_done(TaskID id) {
    this->running_tasks_mut.lock();
    bool res = task_runables.find(id) == task_runables.end();
    this->running_tasks_mut.unlock();
    return res;
}

TaskID TaskSystemParallelThreadPoolSleeping::runAsyncWithDeps(IRunnable* runnable, int num_total_tasks,
                                                    const std::vector<TaskID>& deps) {
    /*1. get cur task index*/
    this->allocate_task_id_mut.lock();
    int cur_task_allocateid = allocate_task_id;
    allocate_task_id++;
    this->allocate_task_id_mut.unlock();
    /*2. judge if need added to waiting queue*/
    
    bool is_wait = false;
    for (auto i : deps) {
        if (!judge_is_done(i)) {
            is_wait = true;
            break;
        }
    }
    all_tasks_mut.lock();
    task_runables.insert(std::pair<TaskID, IRunnable*>(cur_task_allocateid, runnable));
    task_total_nums.insert(std::pair<TaskID, TaskTotalNumType>(cur_task_allocateid, num_total_tasks));
    task_done_nums.insert(std::pair<TaskID, TaskDoneNumType>(cur_task_allocateid, 0));
    task_done_nums_pmuts.insert(std::pair<TaskID, std::mutex*>(cur_task_allocateid,
                                                               new std::mutex()));
    all_tasks_mut.unlock();
    /*3. if add to waiting tasks*/
    if (is_wait) {
        std::vector<TaskID>* waiting_set = new std::vector<TaskID>();
        for (auto& i : deps) {
            waiting_set->push_back(i);
        }
        /*put into wait*/
        waiting_task_mut.lock();
        waiting_tasks.insert(std::pair<TaskID, std::vector<TaskID>*>(cur_task_allocateid,
                             waiting_set));//may deadlock
        waiting_task_mut.unlock();
        return cur_task_allocateid;
    }
    /*4. run directly*/
    running_tasks_mut.lock();
    running_tasks.insert(cur_task_allocateid);
    running_tasks_mut.unlock();

    /*alocate cur task*/
    this->allocate_task(cur_task_allocateid, num_total_tasks);
    
    return cur_task_allocateid;
}

void TaskSystemParallelThreadPoolSleeping::allocate_task(int taskid, int total, bool is_mask, int selfid) {
    /*alocate cur task*/
    for (int i = 0; i < total; i++) {
        allocate_deque_id_mut.lock();
        int deque_index = allocate_deque_id;
        allocate_deque_id = (allocate_deque_id + 1) % this->num_threads;
        allocate_deque_id_mut.unlock();
        /* 4.1 update shared var: ith thread_pool_deques */
        DequeElemType cur_elem = {i, taskid};
    //critical section begin
        this->thread_pool_deques_locks[deque_index]->lock();
        thread_pool_deques[deque_index].push_back(cur_elem);
        this->thread_pool_deques_locks[deque_index]->unlock();
    //critical section end
        /* 4.2 and notify corresponding thread if sleep*/
        this->worker_threads_cv_muts[deque_index]->lock();
        if (this->deques_is_wait[deque_index] && (is_mask ? deque_index != selfid : true)) {
            this->worker_threads_cv_muts[deque_index]->unlock();
            this->worker_threads_cvs[deque_index]->notify_one();
        } else {
            this->worker_threads_cv_muts[deque_index]->unlock();
        }
    }
}

void TaskSystemParallelThreadPoolSleeping::sync() {

    //shared var lock
    std::unique_lock<std::mutex> lk(this->main_thread_cv_mut);
    if (this->judge_is_allDone()) {
        return;
    } else {
        is_sync = true;
        if (this->judge_is_allDone()) {
            is_sync = false;
            return;
        }
    }
    /*wait until done*/
    this->main_thread_cv.wait(lk);
    is_sync = false;
    lk.unlock();
}
