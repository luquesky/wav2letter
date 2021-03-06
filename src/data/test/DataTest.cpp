/**
 * Copyright (c) Facebook, Inc. and its affiliates.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <arrayfire.h>
#include <flashlight/flashlight.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "common/Defines.h"
#include "common/Utils.h"
#include "data/Featurize.h"
#include "data/NumberedFilesLoader.h"
#include "data/W2lListFilesDataset.h"
#include "data/W2lNumberedFilesDataset.h"

using namespace w2l;

namespace {

std::string loadPath = "";

Dictionary getDict() {
  Dictionary dict;
  std::string ltr = "a";
  int alphabet_sz = 26;
  while (alphabet_sz--) {
    dict.addToken(ltr);
    ltr[0] += 1;
  }
  dict.addToken("|");
  dict.addToken("'");
  dict.addToken("L", dict.getIndex("|"));
  dict.addToken("N", dict.getIndex("|"));
  return dict;
}

LexiconMap getLexicon() {
  LexiconMap lexicon;
  lexicon["uh"].push_back({"u", "h"});
  lexicon["oh"].push_back({"o", "h"});
  lexicon[kUnkToken] = {};
  return lexicon;
}
} // namespace

TEST(DataTest, inputFeaturizer) {
  auto dict = getDict();
  auto inputFeaturizer = [](std::vector<std::vector<float>> in,
                            const Dictionary& d) {
    std::vector<W2lLoaderData> data;
    for (const auto& i : in) {
      data.emplace_back();
      data.back().input = i;
    }

    DictionaryMap dicts;
    dicts.insert({kTargetIdx, d});

    auto feat = featurize(data, dicts);
    return af::array(feat.inputDims, feat.input.data());
  };

  std::vector<std::vector<float>> inputs;
  gflags::FlagSaver flagsaver;
  w2l::FLAGS_channels = 2;
  w2l::FLAGS_samplerate = 16000;
  for (int i = 0; i < 10; ++i) {
    inputs.emplace_back(i * w2l::FLAGS_samplerate * w2l::FLAGS_channels);
    for (int j = 0; j < inputs.back().size(); ++j) {
      inputs.back()[j] = std::sin(2 * M_PI * (j / 2) / FLAGS_samplerate);
    }
  }
  auto inArray = inputFeaturizer(inputs, dict);
  ASSERT_EQ(
      inArray.dims(), af::dim4(9 * FLAGS_samplerate, FLAGS_channels, 1, 10));
  af::array ch1 = inArray(af::span, 0, af::span);
  af::array ch2 = inArray(af::span, 1, af::span);
  ASSERT_TRUE(af::max<double>(af::abs(ch1 - ch2)) < 1E-5);

  w2l::FLAGS_mfsc = true;
  inArray = inputFeaturizer(inputs, dict);
  auto nFrames = 1 + (9 * FLAGS_samplerate - 25 * 16) / (10 * 16);
  ASSERT_EQ(inArray.dims(), af::dim4(nFrames, 40, FLAGS_channels, 10));
  ch1 = inArray(af::span, af::span, 0, af::span);
  ch2 = inArray(af::span, af::span, 1, af::span);
  ASSERT_TRUE(af::max<double>(af::abs(ch1 - ch2)) < 1E-5);
}

TEST(DataTest, targetFeaturizer) {
  auto dict = getDict();
  dict.addToken(kEosToken);
  std::vector<std::vector<std::string>> targets = {{"a", "b", "c", "c", "c"},
                                                   {"b", "c", "d", "d"}};

  gflags::FlagSaver flagsaver;
  w2l::FLAGS_replabel = 0;
  w2l::FLAGS_criterion = kCtcCriterion;

  auto targetFeaturizer = [](std::vector<std::vector<std::string>> tgt,
                             const Dictionary& d) {
    std::vector<W2lLoaderData> data;
    for (const auto& t : tgt) {
      data.emplace_back();
      data.back().targets[kTargetIdx] = t;
    }

    DictionaryMap dicts;
    dicts.insert({kTargetIdx, d});

    auto feat = featurize(data, dicts);
    return af::array(
        feat.targetDims[kTargetIdx], feat.targets[kTargetIdx].data());
  };

  auto tgtArray = targetFeaturizer(targets, dict);
  int tgtLen = 5;
  ASSERT_EQ(tgtArray.dims(0), tgtLen);
  ASSERT_EQ(tgtArray(tgtLen - 1, 0).scalar<int>(), 2);
  ASSERT_EQ(tgtArray(tgtLen - 1, 1).scalar<int>(), kTargetPadValue);
  ASSERT_EQ(tgtArray(tgtLen - 2, 1).scalar<int>(), 3);

  w2l::FLAGS_eostoken = true;
  tgtArray = targetFeaturizer(targets, dict);
  tgtLen = 6;
  int eosIdx = dict.getIndex(kEosToken);
  ASSERT_EQ(tgtArray.dims(0), tgtLen);
  ASSERT_EQ(tgtArray(tgtLen - 1, 0).scalar<int>(), eosIdx);
  ASSERT_EQ(tgtArray(tgtLen - 1, 1).scalar<int>(), eosIdx);
  ASSERT_EQ(tgtArray(tgtLen - 2, 1).scalar<int>(), eosIdx);
}

TEST(DataTest, NumberedFilesLoader) {
  NumberedFilesLoader numfilesds(
      w2l::pathsConcat(loadPath, "dataset"), "wav", {{kTargetIdx, "tkn"}});

  ASSERT_EQ(numfilesds.size(), 3);

  auto sample = numfilesds.get(1);
  std::vector<std::string> expectedTarget = {"u", "h", "o", "h"};
  ASSERT_EQ(sample.targets[kTargetIdx].size(), expectedTarget.size());
  for (int i = 0; i < expectedTarget.size(); ++i) {
    ASSERT_EQ(sample.targets[kTargetIdx][i], expectedTarget[i]);
  }

  // ASSERT_EQ(sample.info.samplerate, 8000);
  // ASSERT_EQ(sample.info.frames, 10054);
  // ASSERT_EQ(sample.info.channels, 1);

  ASSERT_EQ(sample.input.size(), 24000);

  ASSERT_NEAR(sample.input[0], 0.03092256002, 1E-6);
  ASSERT_NEAR(sample.input[10], -0.49759358, 1E-6);
  ASSERT_NEAR(sample.input[674], 0.49851027, 1E-6);
  ASSERT_NEAR(sample.input[5000], 0, 1E-6);
  ASSERT_NEAR(sample.input[10000], 0, 1E-6);
}

TEST(DataTest, W2lDataset) {
  gflags::FlagSaver flagsaver;
  w2l::FLAGS_mfcc = false;
  w2l::FLAGS_mfsc = false;
  w2l::FLAGS_pow = false;
  w2l::FLAGS_nthread = 6;
  w2l::FLAGS_replabel = 0;
  w2l::FLAGS_surround = "";
  w2l::FLAGS_dataorder = "none";
  w2l::FLAGS_input = "wav";

  auto dict = getDict();
  DictionaryMap dicts;
  dicts.insert({kTargetIdx, dict});

  W2lNumberedFilesDataset ds(w2l::pathsConcat(loadPath, "dataset"), dicts, 1);

  auto fields = ds.get(0);
  auto& input = fields[kInputIdx];
  auto& target = fields[kTargetIdx];
  std::vector<int> expectedTarget = {20, 7, 20, 7}; // Tokens are "uh-uh"
  ASSERT_EQ(target.dims(), af::dim4(expectedTarget.size()));
  for (int i = 0; i < expectedTarget.size(); ++i) {
    ASSERT_EQ(target(i).scalar<int>(), expectedTarget[i]);
  }
  ASSERT_EQ(input.dims(), af::dim4(24000));
}

TEST(DataTest, W2lListDataset) {
  gflags::FlagSaver flagsaver;
  w2l::FLAGS_mfcc = false;
  w2l::FLAGS_mfsc = false;
  w2l::FLAGS_pow = false;
  w2l::FLAGS_nthread = 6;
  w2l::FLAGS_replabel = 0;
  w2l::FLAGS_surround = "";
  w2l::FLAGS_dataorder = "none";

  // generate the file list
  char* user = getenv("USER");
  std::string userstr = "unknown";
  if (user != nullptr) {
    userstr = std::string(user);
  }
  auto fileList = "/tmp/" + userstr + "_filelist.txt";

  std::ofstream fs(fileList, std::ofstream::out);
  if (!fs.is_open()) {
    throw std::runtime_error("failed to write to " + fileList);
  }

  for (int64_t idx = 0; idx < 3; idx++) {
    std::array<char, 20> fchar;
    snprintf(fchar.data(), fchar.size(), "%09ld.", idx);
    auto audioFile =
        pathsConcat(loadPath, "dataset/" + std::string(fchar.data()) + "wav");
    auto wordFile =
        pathsConcat(loadPath, "dataset/" + std::string(fchar.data()) + "wrd");

    auto info = speech::loadSoundInfo(audioFile.c_str());
    auto durationMs =
        (static_cast<double>(info.frames) / info.samplerate) * 1e3;

    auto targets = loadTarget(wordFile);

    fs << idx << " " << audioFile << " " << durationMs;
    for (auto t : targets) {
      fs << " " << t;
    }
    fs << std::endl;
  }
  fs.close();

  auto dict = getDict();
  auto lexicon = getLexicon();
  DictionaryMap dicts;
  dicts.insert({kTargetIdx, dict});

  W2lListFilesDataset ds(fileList, dicts, lexicon, 1);

  auto fields = ds.get(0);
  auto& input = fields[kInputIdx];
  auto& target = fields[kTargetIdx];
  std::vector<int> expectedTarget = {20, 7, 26, 20, 7}; // "u h | u h"
  ASSERT_EQ(target.dims(), af::dim4(expectedTarget.size()));
  for (int i = 0; i < expectedTarget.size(); ++i) {
    ASSERT_EQ(target(i).scalar<int>(), expectedTarget[i]);
  }
  ASSERT_EQ(input.dims(), af::dim4(24000));
}

TEST(DataTest, W2lDatasetDeterministicSampling) {
  w2l::FLAGS_input = "wav";
  w2l::FLAGS_target = "phn";
  std::string hubPaths = "dataset/train";
  auto dict = createTokenDict(w2l::pathsConcat(loadPath, "dict39.phn"));
  DictionaryMap dicts;
  dicts.insert({kTargetIdx, dict});

  W2lNumberedFilesDataset ds(hubPaths, dicts, 3, 0, 1, loadPath);
  {
    gflags::FlagSaver sv;
    FLAGS_nthread = 2;
    std::unordered_map<int, int> globalBatchIdxToSampleSize;
    int test_rounds = 10;
    for (int round = 0; round < test_rounds; round++) {
      for (int idx = 0; idx < ds.size(); idx++) {
        auto sample = ds.get(idx);
        int batchIdx = ds.getGlobalBatchIdx(idx);
        if (round == 0) {
          globalBatchIdxToSampleSize[batchIdx] = sample[kInputIdx].elements();
        } else {
          ASSERT_EQ(
              globalBatchIdxToSampleSize[batchIdx],
              sample[kInputIdx].elements());
        }
      }
      ds.shuffle(round);
    }
  }
}

TEST(RoundRobinBatchShufflerTest, params) {
  auto packer = RoundRobinBatchPacker(2, 2, 0);
  auto batches = packer.getBatches(11, 0);
  EXPECT_EQ(batches.size(), 3);
  ASSERT_THAT(batches[0], ::testing::ElementsAre(8, 9));
  ASSERT_THAT(batches[1], ::testing::ElementsAre(0, 1));
  ASSERT_THAT(batches[2], ::testing::ElementsAre(4, 5));

  packer = RoundRobinBatchPacker(2, 2, 1);
  batches = packer.getBatches(11, 0);
  EXPECT_EQ(batches.size(), 3);
  ASSERT_THAT(batches[0], ::testing::ElementsAre(10));
  ASSERT_THAT(batches[1], ::testing::ElementsAre(2, 3));
  ASSERT_THAT(batches[2], ::testing::ElementsAre(6, 7));

  // No shuffling
  packer = RoundRobinBatchPacker(2, 2, 0);
  batches = packer.getBatches(11, -1);
  EXPECT_EQ(batches.size(), 3);
  ASSERT_THAT(batches[0], ::testing::ElementsAre(0, 1));
  ASSERT_THAT(batches[1], ::testing::ElementsAre(4, 5));
  ASSERT_THAT(batches[2], ::testing::ElementsAre(8, 9));

  batches = packer.getBatches(10, -1);
  EXPECT_EQ(batches.size(), 3);
  ASSERT_THAT(batches[0], ::testing::ElementsAre(0, 1));
  ASSERT_THAT(batches[1], ::testing::ElementsAre(4, 5));
  ASSERT_THAT(batches[2], ::testing::ElementsAre(8));

  batches = packer.getBatches(9, -1);
  EXPECT_EQ(batches.size(), 2);
  ASSERT_THAT(batches[0], ::testing::ElementsAre(0, 1));
  ASSERT_THAT(batches[1], ::testing::ElementsAre(4, 5));

  batches = packer.getBatches(8, -1);
  EXPECT_EQ(batches.size(), 2);
  ASSERT_THAT(batches[0], ::testing::ElementsAre(0, 1));
  ASSERT_THAT(batches[1], ::testing::ElementsAre(4, 5));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);

  // Resolve directory for data
#ifdef DATA_TEST_DATADIR
  loadPath = DATA_TEST_DATADIR;
#endif

  return RUN_ALL_TESTS();
}
