// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef COMPAT_BASE_H
#define COMPAT_BASE_H

// Base utilities shared by the v20 compatibility module.
//
// The compatibility code was originally written against coost (compat::Json,
// std::string, co::fs, co::path, compat::Channel, ...). This header provides the same
// small API surface on top of picojson, the C++ standard library and
// std::filesystem, so the wire protocol and the business logic stay untouched
// while the coost dependency can be removed.

#include <picojson/picojson.h>

#include "common/compatlog.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <initializer_list>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

// The generated v20 protocol structs use the short coost integer aliases.
using int8 = std::int8_t;
using int16 = std::int16_t;
using int32 = std::int32_t;
using int64 = std::int64_t;
using uint8 = std::uint8_t;
using uint16 = std::uint16_t;
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;

namespace compat {

// ---------------------------------------------------------------------------
// JSON
// ---------------------------------------------------------------------------

/**
 * @brief Small compat::Json compatible JSON value.
 *
 * Copies share the owning tree, so views returned by get()/operator[] stay
 * valid as long as any copy is alive. Missing members behave like null: reads
 * return an empty string, zero or false instead of throwing.
 *
 * A view points into the owning tree, so mutating it (add_member(), remove(),
 * push_back()) is reflected in the parent. Erasing the viewed member from the
 * parent invalidates the view; keep the member in place while working on it.
 */
class Json
{
public:
    Json() = default;
    Json(const Json &) = default;
    Json &operator=(const Json &) = default;

    Json(const char *text)
    {
        assign(picojson::value(std::string(text ? text : "")));
    }
    Json(const std::string &text)
    {
        assign(picojson::value(text));
    }
    Json(bool value)
    {
        assign(picojson::value(value));
    }
    Json(double value)
    {
        assign(picojson::value(value));
    }
    template <typename T,
              typename std::enable_if<std::is_integral<T>::value
                                              && !std::is_same<T, bool>::value,
                                      int>::type = 0>
    Json(T value)
    {
        assign(picojson::value(static_cast<double>(value)));
    }

    Json(std::initializer_list<std::pair<const std::string, Json>> members)
    {
        picojson::object object;
        for (const auto &member : members)
            object[member.first] = member.second.copyValue();
        assign(picojson::value(object));
    }

    void reset()
    {
        _root.reset();
        _node = &nullValue();
    }

    bool parse_from(const std::string &text)
    {
        auto root = std::make_shared<picojson::value>();
        const std::string error = picojson::parse(*root, text);
        if (!error.empty()) {
            reset();
            return false;
        }
        _root = std::move(root);
        _node = _root.get();
        return true;
    }

    Json get(const std::string &key) const
    {
        if (!_node->is<picojson::object>())
            return Json();
        auto &object = _node->get<picojson::object>();
        auto it = object.find(key);
        if (it == object.end())
            return Json();
        return Json(_root, &it->second);
    }

    template <typename... Rest>
    Json get(const std::string &key, const Rest &...rest) const
    {
        return get(key).get(rest...);
    }

    Json operator[](size_t index) const
    {
        if (!_node->is<picojson::array>())
            return Json();
        auto &array = _node->get<picojson::array>();
        if (index >= array.size())
            return Json();
        return Json(_root, &array[index]);
    }

    void add_member(const std::string &key, const Json &value)
    {
        ensureObject();
        _node->get<picojson::object>()[key] = value.copyValue();
    }

    template <typename T>
    void add_member(const std::string &key, const T &value)
    {
        add_member(key, Json(value));
    }

    void push_back(const Json &value)
    {
        ensureArray();
        _node->get<picojson::array>().push_back(value.copyValue());
    }

    bool is_object() const { return _node->is<picojson::object>(); }

    bool is_null() const { return _node->is<picojson::null>(); }

    bool has_member(const std::string &key) const
    {
        if (!_node->is<picojson::object>())
            return false;
        const auto &object = _node->get<picojson::object>();
        return object.find(key) != object.end();
    }

