
#ifndef KYM_ERROR_HPP_
#define KYM_ERROR_HPP_

#include <iostream>

namespace kym {

enum class StatusCode {
  /// Not an error; returned on success.
  kOk = 0,

  kUnknown = 1,
};

class Status {
 public:
  Status() = default;

  explicit Status(StatusCode status_code, std::string message)
      : code_(status_code), message_(std::move(message)) {}

  bool ok() const { return code_ == StatusCode::kOk; }
  explicit operator bool() const { return ok(); }

  StatusCode code() const { return code_; }
  std::string const& message() const { return message_; }

 private:
  StatusCode code_{StatusCode::kOk};
  std::string message_;
};

inline std::ostream& operator<<(std::ostream& os, Status const& status) {
  return os << status.message();
}


template<typename T>
class StatusOr {
  public:
    StatusOr() : StatusOr(Status(StatusCode::kUnknown, "default")) {}

    StatusOr(Status status) : status_(std::move(status)) {}

    StatusOr(const T value) : value_(std::move(value))  {}

    T value () const { return value_; }

    bool ok() const { return status_.ok(); }
    explicit operator bool() const { return status_.ok(); }

    /**
     * @name Status accessors.
     *
     * @return All these member functions return the (properly ref and
     *     const-qualified) status. If the object contains a value then
     *     `status().ok() == true`.
     */
    Status& status() & { return status_; }
    Status const& status() const& { return status_; }
    Status&& status() && { return std::move(status_); }

  private:
    Status status_;
    T value_;
};

}
#endif // KYM_ERROR_HPP_
