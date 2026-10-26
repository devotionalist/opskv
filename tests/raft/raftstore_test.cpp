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

// @file raftstore_test.cpp
// Store / Router / Hibernate / Apply 队列 QoS 烟雾。

#include "opskv/raftstore/store.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <thread>

using opskv::raft::RaftNode;
using opskv::raftstore::Store;

TEST(RaftstoreTest, StoreTickElectsLeader) {
  Store::Options opts;
  opts.hibernate_after_ms = 0;
  Store store(opts, [](uint32_t shard) {
    RaftNode::Options o;
    o.shard_id = shard;
    o.self_id = "n0";
    o.peers = {{"n0", "a", false}};
    o.election_timeout_ms = 100;
    o.heartbeat_ms = 40;
    return std::make_shared<RaftNode>(o, [](uint64_t, const std::string&) {},
                                      [](auto&&...) {});
  });
  store.get_or_create(1);
  for (int i = 0; i < 50; ++i) store.schedule_tick();
  ASSERT_TRUE(store.get(1)->is_leader());
}

TEST(RaftstoreTest, HibernateSkipsTickUntilWake) {
  Store::Options opts;
  opts.hibernate_after_ms = 1;
  Store store(opts, [](uint32_t shard) {
    RaftNode::Options o;
    o.shard_id = shard;
    o.self_id = "n0";
    o.peers = {{"n0", "a", false}, {"n1", "b", false}};
    o.election_timeout_ms = 5000;
    o.heartbeat_ms = 1000;
    o.check_quorum = false;
    o.enable_pre_vote = false;
    return std::make_shared<RaftNode>(o, [](uint64_t, const std::string&) {},
                                      [](auto&&...) {});
  });
  auto& n = store.get_or_create(9);
  // 非 Leader，立刻变冷
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  store.schedule_tick();  // 应进入 hibernate
  store.schedule_tick();
  EXPECT_FALSE(n.is_leader());
  store.mark_hot(9);
  store.schedule_tick();
}

TEST(RaftstoreTest, ApplyQosFlag) {
  Store::Options opts;
  opts.batch.max_apply_queue_depth = 1;
  Store store(opts, [](uint32_t shard) {
    RaftNode::Options o;
    o.shard_id = shard;
    o.self_id = "n0";
    o.peers = {{"n0", "a", false}};
    return std::make_shared<RaftNode>(o, [](uint64_t, const std::string&) {},
                                      [](auto&&...) {});
  });
  store.get_or_create(3);
  EXPECT_FALSE(store.apply_queue_overloaded(3));
}
