// Sample test program
// Copyright (C) 2017  Harro Verkouter, verkouter@jive.eu
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
#include <argparse.h>
#include <list>
#include <iterator>
#include <algorithm>
#include <numeric>
#include <iostream>

// shorthands
namespace ap = argparse;

// Turn std::max into a templated functor
template <typename T>
struct max_f: public std::binary_function<T, T, T> {
    T operator()(T const& l, T const& r) const {
        return std::max(l, r);
    }
};

// Make it easier to construct an accumulation function
template <typename T, template <typename X, typename...> class Accu, typename... Rest, typename... Args>
std::function<T(T const&, T const&)> mk_accumulator(Args&&... args) {
    return std::function<T(T const&, T const&)>(Accu<T, Rest...>(std::forward<Args>(args)...));
}

int main(int argc, char*const*const argv) {
    auto           cmd    = ap::ArgumentParser( ap::docstring("Process some integers.") );
    auto           accufn = mk_accumulator<int, max_f>();
    std::list<int> ints;    // the integers collected from the command line

    // We have '-h|--help', '--sum' and the arguments, integers
    // The argparse11 library does not automatically add "--help" - it's at
    // your discretion wether to add it and under which flag(s).
    cmd.add( ap::long_name("help"), ap::short_name('h'), ap::print_help() );

    // If '--sum' is provided, use that, otherwise find the max.
    cmd.add(ap::docstring("Sum the integers (default: find the max)"),
            ap::store_const_into(mk_accumulator<int, std::plus>(), accufn),
            ap::long_name("sum"));

    // Let the command line parser collect the converted command line
    // options into our STL container. By not giving this 'option' a name, the
    // system takes that to mean "oh, these must be the command line arguments then"
    // Note: we require at least one entry on the command line
    // Note: the argparse11 library will automatically infer from the
    //       container's type that it collects ints - we don't have to tell
    //       it that! (So it will autmatically convert command line
    //       arguments to int and complain loudly if it fails at that)
    cmd.add( ap::collect_into(ints), ap::at_least(1),
             ap::docstring("an integer for the accumulator") );

    // Parse the command line
    cmd.parse(argc, argv);

    // And do the accumulation. Python "sum([...])" and "max([...])" are
    // obviously smarter then C++11's std algorithms but still quite cool it
    // can be done.
    // We can safely use "++begin()" because we /required/ "at_least(1)" ;-)
    std::cout
        << std::accumulate(++std::begin(ints), std::end(ints), *std::begin(ints), accufn)
        << std::endl;
    return 0;
}

