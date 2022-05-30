#pragma once

#include <span>
#include <ranges>
#include <vector>
#include <algorithm>
#include <type_traits>
#include <iostream>
#include <fstream>
#include <utility>

#include "boost/pfr.hpp"
#include "tinyxml2.h"
#include "base64.h"

namespace serialization {

    // MARK: Concepts

    // Basic concepts on types.
    template<typename T> concept regular = std::is_trivial_v<T> && std::is_standard_layout_v<T>;
    template<typename T> concept aggregate = std::is_aggregate_v<T> && !regular<T>;

    template<typename T> concept insertable = requires(T container) {
        { std::inserter(container, std::end(container)) } -> std::same_as<std::insert_iterator<T>>;
    };
    template<typename T> concept iterable = std::ranges::forward_range<T> && insertable<T>;

    // For checking whether a type is specialization of a template.
    template<typename Test, template<typename...> class Template>
    struct is_specialization : std::false_type {};
    template<template<typename ...> class Template, typename... Args>
    struct is_specialization<Template<Args...>, Template> : std::true_type {};

    template<typename T> concept tuple_like = is_specialization<T, std::tuple>::value ||
                                              is_specialization<T, std::pair>::value;

    // MARK: Helper functions

    // Compile-time template-based for loop in a range.
    template<auto Start, auto End, typename Func>
    constexpr void for_range(Func &&f) {
        if constexpr (Start < End) {
            f(std::integral_constant<decltype(Start), Start>());
            for_range<Start + 1, End>(f);
        }
    }

    // Compile-time tuple-like access.
    template<typename T, typename Func>
    requires tuple_like<std::remove_cvref_t<T>>
    constexpr void for_each_element(T &&tuple, Func &&f) {
        constexpr auto size = std::tuple_size_v<std::remove_reference_t<T>>;
        for_range<0, size>([&](auto i) { f(std::get<i.value>(tuple)); });
    }

    template <typename T>
    constexpr const char *tag_name() {
        if constexpr (std::is_integral_v<T> && std::is_unsigned_v<T>)
            return "unsigned_int";
        else if (std::is_integral_v<T> && std::is_signed_v<T>)
            return "int";
        else if (std::is_floating_point_v<T>)
            return "float";
        else return "unknown";
    }

    // MARK: Error type
    struct parse_error : std::runtime_error {
        using std::runtime_error::runtime_error;
    };

    // MARK: Core serializers

    namespace binary {
        using bytes = std::vector<std::byte>;
        using bytes_view = std::span<const std::byte>;
        using writable_bytes_view = std::span<std::byte>;

        template<typename T>
        struct serializer {};

        // A type is serializable when there is a matched serializer for it.
        template<typename T> concept serializable = requires(T v) {
            { serializer<T>::length(v) }  -> std::same_as<std::size_t>;
            { serializer<T>::write(v, std::declval<writable_bytes_view>()) }  -> std::same_as<std::size_t>;
            { serializer<T>::read(std::declval<bytes_view &>()) }  -> std::same_as<T>;
        };

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

