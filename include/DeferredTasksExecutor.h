#ifndef DEFERREDTASKSEXECUTOR_H
#define DEFERREDTASKSEXECUTOR_H

#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <deque>

// please read "implementation notes" section in readme.md before review this code

/**
 * \brief Represents concurrent task, useful in combination with DeferredTasksExecutor
 */
class DeferredTask {
public:
  /**
   * \brief Represents possible state of DeferredTask
   */
  enum State {
    /**
     * \brief Task is newly created and ready to execution or waiting
     */
    NEW,
    /**
     * \brief Task is currently executing
     */
    EXECUTING,
    /**
     * \brief Task execution finished
     */
    DONE,
    /**
     * \brief Task execution failed due to unexpected exception
     */
    FAILED
  };
private:
  std::mutex _mutex;
  std::condition_variable _done_cond;
  std::atomic<State> _state;
  std::exception_ptr _exception;
protected:
  /**
   * \brief Should contain business logic of task, implement it in your subclass
   * 
   * Don't make this method public, use execute() instead
   */
  virtual void run() = 0;
public:
  DeferredTask();
  virtual ~DeferredTask();
  /**
   * \brief Executes task(including run())
   * 
   * All threads blocked with waitFor() released after this call
   */
  void execute();
  /**
   * \brief Block current thread until task executing and timeout is not expired
   * 
   * This method is thread-safe
   * \param timeout_ms max blocking time in milliseconds, 0 means block forever
   * \return true if task executed or failed, false if timeout expired
   */
  bool waitFor(unsigned int timeout_ms = 0);
  /**
   * \brief Returns current state of task
   */
  State getState() const;
  /**
  * \brief Returns exception caught from run() in case state is FAILED
  */
  std::exception_ptr getException() const;
  // TODO: implement DeferredTask::cancel()
};

/**
 * \brief Represents taks executor, this class executes tasks in worker threads
 * 
 * All methods are thread-safe
 */
class DeferredTasksExecutor {
  struct QueueNode {
    DeferredTask *task;
    int priority;
    bool auto_delete;
    QueueNode(DeferredTask *task, int priority);
  };
  std::vector<std::thread> _thread_pool;
  typedef std::deque<QueueNode> tasks_container_t;
  tasks_container_t _tasks_queue; // TODO: compare vs vector/list in real scenarios
  mutable std::mutex _tasks_mutex;
  std::condition_variable _wakeup_threads_cond;
  std::atomic<bool> _stop;
  void threadRoutine();
  void enqueueTask(const QueueNode &node);
  tasks_container_t::const_iterator findTask(const DeferredTask *task) const;
public:
  static const int DefaultPriority = 0;
  /**
   * \brief Create executor with default worker threads count(equal to CPU cores)
   */
  DeferredTasksExecutor();
  /**
   * \brief Create executor with custom worker threads count
   * \param max_parallel_tasks count of worker threads
   */
  DeferredTasksExecutor(size_t max_parallel_tasks);
  /**
   * \brief Stop and destroy executor
   */
  ~DeferredTasksExecutor();
  /**
   * \brief Returns count of worker threads executor will use
   */
  size_t getMaxParallelTasks() const;
  /**
   * \brief Enqueue task, first available worker thread will take and execute it
   * 
   * Tasks enqueued using FIFO approach, tasks with higher priority executed first
   * \param task Task to enqueue, memory should be valid until executed
   * \param priority Task priority
   */
  void submit(DeferredTask *task, int priority = DefaultPriority);
  /**
   * \brief Same as submit() but additionally delete task after execution
   * \param task Task to enqueue, should be heap-allocated, It's not safe to do any actions with task after this call
   * \param priority Task priority
   */
  void submitAndAutoDelete(DeferredTask *task, int priority = DefaultPriority);
  /**
   * \brief Block current thread until all queued tasks are executed and stop worker threads
   */
  void stop();
  /**
   * \brief Check if task is enqueued
   * \param task Task to check
   * \return true if enqueued false if executing or is not enqueued
   */
  bool inQueue(const DeferredTask *task) const;
  /**
   * \brief Cancel enqueued task
   * \param task Task to cancel
   * \return true if cancelled(in this case ownership of task object transfered to caller and he should delete it), false if task already executing or is no enqueued
   */
  bool cancel(const DeferredTask *task);
};

#endif