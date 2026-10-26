// AI translation with llama.cpp (as InkPeek's llm_jni.cpp): greedy decoding, the prompt as the model's template made it.
#include <algorithm>
#include <mutex>
#include <stdexcept>
#include "llama.h"
#include "mt/engines.hpp"

namespace lexipeek::mt {

namespace {
constexpr int CTX = 2048;
void quiet(ggml_log_level, const char *, void *) {}
}  // namespace

struct LlmModel::Impl {
  llama_model *model = nullptr;
  llama_context *ctx = nullptr;
  ~Impl() {
    if (ctx) llama_free(ctx);
    if (model) llama_model_free(model);
  }
};

LlmModel::LlmModel(const std::string &path, int threads) : impl_(std::make_unique<Impl>()) {
  static std::once_flag once;
  std::call_once(once, [] {
    llama_log_set(quiet, nullptr);
    llama_backend_init();
  });
  auto mp = llama_model_default_params();
  mp.load_mode = LLAMA_LOAD_MODE_MMAP;
  mp.use_extra_bufts = false;  // repacked weights would be a second copy in memory
  impl_->model = llama_model_load_from_file(path.c_str(), mp);
  if (!impl_->model) throw std::runtime_error("Cannot load " + path);
  auto cp = llama_context_default_params();
  cp.n_ctx = CTX;
  cp.n_batch = 512;
  cp.n_threads = threads;
  cp.n_threads_batch = threads;
  impl_->ctx = llama_init_from_model(impl_->model, cp);
  if (!impl_->ctx) throw std::runtime_error("Cannot start the model");
}

LlmModel::~LlmModel() = default;

std::string LlmModel::generate(const std::string &prompt, int max_tokens, const std::atomic<bool> *stop) {
  auto *vocab = llama_model_get_vocab(impl_->model);
  llama_memory_clear(llama_get_memory(impl_->ctx), true);
  // The template has the model's special tokens as text: parsed, no extra BOS.
  auto size = static_cast<int32_t>(prompt.size());
  int nt = -llama_tokenize(vocab, prompt.c_str(), size, nullptr, 0, false, true);
  std::vector<llama_token> toks(static_cast<size_t>(std::max(nt, 0)));
  llama_tokenize(vocab, prompt.c_str(), size, toks.data(), nt, false, true);
  if (nt >= CTX - 8) throw std::runtime_error("Text too long for the model");
  int room = std::min(max_tokens, CTX - nt - 1);
  if (llama_decode(impl_->ctx, llama_batch_get_one(toks.data(), nt))) throw std::runtime_error("Decoding failed");
  std::unique_ptr<llama_sampler, decltype(&llama_sampler_free)> smpl(llama_sampler_init_greedy(), llama_sampler_free);
  std::string out;
  for (int i = 0; i < room; i++) {
    if (stop && stop->load()) throw std::runtime_error("Cancelled");
    llama_token t = llama_sampler_sample(smpl.get(), impl_->ctx, -1);
    if (llama_vocab_is_eog(vocab, t)) break;
    char buf[256];
    int k = llama_token_to_piece(vocab, t, buf, sizeof buf, 0, false);
    if (k > 0) out.append(buf, static_cast<size_t>(k));
    // A failed step would return half a translation as a whole one: an error instead.
    if (llama_decode(impl_->ctx, llama_batch_get_one(&t, 1))) throw std::runtime_error("Decoding failed");
  }
  return out;
}

}  // namespace lexipeek::mt
