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

    static Json Parse(const std::string& text);

    // Factories used by the parser to build values; also handy for tests.
    static Json MakeBool(bool value);
    static Json MakeNumber(double value);
    static Json MakeString(std::string value);
    static Json MakeArray(std::vector<Json> values);
    static Json MakeObject(std::map<std::string, Json> values);

    Type GetType() const { return type; }

    bool AsBool(bool fallback = false) const;
    double AsNumber(double fallback = 0.0) const;
    std::string AsString(const std::string& fallback = "") const;
    const std::vector<Json>& AsArray() const;

    // Object member access; returns a Null Json if this isn't an object or
    // the key is missing.
    const Json& operator[](const std::string& key) const;

private:
    Type type;
    bool boolValue = false;
    double numberValue = 0.0;
    std::string stringValue;
    std::vector<Json> arrayValue;
    std::map<std::string, Json> objectValue;
};
