#include "Json.hpp"

#include <cctype>
#include <stdexcept>

namespace {
    class JsonParser {
    public:
        explicit JsonParser(std::string text) : text(std::move(text)) {}

        Json Parse() {
            SkipWhitespace();
            return ParseValue();
        }

    private:
        std::string text;
        size_t pos = 0;

        char Peek() const { return pos < text.size() ? text[pos] : '\0'; }
        char Next() { return pos < text.size() ? text[pos++] : '\0'; }

        void SkipWhitespace() {
            while (std::isspace(static_cast<unsigned char>(Peek()))) ++pos;
        }

        void Expect(char c) {
            if (Next() != c) {
                throw std::runtime_error(std::string("JSON: expected '") + c + "'");
            }
        }

        Json ParseValue() {
            SkipWhitespace();
            switch (Peek()) {
                case '{': return ParseObject();
                case '[': return ParseArray();
                case '"': return ParseString();
                case 't':
                case 'f': return ParseBool();
                case 'n': return ParseNull();
                default:  return ParseNumber();
            }
        }

        Json ParseObject() {
            Expect('{');
            std::map<std::string, Json> members;
            SkipWhitespace();
            if (Peek() == '}') { Next(); return Json::MakeObject(std::move(members)); }
            while (true) {
                SkipWhitespace();
                std::string key = ParseString().AsString();
                SkipWhitespace();
                Expect(':');
                members[key] = ParseValue();
                SkipWhitespace();
                char c = Next();
                if (c == '}') break;
                if (c != ',') throw std::runtime_error("JSON: expected ',' or '}'");
            }
            return Json::MakeObject(std::move(members));
        }

        Json ParseArray() {
            Expect('[');
            std::vector<Json> values;
            SkipWhitespace();
            if (Peek() == ']') { Next(); return Json::MakeArray(std::move(values)); }
            while (true) {
                values.push_back(ParseValue());
                SkipWhitespace();
                char c = Next();
                if (c == ']') break;
                if (c != ',') throw std::runtime_error("JSON: expected ',' or ']'");
            }
            return Json::MakeArray(std::move(values));
        }

        Json ParseString() {
            Expect('"');
            std::string value;
            while (true) {
                char c = Next();
                if (c == '"' || c == '\0') break;
                if (c == '\\') {
                    char escaped = Next();
                    switch (escaped) {
                        case 'n':  value += '\n'; break;
                        case 't':  value += '\t'; break;
                        case '"':  value += '"';  break;
                        case '\\': value += '\\'; break;
                        case '/':  value += '/';  break;
                        default:   value += escaped; break;
                    }
                } else {
                    value += c;
                }
            }
            return Json::MakeString(std::move(value));
        }

        Json ParseBool() {
            if (text.compare(pos, 4, "true") == 0) { pos += 4; return Json::MakeBool(true); }
            if (text.compare(pos, 5, "false") == 0) { pos += 5; return Json::MakeBool(false); }
            throw std::runtime_error("JSON: invalid literal");
        }

        Json ParseNull() {
            if (text.compare(pos, 4, "null") != 0) throw std::runtime_error("JSON: invalid literal");
            pos += 4;
            return Json();
        }

        Json ParseNumber() {
            size_t start = pos;
            if (Peek() == '-') Next();
            while (std::isdigit(static_cast<unsigned char>(Peek()))) Next();
            if (Peek() == '.') {
                Next();
                while (std::isdigit(static_cast<unsigned char>(Peek()))) Next();
            }
            if (Peek() == 'e' || Peek() == 'E') {
                Next();
                if (Peek() == '+' || Peek() == '-') Next();
                while (std::isdigit(static_cast<unsigned char>(Peek()))) Next();
            }
            if (pos == start) throw std::runtime_error("JSON: invalid value");

            return Json::MakeNumber(std::stod(text.substr(start, pos - start)));
        }
    };
}

Json Json::Parse(const std::string& text) {
    return JsonParser(text).Parse();
}

Json Json::MakeBool(bool value) {
    Json result;
    result.type = Type::Bool;
    result.boolValue = value;
    return result;
}

Json Json::MakeNumber(double value) {
    Json result;
    result.type = Type::Number;
    result.numberValue = value;
    return result;
}

Json Json::MakeString(std::string value) {
    Json result;
    result.type = Type::String;
    result.stringValue = std::move(value);
    return result;
}

Json Json::MakeArray(std::vector<Json> values) {
    Json result;
    result.type = Type::Array;
    result.arrayValue = std::move(values);
    return result;
}

Json Json::MakeObject(std::map<std::string, Json> values) {
    Json result;
    result.type = Type::Object;
    result.objectValue = std::move(values);
    return result;
}

bool Json::AsBool(bool fallback) const {
    return type == Type::Bool ? boolValue : fallback;
}

double Json::AsNumber(double fallback) const {
    return type == Type::Number ? numberValue : fallback;
}

std::string Json::AsString(const std::string& fallback) const {
    return type == Type::String ? stringValue : fallback;
}

const std::vector<Json>& Json::AsArray() const {
    static const std::vector<Json> empty;
    return type == Type::Array ? arrayValue : empty;
}

const Json& Json::operator[](const std::string& key) const {
    static const Json null;
    if (type != Type::Object) return null;
    auto it = objectValue.find(key);
    return it != objectValue.end() ? it->second : null;
}
