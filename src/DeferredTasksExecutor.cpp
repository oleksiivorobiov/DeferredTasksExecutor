#include "DeferredTasksExecutor.h"

using std::thread;
using std::lock_guard;
using std::unique_lock;
using std::mutex;

DeferredTask::DeferredTask() : _state(NEW) {}

DeferredTask::~DeferredTask() {}

void DeferredTask::execute() {
  _state = EXECUTING;
  run();
  _state = DONE;
  _done_cond.notify_all();
}

bool DeferredTask::waitFor(unsigned int timeout_ms) {
  auto done_cond_pred = [&]() { return _state == DONE; };

  unique_lock<mutex> lock(_mutex);

  if (timeout_ms == 0) {
    _done_cond.wait(lock, done_cond_pred);
    return true;
  }

  return _done_cond.wait_for(lock, std::chrono::milliseconds(timeout_ms), done_cond_pred);
}

DeferredTask::State DeferredTask::getState() const {
  return _state;
}

void DeferredTasksExecutor::threadRoutine() {
  while (true) {
    unique_lock<mutex> lock(_tasks_mutex);

    _wakeup_threads.wait(lock, [&]() { return _stop || !_tasks.empty(); });

    if (_stop && _tasks.empty())
      return;

    auto task = _tasks.front();
    _tasks.pop();
    lock.unlock();

    task->execute();
  }
}

DeferredTasksExecutor::DeferredTasksExecutor() : DeferredTasksExecutor(thread::hardware_concurrency()) {}

DeferredTasksExecutor::DeferredTasksExecutor(size_t max_parallel_tasks) : _stop(false) {
  if (max_parallel_tasks == 0)
    throw std::invalid_argument("max_parallel_tasks should be greater than zero");

  for (size_t i = 0; i < max_parallel_tasks; ++i)
    _thread_pool.emplace_back(&DeferredTasksExecutor::threadRoutine, this);
}

size_t DeferredTasksExecutor::getMaxParallelTasks() const {
  return _thread_pool.size();
}

DeferredTasksExecutor::~DeferredTasksExecutor() {
  stop();
}

void DeferredTasksExecutor::submit(std::shared_ptr<DeferredTask> task) {
  lock_guard<mutex> lock(_tasks_mutex);
  _tasks.push(task);
  _wakeup_threads.notify_one();
}

void DeferredTasksExecutor::stop() {
  if (_stop)
    return;

  _stop = true;
  _wakeup_threads.notify_all();
  for (auto &thread : _thread_pool)
    thread.join();
}
