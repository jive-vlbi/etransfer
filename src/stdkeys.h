// Some oft-used keys for setting parameter values
#ifndef ETDC_STDKEYS_H
#define ETDC_STDKEYS_H
// For code that uses:
//
//   namespace keys = etdc::stdkeys;
//   foo() {
//       ...
//       settings.update( keys::mtu=9000, keys::timeout=0.5 );
//   }
#include <keywordargs.h>


namespace etdc { namespace stdkeys {
    const auto mtu        = key("mtu");
    const auto timeout    = key("timeout");
    const auto rcvbufSize = key("rcvbufSize");
    const auto sndbufSize = key("sndbufSize");
}}

#endif
