#ifndef DEFERREDTASKSEXECUTOR_H
#define DEFERREDTASKSEXECUTOR_H

#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <deque>

class DeferredTask {
public:
  enum State { NEW, EXECUTING, DONE };
private:
  std::mutex _mutex;
  std::condition_variable _done_cond;
  std::atomic<State> _state;
protected:
  virtual void run() = 0;
public:
  DeferredTask();
  virtual ~DeferredTask();
  void execute();
  bool waitFor(unsigned int timeout_ms = 0);
  State getState() const;
};

class DeferredTasksExecutor {
  std::vector<std::thread> _thread_pool;
  typedef std::deque<std::pair<int, std::shared_ptr<DeferredTask>>> tasks_container_t;
  tasks_container_t _tasks; // TODO: compare vs vector/list in real scenarios
  std::mutex _tasks_mutex;
  std::condition_variable _wakeup_threads;
  std::atomic<bool> _stop;
  void threadRoutine();
  tasks_container_t::const_iterator findTask(std::shared_ptr<DeferredTask> task) const;
public:
  DeferredTasksExecutor();
  DeferredTasksExecutor(size_t max_parallel_tasks);
  size_t getMaxParallelTasks() const;
  ~DeferredTasksExecutor();
  void submit(std::shared_ptr<DeferredTask> task, int priority = 0);
  // TODO: implement DeferredTask::cancel()
  void stop();
  bool inQueue(std::shared_ptr<DeferredTask> task);
  bool cancel(std::shared_ptr<DeferredTask> task);
};

#endif