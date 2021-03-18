// utility classes/functions for dealing with signals
// Copyright (C) 2007-2016 Harro Verkouter
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// any later version.
// 
// This program is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
// PARTICULAR PURPOSE.  See the GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
// 
// Author:  Harro Verkouter - verkouter@jive.eu
//          Joint Institute for VLBI in Europe
//          P.O. Box 2
//          7990 AA Dwingeloo
#ifndef ETDC_SIGNAL_H
#define ETDC_SIGNAL_H

// own includes
#include <utilities.h>
#include <reentrant.h>
#include <etdc_assert.h>
#include <etdc_streamutil.h>
#include <etdc_thread_local.h>

// std c++
#include <set>
#include <stack>
#include <string>
#include <numeric>
#include <iomanip>
#include <iterator>
#include <iostream>
#include <algorithm>
#include <exception>
#include <type_traits>

// good old C
#include <signal.h>
#include <pthread.h>

namespace etdc {
    constexpr int getMaskValue = 0xDEADBEEF; // Let's hope that SIG_SETMASK, SIG_BLOCK, SIG_UNBLOCK are never this
    // Transform the #defines from <signal.h> into a proper type. Types are
    // good. Let's have more of those. With the new 'enum class' we can't
    // mistake e.g. MaskOp::setMask with plain old integers anymore!!!!!! Yay!
    enum class MaskOp : int { setMask = SIG_SETMASK, addMask = SIG_BLOCK, delMask = SIG_UNBLOCK, getMask = getMaskValue };

    template <typename... Traits>
    std::basic_ostream<Traits...>& operator<<(std::basic_ostream<Traits...>& os, MaskOp const& mo ) {
        const int val = static_cast<int>(mo);
#define KEES(a) case a: os << #a; break
        switch( val ) {
            KEES(SIG_SETMASK);
            KEES(SIG_BLOCK);
            KEES(SIG_UNBLOCK);
            case getMaskValue:  os << "getMask"; break;
            default: os << "INVALID"; break;
        }
#undef KEES
        return os;
    }

    // Control how scoped signal masks are displayed: in Human Readable Form or in hex
    enum class MaskDisplayFormat : int { showMaskInHRF = 42, showMaskInHex = 666, defaultMaskFormat = showMaskInHex, noChange = 0 };

    // How to display the sigmask is kept per thread ...
    //thread_local MaskDisplayFormat curMaskDisplay{ MaskDisplayFormat::defaultMaskFormat };
    using maskdisplaystack_type = std::stack<MaskDisplayFormat>;
    static etdc::tls_object_type<MaskDisplayFormat>     curMaskDisplay{ MaskDisplayFormat::defaultMaskFormat };
    static etdc::tls_object_type<maskdisplaystack_type> maskDisplayStack;

    template <typename... Traits>
    std::basic_ostream<Traits...>& showMaskInHRF(std::basic_ostream<Traits...>& os ) {
        curMaskDisplay = MaskDisplayFormat::showMaskInHRF;
        return os;
    } 
    template <typename... Traits>
    std::basic_ostream<Traits...>& showMaskInHex(std::basic_ostream<Traits...>& os ) {
        curMaskDisplay = MaskDisplayFormat::showMaskInHex;
        return os;
    } 
    template <typename... Traits>
    std::basic_ostream<Traits...>& pushMaskDisplayFormat(std::basic_ostream<Traits...>& os ) {
        maskDisplayStack->push( curMaskDisplay );
        return os;
    } 
    template <typename... Traits>
    std::basic_ostream<Traits...>& popMaskDisplayFormat(std::basic_ostream<Traits...>& os ) {
        if( maskDisplayStack->empty() )
            throw std::logic_error("Attempt to pop maskDisplayFormat from empty stack!");
        curMaskDisplay = maskDisplayStack->top();
        maskDisplayStack->pop();
        return os;
    } 

    //////////////////////////////////////////////////////////////////////////////////////////////////    
    // 
    //          threads + signals can work together, as long as you're keeping a
    //          tight control over signal masks per thread
    //
    //          This thread-aware scoped signal mask will help with that
    //
    //////////////////////////////////////////////////////////////////////////////////////////////////

    template <int(*InitMask)(sigset_t*), int(*AddSignal)(sigset_t*, int), MaskOp How>
    struct scoped_signal_mask {
        // We want our friendly HRF generator to be able to access our private parts
        friend std::ostream& operator<<(std::ostream& os, scoped_signal_mask<InitMask, AddSignal, How> const& ssm) {
            // Filter all the signals that are set in ssm's current signal mask
            // and output them to a stream in a nice format or accumulate
            // them into an integer for hex display
            auto           allSigs  = mk_sequence(1, 31);
            std::set<int>  actualSigs;

            // If 'hex' display requested, accumulate into an integer
            if( curMaskDisplay==MaskDisplayFormat::showMaskInHex ) {
                const int  n = std::accumulate(std::begin(allSigs), std::end(allSigs), 0,
                                               [&](int acc, int s) { return acc | (sigismember(&ssm.__m_cur_sigmask, s) << s);});
                return os << "0x" << std::hex << std::setw(8) << std::setfill('0') << n << std::dec;
            }

            // otherwise first filter 
            std::copy_if(std::begin(allSigs), std::end(allSigs), std::inserter(actualSigs, actualSigs.end()),
                         [&](int s) { return sigismember(&ssm.__m_cur_sigmask, s); });

            // then transform the signal number to readable strings
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
        }

