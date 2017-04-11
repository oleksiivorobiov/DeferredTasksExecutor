#include "DeferredTasksExecutor.h"
#include <random>
#include <iostream>

using namespace std;

int getRandInt(int min, int max) {
  random_device rd;
  mt19937 rng(rd());
  uniform_int_distribution<int> uni(min, max);

  return uni(rng);
}

struct ExampleInfo {
  atomic<size_t> queued, executing, done;
  ExampleInfo () {
    queued = executing = done = 0;
  }
};

class ExampleTask : public DeferredTask {
  ExampleInfo *_info;
protected:
  static void doSomeStuff() {
    chrono::milliseconds sleep_time(getRandInt(0, 10000));
    this_thread::sleep_for(sleep_time);
  }

  void run() override {
    --_info->queued;
    ++_info->executing;
    doSomeStuff();
    --_info->executing;
    ++_info->done;
  }
public:
  ExampleTask(ExampleInfo &info) {
    _info = &info;
  }
};

int main() {
  DeferredTasksExecutor executor;
  ExampleInfo info;

  while (true) {
    int rand_priority = getRandInt(0, 100);
    executor.submitAndAutoDelete(new ExampleTask(info), rand_priority);
    ++info.queued;

    cout << "\rqueued tasks: " << info.queued <<
      ", executing tasks: " << info.executing <<
      ", done tasks: " << info.done;
    this_thread::sleep_for(1s);
  }

  return 0;
}