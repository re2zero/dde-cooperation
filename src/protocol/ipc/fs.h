// Autogenerated.
// DO NOT EDIT. All changes will be undone.
#pragma once

#include "co/rpc.h"

namespace ipc {

class FS : public rpc::Service {
  public:
    typedef std::function<void(co::Json&, co::Json&)> Fun;

    FS() {
        using std::placeholders::_1;
        using std::placeholders::_2;
        _methods["FS.compatible"] = std::bind(&FS::compatible, this, _1, _2);
        _methods["FS.readDir"] = std::bind(&FS::readDir, this, _1, _2);
        _methods["FS.removeDir"] = std::bind(&FS::removeDir, this, _1, _2);
        _methods["FS.create"] = std::bind(&FS::create, this, _1, _2);
        _methods["FS.rename"] = std::bind(&FS::rename, this, _1, _2);
        _methods["FS.removeFiles"] = std::bind(&FS::removeFiles, this, _1, _2);
        _methods["FS.sendFiles"] = std::bind(&FS::sendFiles, this, _1, _2);
        _methods["FS.receiveFiles"] = std::bind(&FS::receiveFiles, this, _1, _2);
        _methods["FS.resumeJob"] = std::bind(&FS::resumeJob, this, _1, _2);
        _methods["FS.cancelJob"] = std::bind(&FS::cancelJob, this, _1, _2);
        _methods["FS.fsReport"] = std::bind(&FS::fsReport, this, _1, _2);
    }

    virtual ~FS() {}

    virtual const char* name() const {
        return "FS";
    }

    virtual const co::map<const char*, Fun>& methods() const {
        return _methods;
    }

    virtual void compatible(co::Json& req, co::Json& res) = 0;

    virtual void readDir(co::Json& req, co::Json& res) = 0;

    virtual void removeDir(co::Json& req, co::Json& res) = 0;

    virtual void create(co::Json& req, co::Json& res) = 0;

    virtual void rename(co::Json& req, co::Json& res) = 0;

    virtual void removeFiles(co::Json& req, co::Json& res) = 0;

    virtual void sendFiles(co::Json& req, co::Json& res) = 0;

    virtual void receiveFiles(co::Json& req, co::Json& res) = 0;

    virtual void resumeJob(co::Json& req, co::Json& res) = 0;

    virtual void cancelJob(co::Json& req, co::Json& res) = 0;

    virtual void fsReport(co::Json& req, co::Json& res) = 0;

  private:
    co::map<const char*, Fun> _methods;
};

struct FileEntry {
    uint32 type;
    fastring name;
    bool hidden;
    uint64 size;
    uint64 modified_time;

    void from_json(const co::Json& _x_) {
        type = (uint32)_x_.get("type").as_int64();
        name = _x_.get("name").as_c_str();
        hidden = _x_.get("hidden").as_bool();
        size = (uint64)_x_.get("size").as_int64();
        modified_time = (uint64)_x_.get("modified_time").as_int64();
    }

    co::Json as_json() const {
        co::Json _x_;
        _x_.add_member("type", type);
        _x_.add_member("name", name);
        _x_.add_member("hidden", hidden);
        _x_.add_member("size", size);
        _x_.add_member("modified_time", modified_time);
        return _x_;
    }
};

struct FileDirectory {
    int32 id;
    fastring path;
    co::vector<FileEntry> entries;

    void from_json(const co::Json& _x_) {
        id = (int32)_x_.get("id").as_int64();
        path = _x_.get("path").as_c_str();
        do {
            auto& _unamed_v1 = _x_.get("entries");
            for (uint32 i = 0; i < _unamed_v1.array_size(); ++i) {
                FileEntry _unamed_v2;
                _unamed_v2.from_json(_unamed_v1[i]);
                entries.emplace_back(std::move(_unamed_v2));
            }
        } while (0);
    }

