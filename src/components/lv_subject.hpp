#ifndef LILYGO_UI_MONITOR_LV_SUBJECT_HPP
#define LILYGO_UI_MONITOR_LV_SUBJECT_HPP

#include <lvgl.h>

#include <array>
#include <cstddef>
#include <cstdint>

class IntSubject {
public:
    explicit IntSubject(std::int32_t value, std::int32_t minimum = INT32_MIN, std::int32_t maximum = INT32_MAX)
    {
        lv_subject_init_int(&subject_, value);
        lv_subject_set_min_value_int(&subject_, minimum);
        lv_subject_set_max_value_int(&subject_, maximum);
    }

    ~IntSubject()
    {
        lv_subject_deinit(&subject_);
    }

    IntSubject(const IntSubject &)            = delete;
    IntSubject &operator=(const IntSubject &) = delete;
    IntSubject(IntSubject &&)                 = delete;
    IntSubject &operator=(IntSubject &&)      = delete;

    void set(std::int32_t value)
    {
        lv_subject_set_int(&subject_, value);
    }

    [[nodiscard]] std::int32_t value() const
    {
        return lv_subject_get_int(const_cast<lv_subject_t *>(&subject_));
    }

    [[nodiscard]] lv_subject_t *get() noexcept
    {
        return &subject_;
    }

private:
    lv_subject_t subject_{};
};

template <std::size_t Capacity>
class StringSubject {
public:
    explicit StringSubject(const char *value = "")
    {
        static_assert(Capacity > 1, "StringSubject needs room for a terminator");
        lv_subject_init_string(&subject_, value_.data(), previous_.data(), Capacity, value);
    }

    ~StringSubject()
    {
        lv_subject_deinit(&subject_);
    }

    StringSubject(const StringSubject &)            = delete;
    StringSubject &operator=(const StringSubject &) = delete;
    StringSubject(StringSubject &&)                 = delete;
    StringSubject &operator=(StringSubject &&)      = delete;

    void set(const char *value)
    {
        lv_subject_copy_string(&subject_, value ? value : "");
    }

    [[nodiscard]] const char *value() const noexcept
    {
        return lv_subject_get_string(const_cast<lv_subject_t *>(&subject_));
    }

    [[nodiscard]] lv_subject_t *get() noexcept
    {
        return &subject_;
    }

private:
    std::array<char, Capacity> value_{};
    std::array<char, Capacity> previous_{};
    lv_subject_t subject_{};
};

#endif
