# Serialization

A serialization library implementing with C++20 standard, supporting binary and XML format on basic types.

## Usage
Binary format:
```cpp
serialization::binary::dump(example, "example.dat");
auto loaded = serialization::binary::load<Example>("example.dat");
```
XML format:
```cpp
serialization::xml::dump(example, "example.xml");
auto loaded = serialization::xml::load<Example>("example.xml");

// With Base64 encoding
serialization::xml::dump<true>(example, "example_base64.xml");
auto loaded_base64 = serialization::xml::load<Example, true>("example_base64.xml");
```

## Exception
When deserializing, the library can throw `serialization::parse_error` if encountered these situations:
1. when working in binary format, the data stream ended earlier than expected;
2. when working in XML format, the expected child element or attribute cannot be found;
3. when working in XML format, the XML file is invalid or empty.
   It’s worth mentioning that the library doesn’t handle cases where file goes corrupted unexpectedly. As a matter of fact, in binary format, the library only sees the file as a stream of bytes, hence it cannot tell whether the fields it read are invalid or not.

## Features
Thanks to introduction to structure binding in C++17, concept and constraint in C++20, reflection turns out easier with template metaprogramming techniques. In this situation, we can alleviate old-style macro-based reflection, which still needs registering fields of aggregate type one by one manually. As usage examples above shown, the syntax is compact and accurate. Only in decoding function a template argument is needed.
What’s more, the serialization is strongly-typed—all reflection is done at compile-time, which means we can be convinced the serialization process cannot go wrong as long as it compiles, and the performance overhead is reduced to as small as possible.

## Supporting Types
Practically speaking, common types are well supported:
- arithmetic types;
- aggregate types;
- `std::string`;
- STL containers: `std::vector`, `std::list`, `std::set`, `std::map`;
- utility wrappers: `std::tuple`, `std::pair`, `std::optional`;
- smart pointer: `std::unique_ptr`.

In fact, serialization is supported as long as the object conforms either situation of type traits:
- `std::is_trivial` and `std::is_standard_layout`;
- `std::is_aggregate`;
- `std::ranges::forward_range` and there is `std::insert_iterator` for the type;
- and other more particular types listed above.

Specifically, concepts used to constrain supporting types are listed as follows for reference:
```cpp
template<typename T> concept regular = std::is_trivial_v<T> && std::is_standard_layout_v<T>;
template<typename T> concept aggregate = std::is_aggregate_v<T> && !regular<T>;

template<typename T> concept insertable = requires(T container) {
    { std::inserter(container, std::end(container)) } -> std::same_as<std::insert_iterator<T>>;
};
template<typename T> concept iterable = std::ranges::forward_range<T> && insertable<T>;

template<typename T> concept tuple_like = is_specialization<T, std::tuple>::value || is_specialization<T, std::pair>::value;
```

## Implementation Details
The core serialization logic is provided by `serialization::binary::serializer<T>` and `serialization::xml::xml_serializer<T>`, each specialization of which is responsible for each kind of types.
Binary format serialization takes advantage of `std::span`, removing unnecessary memory copying during writing and reading. The length of whole data is computed beforehand, and a `std::vector<std::byte>` is created as data buffer, then serialized data is written to the buffer sequentially. The struct is declared roughly as follows:
```cpp
using bytes = std::vector<std::byte>;
using bytes_view = std::span<const std::byte>;
using writable_bytes_view = std::span<std::byte>;

template<typename T>
struct serializer<T> {
	static constexpr std::size_t length(const T &value);
	static constexpr std::size_t write(const T &value, writable_bytes_view buffer);
	static constexpr T read(bytes_view &buffer)
}
```
Similarly, the core struct for XML format is declared as follows:
```cpp
template<typename T, bool Base64>
struct xml_serializer<T, Base64> {
	static constexpr XMLElement *write(const T &value, XMLDocument &doc);
	static constexpr T read(const XMLElement &element);
}
```

## Dependency Libraries
We believe the main effort of this project lies in the modern implementation of serialization with C++20, rather than implementing Base-64 encoding or accessing user-defined type. Thus the code focuses on core logic of static serialization, with the help of dependency libraries listed as follows:
- [`boost/pfr`](https://github.com/boostorg/pfr), providing basic reflection as `std::tuple` like methods for user defined types.
- [`tinyxml2`](https://github.com/leethomason/tinyxml2), providing XML creating and parsing functionality.
- [`cpp-base64`](https://github.com/ReneNyffenegger/cpp-base64), providing Base-64 encoding and decoding functionality.

## Improvements
Further improvements on this library can be made by:
1. supporting non-aggregate user-defined types; (Those with non-trivial constructors, virtual member functions, etc. needs special treatment.)
2. optimizing implementation of contiguous iterable types; (Currently, for iterable, the library uses `std::forward_iterator` for serialization, `std::insert_iterator` for deserialization. This approach is good for generality, but may harm performance. Specialization on serializer for `std::contiguous_iterator` can be added.)
3. directly using literal for string types. (Likewise, string type is processed as an iterable as well for now. Considering that special characters in strings may disturb XML parsing, further inspection is required.)
