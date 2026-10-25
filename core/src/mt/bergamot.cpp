// Adapted from RTranslator's BergamotTranslator.cpp (Copyright Luca Martino, Apache License 2.0) through InkPeek.
#include "common/logging.h"
#include "mt/engines.hpp"
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
};

BergamotModel::BergamotModel(const std::string &config) : impl_(std::make_unique<Impl>()) {
  service();
  impl_->model = std::make_shared<TranslationModel>(parseOptionsFromString(config, /*validate=*/true, /*pathsDir=*/""));
}

BergamotModel::~BergamotModel() = default;

std::string BergamotModel::translate(const std::string &text) {
  std::vector<std::string> in{text};
  std::vector<ResponseOptions> opts(1);
  auto out = service().translateMultiple(impl_->model, std::move(in), opts);
  return out[0].target.text;
}

}  // namespace lexipeek::mt