    bool remove(const std::string &key)
    {
        if (!_node->is<picojson::object>())
            return false;
        return _node->get<picojson::object>().erase(key) != 0;
    }

    size_t size() const
    {
        if (_node->is<picojson::object>())
            return _node->get<picojson::object>().size();
        if (_node->is<picojson::array>())
            return _node->get<picojson::array>().size();
        return 0;
    }

    bool empty() const { return size() == 0; }

    size_t array_size() const
    {
        return _node->is<picojson::array>() ? _node->get<picojson::array>().size() : 0;
    }

    bool is_string() const { return _node->is<std::string>(); }

    // coost compatible conversions: non-string values serialize to their JSON
    // text, numbers are parsed from strings and booleans follow "true"/"1".
    std::string as_string() const
    {
        return is_string() ? _node->get<std::string>() : str();
    }

    const char *as_c_str() const
    {
        if (!_node->is<std::string>())
            return "";
        return _node->get<std::string>().c_str();
    }

    int64_t as_int64() const
    {
        if (_node->is<double>())
            return static_cast<int64_t>(_node->get<double>());
        if (_node->is<bool>())
            return _node->get<bool>() ? 1 : 0;
        if (_node->is<std::string>()) {
            const std::string &text = _node->get<std::string>();
            return text.empty() ? 0 : std::strtoll(text.c_str(), nullptr, 0);
        }
        return 0;
    }

    int as_int() const { return static_cast<int>(as_int64()); }

    bool as_bool() const
    {
        if (_node->is<bool>())
            return _node->get<bool>();
        if (_node->is<double>())
            return _node->get<double>() != 0;
        if (_node->is<std::string>()) {
            const std::string &text = _node->get<std::string>();
            return text == "true" || text == "1";
        }
        return false;
    }

    double as_double() const
    {
        if (_node->is<double>())
            return _node->get<double>();
        if (_node->is<bool>())
            return _node->get<bool>() ? 1.0 : 0.0;
        if (_node->is<std::string>()) {
            const std::string &text = _node->get<std::string>();
            return text.empty() ? 0.0 : std::strtod(text.c_str(), nullptr);
        }
        return 0.0;
    }

    std::string str() const { return _node->serialize(); }
    std::string dbg() const { return _node->serialize(); }

    friend std::ostream &operator<<(std::ostream &os, const Json &json)
    {
        return os << json.str();
    }

private:
    Json(std::shared_ptr<picojson::value> root, picojson::value *node)
        : _root(std::move(root)), _node(node)
    {
    }

    static picojson::value &nullValue()
    {
        static picojson::value null;
        return null;
    }

    void assign(picojson::value value)
    {
        _root = std::make_shared<picojson::value>(std::move(value));
        _node = _root.get();
    }

    picojson::value copyValue() const { return *_node; }

    void ensureObject()
    {
        if (!_node->is<picojson::object>())
            assign(picojson::value(picojson::object()));
    }

    void ensureArray()
    {
        if (!_node->is<picojson::array>())
            assign(picojson::value(picojson::array()));
    }

    std::shared_ptr<picojson::value> _root;
    picojson::value *_node { &nullValue() };
};

// ---------------------------------------------------------------------------
// Path helpers (slash separated, ported from the golang path package)
// ---------------------------------------------------------------------------

namespace path {

inline std::string clean(const std::string &input)
{
    if (input.empty())
        return ".";

    const bool rooted = input.front() == '/';
    std::vector<std::string> parts;
    size_t begin = 0;
    while (begin <= input.size()) {
        size_t end = input.find('/', begin);
        if (end == std::string::npos)
            end = input.size();
        const std::string part = input.substr(begin, end - begin);
        if (part == "..") {
            if (!parts.empty() && parts.back() != "..")
                parts.pop_back();
            else if (!rooted)
                parts.push_back(part);
        } else if (!part.empty() && part != ".") {
            parts.push_back(part);
        }
        begin = end + 1;
        if (end == input.size())
            break;
    }

    std::string result;
    if (rooted)
        result.push_back('/');
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i != 0)
            result.push_back('/');
        result += parts[i];
    }
    if (result.empty())
        return rooted ? "/" : ".";
    return result;
}

