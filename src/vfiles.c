#include "vfiles.h"
/* Only the iostream header: the umbrella SDL.h pulls in Windows fileapi.h,
   whose Win32 GetFileSize() collides with this module's public symbol. */
#include <SDL3/SDL_iostream.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
#  include <direct.h>
#  define MKDIR(p) _mkdir(p)
#else
#  include <unistd.h>
#  define MKDIR(p) mkdir((p), 0755)
#endif

/* SDL_IOFromFile routes through Android's asset/content:// layer and the
   Emscripten virtual filesystem, unlike plain fopen(). */
unsigned char *LoadFileData(const char *path, unsigned int *bytesRead) {
    if (bytesRead) *bytesRead = 0;
    if (!path || !path[0]) return NULL;
    SDL_IOStream *io = SDL_IOFromFile(path, "rb");
    if (!io) return NULL;

    Sint64 sz = SDL_GetIOSize(io);
    if (sz <= 0) { SDL_CloseIO(io); return NULL; }

    unsigned char *buf = malloc((size_t)sz);
    if (!buf) { SDL_CloseIO(io); return NULL; }
    size_t got = SDL_ReadIO(io, buf, (size_t)sz);
    SDL_CloseIO(io);

    if (got != (size_t)sz) { free(buf); return NULL; }
    if (bytesRead) *bytesRead = (unsigned int)got;
    return buf;
}

bool SaveFileData(const char *path, const void *data, unsigned int bytes) {
    if (!path || !path[0] || (!data && bytes > 0)) return false;
    SDL_IOStream *io = SDL_IOFromFile(path, "wb");
    if (!io) return false;
    size_t wrote = bytes ? SDL_WriteIO(io, data, bytes) : 0;
    SDL_CloseIO(io);
    return wrote == bytes;
}

void UnloadFileData(unsigned char *data) {
    if (data) free(data);
}

char *LoadFileText(const char *path) {
    if (!path || !path[0]) return NULL;
    SDL_IOStream *io = SDL_IOFromFile(path, "rb");
    if (!io) return NULL;

    Sint64 sz = SDL_GetIOSize(io);
    if (sz < 0) { SDL_CloseIO(io); return NULL; }

    char *buf = malloc((size_t)sz + 1);
    if (!buf) { SDL_CloseIO(io); return NULL; }
    size_t got = SDL_ReadIO(io, buf, (size_t)sz);
    SDL_CloseIO(io);
    buf[got] = '\0';
    return buf;
}

bool SaveFileText(const char *path, const char *text) {
    if (!text) return false;
    return SaveFileData(path, text, (unsigned int)strlen(text));
}

void UnloadFileText(char *text) {
    if (text) free(text);
}

bool FileExists(const char *path) {
    if (!path || !path[0]) return false;
    SDL_IOStream *io = SDL_IOFromFile(path, "rb");
    if (!io) return false;
    SDL_CloseIO(io);
    return true;
}

bool DirectoryExists(const char *path) {
    if (!path || !path[0]) return false;
    struct stat st;
    if (stat(path, &st) != 0) return false;
#ifdef _WIN32
    return (st.st_mode & _S_IFDIR) != 0;
#else
    return S_ISDIR(st.st_mode);
#endif
}

unsigned int GetFileSizeBytes(const char *path) {
    if (!path || !path[0]) return 0;
    SDL_IOStream *io = SDL_IOFromFile(path, "rb");
    if (!io) return 0;
    Sint64 sz = SDL_GetIOSize(io);
    SDL_CloseIO(io);
    return sz > 0 ? (unsigned int)sz : 0;
}

unsigned int GetFileModTime(const char *path) {
    if (!path || !path[0]) return 0;
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return (unsigned int)st.st_mtime;
}

bool CreateDirectoryIfMissing(const char *path) {
    if (!path || !path[0]) return false;
    if (DirectoryExists(path)) return true;
    return MKDIR(path) == 0;
}