    co::Json as_json() const {
        co::Json _x_;
        _x_.add_member("id", id);
        _x_.add_member("path", path);
        do {
            co::Json _unamed_v1;
            for (size_t i = 0; i < entries.size(); ++i) {
                _unamed_v1.push_back(entries[i].as_json());
            }
            _x_.add_member("entries", _unamed_v1);
        } while (0);
        return _x_;
    }
};

struct DirRead {
    int32 id;
    fastring path;
    bool include_hidden;
    bool recursive;

    void from_json(const co::Json& _x_) {
        id = (int32)_x_.get("id").as_int64();
        path = _x_.get("path").as_c_str();
        include_hidden = _x_.get("include_hidden").as_bool();
        recursive = _x_.get("recursive").as_bool();
    }

    co::Json as_json() const {
        co::Json _x_;
        _x_.add_member("id", id);
        _x_.add_member("path", path);
        _x_.add_member("include_hidden", include_hidden);
        _x_.add_member("recursive", recursive);
        return _x_;
    }
};

struct DirRemove {
    int32 id;
    fastring path;
    bool recursive;
    bool trash;

    void from_json(const co::Json& _x_) {
        id = (int32)_x_.get("id").as_int64();
        path = _x_.get("path").as_c_str();
        recursive = _x_.get("recursive").as_bool();
        trash = _x_.get("trash").as_bool();
    }

    co::Json as_json() const {
        co::Json _x_;
        _x_.add_member("id", id);
        _x_.add_member("path", path);
        _x_.add_member("recursive", recursive);
        _x_.add_member("trash", trash);
        return _x_;
    }
};

struct CreateNew {
    int32 id;
    fastring path;
    bool is_dir;

    void from_json(const co::Json& _x_) {
        id = (int32)_x_.get("id").as_int64();
        path = _x_.get("path").as_c_str();
        is_dir = _x_.get("is_dir").as_bool();
    }

    co::Json as_json() const {
        co::Json _x_;
        _x_.add_member("id", id);
        _x_.add_member("path", path);
        _x_.add_member("is_dir", is_dir);
        return _x_;
    }
};

struct Rename {
    fastring from;
    fastring to;

    void from_json(const co::Json& _x_) {
        from = _x_.get("from").as_c_str();
        to = _x_.get("to").as_c_str();
    }

    co::Json as_json() const {
        co::Json _x_;
        _x_.add_member("from", from);
        _x_.add_member("to", to);
        return _x_;
    }
};

struct FileRemove {
    int32 id;
    co::vector<fastring> paths;
    int32 file_num;
    bool trash;

    void from_json(const co::Json& _x_) {
        id = (int32)_x_.get("id").as_int64();
        do {
            auto& _unamed_v1 = _x_.get("paths");
            for (uint32 i = 0; i < _unamed_v1.array_size(); ++i) {
                paths.push_back(_unamed_v1[i].as_c_str());
            }
        } while (0);
        file_num = (int32)_x_.get("file_num").as_int64();
        trash = _x_.get("trash").as_bool();
    }

    co::Json as_json() const {
        co::Json _x_;
        _x_.add_member("id", id);
        do {
            co::Json _unamed_v1;
            for (size_t i = 0; i < paths.size(); ++i) {
                _unamed_v1.push_back(paths[i]);
            }
            _x_.add_member("paths", _unamed_v1);
        } while (0);
        _x_.add_member("file_num", file_num);
        _x_.add_member("trash", trash);
        return _x_;
    }
};

struct FilesTrans {
    int32 id;
    co::vector<fastring> paths;
    int32 file_num;
    bool sub;
    fastring savedir;

    void from_json(const co::Json& _x_) {
        id = (int32)_x_.get("id").as_int64();
        do {
            auto& _unamed_v1 = _x_.get("paths");
            for (uint32 i = 0; i < _unamed_v1.array_size(); ++i) {
                paths.push_back(_unamed_v1[i].as_c_str());
            }
        } while (0);
        file_num = (int32)_x_.get("file_num").as_int64();
        sub = _x_.get("sub").as_bool();
        savedir = _x_.get("savedir").as_c_str();
    }

