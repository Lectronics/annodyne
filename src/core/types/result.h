#pragma once

#include "core/types/contract.h"
#include "core/types/error.h"

#include <functional>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

// Result<T>: a value or an Error, shaped like C++23 std::expected<T, Error> (the project is C++20).
// Differences from std::expected, by design: value()/error() on the wrong state is a contract
// violation (abort), not an exception; the error type is always melo::Error.
// The class is [[nodiscard]], so ignoring a returned Result is a compile error under -Werror.
// Design: docs/design/phase0-foundations.md §5.

namespace melo {

template <class T>
class Result;

namespace detail {

template <class T>
struct IsResult : std::false_type {};
template <class T>
struct IsResult<Result<T>> : std::true_type {};

template <class From, class To>
concept non_narrowing_from = requires(From&& from) { To{std::forward<From>(from)}; };

template <class U, class T>
concept result_value_source = !std::is_same_v<std::remove_cvref_t<U>, Result<T>> &&
                              !std::is_same_v<std::remove_cvref_t<U>, Unexpected> &&
                              !std::is_same_v<std::remove_cvref_t<U>, Error> &&
                              std::is_constructible_v<T, U&&> && non_narrowing_from<U, T>;

}  // namespace detail

template <class T>
class [[nodiscard]] Result {
    static_assert(!std::is_reference_v<T>, "Result<T&> is not supported; return a value");
    static_assert(!std::is_same_v<std::remove_cv_t<T>, Error>, "Result<Error> is ambiguous");
    static_assert(!std::is_same_v<std::remove_cv_t<T>, Unexpected>,
                  "Result<Unexpected> is ambiguous");

public:
    using value_type = T;

    /// Success. Implicit when U converts implicitly to T, so `return value;` works.
    template <class U = T>
        requires detail::result_value_source<U, T>
    // NOLINTNEXTLINE(cppcoreguidelines-explicit-constructor): implicit like std::expected
    explicit(!std::is_convertible_v<U&&, T>) Result(U&& value)
        : state_{std::in_place_index<0>, std::forward<U>(value)} {}

    /// Failure, so `return fail(code, "...");` works.
    // NOLINTNEXTLINE(cppcoreguidelines-explicit-constructor): implicit like std::expected
    Result(Unexpected failure) : state_{std::in_place_index<1>, std::move(failure.error)} {}

    [[nodiscard]] bool has_value() const noexcept { return state_.index() == 0; }
    explicit operator bool() const noexcept { return has_value(); }

    /// Precondition: has_value().
    [[nodiscard]] T& value() & { return *checked_value(this); }
    [[nodiscard]] const T& value() const& { return *checked_value(this); }
    [[nodiscard]] T value() && { return std::move(*checked_value(this)); }

    /// Precondition: !has_value().
    [[nodiscard]] const Error& error() const& { return *checked_error(this); }
    [[nodiscard]] Error error() && { return std::move(*checked_error(this)); }

    [[nodiscard]] T& operator*() & { return value(); }
    [[nodiscard]] const T& operator*() const& { return value(); }
    [[nodiscard]] T operator*() && { return std::move(*this).value(); }
    [[nodiscard]] T* operator->() { return checked_value(this); }
    [[nodiscard]] const T* operator->() const { return checked_value(this); }

    template <class U>
    [[nodiscard]] T value_or(U&& fallback) const& {
        return has_value() ? *std::get_if<0>(&state_) : static_cast<T>(std::forward<U>(fallback));
    }
    template <class U>
    [[nodiscard]] T value_or(U&& fallback) && {
        return has_value() ? std::move(*std::get_if<0>(&state_))
                           : static_cast<T>(std::forward<U>(fallback));
    }

    /// f(value) must return a Result<U>; an error is passed through unchanged.
    template <class F>
    [[nodiscard]] auto and_then(F&& f) & {
        return and_then_impl(*this, std::forward<F>(f));
    }
    template <class F>
    [[nodiscard]] auto and_then(F&& f) const& {
        return and_then_impl(*this, std::forward<F>(f));
    }
    template <class F>
    [[nodiscard]] auto and_then(F&& f) && {
        return and_then_impl(std::move(*this), std::forward<F>(f));
    }