inline void joinAppend(std::string *) {}

template <typename S, typename... Rest>
void joinAppend(std::string *out, const S &part, const Rest &...rest)
{
    const std::string value(part);
    if (!value.empty()) {
        if (!out->empty() && out->back() != '/')
            out->push_back('/');
        *out += value;
    }
    joinAppend(out, rest...);
}

template <typename... S>
std::string join(const S &...parts)
{
    std::string out;
    joinAppend(&out, parts...);
    return out.empty() ? out : clean(out);
}

inline std::pair<std::string, std::string> split(const std::string &input)
{
    const size_t pos = input.rfind('/');
    if (pos == std::string::npos)
        return { std::string(), input };
    return { input.substr(0, pos + 1), input.substr(pos + 1) };
}

inline std::string dir(const std::string &input)
{
    const std::string parent = split(input).first;
    if (parent.empty())
        return ".";
    return clean(parent);
}

inline std::string base(const std::string &input)
{
    if (input.empty())
        return ".";
    size_t end = input.size();
    while (end > 1 && input[end - 1] == '/')
        --end;
    const size_t pos = input.rfind('/', end - 1);
    if (pos == std::string::npos)
        return input.substr(0, end);
    if (pos + 1 == end)
        return "/";
    return input.substr(pos + 1, end - pos - 1);
}

inline std::string ext(const std::string &input)
{
    if (input.empty())
        return {};
    size_t end = input.size();
    while (end > 0 && input[end - 1] == '/')
        --end;
    const size_t slash = input.rfind('/', end == 0 ? 0 : end - 1);
    const size_t begin = (slash == std::string::npos) ? 0 : slash + 1;
    if (begin >= end)
        return {};
    const size_t dot = input.rfind('.', end - 1);
    if (dot == std::string::npos || dot < begin)
        return {};
    return input.substr(dot, end - dot);
}

}   // namespace path

// ---------------------------------------------------------------------------
// Filesystem helpers
// ---------------------------------------------------------------------------

namespace fs {

using int64 = int64_t;

inline bool exists(const std::string &file)
{
    std::error_code ec;
    return std::filesystem::exists(file, ec);
}

inline bool isdir(const std::string &file)
{
    std::error_code ec;
    return std::filesystem::is_directory(file, ec);
}

inline bool isSymlink(const std::string &file)
{
    std::error_code ec;
    return std::filesystem::is_symlink(std::filesystem::symlink_status(file, ec));
}

inline int64 mtime(const std::string &file)
{
    std::error_code ec;
    const auto time = std::filesystem::last_write_time(file, ec);
    if (ec)
        return 0;
    // C++17 has no portable clock conversion for file_time_type.
    const auto systemTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            time - std::filesystem::file_time_type::clock::now()
            + std::chrono::system_clock::now());
    return static_cast<int64>(
            std::chrono::duration_cast<std::chrono::seconds>(systemTime.time_since_epoch()).count());
}

inline int64 fsize(const std::string &file)
{
    std::error_code ec;
    const auto size = std::filesystem::file_size(file, ec);
    return ec ? 0 : static_cast<int64>(size);
}

inline bool mkdir(const std::string &file, bool parents = false)
{
    std::error_code ec;
    if (parents) {
        std::filesystem::create_directories(file, ec);
    } else {
        std::filesystem::create_directory(file, ec);
    }
    return !ec;
}

inline bool remove(const std::string &file, bool recursive = false)
{
    std::error_code ec;
    if (recursive)
        std::filesystem::remove_all(file, ec);
    else
        std::filesystem::remove(file, ec);
    return !ec;
}

