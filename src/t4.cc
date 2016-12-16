#include <list>
#include <set>
#include <thread>
#include <iostream>
#include <iomanip>
#include <type_traits>
#include <utility>
#include <numeric>

//#include <signal.h>
//#include <string.h>
//#include <pthreadcall.h>
#include <etdc_signal.h>
#include <etdc_thread.h>

using namespace std;

// list of thunks [ std::function<void(*)(void)> ]
// own thread creation wrapper - ignore all signals before creating thread
// return std::thread&& - such that it can be a true wrappert!

#if 0
namespace etdc {
    template <typename T>
    struct Counter {
        Counter(T const& init = T{0}, T const& inc = T{1}): __m_counter(init), __m_increment(inc) {}

        T operator()( void ) const {
            T  ret{ __m_counter };
            return __m_counter+=__m_increment, ret;
        }

        private:
            mutable T   __m_counter;
            T           __m_increment;
    };

    // for use with stl algorithms that work on a pair of iterators
    // this pseudo sequence will allow iteration over the sequence 
    //    init, init+inc, init+2*inc, ...  
    // without actually allocating memory for <nElement> items
    //
    // http://en.cppreference.com/w/cpp/iterator/iterator
    // does mention a class template along these lines. This implementation
    // allows for multiple iterators to iterate over the same sequence
    // because the iterators do not modify the underlying Sequence object.
    // Incrementing one iterator does not invalidate an other.
    template <typename T>
    struct Sequence {
        // delete stuff that we really don't want to enable
        Sequence() = delete;

        // Require at least first, last.
        // Note that if 'inc' == 0 you'll get a divide-by-zero error
        Sequence(T const& first, T const& last, T const& inc = T{1}):
            __m_counter(first), __m_increment(inc), __m_last(&__m_counter + static_cast<unsigned int>(::abs((last-first)/inc)) + 1) {}

        struct iterator_impl {
                // There will be no public c'tor
                friend struct Sequence<T>;
            public:
                // What kind of iterator do we pretend to be?
                // An inputiterator - we can only guarantee single pass validity
                typedef void                    difference_type;
                typedef T                       value_type;
                typedef T*                      pointer;
                typedef T const&                reference;
                typedef std::input_iterator_tag iterator_category;

                iterator_impl() = delete;

                // non-const must be convertible to const iterator
                operator const iterator_impl() { return iterator_impl( *this ); }

                T operator*( void )       { return __m_counter; }
                T operator*( void ) const { return __m_counter; }

                iterator_impl& operator++( void )             { return do_inc(); }
                iterator_impl& operator++( int )              { return do_inc(); }
                iterator_impl const& operator++( void ) const { return do_inc(); }
                iterator_impl const& operator++( int )  const { return do_inc(); }

                bool operator==(iterator_impl const& other) const {
                    return __m_cur==other.__m_cur;
                }
                bool operator!=(iterator_impl const& other) const {
                    return  !(this->operator==(other));
                }

            private:
                iterator_impl(T const& cnt, T* cur, T const& inc):
                    __m_counter( cnt ), __m_increment( inc ), __m_cur( cur )
                {}

                iterator_impl&       do_inc( void )       { __m_counter += __m_increment; __m_cur++; return *this; };
                iterator_impl const& do_inc( void ) const { __m_counter += __m_increment; __m_cur++; return *this; };

                mutable T   __m_counter;
                T           __m_increment;
                mutable T*  __m_cur;
        };


        typedef iterator_impl       iterator;
        typedef const iterator_impl const_iterator;

        iterator       begin( void ) { return iterator_impl(__m_counter, &__m_counter, __m_increment); }
        iterator       end( void )   { return iterator_impl(__m_counter, __m_last, __m_increment); }
        const_iterator begin( void ) const { return iterator_impl(__m_counter, &__m_counter, __m_increment); }
        const_iterator end( void )   const { return iterator_impl(__m_counter, __m_last, __m_increment); }

        private:
            T   __m_counter, __m_increment;
            T*  __m_last;
    };

    template <typename T>
    Sequence<T> mk_sequence(T const& f, T const& l, T const& inc=T{1}) {
        return Sequence<T>(f, l, inc);
    }
}

