#include "DeferredTasksExecutor.h"
#include "gtest/gtest.h"
#include "utils.h"
#include <future>

using namespace std;

chrono::milliseconds future_timeout(2000);

class TestTask : public DeferredTask {
  function<void()> _func;
public:
  TestTask(function<void()> func) : _func(func) {}

  void run() override {
    _func();
  }
};

class BlockThreadsHelper {
  promise<void> block_threads_promise;
  shared_future<void> block_threads_future;
protected:
  promise<void> block_thread_promise, thread_ready_promise;
  future<void> block_thread_future, thread_ready_future;

  BlockThreadsHelper() : block_threads_future(block_threads_promise.get_future()),
    block_thread_future(block_thread_promise.get_future()),
    thread_ready_future(thread_ready_promise.get_future()) {}

  shared_ptr<TestTask> getBlockingTask() {
    return make_shared<TestTask>([&]() {
      thread_ready_promise.set_value();
      block_thread_future.wait_for(future_timeout);
    });
  }

  void makeAllThreadsBusy(DeferredTasksExecutor &executor) {
    auto enough_priority_to_ensure_blocking_tasks_will_be_executed_first = numeric_limits<int>::max();
    for (size_t i = 0; i < executor.getMaxParallelTasks(); ++i)
      executor.submitAndAutoDelete(new TestTask([&]() {
      block_threads_future.wait_for(future_timeout);
    }), enough_priority_to_ensure_blocking_tasks_will_be_executed_first);
  }

  void releaseAllBusyThreads() {
    setPromise(block_threads_promise);
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

TEST_F(DeferredTaskTest, UnexpectedExceptionInRunCaugthAndSaved) {
  TestTask task([&]() {
    throw exception();
  });

  thread executor([&]() {
    task.execute();
  });
  executor.join();

  ASSERT_EQ(DeferredTask::FAILED, task.getState());
  ASSERT_TRUE(task.getException());
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

class DeferredTaskTest_WithBlockThreadsHelper : public DeferredTaskTest, public BlockThreadsHelper {};

TEST_F(DeferredTaskTest_WithBlockThreadsHelper, IsExecutingShouldReturnTrueIfTaskIsExecuting) {
  shared_ptr<TestTask> task(getBlockingTask());

  thread executor([&] () {
    task->execute();
  });

  ASSERT_EQ(std::future_status::ready, thread_ready_future.wait_for(future_timeout));
  ASSERT_EQ(DeferredTask::EXECUTING, task->getState());

  block_thread_promise.set_value();
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
  executor.submit(counting_task.get());

  ASSERT_TRUE(counting_task->waitFor());
  ASSERT_EQ(1, run_called);
}

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
  executor.submit(redundant_task.get());

  ASSERT_FALSE(redundant_task->waitFor(20));
  ASSERT_EQ(0, run_called);

  releaseAllBusyThreads();
  ASSERT_TRUE(redundant_task->waitFor());
}

TEST_F(DeferredTasksExecutorTest_WhenNoIdleThreads, ExecutesRedundantTaskAfterThreadBecomeIdle) {
  auto redundant_task = getCountingTask();
  executor.submit(redundant_task.get());
  releaseAllBusyThreads();

  ASSERT_TRUE(redundant_task->waitFor(20));
  ASSERT_EQ(1, run_called);
}

class DeferredTasksExecutorTest_WithSingleThreadAndBlockingHelper : public DeferredTasksExecutorTest_WithSingleThread, public BlockThreadsHelper {
protected:

};

TEST_F(DeferredTasksExecutorTest_WithSingleThreadAndBlockingHelper, CancelShouldReturnFalseIfTaskAlredyExecuting) {
  auto task = getBlockingTask();
  executor.submit(task.get());

  ASSERT_EQ(std::future_status::ready, thread_ready_future.wait_for(future_timeout));
  ASSERT_FALSE(executor.cancel(task.get()));

  block_thread_promise.set_value();
  task->waitFor();
}

class DeferredTasksExecutorTest_WithSingleBlockedThread : public DeferredTasksExecutorTest_WithSingleThreadAndBlockingHelper {
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
    executor.submitAndAutoDelete(new TestTask([&execution_order, i]() {
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
    executor.submitAndAutoDelete(new TestTask([&priorities, priority, i]() {
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
  auto task = getBlockingTask();
  ASSERT_FALSE(executor.inQueue(task.get()));

  executor.submit(task.get());
  ASSERT_TRUE(executor.inQueue(task.get()));

  releaseAllBusyThreads();
  ASSERT_EQ(std::future_status::ready, thread_ready_future.wait_for(future_timeout));
  ASSERT_FALSE(executor.inQueue(task.get()));

  block_thread_promise.set_value();
  task->waitFor();
  //this_thread::sleep_for(1ms);
  ASSERT_EQ(DeferredTask::DONE, task->getState());
}

TEST_F(DeferredTasksExecutorTest_WithSingleBlockedThread, CancelReturnsTrueIfQueuedTaskWasRemoved) {
  auto task_ptr = make_shared<TestTask>([&]() {});
  auto task = task_ptr.get();
  ASSERT_FALSE(executor.cancel(task));

  executor.submit(task);
  ASSERT_TRUE(executor.cancel(task));
}

TEST(ProofOfConcept, RunTaskSequentally) {
  enum SomePriority { LOW, MID, HIGH };
  DeferredTasksExecutor executor(1);
  string executed_order;

  auto sequentalTask = new TestTask([&] {
    executed_order += "s";
  });
  auto commonTask = new TestTask([&] {
    executed_order += "c";
  });
  auto mainTask = new TestTask([&] {
    // do stuff
    executed_order += "m";
    // after finish submit sequental task with high priority so it will be executed before any queued with mid priority
    executor.submitAndAutoDelete(sequentalTask, HIGH);
  });

  executor.submitAndAutoDelete(mainTask, MID);
  executor.submitAndAutoDelete(commonTask, MID);

  executor.stop();
  ASSERT_STREQ("msc", executed_order.c_str());
}
