/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <fmt/format.h>
#include <cstddef>
#include <exception>
#include <iterator>
#include <memory>
#include <stdexcept>

#include "fbpcf/engine/SecretShareEngine.h"
#include "fbpcf/engine/tuple_generator/ITupleGenerator.h"
#include "fbpcf/engine/util/util.h"

namespace fbpcf::engine {
SecretShareEngine::SecretShareEngine(
    std::unique_ptr<tuple_generator::ITupleGenerator> tupleGenerator,
    std::unique_ptr<communication::ISecretShareEngineCommunicationAgent>
        communicationAgent,
    std::unique_ptr<util::IPrgFactory> prgFactory,
    int myId,
    int numberOfParty)
    : tupleGenerator_{std::move(tupleGenerator)},
      communicationAgent_{std::move(communicationAgent)},
      prgFactory_{std::move(prgFactory)},
      myId_{myId},
      numberOfParty_{numberOfParty} {
  std::map<int, __m128i> randomPrgKey;
  for (int i = 0; i < numberOfParty_; i++) {
    if (i != myId_) {
      randomPrgKey.emplace(i, util::getRandomM128iFromSystemNoise());
    }
  }
  auto inputPrgSeed = communicationAgent_->exchangeKeys(randomPrgKey);
  for (auto& item : inputPrgSeed) {
    inputPrgs_.emplace(
        item.first,
        std::make_pair(
            prgFactory_->create(randomPrgKey.at(item.first)),
            prgFactory_->create(item.second)));
  }
}

bool SecretShareEngine::setInput(int id, std::optional<bool> v) {
  if (id == myId_) {
    if (!v.has_value()) {
      throw std::invalid_argument("needs to provide input value");
    }
    bool rst = v.value();
    for (auto& item : inputPrgs_) {
      rst ^= item.second.first->getRandomBits(1)[0];
    }
    return rst;
  } else {
    assert(inputPrgs_.find(id) != inputPrgs_.end());
    return inputPrgs_.at(id).second->getRandomBits(1)[0];
  }
}

std::vector<bool> SecretShareEngine::setBatchInput(
    int id,
    const std::vector<bool>& v) {
  if (id == myId_) {
    auto size = v.size();
    std::vector<bool> rst(size);
    if (rst.size() == 0) {
      throw std::invalid_argument("empty input!");
    }
    for (size_t i = 0; i < size; i++) {
      rst[i] = v[i];
    }
    for (auto& item : inputPrgs_) {
      auto mask = item.second.first->getRandomBits(size);
      for (size_t i = 0; i < size; i++) {
        rst[i] = rst[i] ^ mask[i];
      }
    }
    return rst;
  } else {
    assert(inputPrgs_.find(id) != inputPrgs_.end());
    return inputPrgs_.at(id).second->getRandomBits(v.size());
  }
}

bool SecretShareEngine::computeSymmetricXOR(bool left, bool right) const {
  return left ^ right;
}

std::vector<bool> SecretShareEngine::computeBatchSymmetricXOR(
    const std::vector<bool>& left,
    const std::vector<bool>& right) const {
  if (left.size() != right.size()) {
    throw std::invalid_argument("The input sizes are not the same.");
  }
  if (left.size() == 0) {
    return std::vector<bool>();
  }
  std::vector<bool> rst(left.size());
  for (size_t i = 0; i < left.size(); i++) {
    rst[i] = left[i] ^ right[i];
  }
  return rst;
}

bool SecretShareEngine::computeAsymmetricXOR(bool left, bool right) const {
  if (myId_ == 0) {
    return left ^ right;
  } else {
    return left;
  }
}

std::vector<bool> SecretShareEngine::computeBatchAsymmetricXOR(
    const std::vector<bool>& left,
    const std::vector<bool>& right) const {
  if (left.size() != right.size()) {
    throw std::invalid_argument("The input sizes are not the same.");
  }
  if (left.size() == 0) {
    return std::vector<bool>();
  }
  if (myId_ == 0) {
    std::vector<bool> rst(left.size());
    for (size_t i = 0; i < left.size(); i++) {
      rst[i] = left[i] ^ right[i];
    }
    return rst;
  } else {
    return left;
  }
}

bool SecretShareEngine::computeSymmetricNOT(bool input) const {
  return !input;
}

std::vector<bool> SecretShareEngine::computeBatchSymmetricNOT(
    const std::vector<bool>& input) const {
  if (input.size() == 0) {
    return std::vector<bool>();
  }
  std::vector<bool> rst(input.size());
  for (size_t i = 0; i < rst.size(); i++) {
    rst[i] = !input[i];
  }
  return rst;
}

bool SecretShareEngine::computeAsymmetricNOT(bool input) const {
  if (myId_ == 0) {
    return !input;
  } else {
    return input;
  }
}

std::vector<bool> SecretShareEngine::computeBatchAsymmetricNOT(
    const std::vector<bool>& input) const {
  if (input.size() == 0) {
    return std::vector<bool>();
  }
  if (myId_ != 0) {
    return input;
  }
  std::vector<bool> rst(input.size());
  for (size_t i = 0; i < input.size(); i++) {
    rst[i] = !input[i];
  }
  return rst;
}

//======== Below are free AND computation API's: ========

bool SecretShareEngine::computeFreeAND(bool left, bool right) const {
  return left & right;
}

std::vector<bool> SecretShareEngine::computeBatchFreeAND(
    const std::vector<bool>& left,
    const std::vector<bool>& right) const {
  if (left.size() != right.size()) {
    throw std::invalid_argument("The input sizes are not the same.");
  }
  if (left.size() == 0) {
    return std::vector<bool>();
  }
  std::vector<bool> rst(left.size());
  for (size_t i = 0; i < left.size(); i++) {
    rst[i] = left[i] & right[i];
  }
  return rst;
}

//======== Below are API's to schedule non-free AND's: ========

uint32_t SecretShareEngine::scheduleAND(bool left, bool right) {
  scheduledANDGates_.push_back(ScheduledAND(left, right));
  return scheduledANDGates_.size() - 1;
}

uint32_t SecretShareEngine::scheduleBatchAND(
    const std::vector<bool>& left,
    const std::vector<bool>& right) {
  if (left.size() != right.size()) {
    throw std::runtime_error("Batch AND's must have the same length");
  }
  scheduledBatchANDGates_.push_back(ScheduledBatchAND(left, right));
  return scheduledBatchANDGates_.size() - 1;
}

uint32_t SecretShareEngine::scheduleCompositeAND(
    bool left,
    std::vector<bool> rights) {
  scheduledCompositeANDGates_.push_back(ScheduledCompositeAND(left, rights));
  return scheduledCompositeANDGates_.size() - 1;
}

uint32_t SecretShareEngine::scheduleBatchCompositeAND(
    const std::vector<bool>& left,
    const std::vector<std::vector<bool>>& rights) {
  auto batchSize = left.size();
  for (auto rightValue : rights) {
    if (rightValue.size() != batchSize) {
      throw std::runtime_error(fmt::format(
          "Batch composite AND must have left.size() = rights[i].size() for all i. (Got {:d} != {:d})",
          left.size(),
          rightValue.size()));
    }
  }
  scheduledBatchCompositeANDGates_.push_back(
      ScheduledBatchCompositeAND(left, rights));
  return scheduledBatchCompositeANDGates_.size() - 1;
}

//======== Below are API's to execute non free AND's: ========

void SecretShareEngine::executeScheduledAND() {
  executionResults_ = computeAllANDsFromScheduledANDs(
      scheduledANDGates_,
      scheduledBatchANDGates_,
      scheduledCompositeANDGates_,
      scheduledBatchCompositeANDGates_);

  scheduledANDGates_.clear();
  scheduledBatchANDGates_.clear();
  scheduledCompositeANDGates_.clear();
  scheduledBatchCompositeANDGates_.clear();
}

std::vector<bool> SecretShareEngine::computeBatchANDImmediately(
    const std::vector<bool>& left,
    const std::vector<bool>& right) {
  if (left.size() != right.size()) {
    throw std::runtime_error("Left and right must have equal length");
  }
  std::vector<ScheduledAND> scheduledANDs;
  scheduledANDs.reserve(left.size());
  std::vector<ScheduledBatchAND> scheduledBatchANDs;
  std::vector<ScheduledCompositeAND> scheduledCompositeANDs;
  std::vector<ScheduledBatchCompositeAND> scheduledBatchCompositeANDs;
  for (size_t i = 0; i < left.size(); i++) {
    scheduledANDs.push_back(ScheduledAND(left.at(i), right.at(i)));
  }
  return computeAllANDsFromScheduledANDs(
             scheduledANDs,
             scheduledBatchANDs,
             scheduledCompositeANDs,
             scheduledBatchCompositeANDs)
      .andResults;
}

//======== Below are API's to retrieve non-free AND results: ========

bool SecretShareEngine::getANDExecutionResult(uint32_t index) const {
  return executionResults_.andResults.at(index);
}

const std::vector<bool>& SecretShareEngine::getBatchANDExecutionResult(
    uint32_t index) const {
  return executionResults_.batchANDResults.at(index);
}

const std::vector<bool>& SecretShareEngine::getCompositeANDExecutionResult(
    uint32_t index) const {
  return executionResults_.compositeANDResults.at(index);
}

const std::vector<std::vector<bool>>&
SecretShareEngine::getBatchCompositeANDExecutionResult(uint32_t index) const {
  return executionResults_.compositeBatchANDResults.at(index);
}

SecretShareEngine::ExecutionResults
SecretShareEngine::computeAllANDsFromScheduledANDs(
    std::vector<ScheduledAND>& ands,
    std::vector<ScheduledBatchAND>& batchAnds,
    std::vector<ScheduledCompositeAND>& compositeAnds,
    std::vector<ScheduledBatchCompositeAND>& batchCompositeAnds) {
  size_t normalTupleCount = ands.size();

  for (size_t i = 0; i < batchAnds.size(); i++) {
    normalTupleCount += batchAnds[i].getLeft().size();
  }

  // temporary for now until multi party case supports composite AND's
  if (tupleGenerator_->supportsCompositeTupleGeneration()) {
    std::map<size_t, uint32_t> compositeTupleCount;
    size_t openedSecretCount = normalTupleCount * 2;

    for (size_t i = 0; i < compositeAnds.size(); i++) {
      size_t compositeSize = compositeAnds[i].getRights().size();
      compositeTupleCount.emplace(compositeSize, 0);
      compositeTupleCount[compositeSize]++;
      openedSecretCount += 1 + compositeSize;
    }

    for (size_t i = 0; i < batchCompositeAnds.size(); i++) {
      size_t compositeSize = batchCompositeAnds[i].getRights().size();
      size_t batchSize = batchCompositeAnds[i].getLeft().size();
      compositeTupleCount.emplace(compositeSize, 0);
      compositeTupleCount[compositeSize] += batchSize;
      openedSecretCount += batchSize * (1 + compositeSize);
    }

    if (normalTupleCount == 0 && compositeTupleCount.empty()) {
      return {
          std::vector<bool>(),
          std::vector<std::vector<bool>>(),
          std::vector<std::vector<bool>>(),
          std::vector<std::vector<std::vector<bool>>>()};
    }

    auto [normalTuples, compositeTuples] =
        tupleGenerator_->getNormalAndCompositeBooleanTuples(
            normalTupleCount, compositeTupleCount);
    auto secretsToOpen = computeSecretSharesToOpen(
        ands,
        batchAnds,
        compositeAnds,
        batchCompositeAnds,
        normalTuples,
        compositeTuples,
        openedSecretCount);

    auto openedSecrets =
        communicationAgent_->openSecretsToAll(std::move(secretsToOpen));
    if (openedSecrets.size() != openedSecretCount) {
      throw std::runtime_error("unexpected number of opened secrets");
    }

    return computeExecutionResultsFromOpenedShares(
        ands,
        batchAnds,
        compositeAnds,
        batchCompositeAnds,
        openedSecrets,
        normalTuples,
        compositeTuples);

  } else {
    for (size_t i = 0; i < compositeAnds.size(); i++) {
      normalTupleCount += compositeAnds[i].getRights().size();
    }

    for (size_t i = 0; i < batchCompositeAnds.size(); i++) {
      size_t compositeSize = batchCompositeAnds[i].getRights().size();
      size_t batchSize = batchCompositeAnds[i].getLeft().size();

      normalTupleCount += compositeSize * batchSize;
    }

    if (normalTupleCount == 0) {
      return {
          std::vector<bool>(),
          std::vector<std::vector<bool>>(),
          std::vector<std::vector<bool>>(),
          std::vector<std::vector<std::vector<bool>>>()};
    }
    auto tuples = tupleGenerator_->getBooleanTuple(normalTupleCount);
    auto secretsToOpen = computeSecretSharesToOpenLegacy(
        ands, batchAnds, compositeAnds, batchCompositeAnds, tuples);

    auto openedSecrets =
        communicationAgent_->openSecretsToAll(std::move(secretsToOpen));

    if (openedSecrets.size() != normalTupleCount * 2) {
      throw std::runtime_error("unexpected number of opened secrets");
    }

    return computeExecutionResultsFromOpenedSharesLegacy(
        ands,
        batchAnds,
        compositeAnds,
        batchCompositeAnds,
        openedSecrets,
        tuples);
  }
}

std::vector<bool> SecretShareEngine::computeSecretSharesToOpen(
    std::vector<ScheduledAND>& ands,
    std::vector<ScheduledBatchAND>& batchAnds,
    std::vector<ScheduledCompositeAND>& compositeAnds,
    std::vector<ScheduledBatchCompositeAND>& batchCompositeAnds,
    std::vector<tuple_generator::ITupleGenerator::BooleanTuple>& normalTuples,
    std::map<
        size_t,
        std::vector<tuple_generator::ITupleGenerator::CompositeBooleanTuple>>&
        compositeTuples,
    size_t openedSecretCount) {
  /* order of secrets:
  * [...andSecrets, // len = 2n
  *  ...batchAndSecrets, // len = sum(batchSize * 2)
     ...compositeAndSecrets, // len = sum(compositeSize + 1)
     ...batchCompositeAndSecrets, // len = sum(batchSize * (1 + compositeSize))
     ]
  */
  std::vector<bool> secretsToOpen(openedSecretCount);
  std::unordered_map<size_t, size_t> compositeTupleSizeToIndex =
      std::unordered_map<size_t, size_t>();

  size_t tupleIndex = 0;
  for (size_t i = 0; i < ands.size(); i++) {
    secretsToOpen[tupleIndex * 2] =
        ands[i].getLeft() ^ normalTuples.at(tupleIndex).getA();
    secretsToOpen[tupleIndex * 2 + 1] =
        ands[i].getRight() ^ normalTuples.at(tupleIndex).getB();
    tupleIndex++;
  }

  for (size_t i = 0; i < batchAnds.size(); i++) {
    auto& leftValues = batchAnds[i].getLeft();
    auto& rightValues = batchAnds[i].getRight();
    for (int j = 0; j < leftValues.size(); j++) {
      secretsToOpen[tupleIndex * 2] =
          leftValues[j] ^ normalTuples.at(tupleIndex).getA();
      secretsToOpen[tupleIndex * 2 + 1] =
          rightValues.at(j) ^ normalTuples.at(tupleIndex).getB();
      tupleIndex++;
    }
  }

  size_t secretIndex = tupleIndex * 2;
  for (size_t i = 0; i < compositeAnds.size(); i++) {
    size_t compositeSize = compositeAnds[i].getRights().size();
    compositeTupleSizeToIndex.emplace(compositeSize, 0);

    tupleIndex = compositeTupleSizeToIndex[compositeSize]++;
    auto tuple = compositeTuples[compositeSize][tupleIndex];
    secretsToOpen[secretIndex++] = compositeAnds[i].getLeft() ^ tuple.getA();

    for (size_t j = 0; j < compositeSize; j++) {
      secretsToOpen[secretIndex++] =
          compositeAnds[i].getRights()[j] ^ tuple.getB()[j];
    }
  }

  for (size_t i = 0; i < batchCompositeAnds.size(); i++) {
    auto& leftValues = batchCompositeAnds[i].getLeft();
    auto& rightValues = batchCompositeAnds[i].getRights();

    size_t batchSize = leftValues.size();
    size_t compositeSize = rightValues.size();
    compositeTupleSizeToIndex.emplace(compositeSize, 0);

    for (size_t j = 0; j < batchSize; j++) {
      tupleIndex = compositeTupleSizeToIndex[compositeSize]++;
      auto tuple = compositeTuples[compositeSize][tupleIndex];
      secretsToOpen[secretIndex++] = leftValues[j] ^ tuple.getA();
      for (size_t k = 0; k < compositeSize; k++) {
        secretsToOpen[secretIndex++] = rightValues[k][j] ^ tuple.getB()[k];
      }
    }
  }

  return secretsToOpen;
}

SecretShareEngine::ExecutionResults
SecretShareEngine::computeExecutionResultsFromOpenedShares(
    std::vector<ScheduledAND>& ands,
    std::vector<ScheduledBatchAND>& batchAnds,
    std::vector<ScheduledCompositeAND>& compositeAnds,
    std::vector<ScheduledBatchCompositeAND>& batchCompositeAnds,
    std::vector<bool>& openedSecrets,
    std::vector<tuple_generator::ITupleGenerator::BooleanTuple>& normalTuples,
    std::map<
        size_t,
        std::vector<tuple_generator::ITupleGenerator::CompositeBooleanTuple>>&
        compositeTuples) {
  std::vector<bool> andResults;
  andResults.reserve(ands.size());
  std::vector<std::vector<bool>> batchAndResults;
  batchAndResults.reserve(batchAnds.size());
  std::vector<std::vector<bool>> compositeAndResults;
  compositeAndResults.reserve(compositeAnds.size());
  std::vector<std::vector<std::vector<bool>>> compositeBatchAndResults;
  compositeBatchAndResults.reserve(batchCompositeAnds.size());

  size_t normalTupleIndex = 0;

  for (size_t i = 0; i < ands.size(); i++) {
    bool val = normalTuples.at(normalTupleIndex).getC() ^
        (openedSecrets.at(2 * normalTupleIndex) &
         normalTuples.at(normalTupleIndex).getB()) ^
        (openedSecrets.at(2 * normalTupleIndex + 1) &
         normalTuples.at(normalTupleIndex).getA());
    if (myId_ == 0) {
      val = val ^
          (openedSecrets.at(2 * normalTupleIndex) &
           openedSecrets.at(2 * normalTupleIndex + 1));
    }
    andResults.push_back(val);
    normalTupleIndex++;
  }

  for (size_t i = 0; i < batchAnds.size(); i++) {
    auto& leftValues = batchAnds[i].getLeft();
    auto batchSize = leftValues.size();
    std::vector<bool> rst(batchSize);
    for (int j = 0; j < batchSize; j++) {
      bool val = normalTuples.at(normalTupleIndex).getC() ^
          (openedSecrets.at(2 * normalTupleIndex) &
           normalTuples.at(normalTupleIndex).getB()) ^
          (openedSecrets.at(2 * normalTupleIndex + 1) &
           normalTuples.at(normalTupleIndex).getA());
      if (myId_ == 0) {
        val = val ^
            (openedSecrets.at(2 * normalTupleIndex) &
             openedSecrets.at(2 * normalTupleIndex + 1));
      }
      rst[j] = val;
      normalTupleIndex++;
    }
    batchAndResults.push_back(std::move(rst));
  }

  std::unordered_map<size_t, size_t> compositeTupleSizeToIndex =
      std::unordered_map<size_t, size_t>();

  size_t secretIndex = normalTupleIndex * 2;
  for (size_t i = 0; i < compositeAnds.size(); i++) {
    auto compositeSize = compositeAnds[i].getRights().size();
    compositeTupleSizeToIndex.emplace(compositeSize, 0);
    auto compositeTupleIndex = compositeTupleSizeToIndex[compositeSize]++;
    auto tuple = compositeTuples.at(compositeSize).at(compositeTupleIndex);

    auto leftSecretIndex = secretIndex++;
    std::vector<bool> rst(compositeSize);

    for (size_t j = 0; j < compositeSize; j++) {
      bool val = tuple.getC()[j] ^
          (openedSecrets.at(leftSecretIndex) & tuple.getB()[j]) ^
          (openedSecrets.at(secretIndex) & tuple.getA());
      if (myId_ == 0) {
        val = val ^
            (openedSecrets.at(leftSecretIndex) & openedSecrets.at(secretIndex));
      }
      rst[j] = (val);
      secretIndex++;
    }
    compositeAndResults.push_back(std::move(rst));
  }

  for (size_t i = 0; i < batchCompositeAnds.size(); i++) {
    auto batchSize = batchCompositeAnds[i].getLeft().size();
    auto compositeSize = batchCompositeAnds[i].getRights().size();
    compositeTupleSizeToIndex.emplace(compositeSize, 0);

    std::vector<std::vector<bool>> compositeResult(
        compositeSize, std::vector<bool>(batchSize));
    for (int j = 0; j < batchSize; j++) {
      auto compositeTupleIndex = compositeTupleSizeToIndex[compositeSize]++;
      auto tuple = compositeTuples[compositeSize][compositeTupleIndex];
      auto leftSecretIndex = secretIndex++;

      for (int k = 0; k < compositeSize; k++) {
        bool val = tuple.getC().at(k) ^
            (openedSecrets.at(leftSecretIndex) & tuple.getB()[k]) ^
            (openedSecrets.at(secretIndex) & tuple.getA());
        if (myId_ == 0) {
          val = val ^
              (openedSecrets.at(leftSecretIndex) &
               openedSecrets.at(secretIndex));
        }
        compositeResult[k][j] = val;
        secretIndex++;
      }
    }
    compositeBatchAndResults.push_back(std::move(compositeResult));
  }

  return {
      std::move(andResults),
      std::move(batchAndResults),
      std::move(compositeAndResults),
      std::move(compositeBatchAndResults)};
}

std::vector<bool> SecretShareEngine::computeSecretSharesToOpenLegacy(
    std::vector<ScheduledAND>& ands,
    std::vector<ScheduledBatchAND>& batchAnds,
    std::vector<ScheduledCompositeAND>& compositeAnds,
    std::vector<ScheduledBatchCompositeAND>& batchCompositeAnds,
    std::vector<tuple_generator::ITupleGenerator::BooleanTuple>& tuples) {
  std::vector<bool> secretsToOpen(tuples.size() * 2);

  size_t index = 0;
  for (size_t i = 0; i < ands.size(); i++) {
    secretsToOpen[index * 2] = ands[i].getLeft() ^ tuples.at(index).getA();
    secretsToOpen[index * 2 + 1] = ands[i].getRight() ^ tuples.at(index).getB();
    index++;
  }

  for (size_t i = 0; i < batchAnds.size(); i++) {
    auto& leftValues = batchAnds[i].getLeft();
    auto& rightValues = batchAnds[i].getRight();
    for (size_t j = 0; j < leftValues.size(); j++) {
      secretsToOpen[index * 2] = leftValues[j] ^ tuples.at(index).getA();
      secretsToOpen[index * 2 + 1] =
          rightValues.at(j) ^ tuples.at(index).getB();
      index++;
    }
  }

  for (size_t i = 0; i < compositeAnds.size(); i++) {
    for (size_t j = 0; j < compositeAnds[i].getRights().size(); j++) {
      secretsToOpen[index * 2] =
          compositeAnds[i].getLeft() ^ tuples.at(index).getA();
      secretsToOpen[index * 2 + 1] =
          compositeAnds[i].getRights()[j] ^ tuples.at(index).getB();
      index++;
    }
  }

  for (size_t i = 0; i < batchCompositeAnds.size(); i++) {
    auto& leftValues = batchCompositeAnds[i].getLeft();
    for (auto& rightValues : batchCompositeAnds[i].getRights()) {
      for (size_t j = 0; j < leftValues.size(); j++) {
        secretsToOpen[index * 2] = leftValues[j] ^ tuples.at(index).getA();
        secretsToOpen[index * 2 + 1] =
            rightValues.at(j) ^ tuples.at(index).getB();
        index++;
      }
    }
  }

  return secretsToOpen;
}

SecretShareEngine::ExecutionResults
SecretShareEngine::computeExecutionResultsFromOpenedSharesLegacy(
    std::vector<ScheduledAND>& ands,
    std::vector<ScheduledBatchAND>& batchAnds,
    std::vector<ScheduledCompositeAND>& compositeAnds,
    std::vector<ScheduledBatchCompositeAND>& batchCompositeAnds,
    std::vector<bool>& openedSecrets,
    std::vector<tuple_generator::ITupleGenerator::BooleanTuple>& tuples) {
  std::vector<bool> andResults;
  andResults.reserve(ands.size());
  std::vector<std::vector<bool>> batchAndResults;
  batchAndResults.reserve(batchAnds.size());
  std::vector<std::vector<bool>> compositeAndResults;
  compositeAndResults.reserve(compositeAnds.size());
  std::vector<std::vector<std::vector<bool>>> compositeBatchAndResults;
  compositeBatchAndResults.reserve(batchCompositeAnds.size());
  size_t index = 0;

  for (size_t i = 0; i < ands.size(); i++) {
    bool val = tuples.at(index).getC() ^
        (openedSecrets.at(2 * index) & tuples.at(index).getB()) ^
        (openedSecrets.at(2 * index + 1) & tuples.at(index).getA());
    if (myId_ == 0) {
      val =
          val ^ (openedSecrets.at(2 * index) & openedSecrets.at(2 * index + 1));
    }
    andResults.push_back(val);
    index++;
  }

  for (size_t i = 0; i < batchAnds.size(); i++) {
    auto& leftValues = batchAnds[i].getLeft();
    auto batchSize = leftValues.size();
    std::vector<bool> rst(batchSize);
    for (size_t j = 0; j < batchSize; j++) {
      bool val = tuples.at(index).getC() ^
          (openedSecrets.at(2 * index) & tuples.at(index).getB()) ^
          (openedSecrets.at(2 * index + 1) & tuples.at(index).getA());
      if (myId_ == 0) {
        val = val ^
            (openedSecrets.at(2 * index) & openedSecrets.at(2 * index + 1));
      }
      rst[j] = val;
      index++;
    }
    batchAndResults.push_back(std::move(rst));
  }

  for (size_t i = 0; i < compositeAnds.size(); i++) {
    auto outputSize = compositeAnds[i].getRights().size();
    std::vector<bool> rst(outputSize);
    for (size_t j = 0; j < outputSize; j++) {
      bool val = tuples.at(index).getC() ^
          (openedSecrets.at(2 * index) & tuples.at(index).getB()) ^
          (openedSecrets.at(2 * index + 1) & tuples.at(index).getA());
      if (myId_ == 0) {
        val = val ^
            (openedSecrets.at(2 * index) & openedSecrets.at(2 * index + 1));
      }
      rst[j] = (val);
      index++;
    }
    compositeAndResults.push_back(std::move(rst));
  }

  for (size_t i = 0; i < batchCompositeAnds.size(); i++) {
    auto& leftValues = batchCompositeAnds[i].getLeft();
    auto batchSize = leftValues.size();
    auto outputSize = batchCompositeAnds[i].getRights().size();
    std::vector<std::vector<bool>> compositeResult(outputSize);
    for (size_t j = 0; j < outputSize; j++) {
      std::vector<bool> innerBatchResult(batchSize);
      for (size_t k = 0; k < batchSize; k++) {
        bool val = tuples.at(index).getC() ^
            (openedSecrets.at(2 * index) & tuples.at(index).getB()) ^
            (openedSecrets.at(2 * index + 1) & tuples.at(index).getA());
        if (myId_ == 0) {
          val = val ^
              (openedSecrets.at(2 * index) & openedSecrets.at(2 * index + 1));
        }
        innerBatchResult[k] = val;
        index++;
      }
      compositeResult[j] = std::move(innerBatchResult);
    }
    compositeBatchAndResults.push_back(std::move(compositeResult));
  }

  return {
      andResults,
      batchAndResults,
      compositeAndResults,
      compositeBatchAndResults};
}

std::vector<bool> SecretShareEngine::revealToParty(
    int id,
    const std::vector<bool>& output) const {
  return communicationAgent_->openSecretsToParty(id, output);
}

} // namespace fbpcf::engine
