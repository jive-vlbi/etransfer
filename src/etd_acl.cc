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
#include <etd_acl.h>
#include <etdc_assert.h>

#include <fstream>
#include <fnmatch.h>

namespace etdc {

    //ACL::ACL( std::string const& s):
    //    _m_root = fkyaml::node::
    //{}

    bool ACL::has_recursive_suffix(std::string const& pattern) {
        return pattern.size() >= 2 && pattern.compare(pattern.size() - 2, 2, "**") == 0;
    }

    void ACL::validate_pattern(std::string const& pattern, std::string const& context) {
        const std::size_t pos = pattern.find("**");
        if (pos != std::string::npos) {
            const std::size_t last_pos = pattern.rfind("**");
            ETDCASSERT(pos == last_pos, "Invalid pattern '" << pattern << "' in " << context
                       << ": multiple '**' sequences are not supported");
            ETDCASSERT(pos == pattern.size() - 2, "Invalid pattern '" << pattern << "' in " << context
                       << ": '**' must terminate the pattern");
            if (pos > 0) {
                ETDCASSERT(pattern[pos - 1] == '/', "Invalid pattern '" << pattern << "' in " << context
                           << ": '**' must be preceded by '/' when a prefix is present");
            }
        }
    }

    void ACL::populate_rule(rule& target_rule) {
        target_rule.has_prefix = has_recursive_suffix(target_rule.pattern);
        if (target_rule.has_prefix) {
            const std::string prefix_glob = target_rule.pattern.substr(0, target_rule.pattern.size() - 2);

            std::string regex_pattern("^");
            regex_pattern.reserve(prefix_glob.size() * 2 + 8);

            for(char ch : prefix_glob) {
                if( ch == '*' ) {
                    regex_pattern.append("[^/]*");
                    continue;
                }
                if( ch == '?' ) {
                    regex_pattern.append("[^/]");
                    continue;
                }

                switch( ch ) {
                    case '.': case '^': case '$': case '+':
                    case '{': case '}': case '(': case ')':
                    case '[': case ']': case '|': case '\\':
                        regex_pattern.push_back('\\');
                        break;
                    default:
                        break;
                }
                regex_pattern.push_back(ch);
            }

            if( prefix_glob.empty() ) {
                regex_pattern.append(".*");
            } else {
                if( !regex_pattern.empty() && regex_pattern.back() == '/' ) {
                    regex_pattern.pop_back();
                }
                regex_pattern.append("(?:/.*)?");
            }

            target_rule.prefix_regex = std::regex(regex_pattern);
        } else {
            target_rule.prefix_regex = std::regex();
        }
    }

