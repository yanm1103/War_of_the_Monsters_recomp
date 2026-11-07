// JSON minimo (objeto, array, string, numero inteiro, booleano, null) para os arquivos que o port deixa legiveis: o save
// (wotm_save.inc) e os mods (monster.json, skin.json, levels/<FASE>.json). Guarda a ordem das chaves e as chaves que o codigo nao
// conhece, para que reescrever um arquivo nao apague o que um mod pos nele. Numero com fracao ou expoente: `d` guarda o valor e `i` o
// truncado (o save so tem inteiros e grava `i`).
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace wotm::json
{
    struct Value
    {
        enum Type { Null, Bool, Int, Str, Arr, Obj } type = Null;
        bool b = false;
        int64_t i = 0;
        double d = 0.0;   // o numero inteiro, com fracao e expoente (asNum)
        std::string s;
        std::vector<Value> arr;
        std::vector<std::pair<std::string, Value>> obj;

        Value() = default;
        static Value boolean(bool v) { Value r; r.type = Bool; r.b = v; return r; }
        static Value integer(int64_t v) { Value r; r.type = Int; r.i = v; r.d = double(v); return r; }
        static Value string(std::string v) { Value r; r.type = Str; r.s = std::move(v); return r; }
        static Value array() { Value r; r.type = Arr; return r; }
        static Value object() { Value r; r.type = Obj; return r; }

        const Value *get(const std::string &key) const
        {
            if (type != Obj)
                return nullptr;
            for (const auto &kv : obj)
                if (kv.first == key)
                    return &kv.second;
            return nullptr;
        }
        // Cria a chave no fim se faltar; transforma em objeto o que nao for.
        Value &at(const std::string &key)
        {
            if (type != Obj)
                *this = object();
            for (auto &kv : obj)
                if (kv.first == key)
                    return kv.second;
            obj.emplace_back(key, Value());
            return obj.back().second;
        }
        bool isInt() const { return type == Int || type == Bool; }
        int64_t asInt(int64_t def = 0) const { return type == Int ? i : type == Bool ? (b ? 1 : 0) : def; }
        double asNum(double def = 0.0) const { return type == Int ? d : type == Bool ? (b ? 1.0 : 0.0) : def; }
        bool asBool(bool def = false) const { return type == Bool ? b : type == Int ? i != 0 : def; }
    };

    namespace detail
    {
        struct Parser
        {
            const char *p, *end;
            std::string err;

            void ws()
            {
                while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
                    ++p;
            }
            bool fail(const char *m)
            {
                if (err.empty())
                    err = m;
                return false;
            }
            bool lit(const char *w)
            {
                for (const char *q = w; *q; ++q, ++p)
                    if (p >= end || *p != *q)
                        return fail("palavra invalida");
                return true;
            }
            bool str(std::string &out)
            {
                if (p >= end || *p != '"')
                    return fail("esperava string");
                ++p;
                while (p < end && *p != '"')
                {
                    char c = *p++;
                    if (c != '\\')
                    {
                        out += c;
                        continue;
                    }
                    if (p >= end)
                        return fail("escape no fim");
                    c = *p++;
                    switch (c)
                    {
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'u':
                    {
                        if (end - p < 4)
                            return fail("\\u curto");
                        unsigned cp = 0;
                        for (int k = 0; k < 4; ++k)
                        {
                            const char h = *p++;
                            cp = cp * 16 + (h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10 : h >= 'A' && h <= 'F' ? h - 'A' + 10 : 0);
                        }
                        if (cp < 0x80)
                            out += static_cast<char>(cp);
                        else if (cp < 0x800)
                        {
                            out += static_cast<char>(0xC0 | (cp >> 6));
                            out += static_cast<char>(0x80 | (cp & 0x3F));
                        }
                        else
                        {
                            out += static_cast<char>(0xE0 | (cp >> 12));
                            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                            out += static_cast<char>(0x80 | (cp & 0x3F));
                        }
                        break;
                    }
                    default: out += c; break;   // \" \\ \/
                    }
                }
                if (p >= end)
                    return fail("string sem fim");
                ++p;
                return true;
            }
            bool value(Value &v, int depth)
            {
                if (depth > 64)
                    return fail("aninhamento demais");
                ws();
                if (p >= end)
                    return fail("fim inesperado");
                const char c = *p;
                if (c == '{')
                {
                    ++p;
                    v = Value::object();
                    ws();
                    if (p < end && *p == '}')
                        return ++p, true;
                    for (;;)
                    {
                        ws();
                        std::string k;
                        if (!str(k))
                            return false;
                        ws();
                        if (p >= end || *p != ':')
                            return fail("esperava ':'");
                        ++p;
                        Value child;
                        if (!value(child, depth + 1))
                            return false;
                        v.at(k) = std::move(child);
                        ws();
                        if (p < end && *p == ',')
                        {
                            ++p;
                            continue;
                        }
                        if (p < end && *p == '}')
                            return ++p, true;
                        return fail("esperava ',' ou '}'");
                    }
                }
                if (c == '[')
                {
                    ++p;
                    v = Value::array();
                    ws();
                    if (p < end && *p == ']')
                        return ++p, true;
                    for (;;)
                    {
                        Value child;
                        if (!value(child, depth + 1))
                            return false;
                        v.arr.push_back(std::move(child));
                        ws();
                        if (p < end && *p == ',')
                        {
                            ++p;
                            continue;
                        }
                        if (p < end && *p == ']')
                            return ++p, true;
                        return fail("esperava ',' ou ']'");
                    }
                }
                if (c == '"')
                {
                    v = Value::string("");
                    return str(v.s);
                }
                if (c == 't')
                    return v = Value::boolean(true), lit("true");
                if (c == 'f')
                    return v = Value::boolean(false), lit("false");
                if (c == 'n')
                    return v = Value(), lit("null");
                if (c == '-' || (c >= '0' && c <= '9'))
                {
                    const bool neg = c == '-';
                    if (neg)
                        ++p;
                    int64_t n = 0;
                    double d = 0.0;
                    bool any = false;
                    while (p < end && *p >= '0' && *p <= '9')
                        d = d * 10.0 + (*p - '0'), n = n * 10 + (*p++ - '0'), any = true;
                    if (!any)
                        return fail("numero invalido");
                    if (p < end && *p == '.')
                        for (double f = 0.1; ++p < end && *p >= '0' && *p <= '9'; f *= 0.1)
                            d += (*p - '0') * f;
                    if (p < end && (*p == 'e' || *p == 'E'))
                    {
                        ++p;
                        const bool eneg = p < end && *p == '-';
                        if (p < end && (*p == '-' || *p == '+'))
                            ++p;
                        int e = 0;
                        bool edig = false;
                        while (p < end && *p >= '0' && *p <= '9')
                            e = std::min(e * 10 + (*p++ - '0'), 400), edig = true;
                        if (!edig)
                            return fail("expoente invalido");
                        for (; e > 0; --e)
                            d = eneg ? d / 10.0 : d * 10.0;
                        n = int64_t(std::min(d, 9.0e18));
                    }
                    v = Value::integer(neg ? -n : n);
                    v.d = neg ? -d : d;
                    return true;
                }
                return fail("caractere inesperado");
            }
        };

        inline void quote(std::string &out, const std::string &s)
        {
            out += '"';
            for (unsigned char c : s)
            {
                if (c == '"' || c == '\\')
                    out += '\\', out += static_cast<char>(c);
                else if (c == '\n')
                    out += "\\n";
                else if (c < 0x20)
                {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                }
                else
                    out += static_cast<char>(c);
            }
            out += '"';
        }

        // Array so de escalares fica numa linha (os 4 trajes, os 2 controles); o resto, um item por linha.
        inline bool flat(const Value &v)
        {
            if (v.type != Value::Arr)
                return false;
            for (const auto &e : v.arr)
                if (e.type == Value::Arr || e.type == Value::Obj)
                    return false;
            return true;
        }

        inline void write(std::string &out, const Value &v, int ind)
        {
            const std::string pad(ind * 2, ' '), pad2((ind + 1) * 2, ' ');
            switch (v.type)
            {
            case Value::Null: out += "null"; break;
            case Value::Bool: out += v.b ? "true" : "false"; break;
            case Value::Int: out += std::to_string(v.i); break;
            case Value::Str: quote(out, v.s); break;
            case Value::Arr:
                if (v.arr.empty())
                    out += "[]";
                else if (flat(v))
                {
                    out += '[';
                    for (size_t k = 0; k < v.arr.size(); ++k)
                        out += k ? ", " : "", write(out, v.arr[k], 0);
                    out += ']';
                }
                else
                {
                    out += "[\n";
                    for (size_t k = 0; k < v.arr.size(); ++k)
                    {
                        out += pad2;
                        write(out, v.arr[k], ind + 1);
                        out += k + 1 < v.arr.size() ? ",\n" : "\n";
                    }
                    out += pad + "]";
                }
                break;
            case Value::Obj:
                if (v.obj.empty())
                {
                    out += "{}";
                    break;
                }
                out += "{\n";
                for (size_t k = 0; k < v.obj.size(); ++k)
                {
                    out += pad2;
                    quote(out, v.obj[k].first);
                    out += ": ";
                    write(out, v.obj[k].second, ind + 1);
                    out += k + 1 < v.obj.size() ? ",\n" : "\n";
                }
                out += pad + "}";
                break;
            }
        }
    }

    // false com `err` preenchido se o texto nao for JSON valido (inclusive lixo depois do valor).
    inline bool parse(const std::string &text, Value &out, std::string *err = nullptr)
    {
        detail::Parser ps{text.data(), text.data() + text.size(), {}};
        // BOM do UTF-8 (o Bloco de Notas pode gravar)
        if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
            static_cast<unsigned char>(text[2]) == 0xBF)
            ps.p += 3;
        bool ok = ps.value(out, 0);
        if (ok)
        {
            ps.ws();
            if (ps.p != ps.end)
                ok = ps.fail("lixo depois do valor");
        }
        if (!ok && err)
        {
            int line = 1;
            for (const char *q = text.data(); q < ps.p && q < ps.end; ++q)
                line += *q == '\n';
            *err = ps.err + " (linha " + std::to_string(line) + ")";
        }
        return ok;
    }

    inline std::string dump(const Value &v)
    {
        std::string out;
        detail::write(out, v, 0);
        out += '\n';
        return out;
    }
}