            static constexpr auto read(bytes_view &buffer) {
                T value;
                auto data = reinterpret_cast<std::byte *>(&value);
                if (buffer.size() < sizeof(T)) throw parse_error{"Reached end of data"};
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

            static constexpr auto read(bytes_view &buffer) {
                T object;
                boost::pfr::for_each_field(object, [&](auto &field) {
                    using field_type = std::remove_reference_t<decltype(field)>;
                    field = std::move(serializer<field_type>::read(buffer));
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

            static constexpr auto read(bytes_view &buffer) {
                T container;
                regular auto size = serializer<std::size_t>::read(buffer);
                auto inserter = std::inserter(container, std::end(container));
                for (std::size_t i = 0; i < size; ++i)
                    inserter = std::move(serializer<value_type>::read(buffer));
                return container;
            }
        };

        template<tuple_like T>
        struct serializer<T> {
            static constexpr auto length(const T &tuple) {
                std::size_t bytes_written = 0;
                for_each_element(tuple, [&](const auto &element) {
                    using element_type = std::remove_cvref_t<decltype(element)>;
                    bytes_written += serializer<element_type>::length(element);
                });
                return bytes_written;
            }

            static constexpr auto write(const T &tuple, writable_bytes_view buffer) {
                std::size_t bytes_written = 0;
                for_each_element(tuple, [&](const auto &element) {
                    using element_type = std::remove_cvref_t<decltype(element)>;
                    bytes_written += serializer<element_type>::write(element, buffer.subspan(bytes_written));
                });
                return bytes_written;
            }

            static constexpr auto read(bytes_view &buffer) {
                T tuple;
                for_each_element(tuple, [&](auto &element) {
                    using element_type = std::remove_cvref_t<decltype(element)>;
                    // For `std::map`, element_type is `std::pair<const std::string, Value>`
                    auto& elem = const_cast<element_type&>(element);
                    elem = std::move(serializer<element_type>::read(buffer));
                });
                return tuple;
            }
        };

        template<typename T>
        struct serializer<std::optional<T>> {
            using optional = std::optional<T>;

            static constexpr auto length(const optional &opt) {
                std::size_t length = opt ? 1 + serializer<T>::length(*opt) : 1;
                return length;
            }

            static constexpr auto write(const optional &opt, writable_bytes_view buffer) {
                // Serialize bool value of the optional first.
                auto bytes_written = serializer<bool>::write(opt.has_value(), buffer);
                if (opt) bytes_written += serializer<T>::write(*opt, buffer.subspan(1));
                return bytes_written;
            }

            static constexpr optional read(bytes_view &buffer) {
                bool has_value = serializer<bool>::read(buffer);
                if (has_value) return serializer<T>::read(buffer);
                return std::nullopt;
            }
        };

        template<typename T>
        struct serializer<std::unique_ptr<T>> {
            using unique_ptr = std::unique_ptr<T>;

            static constexpr auto length(const unique_ptr &ptr) {
                std::size_t length = ptr ? 1 + serializer<T>::length(*ptr) : 1;
                return length;
            }

            static constexpr auto write(const unique_ptr &ptr, writable_bytes_view buffer) {
                // Serialize bool value of the pointer first.
                auto bytes_written = serializer<bool>::write(ptr != nullptr, buffer);
                if (ptr) bytes_written += serializer<T>::write(*ptr, buffer.subspan(1));
                return bytes_written;
            }

            static constexpr unique_ptr read(bytes_view &buffer) {
                bool has_value = serializer<bool>::read(buffer);
                if (has_value) return std::make_unique<T>(serializer<T>::read(buffer));
                return nullptr;
            }
        };
    }

    namespace xml {
        using namespace tinyxml2;

        template<typename T, bool Base64 = false>
        struct xml_serializer {};

        // A type is serializable when there is a matched serializer for it.
        template<typename T, bool Base64> concept xml_serializable = requires(T v) {
            { xml_serializer<T, Base64>::write(v, std::declval<XMLDocument &>()) }  -> std::same_as<XMLElement *>;
            { xml_serializer<T, Base64>::read(std::declval<XMLElement &>()) }  -> std::same_as<T>;
        };

        // Text XML serializer (only support arithmetic)
        template<typename T> requires std::is_arithmetic_v<T>
        struct xml_serializer<T, false> {
            static constexpr auto write(const T &value, XMLDocument &doc) {
                auto element = doc.NewElement(tag_name<T>());
                if constexpr (std::is_integral_v<T> && std::is_unsigned_v<T>)
                    element->SetAttribute("value", static_cast<uint64_t>(value));
                else if (std::is_integral_v<T> && std::is_signed_v<T>)
                    element->SetAttribute("value", static_cast<int64_t>(value));
                else if (std::is_floating_point_v<T>)
                    element->SetAttribute("value", static_cast<double>(value));
                return element;
            }

            static constexpr auto read(const XMLElement &element) {
                T value;
                auto attribute = element.FindAttribute("value");
                if (attribute == nullptr) throw parse_error{"Cannot find attribute"};
                if constexpr (std::is_integral_v<T> && std::is_unsigned_v<T>) {
                    uint64_t v;
                    attribute->QueryUnsigned64Value(&v);
                    value = v;
                } else if (std::is_integral_v<T> && std::is_signed_v<T>) {
                    int64_t v;
                    attribute->QueryInt64Value(&v);
                    value = v;
                } else if (std::is_floating_point_v<T>) {
                    double v;
                    attribute->QueryDoubleValue(&v);
                    value = v;
                }
                return value;
            }
        };

        // Base-64 XML serializer
        template<regular T>
        struct xml_serializer<T, true> {
            static auto write(const T &value, XMLDocument &doc) {
                auto element = doc.NewElement(tag_name<T>());
                auto data = std::string_view{reinterpret_cast<const char *>(&value)};
                auto encoded = base64_encode(data);
                element->SetAttribute("base64", encoded.c_str());
                return element;
            }

            static auto read(const XMLElement &element) {
                T value;
                auto attribute = element.FindAttribute("base64");
                if (attribute == nullptr) throw parse_error{"Cannot find attribute"};
                auto decoded = base64_decode(static_cast<std::string_view>(attribute->Value()));
                auto data = reinterpret_cast<char *>(&value);
                std::copy_n(decoded.data(), sizeof(T), data);
                return value;
            }
        };

        template<aggregate T, bool Base64>
        struct xml_serializer<T, Base64> {
            static constexpr auto write(const T &object, XMLDocument &doc) {
                auto parent = doc.NewElement("aggregate");
                boost::pfr::for_each_field(object, [&](const auto &field) {
                    using field_type = std::remove_cvref_t<decltype(field)>;
                    auto child = xml_serializer<field_type, Base64>::write(field, doc);
                    parent->InsertEndChild(child);
                });
                return parent;
            }

            static constexpr auto read(const XMLElement &parent) {
                T object;
                auto child = parent.FirstChildElement();
                boost::pfr::for_each_field(object, [&](auto &field) {
                    using field_type = std::remove_reference_t<decltype(field)>;
                    if (child == nullptr) throw parse_error{"Cannot find child element"};
                    field = std::move(xml_serializer<field_type, Base64>::read(*child));
                    child = child->NextSiblingElement();
                });
                return object;
            }
        };

        template<iterable T, bool Base64>
        struct xml_serializer<T, Base64> {
            using value_type = typename T::value_type;

            static constexpr auto write(const T &container, XMLDocument &doc) {
                auto parent = doc.NewElement("iterable");
                parent->SetAttribute("size", static_cast<uint64_t>(std::ranges::size(container)));
                for (const auto &item: container) {
                    auto child = xml_serializer<value_type, Base64>::write(item, doc);
                    parent->InsertEndChild(child);
                }
                return parent;
            }

            static constexpr auto read(const XMLElement &parent) {
                T container;
                auto size_attribute = parent.FindAttribute("size");
                if (size_attribute == nullptr) throw parse_error{"Cannot find size attribute"};
                auto size = size_attribute->Unsigned64Value();
                auto child = parent.FirstChildElement();
                auto inserter = std::inserter(container, std::end(container));
                for (std::size_t i = 0; i < size; ++i) {
                    if (child == nullptr) throw parse_error{"Cannot find child element"};
                    inserter = std::move(xml_serializer<value_type, Base64>::read(*child));
                    child = child->NextSiblingElement();
                }
                return container;
            }
        };

        template<tuple_like T, bool Base64>
        struct xml_serializer<T, Base64> {
            static constexpr auto write(const T &tuple, XMLDocument &doc) {
                auto parent = doc.NewElement("tuple");
                for_each_element(tuple, [&](const auto &element) {
                    using element_type = std::remove_cvref_t<decltype(element)>;
                    auto child = xml_serializer<element_type, Base64>::write(element, doc);
                    parent->InsertEndChild(child);
                });
                return parent;
            }

            static constexpr auto read(const XMLElement &parent) {
                T tuple;
                auto child = parent.FirstChildElement();
                for_each_element(tuple, [&](auto &element) {
                    using element_type = std::remove_cvref_t<decltype(element)>;
                    if (child == nullptr) throw parse_error{"Cannot find child element"};
                    // For `std::map`, element_type is `std::pair<const std::string, Value>`
                    auto &elem = const_cast<element_type&>(element);
                    elem = std::move(xml_serializer<element_type, Base64>::read(*child));
                    child = child->NextSiblingElement();
                });
                return tuple;
            }
        };

        template<typename T, bool Base64>
        struct xml_serializer<std::optional<T>, Base64> {
            using optional = std::optional<T>;

            static constexpr auto write(const optional &opt, XMLDocument &doc) {
                auto element = doc.NewElement("optional");
                element->SetAttribute("has_value", opt.has_value());
                if (opt) {
                    auto child = xml_serializer<T, Base64>::write(*opt, doc);
                    element->InsertEndChild(child);
                }
                return element;
            }

            static constexpr optional read(const XMLElement &parent) {
                auto has_value_attribute = parent.FindAttribute("has_value");
                if (has_value_attribute == nullptr) throw parse_error{"Cannot find has_value attribute"};
                bool has_value = has_value_attribute->BoolValue();
                if (has_value) {
                    auto child = parent.FirstChildElement();
                    if (child == nullptr) throw parse_error{"Cannot find optional value element"};
                    return xml_serializer<T, Base64>::read(*child);
                }
                return std::nullopt;
            }
        };

        template<typename T, bool Base64>
        struct xml_serializer<std::unique_ptr<T>, Base64> {
            using unique_ptr = std::unique_ptr<T>;

            static constexpr auto write(const unique_ptr &ptr, XMLDocument &doc) {
                auto element = doc.NewElement("unique_ptr");
                element->SetAttribute("has_value", ptr != nullptr);
                if (ptr) {
                    auto child = xml_serializer<T, Base64>::write(*ptr, doc);
                    element->InsertEndChild(child);
                }
                return element;
            }

            static constexpr unique_ptr read(const XMLElement &parent) {
                auto has_value_attribute = parent.FindAttribute("has_value");
                if (has_value_attribute == nullptr) throw parse_error{"Cannot find has_value attribute"};
                bool has_value = has_value_attribute->BoolValue();
                if (has_value) {
                    auto child = parent.FirstChildElement();
                    if (child == nullptr) throw parse_error{"Cannot find unique_ptr value element"};
                    return std::make_unique<T>(xml_serializer<T, Base64>::read(*child));
                }
                return nullptr;
            }
        };
    }

    // MARK: Interface functions

    namespace binary {
        auto get_file_length(std::ifstream &file) {
            file.seekg(0, std::ios::end);
            auto file_length = file.tellg();
            file.seekg(0, std::ios::beg);
            return file_length;
        }

        template<serializable T>
        void dump(const T &obj, const std::string &path) {
            bytes buffer{serializer<T>::length(obj)};
            serializer<T>::write(obj, buffer);
            std::ofstream file{path, std::ios::binary};
            file.write(reinterpret_cast<const char *>(buffer.data()), static_cast<long>(buffer.size()));
        }

        template<serializable T>
        auto load(const std::string &path) {
            std::ifstream file{path, std::ios::binary};
            file.unsetf(std::ios::skipws);
            auto file_length = get_file_length(file);

            bytes data(file_length);
            file.read(reinterpret_cast<char *>(data.data()), file_length);
            bytes_view buffer{data};
            return serializer<T>::read(buffer);
        }
    }

    namespace xml {
        template<bool Base64 = false, typename T>
        requires xml_serializable<T, Base64>
        void dump(const T &obj, const std::string &path) {
            XMLDocument doc;
            auto element = xml_serializer<T, Base64>::write(obj, doc);
            doc.InsertEndChild(element);
            doc.SaveFile(path.c_str());
        }

        template<typename T, bool Base64 = false>
        requires xml_serializable<T, Base64>
        auto load(const std::string &path) {
            XMLDocument doc;
            auto code = doc.LoadFile(path.c_str());
            if (code != tinyxml2::XML_SUCCESS) throw parse_error{"Invalid XML"};
            auto root = doc.RootElement();
            if (root == nullptr) throw parse_error{"Empty XML"};
            return xml_serializer<T, Base64>::read(*root);
        }
    }

}
