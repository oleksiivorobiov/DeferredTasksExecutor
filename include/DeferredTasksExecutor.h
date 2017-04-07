#ifndef DEFERREDTASKSEXECUTOR_H
#define DEFERREDTASKSEXECUTOR_H

#include <vector>
#include <thread>
#include <queue>
#include <mutex>
#include <atomic>

class DeferredTask {
  std::mutex _mutex;
  std::condition_variable _done_cond;
  bool _done;
protected:
  virtual void run() = 0;
public:
  DeferredTask();
  virtual ~DeferredTask();
  void execute();
  bool waitFor(unsigned int timeout_ms = 0);
};

class DeferredTasksExecutor {
  std::vector<std::thread> _thread_pool;
  std::queue<std::shared_ptr<DeferredTask>> _tasks;
  std::mutex _tasks_mutex;
  std::condition_variable _wakeup_threads;
  std::atomic<bool> _stop;
  void threadRoutine();
public:
  DeferredTasksExecutor();
  DeferredTasksExecutor(size_t max_parallel_tasks);
  size_t getMaxParallelTasks() const;
  ~DeferredTasksExecutor();
  void submit(std::shared_ptr<DeferredTask> task);
  void stop();
};

#endif