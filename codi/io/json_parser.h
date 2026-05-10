#ifndef JSON_PARSER_H
#define JSON_PARSER_H

// Parser JSON minimalista, header-only, suficiente para los formatos
// producidos por pipeline/contratos.py (json.dumps con indent=2).
//
// Header-only para que pueda incluirse desde múltiples TU (cargar_json.cc,
// distribucio_main.cc, etc.) sin choque del linker.

#include <cctype>
#include <stdexcept>
#include <string>

namespace jsonp {

class Parser {
public:
    explicit Parser(const std::string& src) : s_(src), i_(0) {}

    void skip_ws() {
        while (i_ < s_.size()) {
            char c = s_[i_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++i_;
            else break;
        }
    }

    char peek() {
        skip_ws();
        if (i_ >= s_.size()) error("EOF");
        return s_[i_];
    }

    char next() {
        skip_ws();
        if (i_ >= s_.size()) error("EOF");
        return s_[i_++];
    }

    void expect(char c) {
        char got = next();
        if (got != c) {
            error(std::string("expected '") + c + "' got '" + got + "'");
        }
    }

    bool consume(char c) {
        skip_ws();
        if (i_ < s_.size() && s_[i_] == c) { ++i_; return true; }
        return false;
    }

    std::string parse_string() {
        expect('"');
        std::string out;
        while (i_ < s_.size()) {
            char c = s_[i_++];
            if (c == '"') return out;
            if (c == '\\') {
                if (i_ >= s_.size()) error("EOF in string");
                char esc = s_[i_++];
                switch (esc) {
                    case '"':  out.push_back('"');  break;
                    case '\\': out.push_back('\\'); break;
                    case '/':  out.push_back('/');  break;
                    case 'n':  out.push_back('\n'); break;
                    case 't':  out.push_back('\t'); break;
                    case 'r':  out.push_back('\r'); break;
                    case 'b':  out.push_back('\b'); break;
                    case 'f':  out.push_back('\f'); break;
                    case 'u':
                        if (i_ + 4 > s_.size()) error("EOF in \\u");
                        i_ += 4;
                        out.push_back('?');
                        break;
                    default: error(std::string("bad escape \\") + esc);
                }
            } else {
                out.push_back(c);
            }
        }
        error("EOF in string");
        return "";
    }

    double parse_number() {
        skip_ws();
        std::size_t start = i_;
        if (i_ < s_.size() && (s_[i_] == '-' || s_[i_] == '+')) ++i_;
        while (i_ < s_.size() && (std::isdigit(static_cast<unsigned char>(s_[i_])) ||
                                  s_[i_] == '.' || s_[i_] == 'e' || s_[i_] == 'E' ||
                                  s_[i_] == '+' || s_[i_] == '-')) {
            ++i_;
        }
        if (i_ == start) error("expected number");
        return std::stod(s_.substr(start, i_ - start));
    }

    bool parse_bool() {
        skip_ws();
        if (s_.compare(i_, 4, "true") == 0)  { i_ += 4; return true; }
        if (s_.compare(i_, 5, "false") == 0) { i_ += 5; return false; }
        error("expected bool");
        return false;
    }

    bool is_null() {
        skip_ws();
        if (s_.compare(i_, 4, "null") == 0) { i_ += 4; return true; }
        return false;
    }

    void skip_value() {
        skip_ws();
        char c = peek();
        if (c == '{') skip_object();
        else if (c == '[') skip_array();
        else if (c == '"') parse_string();
        else if (c == 't' || c == 'f') parse_bool();
        else if (c == 'n' && is_null()) ;
        else parse_number();
    }

    void skip_object() {
        expect('{');
        skip_ws();
        if (consume('}')) return;
        while (true) {
            parse_string();
            expect(':');
            skip_value();
            if (consume(',')) continue;
            expect('}');
            return;
        }
    }

    void skip_array() {
        expect('[');
        skip_ws();
        if (consume(']')) return;
        while (true) {
            skip_value();
            if (consume(',')) continue;
            expect(']');
            return;
        }
    }

    template <typename F>
    void parse_object_fields(F&& on_key) {
        expect('{');
        skip_ws();
        if (consume('}')) return;
        while (true) {
            std::string key = parse_string();
            expect(':');
            on_key(key);
            if (consume(',')) continue;
            expect('}');
            return;
        }
    }

    template <typename F>
    void parse_array_elems(F&& on_elem) {
        expect('[');
        skip_ws();
        if (consume(']')) return;
        while (true) {
            on_elem();
            if (consume(',')) continue;
            expect(']');
            return;
        }
    }

    [[noreturn]] void error(const std::string& msg) {
        throw std::runtime_error("json_parser: " + msg + " near pos " + std::to_string(i_));
    }

private:
    const std::string& s_;
    std::size_t i_;
};

}  // namespace jsonp

#endif