    /// f(value) returns U, giving Result<U> (Result<void> for a void f); errors pass through.
    template <class F>
    [[nodiscard]] auto transform(F&& f) & {
        return transform_impl(*this, std::forward<F>(f));
    }
    template <class F>
    [[nodiscard]] auto transform(F&& f) const& {
        return transform_impl(*this, std::forward<F>(f));
    }
    template <class F>
    [[nodiscard]] auto transform(F&& f) && {
        return transform_impl(std::move(*this), std::forward<F>(f));
    }

private:
    template <class Self>
    static auto checked_value(Self* self) {
        MELO_EXPECTS(self->has_value());
        return std::get_if<0>(&self->state_);
    }
    template <class Self>
    static auto checked_error(Self* self) {
        MELO_EXPECTS(!self->has_value());
        return std::get_if<1>(&self->state_);
    }

    template <class Self, class F>
    static auto and_then_impl(Self&& self, F&& f);
    template <class Self, class F>
    static auto transform_impl(Self&& self, F&& f);

    std::variant<T, Error> state_;
};

/// Success-or-Error for functions with nothing to return. Default-constructed = success.
template <>
class [[nodiscard]] Result<void> {
public:
    using value_type = void;

    Result() noexcept = default;
    // NOLINTNEXTLINE(cppcoreguidelines-explicit-constructor): implicit like std::expected
    Result(Unexpected failure) : error_{std::move(failure.error)} {}

    [[nodiscard]] bool has_value() const noexcept { return !error_.has_value(); }
    explicit operator bool() const noexcept { return has_value(); }

    /// Precondition: has_value(). Exists for symmetry with Result<T>.
    void value() const { MELO_EXPECTS(has_value()); }

    /// Precondition: !has_value().
    [[nodiscard]] const Error& error() const& {
        MELO_EXPECTS(!has_value());
        return *error_;
    }
    [[nodiscard]] Error error() && {
        MELO_EXPECTS(!has_value());
        return std::move(*error_);
    }

    /// f() must return a Result<U>.
    template <class F>
    [[nodiscard]] auto and_then(F&& f) const& {
        using R = std::remove_cvref_t<std::invoke_result_t<F>>;
        static_assert(detail::IsResult<R>::value, "and_then: f must return a melo::Result");
        if (!has_value()) {
            return R{Unexpected{*error_}};
        }
        return R{std::invoke(std::forward<F>(f))};
    }

    /// f() returns U, giving Result<U> (Result<void> for a void f).
    template <class F>
    [[nodiscard]] auto transform(F&& f) const& {
        using U = std::remove_cv_t<std::invoke_result_t<F>>;
        if constexpr (std::is_void_v<U>) {
            if (!has_value()) {
                return Result<void>{Unexpected{*error_}};
            }
            std::invoke(std::forward<F>(f));
            return Result<void>{};
        } else {
            if (!has_value()) {
                return Result<U>{Unexpected{*error_}};
            }
            return Result<U>{std::invoke(std::forward<F>(f))};
        }
    }

private:
    std::optional<Error> error_;
};

template <class T>
template <class Self, class F>
auto Result<T>::and_then_impl(Self&& self, F&& f) {
    using Arg = decltype(std::forward<Self>(self).value());
    using R = std::remove_cvref_t<std::invoke_result_t<F, Arg>>;
    static_assert(detail::IsResult<R>::value, "and_then: f must return a melo::Result");
    if (!self.has_value()) {
        return R{Unexpected{std::forward<Self>(self).error()}};
    }
    return R{std::invoke(std::forward<F>(f), std::forward<Self>(self).value())};
}

template <class T>
template <class Self, class F>
auto Result<T>::transform_impl(Self&& self, F&& f) {
    using Arg = decltype(std::forward<Self>(self).value());
    using U = std::remove_cv_t<std::invoke_result_t<F, Arg>>;
    if constexpr (std::is_void_v<U>) {
        if (!self.has_value()) {
            return Result<void>{Unexpected{std::forward<Self>(self).error()}};
        }
        std::invoke(std::forward<F>(f), std::forward<Self>(self).value());
        return Result<void>{};
    } else {
        if (!self.has_value()) {
            return Result<U>{Unexpected{std::forward<Self>(self).error()}};
        }
        return Result<U>{std::invoke(std::forward<F>(f), std::forward<Self>(self).value())};
    }
}

}  // namespace melo
