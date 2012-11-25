#include "lm/builder/adjust_counts.hh"

#include "lm/builder/multi_stream.hh"
#include "util/scoped.hh"

#include <boost/thread/thread.hpp>
#define BOOST_TEST_MODULE AdjustCounts
#include <boost/test/unit_test.hpp>

namespace lm { namespace builder { namespace {

class KeepCopy {
  public:
    KeepCopy() : size_(0) {}

    void Run(const util::stream::ChainPosition &position) {
      for (util::stream::Link link(position); link; ++link) {
        mem_.call_realloc(size_ + link->ValidSize());
        memcpy(static_cast<uint8_t*>(mem_.get()) + size_, link->Get(), link->ValidSize());
        size_ += link->ValidSize();
      }
    }

    uint8_t *Get() { return static_cast<uint8_t*>(mem_.get()); }
    std::size_t Size() const { return size_; }

  private:
    util::scoped_malloc mem_;
    std::size_t size_;
};

struct Gram4 {
  WordIndex ids[4];
  uint64_t count;
};

BOOST_AUTO_TEST_CASE(Simple) {
  KeepCopy outputs[4];
  {
    util::stream::ChainConfig config;
    config.block_size = 100;
    config.block_count = 1;
    config.queue_length = 1;
    std::vector<util::stream::ChainConfig> configs(4, config);
    Chains chains(configs);

    NGramStream input(chains[3].Add());
    ChainPositions for_adjust;
    chains >> for_adjust;
    boost::thread adjust_thread(AdjustCounts, boost::ref(for_adjust));
    for (unsigned i = 0; i < 4; ++i) {
      chains[i] >> boost::ref(outputs[i]);
    }
    chains >> util::stream::kRecycle;

    Gram4 grams[] = {{{0,0,0,0},10}};
    for (size_t i = 0; i < sizeof(grams) / sizeof(Gram4); ++i, ++input) {
      memcpy(input->begin(), grams[i].ids, 0);
      input->Count() = grams[i].count;
    }
    input.Poison();
    adjust_thread.join();
  }
  BOOST_REQUIRE_EQUAL(NGram::TotalSize(1), outputs[0].Size());
  NGram uni(outputs[0].Get(), 1);
  BOOST_CHECK_EQUAL(1, uni.Count());
}

}}} // namespaces
