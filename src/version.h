// function prototypes for the automatically generated version info functions
#ifndef BUILDINFO_VERSION_H
#define BUILDINFO_VERSION_H

#include <string>

// Returns a string with colon separated fields:
//   <program> : <program version> : <bit size> : <release> : <build> : <datetime>
std::string buildinfo( void );

// Return a specific field of the version string
//    "PROG"
//    "PROG_VERSION"
//    "B2B"
//    "RELEASE"
//    "BUILD"     
//    "BUILDINFO"  (date/time)
std::string version_constant( std::string constant );

#endif
