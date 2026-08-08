#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <kt_node.h>

namespace ktnode {

class Error : public std::runtime_error {
 public:
  explicit Error(const std::string& message) : std::runtime_error(message) {}
};

inline kt_string_view_t string_view(std::string_view value) {
  return kt_string_view_t{value.data(), static_cast<uint64_t>(value.size())};
}

inline kt_bytes_view_t bytes_view(const std::vector<uint8_t>& value) {
  return kt_bytes_view_t{value.data(), static_cast<uint64_t>(value.size())};
}

enum class NextStep : uint32_t {
  Continue = KT_ALGORITHM_CONTINUE,
  Stop = KT_ALGORITHM_STOP,
  Recoverable = KT_ALGORITHM_RECOVERABLE,
  Fatal = KT_ALGORITHM_FATAL,
};

enum class ReadMode : uint32_t {
  One = KT_READ_ONE,
  AllAvailable = KT_READ_ALL_AVAILABLE,
  Count = KT_READ_COUNT,
};

struct Message {
  std::vector<uint8_t> payload;
  std::string source_id;
  bool has_source = false;
  int64_t remote_time_ns = 0;
  bool has_remote_time = false;
};

class Context {
 public:
  explicit Context(kt_algorithm_context_t* context) : context_(context) {}

  void set(std::string_view channel, const std::vector<uint8_t>& payload) {
    kt_error_t* error = nullptr;
    kt_status_t status = kt_context_set(context_, string_view(channel), bytes_view(payload), &error);
    if (status != KT_STATUS_OK) {
      std::string message = take_error(error, "kt_context_set failed");
      throw Error(message);
    }
  }

  std::vector<Message> get(std::string_view channel, ReadMode mode = ReadMode::One, uint64_t count = 0) {
    kt_read_options_v1 options{};
    options.struct_size = sizeof(options);
    options.abi_version = KT_ABI_VERSION_MAJOR;
    options.mode = static_cast<kt_read_mode_t>(mode);
    options.count = count;
    kt_message_batch_t* batch = nullptr;
    kt_error_t* error = nullptr;
    kt_status_t status = kt_context_read(context_, string_view(channel), &options, &batch, &error);
    if (status != KT_STATUS_OK) throw Error(take_error(error, "kt_context_read failed"));
    if (!batch) return {};
    std::vector<Message> messages;
    const uint64_t total = kt_message_batch_count(batch);
    messages.reserve(static_cast<size_t>(total));
    for (uint64_t i = 0; i < total; ++i) {
      kt_message_view_v1 item{};
      item.struct_size = sizeof(item);
      item.abi_version = KT_ABI_VERSION_MAJOR;
      kt_error_t* item_error = nullptr;
      status = kt_message_batch_item(batch, i, &item, &item_error);
      if (status != KT_STATUS_OK) {
        kt_message_batch_destroy(&batch);
        throw Error(take_error(item_error, "kt_message_batch_item failed"));
      }
      Message message;
      message.payload.assign(item.payload.data, item.payload.data + item.payload.length);
      if (item.has_source) {
        message.source_id.assign(item.source_id.data, item.source_id.length);
        message.has_source = true;
      }
      if (item.has_remote_time) {
        message.remote_time_ns = item.remote_time_ns;
        message.has_remote_time = true;
      }
      messages.push_back(std::move(message));
    }
    kt_message_batch_destroy(&batch);
    return messages;
  }

  std::string metrics_json() {
    kt_owned_bytes_t* output = nullptr;
    kt_error_t* error = nullptr;
    kt_status_t status = kt_context_metrics_json(context_, &output, &error);
    if (status != KT_STATUS_OK) throw Error(take_error(error, "kt_context_metrics_json failed"));
    if (!output) return {};
    kt_bytes_view_t view = kt_owned_bytes_view(output);
    std::string result;
    if (view.data && view.length > 0) {
      result.assign(reinterpret_cast<const char*>(view.data), static_cast<size_t>(view.length));
    }
    kt_owned_bytes_destroy(&output);
    return result;
  }