#endif

#if 0
namespace etdc {

    template <typename T>
    std::string repr(T const& t) {
        std::ostringstream oss;
        oss << t;
        return oss.str();
    }

    namespace detail {
        // From: http://stackoverflow.com/a/9407521
        //       "Determine if a type is an STL container at compile time"
        template<typename T>
        struct has_const_iterator
        {
            private:
                typedef char                      yes;
                typedef struct { char array[2]; } no;

                template<typename C> static yes test(typename C::const_iterator*);
                template<typename C> static no  test(...);
            public:
                static const bool value = sizeof(test<T>(0)) == sizeof(yes);
                typedef T type;
        };

        template <typename T>
        struct has_begin_end {
            template<typename C> static char (&f(typename std::enable_if<
              std::is_same<decltype(static_cast<typename C::const_iterator (C::*)() const>(&C::begin)),
              typename C::const_iterator(C::*)() const>::value, void>::type*))[1];

            template<typename C> static char (&f(...))[2];

            template<typename C> static char (&g(typename std::enable_if<
              std::is_same<decltype(static_cast<typename C::const_iterator (C::*)() const>(&C::end)),
              typename C::const_iterator(C::*)() const>::value, void>::type*))[1];

            template<typename C> static char (&g(...))[2];

            static bool const beg_value = sizeof(f<T>(0)) == 1;
            static bool const end_value = sizeof(g<T>(0)) == 1;
        };

        template<typename T> 
        struct is_container :
            std::integral_constant<bool, has_const_iterator<T>::value && has_begin_end<T>::beg_value && has_begin_end<T>::end_value> 
        {};
    }

    template <typename Separator, typename Open = std::string, typename Close = std::string>
    struct streamiter {
        public:
            streamiter(std::ostream& os, Separator sep, Open open = Open(), Close close = Close()):
                __need_separator( false ), __need_close( true ), __m_separator( sep ), __m_close( close ), __m_streamref( os )
            { __m_streamref.get() << open; }

            // In the move constructor we must explicitly tell the object
            // that we've been move-constructed from to NOT output the
            // closing sequence
            streamiter(streamiter&& other):
                __need_separator( false ), __need_close( true ),
                __m_separator( std::move(other.__m_separator) ),
                __m_close( std::move(other.__m_close) ),
                __m_streamref( std::move(other.__m_streamref) )
            {
                other.__need_close = false;
            }
            // iterator interface
            template <typename T>
            streamiter& operator=(T const& t) {
                if( __need_separator )
                     __m_streamref.get() << __m_separator;
                __m_streamref.get() << t, __need_separator = true;
                return *this;
            }

            streamiter& operator++()          { return *this; };
            streamiter& operator++(int)       { return *this; };
            streamiter& operator++() const    { return *this; };
            streamiter& operator++(int) const { return *this; };

            streamiter&       operator*()       { return *this; };
            streamiter const& operator*() const { return *this; };

            // explicitly forbid default c'tor
            streamiter() = delete;

            ~streamiter() {
                __need_close && (__m_streamref.get() << __m_close);
            }

        private:
            using StreamRef = std::reference_wrapper<std::ostream>;

            bool        __need_separator, __need_close;
            Separator   __m_separator;
            Close       __m_close;
            StreamRef   __m_streamref;
    };

    template <typename... Args>
    auto mk_streamiter(std::ostream& os, Args&&... args) -> streamiter<Args...> {
        return streamiter<Args...>{os, std::forward<Args>(args)...};
    }
#if 0
    ostream& operator<<(ostream& os, signal_mask const& sm) {
        std::transform(begin(sm), end(sm), mk_streamiter(os, ", ", '{', '}'), [](int sig) {
                errno = 0;
                char const*  signam{ ::strsignal(sig) };
                if( errno==0 && signam )
                    return string(signam);
                else
                    return string("Unknown: ")+repr(sig);
            });
        return os;
    }
#endif
    // Transform the #defines from <signal.h> into a proper type. Types are
    // good. Let's have more of those.
    enum class MaskOp : int { setMask = SIG_SETMASK, addMask = SIG_BLOCK, delMask = SIG_UNBLOCK, getMask = 0 };
    ostream& operator<<(ostream& os, MaskOp const& mo ) {
        const int val = static_cast<int>(mo);
#define KEES(a) case a: os << #a; break
        switch( val ) {
            KEES(SIG_SETMASK);
            KEES(SIG_BLOCK);
            KEES(SIG_UNBLOCK);
            case 0:  os << "getMask"; break;
            default: os << "INVALID"; break;
        }
#undef KEES
        return os;
    }