        template <MaskOp A=How>
        scoped_signal_mask(typename std::enable_if<A==MaskOp::getMask && InitMask==nullptr && AddSignal==nullptr, int>::type = 0) {
            // According to POSIX [http://pubs.opengroup.org/onlinepubs/9699919799/]
            // "If set is a null pointer, the value of the argument how is
            // not significant and the thread's signal mask shall be
            // unchanged; thus the call can be used to enquire about
            // currently blocked signals."
            ::pthread_sigmask(static_cast<int>(How), nullptr, &__m_cur_sigmask);
        }

        // for some reason we cannot forward std::initializer_list?
        scoped_signal_mask(std::initializer_list<int> il):
            scoped_signal_mask(std::begin(il), std::end(il)) // delegate to c'tor from iterators
        {}
        // constructor from STL like container (std::set<> does not allow this directly
        //     we just forward to c'tor from two iterators)
        template <typename Container,
                  typename std::enable_if<AddSignal!=nullptr && is_container<Container>::value, int>::type = 0>
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
                throw std::runtime_error(std::string("Failed to install signalmask - ")+etdc::strerror(r));
        }
        ~scoped_signal_mask() throw (std::runtime_error) {
            // Restore old signal mask on destruction of instances that modified the signal mask
            if( How!=MaskOp::getMask ) {
                if( int r = ::pthread_sigmask(static_cast<int>(MaskOp::setMask), &__m_old_sigmask, nullptr) )
                    throw std::runtime_error(std::string("Failed to restore signalmask - ")+etdc::strerror(r));
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
    using UnBlock    = scoped_signal_mask<sigfillset,  sigdelset, MaskOp::setMask>;
    using BlockAll   = scoped_signal_mask<sigfillset , nullptr ,  MaskOp::setMask>;
    using UnBlockAll = scoped_signal_mask<sigemptyset, nullptr ,  MaskOp::setMask>;


    // Install signalhandler for the indicated signal(s) in the current thread
    template <typename... Signals>
    void install_handler(void(*handler_fn)(int), Signals&&... sigs) {
        struct sigaction    sa;
        const std::set<int> signalset( std::forward<Signals>(sigs)... );

        // Fill in the sigaction struct
        sa.sa_flags   = 0;
        sa.sa_handler = handler_fn;
        // The mask
        sigfillset(&sa.sa_mask);
        for(auto const& sig: signalset)
            sigdelset(&sa.sa_mask, sig);
        // Install the handler for each signal
        for(auto const& sig: signalset)
            ETDCASSERT(::sigaction(sig, &sa, 0)==0, "failed to install signal handler for signal#" << sig << " - " << etdc::strerror(errno));
    }
    template <typename T>
    void install_handler(void(*handler_fn)(int), std::initializer_list<T> t) {
        install_handler(handler_fn, std::begin(t), std::end(t));
    }
}

// As a convenience - define output operator for "sigset_t" but only if sigset_t
// is not integral type [on many POSIXen sigset_t is just a typedef for unsigned long or so]
// but on (some versions of?) Loonix, it is most definitely not.
// Not an issue at all; sigset_t is supposed to be an opaque type, but in case your
// O/S typedef'ed it as "unsigned long" then this overload below would clash very hard
// with the built-in "cout << (unsigned long) << ..." overload ...
// But with the help of std::enable_if<> we can only introduce this overload in case it's necessary.
template <class CharT, class Traits>
typename std::enable_if<!std::is_integral<sigset_t>::value, std::basic_ostream<CharT, Traits>&>::type
operator<<(std::basic_ostream<CharT, Traits>& os, sigset_t const& ss) {
    // Filter out the first 32 (POSIX?) signals. 
    auto allSigs    = etdc::mk_sequence(1, 31);
    
    return os << std::accumulate(std::begin(allSigs), std::end(allSigs), uint32_t(0),
                                 [&](uint32_t& acc, int s) { return acc |= (sigismember(&ss, s) << s); });
}
#if 0
    // This implementation could be used to display ALL possible signals on Loonix
    // (I think they can go up to 1024 signals)
    using word_type  = uint32_t;
    //using words_type = std::vector<word_type>;
    using words_type = std::map<size_t,word_type>;
    //constexpr unsigned int nWord     = sizeof(sigset_t)/sizeof(word_type);
    constexpr unsigned int nSigPWord = 8 * sizeof(word_type);
    constexpr unsigned int nSig      = 8 * sizeof(sigset_t);

    // Limit ourselves to the first 32 (POSIX?) signals
    auto       allSigs  = etdc::mk_sequence(1, 31);
    //words_type actualSigs( nWord, word_type(0) );

    auto actualSigs = std::accumulate(std::begin(allSigs), std::end(allSigs), words_type(),
                    [&](words_type& acc, int s) { acc[s/nSigPWord] |= (sigismember(&ss, s) << (s % nSigPWord)); return acc; });

    // Now output the words in reverse order (i.e. MSB to the right, LSB to the left)
    // Depending on flags in os ...
    const auto flags = os.flags();

    // Clear showbase [will restore them later]
    os.unsetf( std::ios_base::showbase );

    // Deal with showbase
    if( flags & std::ios_base::showbase )
        os << ((flags & std::ios_base::oct) ? "0" : ((flags & std::ios_base::hex) ? "0x" : ""));

    for(auto w: etdc::reversed(actualSigs))
        os << w.second << " ";
        //os << w << " ";
    os.flags( flags );
    return os;
#endif

#endif // ETDC_SIGNAL_H