  void request_close() {
    kt_context_request_close(context_);
  }

 private:
  static std::string take_error(kt_error_t* error, const char* fallback) {
    if (!error) return fallback;
    kt_string_view_t message = kt_error_message(error);
    std::string text(message.data, message.length);
    kt_error_destroy(&error);
    return text.empty() ? fallback : text;
  }

  kt_algorithm_context_t* context_;
};

class Node {
 public:
  virtual ~Node() = default;
  virtual NextStep setup(Context&) { return NextStep::Continue; }
  virtual NextStep step(Context&) { return NextStep::Stop; }
  virtual NextStep close(Context&) { return NextStep::Stop; }
};

class Runtime {
 public:
  Runtime(std::string package_path, std::string runtime_path, Node& node)
      : package_path_(std::move(package_path)), runtime_path_(std::move(runtime_path)), node_(node) {}

  Runtime(const Runtime&) = delete;
  Runtime& operator=(const Runtime&) = delete;

  ~Runtime() {
    if (runtime_) {
      kt_error_t* error = nullptr;
      kt_runtime_destroy(&runtime_, &error);
      if (error) kt_error_destroy(&error);
    }
  }

  void run() {
    kt_algorithm_callbacks_v1 callbacks{};
    callbacks.struct_size = sizeof(callbacks);
    callbacks.abi_version = KT_ABI_VERSION_MAJOR;
    callbacks.setup = &Runtime::setup_callback;
    callbacks.step = &Runtime::step_callback;
    callbacks.close = &Runtime::close_callback;

    kt_runtime_options_v1 options{};
    options.struct_size = sizeof(options);
    options.abi_version = KT_ABI_VERSION_MAJOR;
    options.package_path = string_view(package_path_);
    options.runtime_path = string_view(runtime_path_);
    options.callbacks = &callbacks;
    options.user_data = this;

    kt_error_t* error = nullptr;
    kt_status_t status = kt_runtime_create_v1(&options, &runtime_, &error);
    if (status != KT_STATUS_OK) throw Error(take_error(error, "kt_runtime_create_v1 failed"));
    status = kt_runtime_run(runtime_, &error);
    if (status != KT_STATUS_OK) throw Error(take_error(error, "kt_runtime_run failed"));
  }

 private:
  static std::string take_error(kt_error_t* error, const char* fallback) {
    if (!error) return fallback;
    kt_string_view_t message = kt_error_message(error);
    std::string text(message.data, message.length);
    kt_error_destroy(&error);
    return text.empty() ? fallback : text;
  }

  static kt_algorithm_outcome_t setup_callback(void* user_data, kt_algorithm_context_t* raw_context) {
    try {
      Context context(raw_context);
      return static_cast<kt_algorithm_outcome_t>(static_cast<Runtime*>(user_data)->node_.setup(context));
    } catch (...) {
      return KT_ALGORITHM_FATAL;
    }
  }

  static kt_algorithm_outcome_t step_callback(void* user_data, kt_algorithm_context_t* raw_context) {
    try {
      Context context(raw_context);
      NextStep next = static_cast<Runtime*>(user_data)->node_.step(context);
      if (next == NextStep::Stop) context.request_close();
      return static_cast<kt_algorithm_outcome_t>(next);
    } catch (...) {
      return KT_ALGORITHM_FATAL;
    }
  }

  static kt_algorithm_outcome_t close_callback(void* user_data, kt_algorithm_context_t* raw_context) {
    try {
      Context context(raw_context);
      return static_cast<kt_algorithm_outcome_t>(static_cast<Runtime*>(user_data)->node_.close(context));
    } catch (...) {
      return KT_ALGORITHM_FATAL;
    }
  }

  std::string package_path_;
  std::string runtime_path_;
  Node& node_;
  kt_runtime_t* runtime_ = nullptr;
};

inline void run(const std::string& package_path, const std::string& runtime_path, Node& node) {
  Runtime runtime(package_path, runtime_path, node);
  runtime.run();
}

}  // namespace ktnode
