// some of the variables need to be defined only once in the whole program
#include <etdc_debug.h>

namespace etdc { namespace detail {
    std::mutex       __m_iolock{};
    std::atomic<int> __m_dbglev{1};
    std::atomic<int> __m_fnthres{5};

    std::string timestamp( void ) {
        char           buff[32];
        struct tm      raw_tm;
        struct timeval raw_t1m3;

        ::gettimeofday(&raw_t1m3, NULL);
        ::gmtime_r(&raw_t1m3.tv_sec, &raw_tm);
        ::strftime( buff, sizeof(buff), "%Y-%m-%d %H:%M:%S", &raw_tm );
        ::snprintf( buff + 19, sizeof(buff)-19, ".%02ld: ", (long int)(raw_t1m3.tv_usec / 10000) );
        return buff;
    }

    } // namespace detail 
} // namespace etdc
