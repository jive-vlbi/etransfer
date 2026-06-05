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
#include <vector>
#include <regex>


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

            bool allowRead( std::string const& path ) const;
            bool allowWrite( std::string const& path ) const;

        private:
            enum class section_type { Read = 0, Write = 1 };

            class rule {
                public:
                    rule() = default;
                    rule( bool allow, std::string pattern );

                    bool               allow()        const noexcept { return _m_allow;        }
                    std::string const& pattern()      const noexcept { return _m_pattern;      }
                    bool               has_prefix()   const noexcept { return _m_has_prefix;   }
                    std::regex  const& prefix_regex() const noexcept { return _m_prefix_regex; }

                private:
                    bool        _m_allow{false};
                    std::string _m_pattern{};
                    bool        _m_has_prefix{false};
                    std::regex  _m_prefix_regex{};
            };

            class section {
                public:
                    section() = default;
                    section( rule default_rule, std::vector<rule> allow_rules, std::vector<rule> deny_rules );

                    rule              const& default_rule() const noexcept { return _m_default_rule; }
                    std::vector<rule> const& allow_rules()  const noexcept { return _m_allow_rules;  }
                    std::vector<rule> const& deny_rules()   const noexcept { return _m_deny_rules;   }

                private:
                    rule              _m_default_rule{};
                    std::vector<rule> _m_allow_rules{};
                    std::vector<rule> _m_deny_rules{};
            };

            fkyaml::node    _m_root;
            section         _m_sections[2];

            // Will throw on wonky yaml
            void               initFromYAML( void );
            bool               check( section_type which, std::string const& path ) const;
            static bool        has_recursive_suffix(std::string const& pattern);
            static void        validate_pattern(std::string const& pattern, std::string const& context);
            static std::regex  compile_prefix_regex(std::string const& pattern);
    };

    using ACLptr = std::shared_ptr<etdc::ACL>;
}

#endif
