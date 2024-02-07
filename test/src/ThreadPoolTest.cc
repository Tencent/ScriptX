/*
 * Tencent is pleased to support the open source community by making ScriptX available.
 * Copyright (C) 2021 THL A29 Limited, a Tencent company.  All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <array>
#include <atomic>
#include <cmath>
#include <iomanip>
#include "test.h"

namespace script::utils::test {
static void handleMessage(Message& msg) {
  auto* i = static_cast<std::atomic_int64_t*>(msg.ptr0);
  (*i)++;
}

TEST(ThreadPool, Run) {
  ThreadPool tp(2, std::make_unique<MessageQueue>(1000));
  EXPECT_EQ(2, tp.workerCount());

  auto i = std::make_unique<std::atomic_int64_t>();

  constexpr auto max = 1000;
  for (int j = 0; j < max; ++j) {
    Message msg(handleMessage, nullptr);
    msg.ptr0 = i.get();
    tp.postMessage(msg);
  }

  tp.shutdown(false);
  tp.awaitTermination();

  EXPECT_EQ(max, i->load());
}

TEST(ThreadPool, MultiThreadRun) {
  constexpr auto kWorkerCount = 2;
  constexpr auto kProducerCount = 4;
  constexpr auto max = 1000;

  ThreadPool tp(kWorkerCount);
  EXPECT_EQ(kWorkerCount, tp.workerCount());

  auto i = std::make_unique<std::atomic_int64_t>();

  std::array<std::unique_ptr<std::thread>, kProducerCount> p;
  for (auto& t : p) {
    t = std::make_unique<std::thread>([&]() {
      for (int j = 0; j < max; ++j) {
        Message msg(handleMessage, nullptr);
        msg.ptr0 = i.get();
        tp.postMessage(msg);
      }
    });
  }

  for (auto& t : p) {
    t->join();
  }
  tp.shutdown(true);

  EXPECT_EQ(max * kProducerCount, i->load());
}

template <size_t kProducerThreads, size_t kWorkerThreads>
void runThreadpoolBenchmark() {
  using std::chrono::duration;
  using std::chrono::duration_cast;
  using std::chrono::milliseconds;
  using std::chrono::nanoseconds;
  using std::chrono::steady_clock;
  using std::chrono_literals::operator""ms;

  constexpr auto kEnableMultiThreadTest = false;
  constexpr auto kRunTimeMs = 200ms;

  // simple benchmark
  if (!kEnableMultiThreadTest) return;

  auto start = steady_clock::now();

  ThreadPool tp(kWorkerThreads, std::make_unique<MessageQueue>(1000));
  auto i = std::make_unique<std::atomic_int64_t>();

  Message stopMsg([](auto& msg) { static_cast<ThreadPool*>(msg.ptr0)->shutdownNow(false); },
                  nullptr);
  stopMsg.ptr0 = &tp;

  tp.postMessage(stopMsg, kRunTimeMs);

  auto handleMessage = [](Message& msg) {
    auto* i = static_cast<std::atomic_int64_t*>(msg.ptr0);
    for (int j = 0; j < 100 * 1000; ++j) {
      // do a bit havey work
      auto x = sinf(i->load());
      static_cast<void>(x);
    }
    (*i)++;
  };

  std::array<std::unique_ptr<std::thread>, kProducerThreads> p;
  for (auto& t : p) {
    t = std::make_unique<std::thread>([&]() {
      while (true) {
        Message msg(handleMessage, nullptr);
        msg.ptr0 = i.get();
        if (tp.postMessage(msg) == 0) {
          break;
        }
      }
    });
  }

  tp.awaitTermination();
  for (auto& t : p) {
    t->join();
  }

  // run time should be close to kRunTime
  const auto runTimeMillis = duration_cast<milliseconds>((steady_clock::now() - start)).count();

  const auto opsPerSecond = i->load() * 1000 / runTimeMillis;
  const auto nanosencodsPerOp = duration_cast<duration<double, std::nano>>(
      duration<double, std::milli>(static_cast<double>(runTimeMillis) / i->load()));

  std::cout << kProducerThreads << "-producers " << kWorkerThreads << "-workers "
            << "time:" << runTimeMillis << "ms, " << std::setw(9) << i->load() << " ops"
            << " [" << std::setw(9) << opsPerSecond << " ops/s]"
            << " [" << std::setw(9) << nanosencodsPerOp.count() << " ns/op]" << std::endl;
}

TEST(ThreadPool, Benchmark_1p_1w) { runThreadpoolBenchmark<1, 1>(); }

TEST(ThreadPool, Benchmark_1p_2w) { runThreadpoolBenchmark<1, 2>(); }

TEST(ThreadPool, Benchmark_1p_4w) { runThreadpoolBenchmark<1, 4>(); }

TEST(ThreadPool, Benchmark_2p_1w) { runThreadpoolBenchmark<2, 1>(); }

TEST(ThreadPool, Benchmark_4p_1w) { runThreadpoolBenchmark<4, 1>(); }

TEST(ThreadPool, Benchmark_2p_2w) { runThreadpoolBenchmark<2, 2>(); }

TEST(ThreadPool, Benchmark_4p_4w) { runThreadpoolBenchmark<4, 4>(); }
}  // namespace script::utils::test
