#pragma once

#include <map>
#include <string>
#include <vector>

// Minimal hand-rolled JSON reader for loading simple config files: objects,
// arrays, strings, bools, numbers. No writer, no comments, no unicode escapes.
class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Json() : type(Type::Null) {}

    static Json parse(const std::string& text);

    // Factories used by the parser to build values; also handy for tests.
    static Json make_bool(bool value);
    static Json make_number(double value);
    static Json make_string(std::string value);
    static Json make_array(std::vector<Json> values);
    static Json make_object(std::map<std::string, Json> values);

    Type get_type() const { return type; }

    bool as_bool(bool fallback = false) const;
    double as_number(double fallback = 0.0) const;
    std::string as_string(const std::string& fallback = "") const;
    const std::vector<Json>& as_array() const;

    // Object member access; returns a Null Json if this isn't an object or
    // the key is missing.
    const Json& operator[](const std::string& key) const;

private:
    Type type;
    bool bool_value = false;
    double number_value = 0.0;
    std::string string_value;
    std::vector<Json> array_value;
    std::map<std::string, Json> object_value;
};
