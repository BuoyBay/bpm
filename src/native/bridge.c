/* bpm portable host primitives. Scalar UTF-8 ABI; no shell invocation.
 * The generated include is ProcessKit with private symbol prefixes and a
 * narrowly checked environment-deletion extension. See build_bpm_host.py. */
#include "bpm_processkit.inc"
#include <sys/stat.h>
#include <time.h>
#ifdef _WIN32
#include <bcrypt.h>
#include <fcntl.h>
#else
#include <utime.h>
#endif

#ifdef _MSC_VER
#define BPH_TLS __declspec(thread)
#else
#define BPH_TLS _Thread_local
#endif
static BPH_TLS char bph_message[256];
API const char *bph_error(void) { return bph_message; }
static int64_t bph_fail(const char *operation) {
#ifdef _WIN32
    DWORD saved = GetLastError();
    snprintf(bph_message, sizeof(bph_message), "%s (Windows error %lu)", operation, (unsigned long)saved);
    SetLastError(saved);
#else
    int saved = errno;
    snprintf(bph_message, sizeof(bph_message), "%s: %s", operation, strerror(saved));
    errno = saved;
#endif
    return 0;
}
static int64_t bph_pathfail(const char *operation) { bph_fail(operation); return -1; }
static int64_t bph_copy(const char *text, char *buffer, int64_t capacity) {
    size_t n = strlen(text) + 1;
    if (!buffer || capacity < 1 || (uint64_t)capacity < n) { errno = ERANGE; return bph_pathfail("Insufficient path buffer"); }
    memcpy(buffer, text, n); return (int64_t)n - 1;
}
#ifdef _WIN32
static int64_t bph_utf8(const wchar_t *text, char *buffer, int64_t capacity) {
    if (!buffer || capacity < 1 || capacity > INT_MAX) return bph_pathfail("Invalid path buffer");
    int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, buffer, (int)capacity, NULL, NULL);
    if (!size) return bph_pathfail("Cannot encode UTF-8 path");
    return size - 1;
}
#endif
API int64_t bph_pid(void) {
#ifdef _WIN32
    return GetCurrentProcessId();
#else
    return getpid();
#endif
}
API int64_t bph_alive(int64_t pid) {
    if (pid < 1
#ifdef _WIN32
        || (uint64_t)pid > UINT32_MAX
#else
        || pid > INT32_MAX
#endif
    ) { errno = EINVAL; bph_fail("Invalid process identifier"); return -1; }
#ifdef _WIN32
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)pid);
    if (!process) {
        if (GetLastError() == ERROR_INVALID_PARAMETER) return 0;
        bph_fail("Cannot inspect process"); return -1;
    }
    DWORD state = WaitForSingleObject(process, 0); CloseHandle(process);
    if (state == WAIT_OBJECT_0) return 0;
    if (state == WAIT_TIMEOUT) return 1;
    bph_fail("Cannot inspect process state"); return -1;
#else
    if (kill((pid_t)pid, 0) == 0 || errno == EPERM) return 1;
    if (errno == ESRCH) return 0;
    bph_fail("Cannot inspect process"); return -1;
