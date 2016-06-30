#ifndef ETDC_FDWRAP_H
#define ETDC_FDWRAP_H

#include <runtime.h>
#include <threadfns.h>
#include <ezexcept.h>
#include <mountpoint.h>

#include <list>
#include <string>
#include <map>


typedef ssize_t (*writefnptr)(int, const void*, size_t, int);
typedef ssize_t (*readfnptr)(int, void*, size_t, int);
typedef void    (*setipdfnptr)(int, int);
typedef int     (*closefnptr)(int);

struct fdoperations_type {
    // will create something with NULL function pointers!
    fdoperations_type();

    // this is the c'tor to use
    fdoperations_type(const std::string& proto);


    writefnptr  writefn;
    readfnptr   readfn;
    setipdfnptr setipdfn;
    closefnptr  closefn;

    // These functions will implement read/write using
    // loops, in order to make the read/write work even 
    // if "n" is HUGE (e.g. 512MB). Single read/write
    // operations on sockets of this size typically just fail.
    //
    // They don't throw, just check if return value == how many
    // you wanted to read/write. If unequal, inspect errno
    ssize_t     read(int fd, void* ptr, size_t n, int f=0) const;
    ssize_t     write(int fd, const void* ptr, size_t n, int f=0) const;
    void        set_ipd(int fd, int ipd) const;
    int         close(int fd) const;
};

// Helper function to read the "itcp_id" style header (see "kvmap.h")
std::string read_itcp_header(int fd, const fdoperations_type& fdops);


#endif