inline bool rename(const std::string &from, const std::string &to)
{
    std::error_code ec;
    std::filesystem::rename(from, to, ec);
    return !ec;
}

/**
 * @brief RAII file wrapper with the coost compat::fs::file modes.
 *
 * Modes: 'r' read, 'w' truncate, 'm' modify (create, no truncate),
 * '+' read/write (create), 'a' append.
 */
class file
{
public:
    file() = default;
    explicit file(size_t) {}
    file(const char *path, char mode) { open(path, mode); }
    file(const std::string &path, char mode) { open(path, mode); }
    ~file() { close(); }

    file(const file &) = delete;
    file &operator=(const file &) = delete;

    explicit operator bool() const { return _fp != nullptr; }
    bool operator!() const { return _fp == nullptr; }

    const char *path() const { return _path.c_str(); }
    int64 size() const { return this->exists() ? compat::fs::fsize(_path) : 0; }
    bool exists() const { return !_path.empty() && compat::fs::exists(_path); }

    bool open(const char *path, char mode)
    {
        close();
        _path = path ? path : "";
        if (_path.empty())
            return false;

        const char *fmode = "rb";
        switch (mode) {
        case 'w':
            fmode = "wb";
            break;
        case 'm':
        case '+': {
            // 'm' creates when missing but never truncates an existing file.
            fmode = "r+b";
            _fp = std::fopen(_path.c_str(), fmode);
            if (_fp == nullptr)
                fmode = "wb";
            break;
        }
        case 'a':
            fmode = "ab";
            break;
        case 'r':
        default:
            fmode = "rb";
            break;
        }
        if (_fp == nullptr)
            _fp = std::fopen(_path.c_str(), fmode);
        return _fp != nullptr;
    }

    bool open(const std::string &path, char mode) { return open(path.c_str(), mode); }

    void close()
    {
        if (_fp != nullptr) {
            std::fclose(_fp);
            _fp = nullptr;
        }
    }

    void seek(int64 offset, int whence = seek_beg)
    {
        if (_fp == nullptr)
            return;
#ifdef _WIN32
        _fseeki64(_fp, static_cast<__int64>(offset), whence);
#else
        fseeko(_fp, static_cast<off_t>(offset), whence);
#endif
    }

    size_t read(void *buffer, size_t size)
    {
        return _fp == nullptr ? 0 : std::fread(buffer, 1, size, _fp);
    }

    std::string read(size_t size)
    {
        std::string data(size, '\0');
        const size_t count = read(&data[0], size);
        data.resize(count);
        return data;
    }

    size_t write(const void *buffer, size_t size)
    {
        if (_fp == nullptr)
            return 0;
        const size_t count = std::fwrite(buffer, 1, size, _fp);
        return count;
    }

    size_t write(const char *text) { return text == nullptr ? 0 : write(text, std::strlen(text)); }
    size_t write(const std::string &text) { return write(text.data(), text.size()); }
    size_t write(char c) { return write(&c, 1); }

    static const int seek_beg = SEEK_SET;
    static const int seek_cur = SEEK_CUR;
    static const int seek_end = SEEK_END;

private:
    std::string _path;
    std::FILE *_fp { nullptr };
};

class dir
{
public:
    dir() = default;
    explicit dir(const char *path) { open(path); }
    explicit dir(const std::string &path) { open(path); }

    bool open(const char *path)
    {
        _path = path ? path : "";
        return !_path.empty();
    }
    bool open(const std::string &path) { return open(path.c_str()); }
    void close() {}
    const char *path() const { return _path.c_str(); }

    std::vector<std::string> all() const
    {
        std::vector<std::string> entries;
        std::error_code ec;
        std::filesystem::directory_iterator it(_path, ec);
        if (ec)
            return entries;
        for (const auto &entry : it) {
            const std::string name = entry.path().filename().string();
            if (name == "." || name == "..")
                continue;
            entries.push_back(name);
        }
        return entries;
    }

private:
    std::string _path;
};

}   // namespace fs

