/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <emmintrin.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <filesystem>
#include <memory>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

#include "fbpcf/engine/communication/InMemoryPartyCommunicationAgentFactory.h"
#include "fbpcf/engine/communication/InMemoryPartyCommunicationAgentHost.h"
#include "fbpcf/engine/communication/SocketPartyCommunicationAgentFactory.h"
#include "fbpcf/engine/communication/test/AgentFactoryCreationHelper.h"
#include "fbpcf/engine/communication/test/TlsCommunicationUtils.h"

namespace fbpcf::engine::communication {

void sendAndReceive(std::unique_ptr<IPartyCommunicationAgent> agent, int size) {
  int repeat = 10;
  for (int t = 0; t < repeat; t++) {
    std::vector<unsigned char> sendData(size);
    for (int i = 0; i < size; i++) {
      sendData[i] = i & 0xFF;
    }
    agent->send(sendData);
    auto receivedData = agent->receive(size);
    EXPECT_EQ(receivedData.size(), size);
    for (int i = 0; i < size; i++) {
      EXPECT_EQ(receivedData[i], i & 0xFF);
    }
    auto traffic = agent->getTrafficStatistics();
    EXPECT_EQ(traffic.first, size * (t + 1));
    EXPECT_EQ(traffic.second, size * (t + 1));
  }
}

void testAgentFactory(
    int myId,
    int totalParty,
    int size,
    std::unique_ptr<IPartyCommunicationAgentFactory> factory) {
  for (int i = 0; i < totalParty; i++) {
    std::vector<std::thread> testThreads;
    if (i != myId) {
      auto agent = factory->create(i);
      testThreads.push_back(
          std::thread(sendAndReceive, std::move(agent), size));
    }
    for (auto& t : testThreads) {
      t.join();
    }
  }
}

TEST(InMemoryPartyCommunicationAgentTest, testSendAndReceive) {
  auto factorys = getInMemoryAgentFactory(2);

  int size = 1024;
  auto thread0 =
      std::thread(testAgentFactory, 0, 2, size, std::move(factorys[0]));
  auto thread1 =
      std::thread(testAgentFactory, 1, 2, size, std::move(factorys[1]));

  thread1.join();
  thread0.join();
}

TEST(SocketPartyCommunicationAgentTest, testSendAndReceiveWithTls) {
  auto createdDir = setUpTlsFiles();

  std::random_device rd;
  std::default_random_engine defEngine(rd());
  std::uniform_int_distribution<int> intDistro(10000, 25000);

  /*
   * We use random ports rather than constants because, during
   * stress runs, we get errors when trying to bind to the
   * same port multiple times.
   */
  auto port01 = intDistro(defEngine);
  auto port02 = port01 + 4;
  auto port12 = port01 + 8;

  std::map<int, SocketPartyCommunicationAgentFactory::PartyInfo> partyInfo0 = {
      {1, {"127.0.0.1", port01}}, {2, {"127.0.0.1", port02}}};
  std::map<int, SocketPartyCommunicationAgentFactory::PartyInfo> partyInfo1 = {
      {0, {"127.0.0.1", port01}}, {2, {"127.0.0.1", port12}}};
  std::map<int, SocketPartyCommunicationAgentFactory::PartyInfo> partyInfo2 = {
      {0, {"127.0.0.1", port02}}, {1, {"127.0.0.1", port12}}};

  auto factory0 = std::make_unique<SocketPartyCommunicationAgentFactory>(
      0, partyInfo0, true, createdDir);
  auto factory1 = std::make_unique<SocketPartyCommunicationAgentFactory>(
      1, partyInfo1, true, createdDir);
  auto factory2 = std::make_unique<SocketPartyCommunicationAgentFactory>(
      2, partyInfo2, true, createdDir);

  int size = 1048576; // 1024 ^ 2
  auto thread0 = std::thread(testAgentFactory, 0, 3, size, std::move(factory0));
  auto thread1 = std::thread(testAgentFactory, 1, 3, size, std::move(factory1));
  auto thread2 = std::thread(testAgentFactory, 2, 3, size, std::move(factory2));

  thread2.join();
  thread1.join();
  thread0.join();

  deleteTlsFiles(createdDir);
}

TEST(SocketPartyCommunicationAgentTest, testSendAndReceiveWithoutTls) {
  std::random_device rd;
  std::default_random_engine defEngine(rd());
  std::uniform_int_distribution<int> intDistro(10000, 25000);

  auto port01 = intDistro(defEngine);
  auto port02 = intDistro(defEngine);
  auto port12 = intDistro(defEngine);

  std::map<int, SocketPartyCommunicationAgentFactory::PartyInfo> partyInfo0 = {
      {1, {"127.0.0.1", port01}}, {2, {"127.0.0.1", port02}}};
  std::map<int, SocketPartyCommunicationAgentFactory::PartyInfo> partyInfo1 = {
      {0, {"127.0.0.1", port01}}, {2, {"127.0.0.1", port12}}};
  std::map<int, SocketPartyCommunicationAgentFactory::PartyInfo> partyInfo2 = {
      {0, {"127.0.0.1", port02}}, {1, {"127.0.0.1", port12}}};

  auto factory0 =
      std::make_unique<SocketPartyCommunicationAgentFactory>(0, partyInfo0);
  auto factory1 =
      std::make_unique<SocketPartyCommunicationAgentFactory>(1, partyInfo1);
  auto factory2 =
      std::make_unique<SocketPartyCommunicationAgentFactory>(2, partyInfo2);

  int size = 1048576; // 1024 ^ 2
  auto thread0 = std::thread(testAgentFactory, 0, 3, size, std::move(factory0));
  auto thread1 = std::thread(testAgentFactory, 1, 3, size, std::move(factory1));
  auto thread2 = std::thread(testAgentFactory, 2, 3, size, std::move(factory2));

  thread2.join();
  thread1.join();
  thread0.join();
}
} // namespace fbpcf::engine::communication
