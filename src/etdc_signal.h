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
#include <streamutil.h>
#include <reentrant.h>
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
    std::ostream& operator<<(std::ostream& os, MaskOp const& mo ) {
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
    etdc::thrd_local<MaskDisplayFormat>     curMaskDisplay{ MaskDisplayFormat::defaultMaskFormat };
    etdc::thrd_local<maskdisplaystack_type> maskDisplayStack;

    template <class CharT, class Traits>
    std::basic_ostream<CharT, Traits>& showMaskInHRF(std::basic_ostream<CharT, Traits>& os ) {
        curMaskDisplay = MaskDisplayFormat::showMaskInHRF;
        return os;
    } 
    template <class CharT, class Traits>
    std::basic_ostream<CharT, Traits>& showMaskInHex(std::basic_ostream<CharT, Traits>& os ) {
        curMaskDisplay = MaskDisplayFormat::showMaskInHex;
        return os;
    } 
    template <class CharT, class Traits>
    std::basic_ostream<CharT, Traits>& pushMaskDisplayFormat(std::basic_ostream<CharT, Traits>& os ) {
        maskdisplaystack_type&  lclMaskDisplayStack( maskDisplayStack.get() );
        lclMaskDisplayStack.push( curMaskDisplay );
        return os;
    } 
    template <class CharT, class Traits>
    std::basic_ostream<CharT, Traits>& popMaskDisplayFormat(std::basic_ostream<CharT, Traits>& os ) {
        maskdisplaystack_type&  lclMaskDisplayStack( maskDisplayStack.get() );
        if( lclMaskDisplayStack.empty() )
            throw std::logic_error("Attempt to pop maskDisplayFormat from empty stack!");
        curMaskDisplay = lclMaskDisplayStack.top();
        lclMaskDisplayStack.pop();
        return os;
    } 
#if 0
    struct saveMask_impl {
        public:
            saveMask_impl(): __m_previous_fmt( curMaskDisplay ) {}
            // in the move constructor, disable the putting back of the old
            // value in the object we are move-constructed from
            saveMask_impl(saveMask_impl&& other): __m_previous_fmt( other.__m_previous_fmt ) {
                other.__m_previous_fmt = MaskDisplayFormat::noChange;
            }

            ~saveMask_impl()  {
                if( __m_previous_fmt!=MaskDisplayFormat::noChange )
                    curMaskDisplay = __m_previous_fmt;
            }
            
        private:
            MaskDisplayFormat   __m_previous_fmt;
    };
    std::ostream& operator<<(std::ostream& os, saveMask_impl const&) { return os; }

    saveMask_impl saveMask( void ) { return saveMask_impl(); }
#endif    
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
        ~scoped_signal_mask() {
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
    using UnBlock    = scoped_signal_mask<sigemptyset, sigaddset, MaskOp::setMask>;
    using BlockAll   = scoped_signal_mask<sigfillset , nullptr ,  MaskOp::setMask>;
    using UnBlockAll = scoped_signal_mask<sigemptyset, nullptr ,  MaskOp::setMask>;
}


#endif // ETDC_SIGNAL_H
