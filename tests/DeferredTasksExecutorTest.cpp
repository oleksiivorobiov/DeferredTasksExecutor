#include "DeferredTasksExecutor.h"
#include "gtest/gtest.h"
#include "utils.h"
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

TEST_F(DeferredTaskTest_WithEmptyTask, CreatedTaskSholdBeInNewState) {
  ASSERT_EQ(DeferredTask::NEW, task.getState());
}

TEST_F(DeferredTaskTest_WithEmptyTask, ExecutedTaskSholdBeInDoneState) {
  task.execute();

  ASSERT_EQ(DeferredTask::DONE, task.getState());
}

class DeferredTaskTest_WithExecutingTask : public DeferredTaskTest {
protected:
  promise<void> block_thread_promise;
  future<void> block_thread_future;
  TestTask task;

  DeferredTaskTest_WithExecutingTask() : block_thread_future(block_thread_promise.get_future()), task([&]() {
                                             block_thread_future.get();
                                           }) {}

  void releaseBlockedTasks() {
    setPromise(block_thread_promise);
  }

  void TearDown() override {
    releaseBlockedTasks();
  }
};

TEST_F(DeferredTaskTest_WithExecutingTask, IsExecutingShouldReturnTrueIfTaskIsExecuting) {
  thread executor([&] () {
    task.execute();
  });
  ASSERT_EQ(DeferredTask::EXECUTING, task.getState());

  releaseBlockedTasks();
  executor.join();
}

class DeferredTasksExecutorTest : public ::testing::Test {
protected:

};

class DeferredTasksExecutorTest_WithSingleThread : public DeferredTasksExecutorTest {
protected:
  DeferredTasksExecutor executor;

  DeferredTasksExecutorTest_WithSingleThread() : executor(1) {}
};

TEST_F(DeferredTasksExecutorTest_WithSingleThread, MaxParallelTasksShouldBeOne) {
  ASSERT_EQ(1, executor.getMaxParallelTasks());
}

class DeferredTasksExecutorTest_WithDefaultThreadsCount : public DeferredTasksExecutorTest {
protected:
  DeferredTasksExecutor executor;
  atomic<unsigned int> run_called;

  DeferredTasksExecutorTest_WithDefaultThreadsCount() : run_called(0) {}

  shared_ptr<TestTask> getCountingTask() {
    return make_shared<TestTask>([&]() {
      ++run_called;
    });
  }
};

TEST_F(DeferredTasksExecutorTest_WithDefaultThreadsCount, ExecutesTaskAfterSubmit) {
  auto counting_task = getCountingTask();
  executor.submit(counting_task);

  ASSERT_TRUE(counting_task->waitFor());
  ASSERT_EQ(1, run_called);
}

class BlockThreadsHelper {
protected:
  promise<void> block_threads_promise;
  shared_future<void> block_threads_future;
  std::chrono::milliseconds future_timeout;

  BlockThreadsHelper() : block_threads_future(block_threads_promise.get_future()), future_timeout(2000) {}

  shared_ptr<TestTask> getBlockingTask() {
    return make_shared<TestTask>([&]() {
      block_threads_future.get();
    });
  }

  void makeAllThreadsBusy(DeferredTasksExecutor &executor) {
    for (size_t i = 0; i < executor.getMaxParallelTasks(); ++i)
      executor.submit(getBlockingTask());
  }

  void releaseAllBusyThreads() {
    setPromise(block_threads_promise);
  }
};

class DeferredTasksExecutorTest_WhenNoIdleThreads : public DeferredTasksExecutorTest_WithDefaultThreadsCount, public BlockThreadsHelper {
protected:
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
  ASSERT_EQ(0, run_called);
}

TEST_F(DeferredTasksExecutorTest_WhenNoIdleThreads, ExecutesRedundantTaskAfterThreadBecomeIdle) {
  auto redundant_task = getCountingTask();
  executor.submit(redundant_task);
  releaseAllBusyThreads();

  ASSERT_TRUE(redundant_task->waitFor(20));
  ASSERT_EQ(1, run_called);
}

class DeferredTasksExecutorTest_WithSingleBlockedThread : public DeferredTasksExecutorTest_WithSingleThread, public BlockThreadsHelper {
protected:
  void SetUp() override {
    makeAllThreadsBusy(executor);
  }

  void TearDown() override {
    releaseAllBusyThreads();
    executor.stop();
  }
};

TEST_F(DeferredTasksExecutorTest_WithSingleBlockedThread, ExecutesAllTasksWithSamePriorityInSubmitOrder) {
  vector<size_t> execution_order;

  const int tasks_size = 5000;
  for (size_t i = 0; i < tasks_size; ++i)
    executor.submit(make_shared<TestTask>([&execution_order, i]() {
      execution_order.push_back(i);
    }));

  releaseAllBusyThreads();
  executor.stop();

  ASSERT_EQ(tasks_size, execution_order.size());
  for (size_t i = 0; i < execution_order.size(); ++i)
    ASSERT_EQ(i, execution_order[i]);
}

TEST_F(DeferredTasksExecutorTest_WithSingleBlockedThread, ExecutesTasksWithHighPriorityFirst) {
  vector<int> priorities;

  const int tasks_size = 2000;
  for (size_t i = 0; i < tasks_size; ++i) {
    int priority = getRandInt(-500, 500);
    executor.submit(make_shared<TestTask>([&priorities, priority, i]() {
      priorities.push_back(priority);
    }), priority);
  }

  releaseAllBusyThreads();
  executor.stop();

  ASSERT_EQ(tasks_size, priorities.size());
  for (size_t i = 0; i < priorities.size() - 1; ++i)
    ASSERT_GE(priorities[i], priorities[i + 1]);
}

TEST_F(DeferredTasksExecutorTest_WithSingleBlockedThread, InQueueShouldReturnCorrectValue) {
  promise<void> block_thread_promise, thread_ready_promise;
  future<void> block_thread_future(block_thread_promise.get_future()), thread_ready_future(thread_ready_promise.get_future());

  auto task = make_shared<TestTask>([&]() {
    thread_ready_promise.set_value();
    block_thread_future.wait_for(future_timeout);
  });
  ASSERT_FALSE(executor.inQueue(task));

  executor.submit(task);
  ASSERT_TRUE(executor.inQueue(task));

  releaseAllBusyThreads();
  ASSERT_EQ(std::future_status::ready, thread_ready_future.wait_for(future_timeout));
  ASSERT_FALSE(executor.inQueue(task));

  block_thread_promise.set_value();
  task->waitFor();
}