#endif
}
API int64_t bph_millis(void) {
#ifdef _WIN32
    return (int64_t)GetTickCount64();
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts)) return -1;
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
}
API void bph_sleep(int64_t milliseconds) {
    if (milliseconds < 0 || milliseconds > 60000) return;
#ifdef _WIN32
    Sleep((DWORD)milliseconds);
#else
    struct timespec delay = {(time_t)(milliseconds / 1000), (long)(milliseconds % 1000) * 1000000};
    while (nanosleep(&delay, &delay) && errno == EINTR) {}
#endif
}
API int64_t bph_mkdir_exclusive(const char *path) {
    if (!path || !*path) { errno = EINVAL; return bph_fail("Empty directory path"); }
#ifdef _WIN32
    wchar_t *wide = wide_text(path); if (!wide) return bph_fail("Cannot decode directory path");
    int success = CreateDirectoryW(wide, NULL); free(wide);
#else
    int success = mkdir(path, 0700) == 0;
#endif
    return success ? 1 : bph_fail("Cannot exclusively create directory");
}
API int64_t bph_no_symlink(const char *path) {
#ifdef _WIN32
    wchar_t *wide = wide_text(path); if (!wide) { bph_fail("Cannot decode path"); return -1; }
    DWORD attributes = GetFileAttributesW(wide); DWORD error = GetLastError(); free(wide);
    if (attributes != INVALID_FILE_ATTRIBUTES) return (attributes & FILE_ATTRIBUTE_REPARSE_POINT) ? 0 : 1;
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return 1;
    SetLastError(error); bph_fail("Cannot inspect path"); return -1;
#else
    struct stat st;
    if (lstat(path, &st) == 0) return S_ISLNK(st.st_mode) ? 0 : 1;
    if (errno == ENOENT || errno == ENOTDIR) return 1;
    bph_fail("Cannot inspect path"); return -1;
#endif
}
API int64_t bph_realpath(const char *path, char *buffer, int64_t capacity) {
#ifdef _WIN32
    wchar_t *wide = wide_text(path); if (!wide) return bph_pathfail("Cannot decode directory path");
    HANDLE file = CreateFileW(wide, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL); free(wide);
    if (file == INVALID_HANDLE_VALUE) return bph_pathfail("Cannot open directory");
    BY_HANDLE_FILE_INFORMATION info;
    if (!GetFileInformationByHandle(file, &info) || !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) { CloseHandle(file); return bph_pathfail("Path is not a directory"); }
    DWORD count = GetFinalPathNameByHandleW(file, NULL, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    wchar_t *resolved = count ? calloc((size_t)count + 1, sizeof(wchar_t)) : NULL;
    if (!resolved) { CloseHandle(file); return bph_pathfail("Cannot allocate canonical path"); }
    DWORD written = GetFinalPathNameByHandleW(file, resolved, count + 1, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS); CloseHandle(file);
    if (!written || written > count) { free(resolved); return bph_pathfail("Cannot resolve directory"); }
    wchar_t *plain = resolved;
    if (wcsncmp(resolved, L"\\\\?\\UNC\\", 8) == 0) { resolved[6] = L'\\'; plain = resolved + 6; }
    else if (wcsncmp(resolved, L"\\\\?\\", 4) == 0) plain = resolved + 4;
    for (wchar_t *p = plain; *p; p++) if (*p == L'\\') *p = L'/';
    if (plain[0] >= L'a' && plain[0] <= L'z' && plain[1] == L':') plain[0] -= L'a' - L'A';
    int64_t success = bph_utf8(plain, buffer, capacity); free(resolved); return success;
#else
    char *resolved = realpath(path, NULL); if (!resolved) return bph_pathfail("Cannot resolve directory");
    struct stat st;
    if (stat(resolved, &st) || !S_ISDIR(st.st_mode)) { free(resolved); errno = ENOTDIR; return bph_pathfail("Path is not a directory"); }
    int64_t success = bph_copy(resolved, buffer, capacity); free(resolved); return success;
#endif
}
static int bph_random(unsigned char *bytes, size_t length) {
#ifdef _WIN32
    return BCryptGenRandom(NULL, bytes, (ULONG)length, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#else
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC); if (fd < 0) return 0;
    size_t done = 0;
    while (done < length) {
        ssize_t n = read(fd, bytes + done, length - done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { close(fd); return 0; }
        done += (size_t)n;
    }
    return close(fd) == 0;
#endif
}
API int64_t bph_tempdir(const char *parent, const char *prefix, char *buffer, int64_t capacity) {
    if (!parent || !*parent || !prefix || !*prefix || strlen(prefix) > 128 || strpbrk(prefix, "/\\:")) { errno = EINVAL; return bph_pathfail("Invalid temporary directory prefix"); }
    size_t length = strlen(parent) + strlen(prefix) + 35;
    if (length > INT_MAX || capacity < 1 || (uint64_t)capacity < length) { errno = ERANGE; return bph_pathfail("Insufficient temporary directory buffer"); }
    char *candidate = malloc(length); if (!candidate) return bph_pathfail("Cannot allocate temporary path");
    for (int attempt = 0; attempt < 128; attempt++) {
        unsigned char bytes[16]; if (!bph_random(bytes, sizeof(bytes))) { free(candidate); return bph_pathfail("Cannot generate temporary name"); }
        char suffix[33]; for (size_t i = 0; i < 16; i++) snprintf(suffix + i * 2, 3, "%02x", bytes[i]);
        snprintf(candidate, length, "%s/%s%s", parent, prefix, suffix);
        if (bph_mkdir_exclusive(candidate)) { int64_t ok = bph_copy(candidate, buffer, capacity); free(candidate); return ok; }
#ifdef _WIN32
        if (GetLastError() != ERROR_ALREADY_EXISTS && GetLastError() != ERROR_FILE_EXISTS) break;
#else
        if (errno != EEXIST) break;
#endif
    }
    free(candidate); return -1;
}
API int64_t bph_timestamp(const char *path) {
    if (bph_no_symlink(path) != 1) return 0;
#ifdef _WIN32
    wchar_t *wide = wide_text(path); if (!wide) return bph_fail("Cannot decode timestamp path");
    HANDLE file = CreateFileW(wide, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, NULL); free(wide);
    if (file == INVALID_HANDLE_VALUE) return bph_fail("Cannot open timestamp path");
    ULARGE_INTEGER ticks; ticks.QuadPart = UINT64_C(125911584000000000);
    FILETIME stamp = {ticks.LowPart, ticks.HighPart};
    int ok = SetFileTime(file, NULL, &stamp, &stamp); CloseHandle(file); return ok ? 1 : bph_fail("Cannot normalize timestamp");
#else
    struct timespec times[2] = {{946684800, 0}, {946684800, 0}};
    return utimensat(AT_FDCWD, path, times, AT_SYMLINK_NOFOLLOW) == 0 ? 1 : bph_fail("Cannot normalize timestamp");
#endif
}
API int64_t bph_replace(const char *source, const char *destination) {
    if (bph_no_symlink(source) != 1 || bph_no_symlink(destination) != 1) return 0;
#ifdef _WIN32
    wchar_t *from = wide_text(source), *to = wide_text(destination);
    if (!from || !to) { free(from); free(to); return bph_fail("Cannot decode replacement path"); }
    int ok = MoveFileExW(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH); free(from); free(to);
#else
    int ok = rename(source, destination) == 0;
#endif
    return ok ? 1 : bph_fail("Cannot atomically replace file");
}
/* SHA-256, FIPS 180-4 compression/padding. No unbounded allocation. Verified
 * against Python hashlib for standard vectors, block boundaries and random files. */
typedef struct { uint32_t h[8]; uint64_t length; size_t used; unsigned char block[64]; } BPHSHA;
static uint32_t bph_ror(uint32_t v, unsigned n) { return (v >> n) | (v << (32 - n)); }
static void bph_sha_block(BPHSHA *s, const unsigned char *b) {
    static const uint32_t k[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    uint32_t w[64];
    for (size_t i=0;i<16;i++) w[i]=((uint32_t)b[i*4]<<24)|((uint32_t)b[i*4+1]<<16)|((uint32_t)b[i*4+2]<<8)|b[i*4+3];
    for (size_t i=16;i<64;i++) w[i]=w[i-16]+(bph_ror(w[i-15],7)^bph_ror(w[i-15],18)^(w[i-15]>>3))+w[i-7]+(bph_ror(w[i-2],17)^bph_ror(w[i-2],19)^(w[i-2]>>10));
    uint32_t a=s->h[0],c=s->h[2],d=s->h[3],e=s->h[4],f=s->h[5],g=s->h[6],h=s->h[7],bb=s->h[1];
    for(size_t i=0;i<64;i++){uint32_t t1=h+(bph_ror(e,6)^bph_ror(e,11)^bph_ror(e,25))+((e&f)^(~e&g))+k[i]+w[i];uint32_t t2=(bph_ror(a,2)^bph_ror(a,13)^bph_ror(a,22))+((a&bb)^(a&c)^(bb&c));h=g;g=f;f=e;e=d+t1;d=c;c=bb;bb=a;a=t1+t2;}
    s->h[0]+=a;s->h[1]+=bb;s->h[2]+=c;s->h[3]+=d;s->h[4]+=e;s->h[5]+=f;s->h[6]+=g;s->h[7]+=h;
}
static void bph_sha_update(BPHSHA *s,const unsigned char *data,size_t n){s->length+=(uint64_t)n;while(n){size_t take=64-s->used;if(take>n)take=n;memcpy(s->block+s->used,data,take);s->used+=take;data+=take;n-=take;if(s->used==64){bph_sha_block(s,s->block);s->used=0;}}}
API int64_t bph_sha256_file(const char *path, char *buffer, int64_t capacity) {
    if (!buffer || capacity < 65 || bph_no_symlink(path) != 1) { errno = EINVAL; return bph_pathfail("Invalid SHA-256 input"); }
#ifdef _WIN32
    wchar_t *wide = wide_text(path); if (!wide) return bph_pathfail("Cannot decode hash path");
    HANDLE input = CreateFileW(wide, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, NULL); free(wide);
    if (input == INVALID_HANDLE_VALUE) return bph_pathfail("Cannot open hash input");
    BY_HANDLE_FILE_INFORMATION info;
    if (GetFileType(input) != FILE_TYPE_DISK || !GetFileInformationByHandle(input, &info) || (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) { CloseHandle(input); return bph_pathfail("Hash input is not a regular file"); }
    int descriptor = _open_osfhandle((intptr_t)input, _O_RDONLY | _O_BINARY);
    if (descriptor < 0) { CloseHandle(input); return bph_pathfail("Cannot open hash stream"); }
    FILE *file = _fdopen(descriptor, "rb");
    if (!file) _close(descriptor);
#else
    int descriptor = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (descriptor < 0) return bph_pathfail("Cannot open hash input");
    struct stat info;
    if (fstat(descriptor, &info) || !S_ISREG(info.st_mode)) { close(descriptor); errno = EINVAL; return bph_pathfail("Hash input is not a regular file"); }
    FILE *file = fdopen(descriptor, "rb");
    if (!file) close(descriptor);
#endif
    if (!file) return bph_pathfail("Cannot open hash input");
    BPHSHA s={{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19},0,0,{0}};
    unsigned char chunk[65536];size_t n;
    while((n=fread(chunk,1,sizeof(chunk),file))) bph_sha_update(&s,chunk,n);
    int failed=ferror(file); if(fclose(file)) failed=1;
    if(failed) return bph_pathfail("Cannot read hash input");
    uint64_t bits=s.length*8;s.block[s.used++]=0x80;if(s.used>56){memset(s.block+s.used,0,64-s.used);bph_sha_block(&s,s.block);s.used=0;}memset(s.block+s.used,0,56-s.used);for(size_t i=0;i<8;i++)s.block[63-i]=(unsigned char)(bits>>(i*8));bph_sha_block(&s,s.block);
    for(size_t i=0;i<8;i++) snprintf(buffer+i*8,9,"%08x",s.h[i]);
    return 64;
}
API int64_t bph_find_executable(const char *name, char *buffer, int64_t capacity) {
    if(!name || !*name) { errno=EINVAL;return bph_pathfail("Empty executable name"); }
#ifdef _WIN32
    wchar_t *wide=wide_text(name);if(!wide)return bph_pathfail("Cannot decode executable name");
    size_t len=wcslen(wide);if(len>=2 && wide[1]==L':' && (len<3 || (wide[2]!=L'/' && wide[2]!=L'\\'))){free(wide);return bph_pathfail("Drive-relative executable path rejected");}if(len>=4 && _wcsicmp(wide+len-4,L".exe")!=0 && wcschr(wide,L'.')){free(wide);return bph_pathfail("Only .exe executables are supported");}
    wchar_t *candidate=calloc(32768,sizeof(wchar_t)); if(!candidate){free(wide);return bph_pathfail("Cannot allocate executable path");}
    int found=0;
    if(wcschr(wide,L'/')||wcschr(wide,L'\\')||wcschr(wide,L':')){
        DWORD n=GetFullPathNameW(wide,32768,candidate,NULL);DWORD attrs=n&&n<32768?GetFileAttributesW(candidate):INVALID_FILE_ATTRIBUTES;found=attrs!=INVALID_FILE_ATTRIBUTES && !(attrs&FILE_ATTRIBUTE_DIRECTORY);
    }else{
        DWORD needed=GetEnvironmentVariableW(L"PATH",NULL,0);wchar_t *paths=needed?calloc(needed,sizeof(wchar_t)):NULL;
        if(paths&&GetEnvironmentVariableW(L"PATH",paths,needed)){
            wchar_t *at=paths;
            while(*at&&!found){wchar_t *end=wcschr(at,L';');if(end)*end=0;size_t n=wcslen(at);if(n && n+len+6<32768){swprintf(candidate,32768,L"%ls/%ls%ls",at,wide,len>=4&&_wcsicmp(wide+len-4,L".exe")==0?L"":L".exe");DWORD attrs=GetFileAttributesW(candidate);found=attrs!=INVALID_FILE_ATTRIBUTES&&!(attrs&FILE_ATTRIBUTE_DIRECTORY);}if(!end)break;at=end+1;}
        }free(paths);
    }free(wide);int64_t ok=found?bph_utf8(candidate,buffer,capacity):bph_pathfail("Executable not found on PATH");free(candidate);return ok;
#else
    if(strchr(name,'/')){if(access(name,X_OK))return bph_pathfail("Executable is not accessible");char *full=realpath(name,NULL);if(!full)return bph_pathfail("Cannot resolve executable");struct stat st;int valid=stat(full,&st)==0&&S_ISREG(st.st_mode);int64_t ok=valid?bph_copy(full,buffer,capacity):bph_pathfail("Executable is not a regular file");free(full);return ok;}
    const char *paths=getenv("PATH");if(!paths){errno=ENOENT;return bph_pathfail("PATH is unset");}char *copy=copy_text(paths);if(!copy)return bph_pathfail("Cannot copy PATH");char *at=copy;
    while(*at){char *end=strchr(at,':');if(end)*end=0;if(*at){size_t size=strlen(at)+strlen(name)+2;char *candidate=malloc(size);if(!candidate){free(copy);return bph_pathfail("Cannot allocate executable path");}snprintf(candidate,size,"%s/%s",at,name);struct stat st;if(access(candidate,X_OK)==0&&stat(candidate,&st)==0&&S_ISREG(st.st_mode)){char *full=realpath(candidate,NULL);free(candidate);if(full){int64_t ok=bph_copy(full,buffer,capacity);free(full);free(copy);return ok;}}else free(candidate);}if(!end)break;at=end+1;}
    free(copy);errno=ENOENT;return bph_pathfail("Executable not found on PATH");
#endif
}
/* Deletion differs from setting an empty value. Generated environment merging
 * removes matching inherited keys before conditionally appending an override. */
API int64_t bph_proc_unset(BPKProcess *p, const char *name) {
    if (!bph_proc_env(p, name, "")) return 0;
    free_environment_text(p->env_values[p->env_count-1]);p->env_values[p->env_count-1]=NULL;return 1;
}

API int64_t bph_proc_file_limit(BPKProcess *p, int64_t bytes) {
    if (!p || p->started || bytes < 0) return 0;
#ifdef _WIN32
    if (bytes) { snprintf(p->error, sizeof(p->error), "Per-file process limit is unavailable on Windows"); return 0; }
#else
    p->file_limit = bytes;
#endif
    return 1;
}

#ifdef _WIN32
/* Cleanup is limited to caller-owned temporary roots. Never enumerate a reparse
 * point. Holding each directory without FILE_SHARE_DELETE keeps its name stable
 * during traversal; metadata/deletion use the opened no-follow leaf handle. */
static int bph_cleanup_component(const wchar_t *text, size_t length) {
    if (!length || text[length - 1] == L'.' || text[length - 1] == L' ') return 0;
    for (size_t i = 0; i < length; i++) {
        if (text[i] < 32 || wcschr(L"<>:\"/\\|?*", text[i])) return 0;
    }
    return 1;
}
static int bph_remove_windows_node(const wchar_t *path, unsigned depth) {
    if (depth > 256) {
        SetLastError(ERROR_BAD_PATHNAME);
        return (int)bph_fail("Temporary cleanup nesting limit exceeded");
    }
    HANDLE leaf = CreateFileW(path, DELETE | FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (leaf == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return 1;
        return (int)bph_fail("Cannot open temporary cleanup entry");
    }
    FILE_BASIC_INFO basic;
    if (!GetFileInformationByHandleEx(leaf, FileBasicInfo, &basic, sizeof(basic))) {
        bph_fail("Cannot inspect temporary cleanup entry"); CloseHandle(leaf); return 0;
    }
    if ((basic.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
        !(basic.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        size_t length = wcslen(path);
        if (length > 32760) {
            SetLastError(ERROR_FILENAME_EXCED_RANGE); bph_fail("Temporary cleanup path is too long"); CloseHandle(leaf); return 0;
        }
        wchar_t *pattern = calloc(length + 3, sizeof(wchar_t));
        if (!pattern) {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY); bph_fail("Cannot allocate temporary cleanup pattern"); CloseHandle(leaf); return 0;
        }
        memcpy(pattern, path, length * sizeof(wchar_t));
        pattern[length] = L'\\'; pattern[length + 1] = L'*';
        WIN32_FIND_DATAW entry;
        HANDLE listing = FindFirstFileW(pattern, &entry);
        DWORD error = GetLastError(); free(pattern);
        if (listing == INVALID_HANDLE_VALUE) {
            if (error != ERROR_FILE_NOT_FOUND) {
                SetLastError(error); bph_fail("Cannot enumerate temporary cleanup directory"); CloseHandle(leaf); return 0;
            }
        } else {
            int ok = 1;
            do {
                if (wcscmp(entry.cFileName, L".") == 0 || wcscmp(entry.cFileName, L"..") == 0) continue;
                size_t child_length = wcslen(entry.cFileName);
                if (!bph_cleanup_component(entry.cFileName, child_length)) {
                    SetLastError(ERROR_INVALID_NAME); bph_fail("Refusing ambiguous temporary cleanup filename"); ok = 0; break;
                }
                if (length + child_length + 2 > 32767) {
                    SetLastError(ERROR_FILENAME_EXCED_RANGE); bph_fail("Temporary cleanup path is too long"); ok = 0; break;
                }
                wchar_t *child = calloc(length + child_length + 2, sizeof(wchar_t));
                if (!child) {
                    SetLastError(ERROR_NOT_ENOUGH_MEMORY); bph_fail("Cannot allocate temporary cleanup path"); ok = 0; break;
                }
                memcpy(child, path, length * sizeof(wchar_t)); child[length] = L'\\';
                memcpy(child + length + 1, entry.cFileName, (child_length + 1) * sizeof(wchar_t));
                ok = bph_remove_windows_node(child, depth + 1); free(child);
                if (!ok) break;
            } while (FindNextFileW(listing, &entry));
            if (ok && GetLastError() != ERROR_NO_MORE_FILES) {
                bph_fail("Cannot finish temporary cleanup enumeration"); ok = 0;
            }
            FindClose(listing);
            if (!ok) { CloseHandle(leaf); return 0; }
        }
    }
    if (basic.FileAttributes & FILE_ATTRIBUTE_READONLY) {
        basic.FileAttributes &= ~FILE_ATTRIBUTE_READONLY;
        if (!basic.FileAttributes) basic.FileAttributes = FILE_ATTRIBUTE_NORMAL;
        if (!SetFileInformationByHandle(leaf, FileBasicInfo, &basic, sizeof(basic))) {
            bph_fail("Cannot clear temporary cleanup read-only attribute"); CloseHandle(leaf); return 0;
        }
    }
    FILE_DISPOSITION_INFO disposition; disposition.DeleteFile = TRUE;
    if (!SetFileInformationByHandle(leaf, FileDispositionInfo, &disposition, sizeof(disposition))) {
        bph_fail("Cannot delete temporary cleanup entry"); CloseHandle(leaf); return 0;
    }
    if (!CloseHandle(leaf)) return (int)bph_fail("Cannot close deleted temporary cleanup entry");
    return 1;
}

/* Return the prefix length through the root separator, or zero for a relative,
 * device-namespace, drive-root or UNC-share-root path. GetFullPathName has already
 * collapsed dot segments; only a nonempty descendant is eligible for cleanup. */
static size_t bph_cleanup_root_prefix(const wchar_t *path) {
    size_t length = wcslen(path);
    if (length >= 4 && ((path[0] >= L'A' && path[0] <= L'Z') ||
        (path[0] >= L'a' && path[0] <= L'z')) && path[1] == L':' && path[2] == L'\\') return 3;
    if (length >= 3 && path[0] == L'\\' && path[1] == L'\\' && path[2] != L'?' && path[2] != L'.') {
        const wchar_t *server_end = wcschr(path + 2, L'\\');
        if (!server_end || server_end == path + 2) return 0;
        const wchar_t *share_end = wcschr(server_end + 1, L'\\');
        if (!share_end || share_end == server_end + 1 || !share_end[1]) return 0;
        return (size_t)(share_end - path + 1);
    }
    return 0;
}
#endif

API int64_t bph_remove_tree(const char *path) {
#ifdef _WIN32
    if (!path || !*path) { SetLastError(ERROR_INVALID_PARAMETER); return bph_fail("Refusing empty temporary cleanup path"); }
    wchar_t *wide = wide_text(path);
    if (!wide) return bph_fail("Cannot decode temporary cleanup path");
    for (wchar_t *at = wide; *at; at++) if (*at == L'/') *at = L'\\';
    size_t original_prefix = bph_cleanup_root_prefix(wide);
    if (!original_prefix) {
        free(wide); SetLastError(ERROR_INVALID_PARAMETER); return bph_fail("Temporary cleanup requires an absolute descendant path");
    }
    /* Validate every supplied component before normalization or missing-parent
     * shortcuts. An absent ancestor must not make an ambiguous path acceptable. */
    const wchar_t *original_component = wide + original_prefix;
    for (const wchar_t *at = original_component; ; at++) {
        if (*at != L'\\' && *at) continue;
        if (!bph_cleanup_component(original_component, (size_t)(at - original_component))) {
            free(wide); SetLastError(ERROR_INVALID_NAME); return bph_fail("Refusing ambiguous temporary cleanup path");
        }
        if (!*at) break;
        original_component = at + 1;
    }
    DWORD needed = GetFullPathNameW(wide, 0, NULL, NULL);
    wchar_t *full = needed && needed < 32768 ? calloc(needed, sizeof(wchar_t)) : NULL;
    if (!full) { free(wide); SetLastError(ERROR_INVALID_PARAMETER); return bph_fail("Cannot allocate normalized temporary cleanup path"); }
    DWORD count = GetFullPathNameW(wide, needed, full, NULL); free(wide);
    if (!count || count >= needed) { free(full); return bph_fail("Cannot normalize temporary cleanup path"); }
    while (count && full[count - 1] == L'\\') full[--count] = 0;
    size_t prefix = bph_cleanup_root_prefix(full);
    if (!prefix || count <= prefix || wcschr(full + prefix, L':') || wcspbrk(full + prefix, L"*?")) {
        free(full); SetLastError(ERROR_INVALID_PARAMETER); return bph_fail("Refusing filesystem root or invalid temporary cleanup path");
    }
    /* Reject existing reparse points in ancestors. A reparse leaf itself is safe:
     * the worker opens it with OPEN_REPARSE_POINT and deletes only that handle. */
    wchar_t *component = full + prefix;
    for (wchar_t *at = component; ; at++) {
        if (*at != L'\\' && *at) continue;
        if (!bph_cleanup_component(component, (size_t)(at - component))) {
            free(full); SetLastError(ERROR_INVALID_NAME); return bph_fail("Refusing ambiguous temporary cleanup path");
        }
        if (!*at) break;
        component = at + 1;
        *at = 0; DWORD attributes = GetFileAttributesW(full); DWORD error = GetLastError(); *at = L'\\';
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) { free(full); return 1; }
            free(full); SetLastError(error); return bph_fail("Cannot inspect temporary cleanup ancestor");
        }
        if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) {
            free(full); SetLastError(ERROR_CANT_ACCESS_FILE); return bph_fail("Refusing reparse ancestor in temporary cleanup path");
        }
    }
    int ok = bph_remove_windows_node(full, 0); free(full); return ok;
#else
    (void)path;
    errno = ENOTSUP;
    return bph_fail("Native recursive temporary cleanup is only implemented on Windows");
#endif
}
