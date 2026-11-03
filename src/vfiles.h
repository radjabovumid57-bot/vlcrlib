#ifndef VLCRLIB_VFILES_H
#define VLCRLIB_VFILES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/* --- Бинарный I/O --- */
unsigned char *LoadFileData(const char *path, unsigned int *bytesRead);
bool           SaveFileData(const char *path, const void *data, unsigned int bytes);
void           UnloadFileData(unsigned char *data);

/* --- Текстовый I/O --- */
char *LoadFileText(const char *path);
bool  SaveFileText(const char *path, const char *text);
void  UnloadFileText(char *text);

/* --- Информация о путях --- */
bool         FileExists(const char *path);
bool         DirectoryExists(const char *path);
/* Named Bytes to avoid the Win32 GetFileSize() collision when a consumer
   includes <windows.h>. */
unsigned int GetFileSizeBytes(const char *path);
unsigned int GetFileModTime(const char *path);

#if !defined(_WIN32)
/* Source-compatibility alias on platforms without the Win32 symbol. */
#define GetFileSize(path) GetFileSizeBytes(path)
#endif

/* --- Операции --- */
bool CreateDirectoryIfMissing(const char *path);

#ifdef __cplusplus
}
#endif
#endif