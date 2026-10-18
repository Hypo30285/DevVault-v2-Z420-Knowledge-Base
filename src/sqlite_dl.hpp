#pragma once
#include <cstdint>
#include <dlfcn.h>
#include <stdexcept>
#include <string>

namespace devvault {

struct sqlite3;
struct sqlite3_stmt;

constexpr int SQLITE_OK = 0;
constexpr int SQLITE_ROW = 100;
constexpr int SQLITE_DONE = 101;

using sqlite3_destructor_type = void (*)(void*);
#define SQLITE_TRANSIENT_PTR reinterpret_cast<sqlite3_destructor_type>(-1)

class SqliteApi {
public:
    int (*open)(const char*, sqlite3**) = nullptr;
    int (*close)(sqlite3*) = nullptr;
    int (*exec)(sqlite3*, const char*, int (*)(void*, int, char**, char**), void*, char**) = nullptr;
    void (*free_mem)(void*) = nullptr;
    const char* (*errmsg)(sqlite3*) = nullptr;
    int (*prepare_v2)(sqlite3*, const char*, int, sqlite3_stmt**, const char**) = nullptr;
    int (*step)(sqlite3_stmt*) = nullptr;
    int (*finalize)(sqlite3_stmt*) = nullptr;
    int (*reset)(sqlite3_stmt*) = nullptr;
    int (*bind_text)(sqlite3_stmt*, int, const char*, int, sqlite3_destructor_type) = nullptr;
    int (*bind_int64)(sqlite3_stmt*, int, int64_t) = nullptr;
    int (*bind_double)(sqlite3_stmt*, int, double) = nullptr;
    int (*bind_blob)(sqlite3_stmt*, int, const void*, int, sqlite3_destructor_type) = nullptr;
    const unsigned char* (*column_text)(sqlite3_stmt*, int) = nullptr;
    int64_t (*column_int64)(sqlite3_stmt*, int) = nullptr;
    double (*column_double)(sqlite3_stmt*, int) = nullptr;
    const void* (*column_blob)(sqlite3_stmt*, int) = nullptr;
    int (*column_bytes)(sqlite3_stmt*, int) = nullptr;
    int64_t (*last_insert_rowid)(sqlite3*) = nullptr;
    int (*busy_timeout)(sqlite3*, int) = nullptr;

    static const SqliteApi& instance() {
        static SqliteApi api;
        return api;
    }

private:
    void* handle_ = nullptr;

    template <typename T>
    void load_sym(T& fn, const char* name) {
        fn = reinterpret_cast<T>(dlsym(handle_, name));
        if (!fn) {
            throw std::runtime_error(std::string("Missing symbol in libsqlite3.so.0: ") + name);
        }
    }

    SqliteApi() {
        const char* candidates[] = {
            "libsqlite3.so.0",
            "/lib/x86_64-linux-gnu/libsqlite3.so.0",
            "/usr/lib/x86_64-linux-gnu/libsqlite3.so.0",
            "libsqlite3.so"
        };
        for (const char* c : candidates) {
            handle_ = dlopen(c, RTLD_LAZY | RTLD_GLOBAL);
            if (handle_) break;
        }
        if (!handle_) {
            throw std::runtime_error("Failed to dlopen libsqlite3.so.0");
        }
        load_sym(open, "sqlite3_open");
        load_sym(close, "sqlite3_close");
        load_sym(exec, "sqlite3_exec");
        load_sym(free_mem, "sqlite3_free");
        load_sym(errmsg, "sqlite3_errmsg");
        load_sym(prepare_v2, "sqlite3_prepare_v2");
        load_sym(step, "sqlite3_step");
        load_sym(finalize, "sqlite3_finalize");
        load_sym(reset, "sqlite3_reset");
        load_sym(bind_text, "sqlite3_bind_text");
        load_sym(bind_int64, "sqlite3_bind_int64");
        load_sym(bind_double, "sqlite3_bind_double");
        load_sym(bind_blob, "sqlite3_bind_blob");
        load_sym(column_text, "sqlite3_column_text");
        load_sym(column_int64, "sqlite3_column_int64");
        load_sym(column_double, "sqlite3_column_double");
        load_sym(column_blob, "sqlite3_column_blob");
        load_sym(column_bytes, "sqlite3_column_bytes");
        load_sym(last_insert_rowid, "sqlite3_last_insert_rowid");
        load_sym(busy_timeout, "sqlite3_busy_timeout");
    }
};

} // namespace devvault
