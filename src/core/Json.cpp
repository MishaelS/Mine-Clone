#include "core/Json.hpp"

#include <cctype>
#include <stdexcept>

namespace {
    class JsonParser {
    public:
        explicit JsonParser(std::string text) : text(std::move(text)) {}

        Json parse() {
            skip_whitespace();
            return parse_value();
        }

    private:
        std::string text;
        size_t pos = 0;

        char peek() const { return pos < text.size() ? text[pos] : '\0'; }
        char next() { return pos < text.size() ? text[pos++] : '\0'; }

        void skip_whitespace() {
            while (std::isspace(static_cast<unsigned char>(peek()))) ++pos;
        }

        void expect(char c) {
            if (next() != c) {
                throw std::runtime_error(std::string("JSON: expected '") + c + "'");
            }
        }

        Json parse_value() {
            skip_whitespace();
            switch (peek()) {
                case '{': return parse_object();
                case '[': return parse_array();
                case '"': return parse_string();
                case 't':
                case 'f': return parse_bool();
                case 'n': return parse_null();
                default:  return parse_number();
            }
        }

        Json parse_object() {
            expect('{');
            std::map<std::string, Json> members;
            skip_whitespace();
            if (peek() == '}') { next(); return Json::make_object(std::move(members)); }
            while (true) {
                skip_whitespace();
                std::string key = parse_string().as_string();
                skip_whitespace();
                expect(':');
                members[key] = parse_value();
                skip_whitespace();
                char c = next();
                if (c == '}') break;
                if (c != ',') throw std::runtime_error("JSON: expected ',' or '}'");
            }
            return Json::make_object(std::move(members));
        }

        Json parse_array() {
            expect('[');
            std::vector<Json> values;
            skip_whitespace();
            if (peek() == ']') { next(); return Json::make_array(std::move(values)); }
            while (true) {
                values.push_back(parse_value());
                skip_whitespace();
                char c = next();
                if (c == ']') break;
                if (c != ',') throw std::runtime_error("JSON: expected ',' or ']'");
            }
            return Json::make_array(std::move(values));
        }

        Json parse_string() {
            expect('"');
            std::string value;
            while (true) {
                char c = next();
                if (c == '"' || c == '\0') break;
                if (c == '\\') {
                    char escaped = next();
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
            return Json::make_string(std::move(value));
        }

        Json parse_bool() {
            if (text.compare(pos, 4, "true") == 0) { pos += 4; return Json::make_bool(true); }
            if (text.compare(pos, 5, "false") == 0) { pos += 5; return Json::make_bool(false); }
            throw std::runtime_error("JSON: invalid literal");
        }

        Json parse_null() {
            if (text.compare(pos, 4, "null") != 0) throw std::runtime_error("JSON: invalid literal");
            pos += 4;
            return Json();
        }

        Json parse_number() {
            size_t start = pos;
            if (peek() == '-') next();
            while (std::isdigit(static_cast<unsigned char>(peek()))) next();

            if (peek() == '.') {
                next();
                while (std::isdigit(static_cast<unsigned char>(peek()))) next();
            }

            if (peek() == 'e' || peek() == 'E') {
                next();
                if (peek() == '+' || peek() == '-') next();
                while (std::isdigit(static_cast<unsigned char>(peek()))) next();
            }

            if (pos == start) throw std::runtime_error("JSON: invalid value");

            return Json::make_number(std::stod(text.substr(start, pos - start)));
        }
    };
}

Json Json::parse(const std::string& text)
{
    return JsonParser(text).parse();
}

Json Json::make_bool(bool value)
{
    Json result;
    result.type = Type::Bool;
    result.bool_value = value;
    return result;
}

Json Json::make_number(double value)
{
    Json result;
    result.type = Type::Number;
    result.number_value = value;
    return result;
}

Json Json::make_string(std::string value)
{
    Json result;
    result.type = Type::String;
    result.string_value = std::move(value);
    return result;
}

Json Json::make_array(std::vector<Json> values)
{
    Json result;
    result.type = Type::Array;
    result.array_value = std::move(values);
    return result;
}

Json Json::make_object(std::map<std::string, Json> values)
{
    Json result;
    result.type = Type::Object;
    result.object_value = std::move(values);
    return result;
}

bool Json::as_bool(bool fallback) const
{
    return type == Type::Bool ? bool_value : fallback;
}

double Json::as_number(double fallback) const
{
    return type == Type::Number ? number_value : fallback;
}

std::string Json::as_string(const std::string& fallback) const
{
    return type == Type::String ? string_value : fallback;
}

const std::vector<Json>& Json::as_array() const
{
    static const std::vector<Json> empty;
    return type == Type::Array ? array_value : empty;
}

const std::map<std::string, Json>& Json::as_object() const
{
    static const std::map<std::string, Json> empty;
    return type == Type::Object ? object_value : empty;
}

const Json& Json::operator[](const std::string& key) const
{
    static const Json null;
    if (type != Type::Object) return null;
    auto it = object_value.find(key);
    return it != object_value.end() ? it->second : null;
}