    // Our ACL requires a few things.
    // Permissions tested in order, first to match wins.
    // <path> may contain "*" and "?" globbing or may end with "**" for recursive matching
    //
    // read:
    //  default:
    //    {deny|allow}: <path>
    //
    //  [allow:
    //  - <path>
    //  - <path>
    //  ...]
    //  [deny:
    //  - <path>
    //  - <path>
    //  ...]
    //
    // write:
    //  default:
    //    {deny|allow}: <path>
    //
    //  [allow:
    //  - ...]
    //  [deny:
    //  - ...]
    //
    void ACL::initFromYAML( void ) {
        ETDCASSERT( _m_root.is_mapping(), "Document root is not a mapping" );

        // We must have a read- and write section, that both are mappings
        ETDCASSERT( _m_root.contains("read") && _m_root.contains("write") &&
                    _m_root["read"].is_mapping() && _m_root["write"].is_mapping(),
                    "The ACL YAML does not have have 'read' and 'write' mappings" );

        // Each node needs:
        // default:
        //    {allow|deny}: <path>
        //
        // And may have:
        // allow:
        // - <path>
        // - ...
        // deny:
        // - <path>
        // - ...

        auto assert_sequence_of_strings = [](fkyaml::node const& n, std::string const& nm) {
            ETDCASSERT(n.is_sequence(), "Node '" << nm << " is not a sequence");
            for(auto& entry : n) {
                ETDCASSERT(entry.is_string(), "Node '" << nm << " contains non-string entries");
            }
        };

        auto node_ok = [&](fkyaml::node const& n, std::string const& nm, section_type which) {
            ETDCASSERT(n.contains("default") && n["default"].is_mapping(),
                       "Node '" << nm << "': no 'default:' mapping entry");
            fkyaml::node m_def = n["default"];

            // Must have a string-valued 'allow:' or 'deny:' but not neither or both
            const bool allow = m_def.contains("allow");
            const bool deny  = m_def.contains("deny");

            ETDCASSERT(allow != deny, "Node '" << nm << "': needs exactly one of 'allow:' OR 'deny:'");

            // And the one that exists must be a string
            ETDCASSERT((allow && m_def["allow"].is_string()) || (deny && m_def["deny"].is_string()),
                       "Node '" << nm << "': key '" << (allow? "allow" : "deny") << "' is not a string (path)");

            section& target = _m_sections[static_cast<int>(which)];
            const std::string default_pattern = allow ? m_def["allow"].as_str() : m_def["deny"].as_str();
            validate_pattern(default_pattern, nm + " default rule");
            target.default_rule.allow = allow;
            target.default_rule.pattern = default_pattern;
            populate_rule(target.default_rule);
            target.allow_rules.clear();
            target.deny_rules.clear();

            // *IF* they have 'allow:' and/or 'deny:' they need to be
            // sequences-of-strings
            if( n.contains("allow") )
                assert_sequence_of_strings(n["allow"], nm+" allow:");
            if( n.contains("deny") )
                assert_sequence_of_strings(n["deny"], nm+" deny:");

            if( n.contains("allow") ) {
                for(auto const& entry : n["allow"]) {
                    const std::string pattern = entry.as_str();
                    validate_pattern(pattern, nm + " allow rule");
                    rule allow_rule;
                    allow_rule.allow   = true;
                    allow_rule.pattern = pattern;
                    populate_rule(allow_rule);
                    target.allow_rules.push_back(allow_rule);
                }
            }
            if( n.contains("deny") ) {
                for(auto const& entry : n["deny"]) {
                    const std::string pattern = entry.as_str();
                    validate_pattern(pattern, nm + " deny rule");
                    rule deny_rule;
                    deny_rule.allow   = false;
                    deny_rule.pattern = pattern;
                    populate_rule(deny_rule);
                    target.deny_rules.push_back(deny_rule);
                }
            }
        };

        node_ok( _m_root["read"],  "read",  section_type::Read );
        node_ok( _m_root["write"], "write", section_type::Write );
    }

    ACL ACL::readFromFile( std::string const& fn ) {
        // this shortcut don't compile because it doesn't
        // see the argument as "std::istream&"?
        //return ACL( std::ifstream(fn) );
        std::ifstream ifs(fn);
        return ACL( ifs );
    }

    bool ACL::allowRead( std::string const& path ) const {
        return check(section_type::Read, path);
    }

    bool ACL::allowWrite( std::string const& path ) const {
        return check(section_type::Write, path);
    }

    bool ACL::check( section_type which, std::string const& path ) const {
        auto const& lcl_section = _m_sections[static_cast<int>(which)];
        const auto matches = [&](rule const& r) {
            if( r.has_prefix ) {
                if( path.empty() ) {
                    return false;
                }
                std::smatch match;
                if( !std::regex_match(path, match, r.prefix_regex) ) {
                    return false;
                }
                return match.size() > 0;
            }
            return ::fnmatch(r.pattern.c_str(), path.c_str(), FNM_PATHNAME)==0;
        };

        auto evaluate_rules = [&](std::vector<rule> const& rules, bool& decision) -> bool {
            for(auto const& r : rules) {
                if( matches(r) ) {
                    decision = r.allow;
                    return true;
                }
            }
            return false;
        };

        const bool default_is_allow = lcl_section.default_rule.allow;

        if( default_is_allow ) {
            bool decision;
            if( evaluate_rules(lcl_section.deny_rules, decision) )
                return decision;
            if( evaluate_rules(lcl_section.allow_rules, decision) )
                return decision;
        } else {
            bool decision;
            if( evaluate_rules(lcl_section.allow_rules, decision) )
                return decision;
            if( evaluate_rules(lcl_section.deny_rules, decision) )
                return decision;
        }

        if( matches(lcl_section.default_rule) )
            return lcl_section.default_rule.allow;

        return false;
    }
}
