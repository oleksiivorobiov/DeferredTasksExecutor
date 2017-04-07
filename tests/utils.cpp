#include <random>
#include "utils.h"

using namespace std;

void setPromise(promise<void> &prom) {
  try {
    prom.set_value();
  }
  catch (future_error &e) {
    if (e.code() != make_error_condition(future_errc::promise_already_satisfied))
      throw;
  }
}

int getRandInt(int min, int max) {
  random_device rd;
  mt19937 rng(rd());
  std::uniform_int_distribution<int> uni(min, max);

  return uni(rng);
}