    // threads + signals can work together, as long as you're keeping a
    // tight control over signal masks per thread
    //
    // This thread-aware scoped signal mask will help with that
    template <int(*InitMask)(sigset_t*), int(*AddSignal)(sigset_t*, int), MaskOp How>
    struct scoped_signal_mask {
        // We want our friendly HRF generator to be able to access our private parts
        friend std::ostream& operator<<(std::ostream& os, scoped_signal_mask<InitMask, AddSignal, How> const& ssm) {
            // Filter all the signals that are set in ssm's current signal mask
            // and output them to a stream in a nice format
            auto           allSigs  = mk_countrange(1, 31);
            std::set<int>  actualSigs;

            // filter 
            std::copy_if(std::begin(allSigs), std::end(allSigs), std::inserter(actualSigs, actualSigs.end()),
                         [&](int s) { return sigismember(&ssm.__m_cur_sigmask, s); });

            // transform the signal number to readable strings
            std::transform(std::begin(actualSigs), std::end(actualSigs), mk_streamiter(os, ", ", '{', '}'), [](int sig) {
                    errno = 0;
                    char const*  signam{ ::strsignal(sig) };
                    if( errno==0 && signam )
                        return std::string(signam);
                    else
                        return std::string("Unknown: ")+repr(sig);
                });
            return os;
        }

        // Default c'tor only visible when all or no signals are to be blocked
        template <MaskOp A=How>
        scoped_signal_mask(typename std::enable_if<A==MaskOp::setMask && AddSignal==nullptr, int>::type = 0) {
            InitMask(&__m_cur_sigmask);
            ::pthread_sigmask(static_cast<int>(How), &__m_cur_sigmask, &__m_old_sigmask);
            //cout << "Installed signalmask HOW=" << How << ", sm=" << hex << __m_cur_sigmask << dec << endl;
        }

        template <MaskOp A=How>
        scoped_signal_mask(typename std::enable_if<A==MaskOp::getMask && InitMask==nullptr && AddSignal==nullptr, int>::type = 0) {
            ::pthread_sigmask(static_cast<int>(How), nullptr, &__m_cur_sigmask);
            //cout << "Getting signalmask sm=" << hex << __m_cur_sigmask << dec << endl;
        }

        // for some reason we cannot forward std::initializer_list?
        scoped_signal_mask(std::initializer_list<int> il):
            scoped_signal_mask(std::begin(il), std::end(il)) // delegate to c'tor from iterators
        {}
        // constructor from STL like container (std::set<> does not allow this directly
        //     we just forward to c'tor from two iterators)
        template <typename Container,
                  typename std::enable_if<AddSignal!=nullptr && detail::is_container<Container>::value, int>::type = 0>
        scoped_signal_mask(Container t):
            scoped_signal_mask(std::begin(t), std::end(t)) // delegate to c'tor from iterators
        {}

        // This c'tor is only visible when constructing from two iterators
        // and also the addmask is not a nullptr
        template <typename Iterator, int(*B)(sigset_t*, int)=AddSignal>
        scoped_signal_mask(Iterator first, Iterator last, typename std::enable_if<B!=nullptr, int>::type* = 0)
        {
            // transform the standard functions from <signal.h> into fn's
            // that we can use in a functional form :-)
            auto init_f       = [](sigset_t* ssp)        { InitMask(ssp); return ssp;};
            auto accumulate_f = [](sigset_t* ssp, int s) { AddSignal(ssp, s); return ssp;};

            // Build the signal mask 
            std::accumulate(first, last, init_f(&__m_cur_sigmask), accumulate_f);

            // and install it
            if( int r = ::pthread_sigmask(static_cast<int>(How), &__m_cur_sigmask, &__m_old_sigmask) )
                throw std::runtime_error(string("Failed to install signalmask - ")+::strerror(r));
            //cout << "Installed signalmask HOW=" << How << ", sm=" << hex << __m_cur_sigmask << dec << endl;
        }
        ~scoped_signal_mask() {
            if( InitMask!=nullptr ) {
                //cout << "Uninstalling signalmask HOW=" << How << ", sm=" << hex << __m_cur_sigmask << dec << endl;
                ::pthread_sigmask(SIG_SETMASK, &__m_old_sigmask, nullptr);
            }
        }