    co::Json as_json() const {
        co::Json _x_;
        _x_.add_member("id", id);
        do {
            co::Json _unamed_v1;
            for (size_t i = 0; i < paths.size(); ++i) {
                _unamed_v1.push_back(paths[i]);
            }
            _x_.add_member("paths", _unamed_v1);
        } while (0);
        _x_.add_member("file_num", file_num);
        _x_.add_member("sub", sub);
        _x_.add_member("savedir", savedir);
        return _x_;
    }
};

struct TransJob {
    int32 session_id;
    int32 job_id;
    bool is_remote;

    void from_json(const co::Json& _x_) {
        session_id = (int32)_x_.get("session_id").as_int64();
        job_id = (int32)_x_.get("job_id").as_int64();
        is_remote = _x_.get("is_remote").as_bool();
    }

    co::Json as_json() const {
        co::Json _x_;
        _x_.add_member("session_id", session_id);
        _x_.add_member("job_id", job_id);
        _x_.add_member("is_remote", is_remote);
        return _x_;
    }
};

struct ReportFS {
    int32 id;
    int32 type;
    int32 result;
    fastring path;
    fastring content;

    void from_json(const co::Json& _x_) {
        id = (int32)_x_.get("id").as_int64();
        type = (int32)_x_.get("type").as_int64();
        result = (int32)_x_.get("result").as_int64();
        path = _x_.get("path").as_c_str();
        content = _x_.get("content").as_c_str();
    }

    co::Json as_json() const {
        co::Json _x_;
        _x_.add_member("id", id);
        _x_.add_member("type", type);
        _x_.add_member("result", result);
        _x_.add_member("path", path);
        _x_.add_member("content", content);
        return _x_;
    }
};

struct FileTransBlock {
    int32 id;
    int32 file_num;
    bool compressed;
    uint32 blk_id;

    void from_json(const co::Json& _x_) {
        id = (int32)_x_.get("id").as_int64();
        file_num = (int32)_x_.get("file_num").as_int64();
        compressed = _x_.get("compressed").as_bool();
        blk_id = (uint32)_x_.get("blk_id").as_int64();
    }

    co::Json as_json() const {
        co::Json _x_;
        _x_.add_member("id", id);
        _x_.add_member("file_num", file_num);
        _x_.add_member("compressed", compressed);
        _x_.add_member("blk_id", blk_id);
        return _x_;
    }
};

struct FileTransDigest {
    int32 id;
    int32 blk_id;
    fastring blk_md5;

    void from_json(const co::Json& _x_) {
        id = (int32)_x_.get("id").as_int64();
        blk_id = (int32)_x_.get("blk_id").as_int64();
        blk_md5 = _x_.get("blk_md5").as_c_str();
    }

    co::Json as_json() const {
        co::Json _x_;
        _x_.add_member("id", id);
        _x_.add_member("blk_id", blk_id);
        _x_.add_member("blk_md5", blk_md5);
        return _x_;
    }
};

struct FileTransError {
    int32 id;
    int32 file_num;
    fastring error;

    void from_json(const co::Json& _x_) {
        id = (int32)_x_.get("id").as_int64();
        file_num = (int32)_x_.get("file_num").as_int64();
        error = _x_.get("error").as_c_str();
    }

    co::Json as_json() const {
        co::Json _x_;
        _x_.add_member("id", id);
        _x_.add_member("file_num", file_num);
        _x_.add_member("error", error);
        return _x_;
    }
};

struct FileTransDone {
    int32 id;
    int32 file_num;

    void from_json(const co::Json& _x_) {
        id = (int32)_x_.get("id").as_int64();
        file_num = (int32)_x_.get("file_num").as_int64();
    }

    co::Json as_json() const {
        co::Json _x_;
        _x_.add_member("id", id);
        _x_.add_member("file_num", file_num);
        return _x_;
    }
};

} // ipc
