#include "DeferredTasksExecutor.h"
#include "gtest/gtest.h"
#include <future>

using namespace std;

class TestTask : public DeferredTask {
  function<void()> _func;
public:
  TestTask(function<void()> func) : _func(func) {}

  void run() override {
    _func();
  }
};

class DeferredTaskTest : public ::testing::Test {
protected:

};

TEST_F(DeferredTaskTest, ExecutingShouldCallRunMethod) {
  bool run_called = false;
  TestTask task([&]() {
    run_called = true;
  });
  task.execute();

  ASSERT_TRUE(run_called);
}

class DeferredTaskTest_WithEmptyTask : public DeferredTaskTest {
protected:
  TestTask task;
  const unsigned int timeout = 30;

  DeferredTaskTest_WithEmptyTask() : task([&]() {}) {}
};

TEST_F(DeferredTaskTest_WithEmptyTask, WaitForShouldReturnTrueAfterTaskExecuted) {
  task.execute();

  ASSERT_TRUE(task.waitFor(timeout));
}

TEST_F(DeferredTaskTest_WithEmptyTask, WaitForShouldReturnFalseIfTaskWasNotExecutedInTime) {
  ASSERT_FALSE(task.waitFor(timeout));
}

class DeferredTasksExecutorTest : public ::testing::Test {
protected:
  atomic<unsigned int> run_called;

  DeferredTasksExecutorTest() : run_called(0) {}

  shared_ptr<TestTask> getCountingTask() {
    return make_shared<TestTask>([&]() {
      ++run_called;
    });
  }
};

class DeferredTasksExecutorTest_WithSingleThread : public DeferredTasksExecutorTest {
protected:
  DeferredTasksExecutor executor;

  DeferredTasksExecutorTest_WithSingleThread() : executor(1) {}
};

TEST_F(DeferredTasksExecutorTest_WithSingleThread, ExecutesAllTasksInSubmitOrder) {
  shared_ptr<TestTask> last_task;
  vector<size_t> execution_order;

  const int tasks_size = 50000;
  for (size_t i = 0; i < tasks_size; ++i) {
    last_task = make_shared<TestTask>([&execution_order, i]() {
      if (i == 0)
        this_thread::sleep_for(100ms); // wait for few more tasks to become submitted
      execution_order.push_back(i);
    });
    executor.submit(last_task);
  }

  ASSERT_TRUE(last_task->waitFor(500));

  ASSERT_EQ(tasks_size, execution_order.size());
  for (size_t i = 0; i < execution_order.size(); ++i)
    ASSERT_EQ(i, execution_order[i]);
}

TEST_F(DeferredTasksExecutorTest_WithSingleThread, MaxParallelTasksShouldBeOne) {
  ASSERT_EQ(1, executor.getMaxParallelTasks());
}

class DeferredTasksExecutorTest_WithDefaultThreadsCount : public DeferredTasksExecutorTest {
protected:
  DeferredTasksExecutor executor;
};

TEST_F(DeferredTasksExecutorTest_WithDefaultThreadsCount, ExecutesTaskAfterSubmit) {
  auto counting_task = getCountingTask();
  executor.submit(counting_task);

  ASSERT_TRUE(counting_task->waitFor());
  ASSERT_EQ(1, run_called);
}

class DeferredTasksExecutorTest_WhenNoIdleThreads : public DeferredTasksExecutorTest_WithDefaultThreadsCount {
protected:
  promise<void> block_threads_promise;
  shared_future<void> block_threads_future;

  DeferredTasksExecutorTest_WhenNoIdleThreads() : block_threads_future(block_threads_promise.get_future()) {}

  void makeAllThreadsBusy(DeferredTasksExecutor &executor) {
    for (size_t i = 0; i < executor.getMaxParallelTasks(); ++i)
      executor.submit(getBlockingCountingTask());
  }

  void releaseAllBusyThreads() {
    try {
      block_threads_promise.set_value();
    }
    catch (future_error &e) {
      if (e.code() != make_error_condition(future_errc::promise_already_satisfied))
        throw;
    }
  }

  shared_ptr<TestTask> getBlockingCountingTask() {
    return make_shared<TestTask>([&]() {
      ++run_called;
      block_threads_future.get();
    });
  }

  void SetUp() override {
    makeAllThreadsBusy(executor);
  }

  void TearDown() override {
    releaseAllBusyThreads();
    executor.stop();
  }
};

TEST_F(DeferredTasksExecutorTest_WhenNoIdleThreads, DontExecuteRedundantTask) {
  auto redundant_task = getCountingTask();
  executor.submit(redundant_task);

  ASSERT_FALSE(redundant_task->waitFor(20));
  ASSERT_EQ(executor.getMaxParallelTasks(), run_called);
}

TEST_F(DeferredTasksExecutorTest_WhenNoIdleThreads, ExecutesRedundantTaskAfterThreadBecomeIdle) {
  auto redundant_task = getCountingTask();
  executor.submit(redundant_task);
  releaseAllBusyThreads();

  ASSERT_TRUE(redundant_task->waitFor(20));
  ASSERT_EQ(executor.getMaxParallelTasks() + 1, run_called);
}

