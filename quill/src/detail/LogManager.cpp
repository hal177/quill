#include "quill/detail/LogManager.h"

#include "quill/detail/utiliity/Spinlock.h"
#include <condition_variable>

#include "quill/detail/record/CommandRecord.h"

namespace quill::detail
{

/***/
LogManager::LogManager(Config const& config) : _config(config){};

/***/
void LogManager::flush()
{
  if (!_backend_worker.is_running())
  {
    // Backend worker needs to be running, otherwise we are stuck for ever waiting
    return;
  }

  std::mutex mtx;
  std::condition_variable cond;
  bool done = false;

  // notify will be invoked by the backend thread when this message is processed
  auto notify_callback = [&mtx, &cond, &done]() {
    {
      std::lock_guard<std::mutex> const lock{mtx};
      done = true;
    }
    cond.notify_one();
  };

  std::unique_lock<std::mutex> lock(mtx);

  using log_record_t = detail::CommandRecord;
  bool pushed;
  do
  {
    pushed = _thread_context_collection.local_thread_context()->spsc_queue().try_emplace<log_record_t>(notify_callback);
    // unlikely case if the queue gets full we will wait until we can log
  } while (QUILL_UNLIKELY(!pushed));

  // Wait until notify is called
  cond.wait(lock, [&] { return done; });
}

/***/
void LogManager::start_backend_worker() { _backend_worker.run(); }

/***/
void LogManager::stop_backend_worker() { _backend_worker.stop(); }

} // namespace quill::detail