#ifndef UTILS_H
#define UTILS_H

#include <future>

void setPromise(std::promise<void> &prom);
int getRandInt(int min, int max);

#endif