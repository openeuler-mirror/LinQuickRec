#include "simple_json.h"

#include <cctype>
#include <stdexcept>

namespace simple_json {

static void skipWhitespace(const std::string& s, size_t& pos) {
    while (pos < s.size() && std::isspace(s[pos])) pos++;
}

static std::string parseString(const std::string& s, size_t& pos) {
    if (pos >= s.size() || s[pos] != '"') throw std::runtime_error("expected '\"'");
    pos++;
    std::string result;
    while (pos < s.size() && s[pos] != '"') {
        if (s[pos] == '\\' && pos + 1 < s.size()) {
            pos++;
            switch (s[pos]) {
                case '"': result += '"'; break;
                case '\\': result += '\\'; break;
                case '/': result += '/'; break;
                case 'b': result += '\b'; break;
                case 'f': result += '\f'; break;
                case 'n': result += '\n'; break;
                case 'r': result += '\r'; break;
                case 't': result += '\t'; break;
                default: result += s[pos]; break;
            }
        } else {
            result += s[pos];
        }
        pos++;
    }
    if (pos < s.size()) pos++;
    return result;
}

static Value parseValue(const std::string& s, size_t& pos);

static Value parseObject(const std::string& s, size_t& pos) {
    Value v = Value::object();
    pos++;
    skipWhitespace(s, pos);
    if (pos < s.size() && s[pos] == '}') { pos++; return v; }
    while (true) {
        skipWhitespace(s, pos);
        std::string key = parseString(s, pos);
        skipWhitespace(s, pos);
        if (pos >= s.size() || s[pos] != ':') throw std::runtime_error("expected ':'");
        pos++;
        skipWhitespace(s, pos);
        Value val = parseValue(s, pos);
        v[key] = std::move(val);
        skipWhitespace(s, pos);
        if (pos >= s.size()) throw std::runtime_error("unexpected EOF");
        if (s[pos] == '}') { pos++; break; }
        if (s[pos] != ',') throw std::runtime_error("expected ',' or '}'");
        pos++;
    }
    return v;
}

static Value parseArray(const std::string& s, size_t& pos) {
    Value v = Value::array();
    pos++;
    skipWhitespace(s, pos);
    if (pos < s.size() && s[pos] == ']') { pos++; return v; }
    while (true) {
        skipWhitespace(s, pos);
        v.push_back(parseValue(s, pos));
        skipWhitespace(s, pos);
        if (pos >= s.size()) throw std::runtime_error("unexpected EOF");
        if (s[pos] == ']') { pos++; break; }
        if (s[pos] != ',') throw std::runtime_error("expected ',' or ']'");
        pos++;
    }
    return v;
}

static Value parseValue(const std::string& s, size_t& pos) {
    skipWhitespace(s, pos);
    if (pos >= s.size()) throw std::runtime_error("unexpected EOF");
    char c = s[pos];
    if (c == '"') return Value(parseString(s, pos));
    if (c == '{') return parseObject(s, pos);
    if (c == '[') return parseArray(s, pos);
    if (c == 't' || c == 'f') {
        size_t start = pos;
        while (pos < s.size() && std::isalpha(s[pos])) pos++;
        return Value(""); // boolean -> treat as empty string
    }
    if (c == 'n' && s.substr(pos, 4) == "null") {
        pos += 4;
        return Value(); // null
    }
    // number
    size_t start = pos;
    if (s[pos] == '-') pos++;
    while (pos < s.size() && std::isdigit(s[pos])) pos++;
    if (pos < s.size() && s[pos] == '.') {
        pos++;
        while (pos < s.size() && std::isdigit(s[pos])) pos++;
    }
    std::string num = s.substr(start, pos - start);
    try { return Value(static_cast<int64_t>(std::stoll(num))); }
    catch (...) { return Value(static_cast<int64_t>(0)); }
}

Value Value::parse(const std::string& json) {
    size_t pos = 0;
    return parseValue(json, pos);
}

Value& Value::operator[](const std::string& key) {
    type_ = OBJECT;
    return obj_[key];
}

const Value& Value::get(const std::string& key) const {
    static Value nil;
    auto it = obj_.find(key);
    if (it != obj_.end()) return it->second;
    return nil;
}

bool Value::contains(const std::string& key) const {
    return obj_.find(key) != obj_.end();
}

void Value::push_back(const Value& v) {
    type_ = ARRAY;
    arr_.push_back(v);
}

size_t Value::size() const {
    return arr_.size();
}

const Value& Value::operator[](size_t idx) const {
    return arr_[idx];
}

static void dumpValue(const Value& v, std::ostringstream& oss);

static void dumpString(const std::string& s, std::ostringstream& oss) {
    oss << '"';
    for (char c : s) {
        switch (c) {
            case '"': oss << "\\\""; break;
            case '\\': oss << "\\\\"; break;
            case '\n': oss << "\\n"; break;
            case '\r': oss << "\\r"; break;
            case '\t': oss << "\\t"; break;
            default: oss << c; break;
        }
    }
    oss << '"';
}

static void dumpValue(const Value& v, std::ostringstream& oss) {
    switch (v.type()) {
        case Value::NIL: oss << "null"; break;
        case Value::STRING: dumpString(v.str(), oss); break;
        case Value::INTEGER: oss << v.integer(); break;
        case Value::OBJECT: {
            oss << '{';
            bool first = true;
            for (const auto& [k, val] : v.obj()) {
                if (!first) oss << ',';
                first = false;
                dumpString(k, oss);
                oss << ':';
                dumpValue(val, oss);
            }
            oss << '}';
            break;
        }
        case Value::ARRAY: {
            oss << '[';
            bool first = true;
            for (const auto& val : v.arr()) {
                if (!first) oss << ',';
                first = false;
                dumpValue(val, oss);
            }
            oss << ']';
            break;
        }
    }
}

std::string Value::dump() const {
    std::ostringstream oss;
    dumpValue(*this, oss);
    return oss.str();
}

} // namespace simple_json
