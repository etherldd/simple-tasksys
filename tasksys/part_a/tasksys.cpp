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
}

TaskSystemSerial::~TaskSystemSerial() {}

void TaskSystemSerial::run(IRunnable* runnable, int num_total_tasks) {
    for (int i = 0; i < num_total_tasks; i++) {
        runnable->runTask(i, num_total_tasks);
    }
}

TaskID TaskSystemSerial::runAsyncWithDeps(IRunnable* runnable, int num_total_tasks,
                                          const std::vector<TaskID>& deps) {
    // You do not need to implement this method.
    return 0;
}

void TaskSystemSerial::sync() {
    // You do not need to implement this method.
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
    this->num_threads = num_threads;
}

TaskSystemParallelSpawn::~TaskSystemParallelSpawn() {}

void TaskSystemParallelSpawn::thread_run(int i, int num_total_tasks, IRunnable* runnable) {
    runnable->runTask(i, num_total_tasks);
}

void TaskSystemParallelSpawn::run(IRunnable* runnable, int num_total_tasks) {


    //
    // TODO: CS149 students will modify the implementation of this
    // method in Part A.  The implementation provided below runs all
    // tasks sequentially on the calling thread.
    //
    std::thread* workers = new std::thread[num_threads];
    int cur_unfinished = 0;
    int busy_num = 0;
    for (int i = 0; i < num_total_tasks; i++) {
        if (busy_num == num_threads) {
            workers[cur_unfinished % num_threads].join();
            busy_num--;
            cur_unfinished++;
        }
        //launch
        workers[i % (num_threads)] = std::thread(&TaskSystemParallelSpawn::thread_run, this, i, num_total_tasks, runnable);
        busy_num++;
    }
    for (int i = cur_unfinished; i < num_total_tasks; i++) {
        workers[i % (num_threads)].join();
    }
    delete[] workers;
}

TaskID TaskSystemParallelSpawn::runAsyncWithDeps(IRunnable* runnable, int num_total_tasks,
                                                 const std::vector<TaskID>& deps) {
    // You do not need to implement this method.
    return 0;
}

void TaskSystemParallelSpawn::sync() {
    // You do not need to implement this method.
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
    pthread_pool_deques.resize(num_threads);
    //granularity luck for num_threads lucks
    this->granularity_lucks.resize(num_threads);
    for (int i = 0; i < num_threads; i++) {
        this->granularity_lucks[i] = new std::mutex();
        this->thread_pool[i] = std::thread(&TaskSystemParallelThreadPoolSpinning::thread_run, this, i);
    }

    /*task related:*/
    this->cur_num_total_tasks = 0;
    this->done_num = 0;
    this->cur_runable = nullptr;
    
}

TaskSystemParallelThreadPoolSpinning::~TaskSystemParallelThreadPoolSpinning() {
    //thread pool related:
    this->stop = true;
    for (int i = 0; i < this->num_threads; i++) {
        this->thread_pool[i].join();
        delete this->granularity_lucks[i];
    }
    delete[] this->thread_pool;

    /*task related:*/
}



void TaskSystemParallelThreadPoolSpinning::thread_run(int i) {
    while (true) {
        if (this->stop) {
            return;
        }
        if (pthread_pool_deques[i].empty()) {
            continue;
        }

        /*1. lanch cur_index task*/

        int cur_index = pthread_pool_deques[i].front();
        /*update shared var: ith granularity_lucks*/
        this->granularity_lucks[i]->lock();
        //critical section begin
        pthread_pool_deques[i].pop_front();
        //critical section end
        this->granularity_lucks[i]->unlock();
        this->cur_runable->runTask(cur_index, this->cur_num_total_tasks);

        /*2. update shared var: done_num*/

        this->mut_update_done.lock();
        //critical section begin
        this->done_num++;
        //critical section end
        this->mut_update_done.unlock();

#ifdef DEBUG
        this->DEBUG_PRINT.lock();
        printf("thread %d finish %d task\n", i, cur_index);
        fflush(stdout);
        this->DEBUG_PRINT.unlock();
#endif
    }
}

void TaskSystemParallelThreadPoolSpinning::run(IRunnable* runnable, int num_total_tasks) {
    this->cur_runable = runnable;
    this->cur_num_total_tasks = num_total_tasks;
    this->mut_update_done.lock();
    this->done_num = 0;
    this->mut_update_done.unlock();

    //alocate tasks
    for (int i = 0; i < num_total_tasks; i++) {
        int deque_index = i % (this->num_threads);
        /*update shared var: ith granularity_lucks*/
        this->granularity_lucks[deque_index]->lock();
        //critical section begin
        pthread_pool_deques[deque_index].push_back(i);
        //critical section end
        this->granularity_lucks[deque_index]->unlock();
    }
    //this->mut_update_done.lock();
    while (this->done_num != this->cur_num_total_tasks) {
        // this->mut_update_done.unlock();
        // this->mut_update_done.lock();
    }
    //this->mut_update_done.unlock();
    return;
}