// ---------------------------------------------------------------------------
// OS helpers
// ---------------------------------------------------------------------------

namespace os {

inline std::string homedir()
{
#ifdef _WIN32
    const char *drive = std::getenv("HOMEDRIVE");
    const char *path = std::getenv("HOMEPATH");
    if (drive != nullptr && path != nullptr)
        return std::string(drive) + path;
    const char *profile = std::getenv("USERPROFILE");
    return profile != nullptr ? std::string(profile) : std::string("C:\\");
#else
    const char *home = std::getenv("HOME");
    return home != nullptr ? std::string(home) : std::string("/");
#endif
}

}   // namespace os

// ---------------------------------------------------------------------------
// String helpers
// ---------------------------------------------------------------------------

namespace str {

inline std::vector<std::string> split(const std::string &text, char delimiter)
{
    std::vector<std::string> parts;
    size_t begin = 0;
    while (begin <= text.size()) {
        size_t end = text.find(delimiter, begin);
        if (end == std::string::npos)
            end = text.size();
        parts.push_back(text.substr(begin, end - begin));
        begin = end + 1;
        if (end == text.size())
            break;
    }
    return parts;
}

inline std::string replace(std::string text, const std::string &from, const std::string &to)
{
    if (from.empty())
        return text;
    size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
    return text;
}

}   // namespace str

// ---------------------------------------------------------------------------
// Concurrency, time and random helpers
// ---------------------------------------------------------------------------

inline void sleep(int64_t milliseconds)
{
    if (milliseconds > 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

class Timer
{
public:
    // Like coost's Timer, the clock starts when the object is constructed.
    Timer() : _start(clock::now()) {}

    void restart() { _start = clock::now(); }
    int64_t ms() const
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - _start).count();
    }
    int64_t elapsed() const { return ms(); }

private:
    using clock = std::chrono::steady_clock;
    clock::time_point _start;
};

inline int64_t nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
            .count();
}

inline std::string randstr(size_t length, const char *chars = "0123456789abcdef")
{
    static const char *const defaultChars = "0123456789abcdef";
    if (chars == nullptr || *chars == '\0')
        chars = defaultChars;
    const size_t count = std::strlen(chars);

    static thread_local std::mt19937_64 engine([] {
        std::random_device device;
        return (static_cast<uint64_t>(device()) << 32) ^ device();
    }());

    std::uniform_int_distribution<size_t> distribution(0, count - 1);
    std::string result;
    result.reserve(length);
    for (size_t i = 0; i < length; ++i)
        result.push_back(chars[distribution(engine)]);
    return result;
}

inline std::string randstr(const char *chars, size_t length)
{
    return randstr(length, chars);
}

template <typename T, typename... Args>
T *make(Args &&...args)
{
    return new T(std::forward<Args>(args)...);
}

template <typename T>
void del(T *pointer)
{
    delete pointer;
}

/**
 * @brief Bounded blocking queue with the coost compat::Channel interface.
 */
template <typename T>
class Channel
{
public:
    explicit Channel(size_t capacity = 10, int timeoutMs = -1)
        : _capacity(capacity == 0 ? 1 : capacity), _timeoutMs(timeoutMs)
    {
    }

    Channel &operator<<(const T &value)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        if (_queue.size() >= _capacity) {
            if (!_cond.wait_for(lock, std::chrono::milliseconds(_timeoutMs < 0 ? 0 : _timeoutMs),
                                [this] { return _queue.size() < _capacity; })) {
                _done = false;
                return *this;
            }
        }
        _queue.push_back(value);
        _done = true;
        _cond.notify_all();
        return *this;
    }

    Channel &operator>>(T &value)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        if (_queue.empty()) {
            if (!_cond.wait_for(lock, std::chrono::milliseconds(_timeoutMs < 0 ? 0 : _timeoutMs),
                                [this] { return !_queue.empty(); })) {
                _done = false;
                value = T {};
                return *this;
            }
        }
        value = std::move(_queue.front());
        _queue.pop_front();
        _done = true;
        _cond.notify_all();
        return *this;
    }

    bool done() const { return _done; }
    size_t size() const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _queue.size();
    }

