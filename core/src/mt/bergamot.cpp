// Adapted from RTranslator's BergamotTranslator.cpp (Copyright Luca Martino, Apache License 2.0) through InkPeek.
#include <optional>
#include "common/logging.h"
#include "mt/engines.hpp"
#include "translator/aggregate_batching_pool.h"
#include "translator/parser.h"
#include "translator/response_options.h"
#include "translator/service.h"

namespace lexipeek::mt {

using namespace marian::bergamot;

namespace {

BlockingService &service() {
  // Never destroyed: at exit it would outlive marian's own statics.
  static BlockingService *s = [] {
    marian::setThrowExceptionOnAbort(true);  // a broken model throws instead of aborting the app
    BlockingService::Config sc;
    sc.cacheSize = 0;
    sc.logger.level = "off";
    return new BlockingService(sc);
  }();
  return *s;
}

}  // namespace

struct BergamotModel::Impl {
  std::shared_ptr<TranslationModel> model;
  AggregateBatchingPool pool;  // its own: copies of a model translate on their threads at once
  size_t requests = 0;
};

BergamotModel::BergamotModel(const std::string &config) : impl_(std::make_unique<Impl>()) {
  service();
  impl_->model = std::make_shared<TranslationModel>(parseOptionsFromString(config, /*validate=*/true, /*pathsDir=*/""));
}

BergamotModel::~BergamotModel() = default;

// BlockingService::translateMultiple's steps (no HTML, no cache) on this model's own pool.
std::string BergamotModel::translate(const std::string &text) {
  Response out;
  std::optional<TranslationCache> cache;
  auto request = impl_->model->makeRequest(impl_->requests++, std::string(text), [&](Response &&r) { out = std::move(r); },
                                           ResponseOptions{}, cache);
  impl_->pool.enqueueRequest(impl_->model, request);
  Batch batch;
  marian::Ptr<TranslationModel> model;
  while (impl_->pool.generateBatch(model, batch)) model->translateBatch(0, batch);
  return out.target.text;
}

}  // namespace lexipeek::mt