TaskID TaskSystemParallelThreadPoolSpinning::runAsyncWithDeps(IRunnable* runnable, int num_total_tasks,
                                                              const std::vector<TaskID>& deps) {
    // You do not need to implement this method.
    return 0;
}

void TaskSystemParallelThreadPoolSpinning::sync() {
    // You do not need to implement this method.
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
    /*thread pool related:*/
    this->stop = false;
    this->num_threads = num_threads;
    this->thread_pool.resize(num_threads);
    this->pthread_pool_deques.resize(num_threads);
    //granularity lock for num_threads lucks
    this->granularity_lucks.resize(num_threads);
    this->worker_threads_cvs.resize(num_threads);
    this->worker_threads_cv_muts.resize(num_threads);
    this->deques_is_wait.resize(num_threads);
    for (int i = 0; i < num_threads; i++) {
        this->granularity_lucks[i] = new std::mutex();
        this->worker_threads_cvs[i] = new std::condition_variable();
        this->worker_threads_cv_muts[i] = new std::mutex();
        this->deques_is_wait[i] = false;
        this->thread_pool[i] = std::thread(&TaskSystemParallelThreadPoolSleeping::thread_run, this, i);
    }

    /*task related:*/
    this->cur_num_total_tasks = 0;
    this->done_num = 0;
    this->cur_runable = nullptr;
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
        /*ensure corresponding thread has sleep*/
        this->worker_threads_cv_muts[i]->lock();
        this->worker_threads_cv_muts[i]->unlock();
        this->worker_threads_cvs[i]->notify_one();
        this->thread_pool[i].join();
        delete this->granularity_lucks[i];
        delete this->worker_threads_cvs[i];
        delete this->worker_threads_cv_muts[i];
    }
}

bool TaskSystemParallelThreadPoolSleeping::check_corresponding_deque_not_empty(int index) {
    this->granularity_lucks[index]->lock();
    bool res = !pthread_pool_deques[index].empty();
    bool res_s = res || this->stop;
    this->deques_is_wait[index] = res_s ? 0 : 1;
#ifdef DEBUG2
    int bool_res_s = res_s;
    int bool_deques_is_wait2 = this->deques_is_wait[index];
    printf("%d : res_s: %d, iswait: %d\n", index, bool_res_s, bool_deques_is_wait2);
    fflush(stdout);
#endif
    this->granularity_lucks[index]->unlock();
#ifdef DEBUG2
    if (!res_s) {
        int bool_deques_is_wait = this->deques_is_wait[index];
        printf("thread %d will go to sleep with deques_is_wait[%d] == %d\n", index, index, bool_deques_is_wait);
        fflush(stdout);
    } else {
        int bool_deques_is_wait = this->deques_is_wait[index];
        printf("thread %d will not go to sleep with deques_is_wait[%d] == %d\n", index, index, bool_deques_is_wait);
        fflush(stdout);
    }
#endif
    return res_s;
}

void TaskSystemParallelThreadPoolSleeping::thread_run(int i) {
    const int cur_thread_id = i;
    std::unique_lock<std::mutex> lk(*(this->worker_threads_cv_muts[cur_thread_id]));
    lk.unlock();
    while (true) {
        lk.lock();
        this->worker_threads_cvs[cur_thread_id]->wait(lk, 
              std::bind(&TaskSystemParallelThreadPoolSleeping::check_corresponding_deque_not_empty, this, i));
        lk.unlock();
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
    //lk.unlock();
}

void TaskSystemParallelThreadPoolSleeping::run(IRunnable* runnable, int num_total_tasks) {
    
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
        /*push_back task and notify corresponding thread if deque wait*/
        pthread_pool_deques[deque_index].push_back(i);
        this->granularity_lucks[deque_index]->unlock();
    //critical section end
        this->worker_threads_cv_muts[deque_index]->lock();
        if (this->deques_is_wait[deque_index]) {
            // this->worker_threads_cv_muts[deque_index]->lock();
            this->worker_threads_cv_muts[deque_index]->unlock();
            this->worker_threads_cvs[deque_index]->notify_one();
        } else {
            this->worker_threads_cv_muts[deque_index]->unlock();
        }
    }
    /*wait until done*/
    this->main_thread_cv.wait(lk);
    this->done_num = 0;
    lk.unlock();
    return;
}

TaskID TaskSystemParallelThreadPoolSleeping::runAsyncWithDeps(IRunnable* runnable, int num_total_tasks,
                                                    const std::vector<TaskID>& deps) {


    //
    // TODO: CS149 students will implement this method in Part B.
    //

    return 0;
}

void TaskSystemParallelThreadPoolSleeping::sync() {

    //
    // TODO: CS149 students will modify the implementation of this method in Part B.
    //

    return;
}