private:
    mutable std::mutex _mutex;
    std::condition_variable _cond;
    std::deque<T> _queue;
    size_t _capacity;
    int _timeoutMs;
    bool _done { false };
};

/**
 * @brief Key/value map with the coost lru_map interface used by the daemon.
 *
 * The v20 discovery set is small (one entry per LAN device), so the eviction
 * policy is not needed for correctness; keeping the interface lets the
 * business code stay unchanged.
 */
template <typename Key, typename Value>
class LruMap
{
public:
    using Map = std::map<Key, Value>;
    using iterator = typename Map::iterator;
    using const_iterator = typename Map::const_iterator;

    iterator begin() { return _map.begin(); }
    iterator end() { return _map.end(); }
    const_iterator begin() const { return _map.begin(); }
    const_iterator end() const { return _map.end(); }
    iterator find(const Key &key) { return _map.find(key); }
    const_iterator find(const Key &key) const { return _map.find(key); }
    iterator erase(iterator it) { return _map.erase(it); }
    size_t erase(const Key &key) { return _map.erase(key); }
    void insert(const Key &key, const Value &value) { _map[key] = value; }
    void clear() { _map.clear(); }
    size_t size() const { return _map.size(); }
    bool empty() const { return _map.empty(); }

private:
    Map _map;
};

// ---------------------------------------------------------------------------
// Base64
// ---------------------------------------------------------------------------

inline std::string base64Encode(const std::string &input)
{
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    output.reserve(((input.size() + 2) / 3) * 4);
    size_t i = 0;
    while (i + 2 < input.size()) {
        const uint32_t value = (static_cast<uint8_t>(input[i]) << 16)
                | (static_cast<uint8_t>(input[i + 1]) << 8)
                | static_cast<uint8_t>(input[i + 2]);
        output.push_back(table[(value >> 18) & 0x3f]);
        output.push_back(table[(value >> 12) & 0x3f]);
        output.push_back(table[(value >> 6) & 0x3f]);
        output.push_back(table[value & 0x3f]);
        i += 3;
    }
    if (i < input.size()) {
        uint32_t value = static_cast<uint8_t>(input[i]) << 16;
        if (i + 1 < input.size())
            value |= static_cast<uint8_t>(input[i + 1]) << 8;
        output.push_back(table[(value >> 18) & 0x3f]);
        output.push_back(table[(value >> 12) & 0x3f]);
        output.push_back(i + 1 < input.size() ? table[(value >> 6) & 0x3f] : '=');
        output.push_back('=');
    }
    return output;
}

inline std::string base64Decode(const std::string &input)
{
    auto valueOf = [](char c) -> int {
        if (c >= 'A' && c <= 'Z')
            return c - 'A';
        if (c >= 'a' && c <= 'z')
            return c - 'a' + 26;
        if (c >= '0' && c <= '9')
            return c - '0' + 52;
        if (c == '+')
            return 62;
        if (c == '/')
            return 63;
        return -1;
    };

    std::string output;
    output.reserve((input.size() / 4) * 3);
    uint32_t value = 0;
    int bits = 0;
    for (char c : input) {
        if (c == '=')
            break;
        const int digit = valueOf(c);
        if (digit < 0)
            continue;
        value = (value << 6) | static_cast<uint32_t>(digit);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            output.push_back(static_cast<char>((value >> bits) & 0xff));
        }
    }
    return output;
}

}   // namespace compat

#endif   // COMPAT_BASE_H
