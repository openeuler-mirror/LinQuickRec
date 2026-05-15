#ifndef SIMPLE_JSON_H
#define SIMPLE_JSON_H

#include <cstdint>
#include <cstdlib>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace simple_json {

std::string escape(const std::string& s);
std::string unescape(const std::string& s);
std::string build(const std::string& content);

class Value {
public:
    enum Type { NIL, STRING, INTEGER, OBJECT, ARRAY };

    Value() : type_(NIL) {}
    explicit Value(const std::string& s) : type_(STRING), str_(s) {}
    explicit Value(int64_t i) : type_(INTEGER), int_(i) {}
    static Value object() { Value v; v.type_ = OBJECT; return v; }
    static Value array() { Value v; v.type_ = ARRAY; return v; }

    Type type() const { return type_; }

    const std::string& str() const { return str_; }
    int64_t integer() const { return int_; }

    Value& operator[](const std::string& key);
    const Value& get(const std::string& key) const;
    bool contains(const std::string& key) const;
    const std::unordered_map<std::string, Value>& obj() const { return obj_; }

    void push_back(const Value& v);
    size_t size() const;
    const Value& operator[](size_t idx) const;
    const std::vector<Value>& arr() const { return arr_; }

    std::string dump() const;
    static Value parse(const std::string& json);

private:
    Type type_ = NIL;
    std::string str_;
    int64_t int_ = 0;
    std::unordered_map<std::string, Value> obj_;
    std::vector<Value> arr_;
};

} // namespace simple_json

#endif // SIMPLE_JSON_H
