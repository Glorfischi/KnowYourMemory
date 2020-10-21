#ifndef DEBUG_H_
#define DEBUG_H_
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <sys/time.h>

//extern FILE *log_fp; // can be used to have global stream for debug messages


#define info(stream, fmt, ...) do {\
    fprintf(stream, fmt, ##__VA_ARGS__); \
    fflush(stream); \
} while(0)
#define info_wtime(stream, fmt, ...) do {\
    struct timeval _debug_tv;\
    gettimeofday(&_debug_tv,NULL);\
    fprintf(stream, "[%lu:%06lu] " fmt, _debug_tv.tv_sec, _debug_tv.tv_usec, ##__VA_ARGS__); \
    fflush(stream); \
} while(0)

#ifdef DEBUG
#define debug(stream, fmt, ...) do {\
    struct timeval _debug_tv;\
    gettimeofday(&_debug_tv,NULL);\
    fprintf(stream, "[DEBUG %lu:%lu] %s/%d/%s() " fmt, _debug_tv.tv_sec, _debug_tv.tv_usec, __FILE__, __LINE__, __func__, ##__VA_ARGS__); \
    fflush(stream); \
} while(0)
#define text(stream, fmt, ...) do {\
    fprintf(stream, fmt, ##__VA_ARGS__); \
    fflush(stream); \
} while(0)
#define text_wtime(stream, fmt, ...) do {\
    struct timeval _debug_tv;\
    gettimeofday(&_debug_tv,NULL);\
    fprintf(stream, "[%lu:%lu] " fmt, _debug_tv.tv_sec, _debug_tv.tv_usec, ##__VA_ARGS__); \
    fflush(stream); \
} while(0)
#else
#define debug(stream, fmt, ...)
#define text(stream, fmt, ...)
#define text_wtime(stream, fmt, ...)
#endif


#define error_return(rc, stream, fmt, ...) do { \
    fprintf(stream, "[ERROR] %s/%d/%s() " fmt, __FILE__, __LINE__, __func__, ##__VA_ARGS__); \
    fflush(stream); \
    return (rc);  \
} while(0)


#define error_exit(rc, stream, fmt, ...) do { \
    fprintf(stream, "[ERROR] %s/%d/%s() " fmt, __FILE__, __LINE__, __func__, ##__VA_ARGS__); \
    fflush(stream); \
    exit(rc); \
} while(0)



#endif /* DEBUG_H_ */

