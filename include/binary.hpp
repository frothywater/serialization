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

    // MARK: Concepts
    template<typename T> concept regular = std::is_trivial_v<T> && std::is_standard_layout_v<T>;
    template<typename T> concept aggregate = std::is_aggregate_v<T> && !regular<T>;

    template<typename T> concept insertable = requires(T container) {
        { std::inserter(container, std::end(container)) } -> std::same_as<std::insert_iterator<T>>;
    };
    template<typename T> concept iterable = std::ranges::forward_range<T> && insertable<T>;

    template<typename T> concept serializable = regular<T> || aggregate<T> || iterable<T>;

    // MARK: Declarations
    constexpr auto length(const regular auto &value);

    constexpr auto write(const regular auto &value, writable_bytes_view buffer);

    template<regular T>
    constexpr T read(bytes_view &buffer);

    constexpr auto length(const aggregate auto &object);

    constexpr auto write(const aggregate auto &object, writable_bytes_view buffer);

    template<aggregate T>
    constexpr T read(bytes_view &buffer);

    constexpr auto length(const iterable auto &container);

    constexpr auto write(const iterable auto &container, writable_bytes_view buffer);

    template<iterable T>
    constexpr T read(bytes_view &buffer);

    // MARK: Helper functions
    constexpr bytes_view as_bytes(const regular auto &value) {
        bytes_view data{reinterpret_cast<const std::byte *>(&value), sizeof(value)};
        return data;
    }

    // MARK: Core functions
    constexpr auto length(const regular auto &value) { return sizeof(value); }

    constexpr auto write(const regular auto &value, writable_bytes_view buffer) {
        auto data = as_bytes(value);
        std::copy(data.begin(), data.end(), buffer.data());
        return sizeof(value);
    }

    template<regular T>
    constexpr T read(bytes_view &buffer) {
        T value;
        auto data = reinterpret_cast<std::byte *>(&value);
        std::copy_n(buffer.data(), sizeof(T), data);
        buffer = buffer.subspan(sizeof(T));
        return value;
    }

    constexpr auto length(const aggregate auto &object) {
        std::size_t bytes_written = 0;
        boost::pfr::for_each_field(object, [&](const auto &field) { bytes_written += length(field); });
        return bytes_written;
    }

    constexpr auto write(const aggregate auto &object, writable_bytes_view buffer) {
        std::size_t bytes_written = 0;
        boost::pfr::for_each_field(object, [&](const auto &field) {
            bytes_written += write(field, buffer.subspan(bytes_written));
        });
        return bytes_written;
    }

    template<aggregate T>
    constexpr T read(bytes_view &buffer) {
        T object;
        boost::pfr::for_each_field(object, [&](auto &field) {
            using field_type = std::remove_reference_t<decltype(field)>;
            field = read<field_type>(buffer);
        });
        return object;
    }

    constexpr auto length(const iterable auto &container) {
        auto size = std::ranges::size(container);
        static_assert(std::is_same_v<decltype(size), std::size_t>);
        for (const auto &item: container)
            size += length(item);
        return size;
    }

    constexpr auto write(const iterable auto &container, writable_bytes_view buffer) {
        // Serialize the number of elements in the container.
        auto bytes_written = write(std::ranges::size(container), buffer);
        for (const auto &item: container)
            bytes_written += write(item, buffer.subspan(bytes_written));
        return bytes_written;
    }

    template<iterable T>
    constexpr T read(bytes_view &buffer) {
        using V = typename T::value_type;
        T container;
        regular auto size = read<std::size_t>(buffer);
        auto inserter = std::inserter(container, std::end(container));
        for (std::size_t i = 0; i < size; ++i)
            inserter = std::move(read<V>(buffer));
        return container;
    }

    // MARK: Interface functions
    auto dump(const serializable auto &obj) {
        bytes buffer{length(obj)};
        write(obj, buffer);
        return buffer;
    }

    template<serializable T>
    auto load(bytes_view buffer) {
        return read<T>(buffer);
    }

}
