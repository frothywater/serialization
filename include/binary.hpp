#pragma once

#include <span>
#include <ranges>
#include <algorithm>
#include <vector>
#include <type_traits>
#include <utility>

#include "boost/pfr.hpp"

namespace binary {
    using bytes = std::vector<std::byte>;
    using bytes_view = std::span<const std::byte>;
    using writable_bytes_view = std::span<std::byte>;

    // MARK: Declarations

    template<typename T>
    struct serializer {
    };

    // MARK: Concepts
    template<typename T> concept regular = std::is_trivial_v<T> && std::is_standard_layout_v<T>;
    template<typename T> concept aggregate = std::is_aggregate_v<T> && !regular<T>;

    template<typename T> concept insertable = requires(T container) {
        { std::inserter(container, std::end(container)) } -> std::same_as<std::insert_iterator<T>>;
    };
    template<typename T> concept iterable = std::ranges::forward_range<T> && insertable<T>;

    template<typename T> concept serializable = requires(T v) {
        { serializer<T>::length(v) }  -> std::same_as<std::size_t>;
        { serializer<T>::write(v, std::declval<writable_bytes_view>()) }  -> std::same_as<std::size_t>;
        { serializer<T>::read(std::declval<bytes_view &>()) }  -> std::same_as<T>;
    };

    // MARK: Helper functions


    // MARK: Core serializers

    template<regular T>
    struct serializer<T> {
        static constexpr bytes_view as_bytes(const T &value) {
            bytes_view data{reinterpret_cast<const std::byte *>(&value), sizeof(T)};
            return data;
        }

        static constexpr auto length(const T &value) { return sizeof(T); }

        static constexpr auto write(const T &value, writable_bytes_view buffer) {
            auto data = as_bytes(value);
            std::copy(data.begin(), data.end(), buffer.data());
            return sizeof(T);
        }

        static constexpr T read(bytes_view &buffer) {
            T value;
            auto data = reinterpret_cast<std::byte *>(&value);
            std::copy_n(buffer.data(), sizeof(T), data);
            buffer = buffer.subspan(sizeof(T));
            return value;
        }
    };

    template<aggregate T>
    struct serializer<T> {
        static constexpr auto length(const T &object) {
            std::size_t bytes_written = 0;
            boost::pfr::for_each_field(object, [&](const auto &field) {
                using field_type = std::remove_cvref_t<decltype(field)>;
                bytes_written += serializer<field_type>::length(field);
            });
            return bytes_written;
        }

        static constexpr auto write(const T &object, writable_bytes_view buffer) {
            std::size_t bytes_written = 0;
            boost::pfr::for_each_field(object, [&](const auto &field) {
                using field_type = std::remove_cvref_t<decltype(field)>;
                bytes_written += serializer<field_type>::write(field, buffer.subspan(bytes_written));
            });
            return bytes_written;
        }

        static constexpr T read(bytes_view &buffer) {
            T object;
            boost::pfr::for_each_field(object, [&](auto &field) {
                using field_type = std::remove_reference_t<decltype(field)>;
                field = serializer<field_type>::read(buffer);
            });
            return object;
        }
    };

    template<iterable T>
    struct serializer<T> {
        using value_type = typename T::value_type;

        static constexpr auto length(const T &container) {
            auto bytes_written = sizeof(std::size_t);
            for (const auto &item: container)
                bytes_written += serializer<value_type>::length(item);
            return bytes_written;
        }

        static constexpr auto write(const T &container, writable_bytes_view buffer) {
            // Serialize the number of elements in the container.
            auto bytes_written = serializer<std::size_t>::write(std::ranges::size(container), buffer);
            for (const auto &item: container)
                bytes_written += serializer<value_type>::write(item, buffer.subspan(bytes_written));
            return bytes_written;
        }

        static constexpr T read(bytes_view &buffer) {
            T container;
            regular auto size = serializer<std::size_t>::read(buffer);
            auto inserter = std::inserter(container, std::end(container));
            for (std::size_t i = 0; i < size; ++i) {
                auto item = serializer<value_type>::read(buffer);
                inserter = std::move(item);
            }
            return container;
        }
    };

    // MARK: Interface functions
    template<serializable T>
    auto dump(const T &obj) {
        bytes buffer{serializer<T>::length(obj)};
        serializer<T>::write(obj, buffer);
        return buffer;
    }

    template<serializable T>
    auto load(bytes_view buffer) {
        return serializer<T>::read(buffer);
    }

}