        private:
            sigset_t    __m_old_sigmask;
            sigset_t    __m_cur_sigmask;
    };

    // Using the above template we can easily create types that do as they say
    // constructing one of those will actually do what it sais on the tin;
    // the d'tor will undo its action - i.e. put back the mask that was in
    // effect at the time the object was constructed
    using AddMask    = scoped_signal_mask<sigemptyset, sigaddset, MaskOp::addMask>;
    using DelMask    = scoped_signal_mask<sigemptyset, sigaddset, MaskOp::delMask>;
    using GetMask    = scoped_signal_mask<nullptr    , nullptr,   MaskOp::getMask>;
    using Block      = scoped_signal_mask<sigemptyset, sigaddset, MaskOp::setMask>;
    using UnBlock    = scoped_signal_mask<sigemptyset, sigaddset, MaskOp::setMask>;
    using BlockAll   = scoped_signal_mask<sigfillset , nullptr ,  MaskOp::setMask>;
    using UnBlockAll = scoped_signal_mask<sigemptyset, nullptr ,  MaskOp::setMask>;
}



namespace etdc {
    // Wrapper for std::thread(...) that guarantees the thread is being run
    // w/ all signal blocked
    template <typename... Args>
    std::thread thread(Args&&... args) {
        etdc::BlockAll     block_all{};
        return std::thread(std::forward<Args>(args)...);
    }
};

template <typename Function, typename... Args>
auto exec_with_no_signals(Function&& f, Args&&... args) -> decltype(f(std::forward<Args>(args)...)) {
    etdc::BlockAll     block_all{};
    return f(std::forward<Args>(args)...);
}

int dummy(int a, int b)
{
    cout << a << '+' << b << '=' << (a + b) << endl;
    return a + b;
}
#endif
int foo(int a, char b) {
    cout << "foo(" << a << ", " << b << ")" << endl;
    sigset_t      oldSigSet;
    etdc::GetMask gm{};
    cout << etdc::saveMask() << etdc::showMaskInHRF << "   getmask: " << gm << endl;
#if 0
    ::pthread_sigmask(0, nullptr, &oldSigSet);
    cout << "    sigmask = " << hex << oldSigSet << dec << endl;
#endif
    etdc::DelMask   ub{SIGUSR1};
    ::pthread_sigmask(0, nullptr, &oldSigSet);
    cout << "    after unblock: sigmask = " << hex << oldSigSet << dec << endl;
    return 1+a;
}

#if 0
int main( void ) {
    auto  cr0 = etdc::mk_countrange(0, 10);
    for( auto const&& v: cr0 )
        cout << "v = " << v << endl;
    return 0;
}
#endif

#if 0
int main( void ) {
    etdc::Counter<int>  c0;
    etdc::Counter<float> f0{-3.1, 0.14};

    for(unsigned int i=0; i<10; i++)
        cout << "c0 = " << c0() << ", f0=" << f0() << endl;
    return 0;
}
#endif
#if 1
int main(void) {
    std::list<int>  li{SIGSEGV, SIGUSR1, SIGKILL}; 

    etdc::GetMask         gm{};
    cout << "GetMask: " << gm << endl;
    etdc::AddMask         sm3{li};
    cout << "AddMask: " << sm3 << endl;
    foo(-1, 'b');
    std::thread  t = etdc::thread(foo, 42, 'a');
    t.join(); 
    foo(-2, 'c');
    return 0;
}
#endif
#if 0
int main( void ) {
    cout << exec_with_no_signals(foo, 42, 'a') << endl
         << wrapper(foo, -1) << endl;

    return 0;
}
#endif
