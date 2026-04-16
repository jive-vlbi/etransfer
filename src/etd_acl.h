// Implement an ACL mechanism for the daemon
// Copyright (C) 2007-2026 Marjolein Verkouter
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
// Author:  Marjolein Verkouter - verkouter@jive.eu
//          Joint Institute for VLBI in Europe
//          P.O. Box 2
//          7990 AA Dwingeloo
#ifndef ETD_ACL_H
#define ETD_ACL_H

#include <fkyaml.hpp>   // for fkYAML - https://fktn-k.github.io/fkYAML/
#include <string>
#include <memory>


namespace etdc {

    class ACL {
        public:
            // Initialise from the YAML string (not filename!)
            // (see static method to read from file)
            //ACL( std::string const& );
            // Can pass string but also std::ifstream
            template <typename Input>
            ACL( Input&& ip ):
                _m_root( fkyaml::node::deserialize(std::forward<Input>(ip)) )
            {
                initFromYAML();
            }

            // We are definitely copyable / trivial PoD
            ACL( const ACL& )  = default;
            ACL( ACL&& )       = default;
            ~ACL()             = default;


            // Static function to read from a file
            static ACL  readFromFile( std::string const& fn );

        private:
            fkyaml::node    _m_root;

            // Will throw on wonky yaml
            void            initFromYAML( void );
    };

    using ACLptr = std::shared_ptr<etdc::ACL>;
}

#endif
