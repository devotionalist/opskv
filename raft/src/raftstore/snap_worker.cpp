// Copyright 2026 devotionalist
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// @file snap_worker.cpp

#include "opskv/raftstore/snap_worker.hpp"

#include "opskv/logging.hpp"

#include <chrono>

namespace opskv::raftstore {

SnapWorker::SnapWorker(Options opts, Provider provider, Sender sender)
    : opts_(opts), provider_(std::move(provider)), sender_(std::move(sender)) {
  if (opts_.queue_capacity == 0) opts_.queue_capacity = 64;
}

SnapWorker::~SnapWorker() { stop(); }

void SnapWorker::start() {
  if (running_.exchange(true)) return;
  th_ = std::thread([this] { loop(); });
}

void SnapWorker::stop() {
  if (!running_.exchange(false)) return;
  cv_.notify_all();
  if (th_.joinable()) th_.join();
}

bool SnapWorker::enqueue(SnapTask task) {
  std::lock_guard<std::mutex> lk(mu_);
  if (q_.size() >= opts_.queue_capacity) {
    SKV_WARN << "snap_worker queue full drop region=" << task.region_id;
    return false;
  }
  q_.push_back(std::move(task));
  cv_.notify_one();
  return true;
}

size_t SnapWorker::depth() const {
  std::lock_guard<std::mutex> lk(mu_);
  return q_.size();
}

void SnapWorker::loop() {
  while (running_) {
    SnapTask task;
    {
      std::unique_lock<std::mutex> lk(mu_);
      cv_.wait_for(lk, std::chrono::milliseconds(50),
                   [&] { return !q_.empty() || !running_; });
      if (!running_ && q_.empty()) break;
      if (q_.empty()) continue;
      task = std::move(q_.front());
      q_.pop_front();
    }
    try {
      if (task.data.empty() && provider_) {
        task.data = provider_(task.region_id, &task.meta);
      }
      if (sender_) sender_(task.region_id, task.meta, task.data);
    } catch (const std::exception& e) {
      SKV_WARN << "snap_worker region=" << task.region_id << " err=" << e.what();
    }
  }
}

}  // namespace opskv::raftstore
