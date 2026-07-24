#include "script/CowScript.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace cowscript
{

    // ---------------------------------------------------------------------
    // Value helpers
    // ---------------------------------------------------------------------

    bool Value::truthy() const
    {
        switch (type)
        {
        case Null:
            return false;
        case Bool:
            return boolean;
        case Number:
            return num != 0.0;
        case Str:
            return !str.empty();
        case Handle:
            return handle != nullptr;
        }
        return false;
    }

    double Value::toNumber() const
    {
        switch (type)
        {
        case Number:
            return num;
        case Bool:
            return boolean ? 1.0 : 0.0;
        case Null:
            return 0.0;
        case Str:
            try
            {
                return std::stod(str);
            }
            catch (...)
            {
                return 0.0;
            }
        case Handle:
            return 0.0;
        }
        return 0.0;
    }

    std::string Value::toString() const
    {
        switch (type)
        {
        case Null:
            return "null";
        case Bool:
            return boolean ? "true" : "false";
        case Str:
            return str;
        case Number:
        {
            char buf[64];
            // Print integers without a trailing decimal.
            if (num == static_cast<double>(static_cast<long long>(num)) &&
                std::abs(num) < 1e15)
                std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(num));
            else
                std::snprintf(buf, sizeof(buf), "%g", num);
            return std::string(buf);
        }
        case Handle:
            return "<" + str + ">";
        }
        return "";
    }

    // ---------------------------------------------------------------------
    // Lexer
    // ---------------------------------------------------------------------

    enum class Tok
    {
        End,
        Number,
        String,
        Ident,
        // keywords
        KwLet,
        KwIf,
        KwElse,
        KwWhile,
        KwFn,
        KwReturn,
        KwOn,
        KwTrue,
        KwFalse,
        KwNull,
        KwAnd,
        KwOr,
        KwNot,
        // operators / punctuation
        Plus,
        Minus,
        Star,
        Slash,
        Percent,
        Eq,
        EqEq,
        BangEq,
        Lt,
        Le,
        Gt,
        Ge,
        LParen,
        RParen,
        LBrace,
        RBrace,
        Comma,
        Semi,
        Dot,
    };

    struct LexToken
    {
        Tok kind = Tok::End;
        std::string text;
        double number = 0.0;
        int line = 1;
        int col = 1;
    };

    static const std::unordered_map<std::string, Tok> &keywordMap()
    {
        static const std::unordered_map<std::string, Tok> m = {
            {"let", Tok::KwLet},
            {"if", Tok::KwIf},
            {"else", Tok::KwElse},
            {"while", Tok::KwWhile},
            {"fn", Tok::KwFn},
            {"return", Tok::KwReturn},
            {"on", Tok::KwOn},
            {"true", Tok::KwTrue},
            {"false", Tok::KwFalse},
            {"null", Tok::KwNull},
            {"and", Tok::KwAnd},
            {"or", Tok::KwOr},
            {"not", Tok::KwNot},
        };
        return m;
    }

    struct Lexer
    {
        const std::string &src;
        size_t pos = 0;
        int line = 1;
        int col = 1;

        explicit Lexer(const std::string &s) : src(s) {}

        char peek(size_t off = 0) const
        {
            return pos + off < src.size() ? src[pos + off] : '\0';
        }

        char advance()
        {
            char c = src[pos++];
            if (c == '\n')
            {
                line++;
                col = 1;
            }
            else
                col++;
            return c;
        }

        void skipWhitespace()
        {
            while (pos < src.size())
            {
                char c = peek();
                if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
                {
                    advance();
                }
                else if (c == '/' && peek(1) == '/')
                {
                    while (pos < src.size() && peek() != '\n')
                        advance();
                }
                else
                {
                    break;
                }
            }
        }

        LexToken next()
        {
            skipWhitespace();
            LexToken t;
            t.line = line;
            t.col = col;
            if (pos >= src.size())
            {
                t.kind = Tok::End;
                return t;
            }
            char c = peek();

            if (std::isdigit(static_cast<unsigned char>(c)) ||
                (c == '.' && std::isdigit(static_cast<unsigned char>(peek(1)))))
            {
                std::string num;
                while (pos < src.size() &&
                       (std::isdigit(static_cast<unsigned char>(peek())) || peek() == '.'))
                {
                    num.push_back(advance());
                }
                t.kind = Tok::Number;
                t.text = num;
                t.number = std::stod(num);
                return t;
            }

            if (std::isalpha(static_cast<unsigned char>(c)) || c == '_')
            {
                std::string id;
                while (pos < src.size() &&
                       (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_'))
                    id.push_back(advance());
                auto it = keywordMap().find(id);
                if (it != keywordMap().end())
                {
                    t.kind = it->second;
                    t.text = id;
                    return t;
                }
                t.kind = Tok::Ident;
                t.text = id;
                return t;
            }

            if (c == '"')
            {
                advance();
                std::string s;
                while (pos < src.size() && peek() != '"')
                {
                    char ch = advance();
                    if (ch == '\\' && pos < src.size())
                    {
                        char esc = advance();
                        switch (esc)
                        {
                        case 'n':
                            s.push_back('\n');
                            break;
                        case 't':
                            s.push_back('\t');
                            break;
                        case 'r':
                            s.push_back('\r');
                            break;
                        case '\\':
                            s.push_back('\\');
                            break;
                        case '"':
                            s.push_back('"');
                            break;
                        default:
                            s.push_back(esc);
                            break;
                        }
                    }
                    else
                    {
                        s.push_back(ch);
                    }
                }
                if (pos < src.size())
                    advance(); // closing "
                t.kind = Tok::String;
                t.text = std::move(s);
                return t;
            }

            // operators / punctuation
            char ch = advance();
            switch (ch)
            {
            case '+':
                t.kind = Tok::Plus;
                return t;
            case '-':
                t.kind = Tok::Minus;
                return t;
            case '*':
                t.kind = Tok::Star;
                return t;
            case '/':
                t.kind = Tok::Slash;
                return t;
            case '%':
                t.kind = Tok::Percent;
                return t;
            case '(':
                t.kind = Tok::LParen;
                return t;
            case ')':
                t.kind = Tok::RParen;
                return t;
            case '{':
                t.kind = Tok::LBrace;
                return t;
            case '}':
                t.kind = Tok::RBrace;
                return t;
            case ',':
                t.kind = Tok::Comma;
                return t;
            case ';':
                t.kind = Tok::Semi;
                return t;
            case '.':
                t.kind = Tok::Dot;
                return t;
            case '=':
                if (peek() == '=')
                {
                    advance();
                    t.kind = Tok::EqEq;
                }
                else
                    t.kind = Tok::Eq;
                return t;
            case '!':
                if (peek() == '=')
                {
                    advance();
                    t.kind = Tok::BangEq;
                    return t;
                }
                break;
            case '<':
                if (peek() == '=')
                {
                    advance();
                    t.kind = Tok::Le;
                }
                else
                    t.kind = Tok::Lt;
                return t;
            case '>':
                if (peek() == '=')
                {
                    advance();
                    t.kind = Tok::Ge;
                }
                else
                    t.kind = Tok::Gt;
                return t;
            }

            throw std::runtime_error("Unexpected character '" + std::string(1, ch) +
                                     "' at line " + std::to_string(t.line));
        }
    };

    // ---------------------------------------------------------------------
    // AST
    // ---------------------------------------------------------------------

    enum class ExprKind
    {
        NumberLit,
        StringLit,
        BoolLit,
        NullLit,
        Ident,
        Call,
        Unary,
        Binary,
        Assign,
        Member,       // left . text  (read property)
        MemberAssign, // left . text = right  (write property)
    };

    enum class StmtKind
    {
        ExprStmt,
        Let,
        If,
        While,
        Return,
        Block,
    };

    struct Expr;
    struct Stmt;
    using ExprPtr = std::shared_ptr<Expr>;
    using StmtPtr = std::shared_ptr<Stmt>;

    struct Expr
    {
        ExprKind kind;
        // common storage
        double num = 0.0;
        std::string text;     // identifier name, string lit, op
        bool boolean = false; // bool lit
        std::vector<ExprPtr> args;
        ExprPtr left, right;
        int line = 0;
    };

    struct Stmt
    {
        StmtKind kind;
        // Let
        std::string name;
        ExprPtr expr;
        // If/While
        ExprPtr cond;
        std::vector<StmtPtr> body;
        std::vector<StmtPtr> elseBody;
        int line = 0;
    };

    struct FnDecl
    {
        std::string name;
        std::vector<std::string> params;
        std::vector<StmtPtr> body;
    };

    // ---------------------------------------------------------------------
    // Parser
    // ---------------------------------------------------------------------

    struct Parser
    {
        Lexer lex;
        LexToken cur;
        LexToken prev;

        explicit Parser(const std::string &source) : lex(source) { cur = lex.next(); }

        void advance()
        {
            prev = cur;
            cur = lex.next();
        }

        bool check(Tok k) const { return cur.kind == k; }

        bool match(Tok k)
        {
            if (check(k))
            {
                advance();
                return true;
            }
            return false;
        }

        void expect(Tok k, const char *what)
        {
            if (!check(k))
            {
                throw std::runtime_error(std::string("Expected ") + what +
                                         " at line " + std::to_string(cur.line));
            }
            advance();
        }

        void parseProgram(std::vector<FnDecl> &outFns, std::vector<FnDecl> &outEvents,
                          std::vector<StmtPtr> &outTopLevel)
        {
            while (cur.kind != Tok::End)
            {
                if (check(Tok::KwFn))
                {
                    outFns.push_back(parseFnLike());
                }
                else if (check(Tok::KwOn))
                {
                    outEvents.push_back(parseFnLike());
                }
                else
                {
                    // Allow top-level let / expression statements for global variable init.
                    outTopLevel.push_back(parseStatement());
                }
            }
        }

        FnDecl parseFnLike()
        {
            // 'fn' or 'on' already current; consume it.
            advance();
            if (!check(Tok::Ident))
                throw std::runtime_error("Expected function/event name at line " +
                                         std::to_string(cur.line));
            FnDecl fn;
            fn.name = cur.text;
            advance();
            expect(Tok::LParen, "'('");
            if (!check(Tok::RParen))
            {
                while (true)
                {
                    if (!check(Tok::Ident))
                        throw std::runtime_error("Expected parameter name at line " +
                                                 std::to_string(cur.line));
                    fn.params.push_back(cur.text);
                    advance();
                    if (!match(Tok::Comma))
                        break;
                }
            }
            expect(Tok::RParen, "')'");
            expect(Tok::LBrace, "'{'");
            fn.body = parseBlock();
            return fn;
        }

        std::vector<StmtPtr> parseBlock()
        {
            // '{' was already consumed.
            std::vector<StmtPtr> body;
            while (!check(Tok::RBrace) && !check(Tok::End))
            {
                body.push_back(parseStatement());
            }
            expect(Tok::RBrace, "'}'");
            return body;
        }

        StmtPtr parseStatement()
        {
            auto s = std::make_shared<Stmt>();
            s->line = cur.line;
            if (match(Tok::KwLet))
            {
                if (!check(Tok::Ident))
                    throw std::runtime_error("Expected variable name after 'let'");
                s->kind = StmtKind::Let;
                s->name = cur.text;
                advance();
                expect(Tok::Eq, "'='");
                s->expr = parseExpr();
                match(Tok::Semi);
                return s;
            }
            if (match(Tok::KwIf))
            {
                s->kind = StmtKind::If;
                expect(Tok::LParen, "'('");
                s->cond = parseExpr();
                expect(Tok::RParen, "')'");
                expect(Tok::LBrace, "'{'");
                s->body = parseBlock();
                if (match(Tok::KwElse))
                {
                    if (match(Tok::KwIf))
                    {
                        // chain: else if
                        auto inner = std::make_shared<Stmt>();
                        inner->kind = StmtKind::If;
                        inner->line = prev.line;
                        expect(Tok::LParen, "'('");
                        inner->cond = parseExpr();
                        expect(Tok::RParen, "')'");
                        expect(Tok::LBrace, "'{'");
                        inner->body = parseBlock();
                        if (match(Tok::KwElse))
                        {
                            expect(Tok::LBrace, "'{'");
                            inner->elseBody = parseBlock();
                        }
                        s->elseBody.push_back(inner);
                    }
                    else
                    {
                        expect(Tok::LBrace, "'{'");
                        s->elseBody = parseBlock();
                    }
                }
                return s;
            }
            if (match(Tok::KwWhile))
            {
                s->kind = StmtKind::While;
                expect(Tok::LParen, "'('");
                s->cond = parseExpr();
                expect(Tok::RParen, "')'");
                expect(Tok::LBrace, "'{'");
                s->body = parseBlock();
                return s;
            }
            if (match(Tok::KwReturn))
            {
                s->kind = StmtKind::Return;
                if (!check(Tok::Semi) && !check(Tok::RBrace) && !check(Tok::End))
                {
                    s->expr = parseExpr();
                }
                match(Tok::Semi);
                return s;
            }
            // expression statement (may be assignment)
            s->kind = StmtKind::ExprStmt;
            s->expr = parseExpr();
            match(Tok::Semi);
            return s;
        }

        ExprPtr parseExpr() { return parseAssign(); }

        ExprPtr parseAssign()
        {
            auto lhs = parseOr();
            if (check(Tok::Eq))
            {
                advance();
                auto rhs = parseAssign();
                if (lhs->kind == ExprKind::Ident)
                {
                    auto e = std::make_shared<Expr>();
                    e->kind = ExprKind::Assign;
                    e->text = lhs->text;
                    e->right = rhs;
                    e->line = lhs->line;
                    return e;
                }
                if (lhs->kind == ExprKind::Member)
                {
                    auto e = std::make_shared<Expr>();
                    e->kind = ExprKind::MemberAssign;
                    e->left = lhs->left;
                    e->text = lhs->text;
                    e->right = rhs;
                    e->line = lhs->line;
                    return e;
                }
                throw std::runtime_error("Left side of '=' must be a variable or property");
            }
            return lhs;
        }

        ExprPtr parseOr()
        {
            auto lhs = parseAnd();
            while (match(Tok::KwOr))
            {
                auto rhs = parseAnd();
                auto e = std::make_shared<Expr>();
                e->kind = ExprKind::Binary;
                e->text = "or";
                e->left = lhs;
                e->right = rhs;
                e->line = lhs->line;
                lhs = e;
            }
            return lhs;
        }

        ExprPtr parseAnd()
        {
            auto lhs = parseEq();
            while (match(Tok::KwAnd))
            {
                auto rhs = parseEq();
                auto e = std::make_shared<Expr>();
                e->kind = ExprKind::Binary;
                e->text = "and";
                e->left = lhs;
                e->right = rhs;
                e->line = lhs->line;
                lhs = e;
            }
            return lhs;
        }

        ExprPtr parseEq()
        {
            auto lhs = parseRel();
            while (check(Tok::EqEq) || check(Tok::BangEq))
            {
                std::string op = check(Tok::EqEq) ? "==" : "!=";
                advance();
                auto rhs = parseRel();
                auto e = std::make_shared<Expr>();
                e->kind = ExprKind::Binary;
                e->text = op;
                e->left = lhs;
                e->right = rhs;
                e->line = lhs->line;
                lhs = e;
            }
            return lhs;
        }

        ExprPtr parseRel()
        {
            auto lhs = parseAdd();
            while (check(Tok::Lt) || check(Tok::Le) || check(Tok::Gt) || check(Tok::Ge))
            {
                std::string op;
                if (check(Tok::Lt))
                    op = "<";
                else if (check(Tok::Le))
                    op = "<=";
                else if (check(Tok::Gt))
                    op = ">";
                else
                    op = ">=";
                advance();
                auto rhs = parseAdd();
                auto e = std::make_shared<Expr>();
                e->kind = ExprKind::Binary;
                e->text = op;
                e->left = lhs;
                e->right = rhs;
                e->line = lhs->line;
                lhs = e;
            }
            return lhs;
        }

        ExprPtr parseAdd()
        {
            auto lhs = parseMul();
            while (check(Tok::Plus) || check(Tok::Minus))
            {
                std::string op = check(Tok::Plus) ? "+" : "-";
                advance();
                auto rhs = parseMul();
                auto e = std::make_shared<Expr>();
                e->kind = ExprKind::Binary;
                e->text = op;
                e->left = lhs;
                e->right = rhs;
                e->line = lhs->line;
                lhs = e;
            }
            return lhs;
        }

        ExprPtr parseMul()
        {
            auto lhs = parseUnary();
            while (check(Tok::Star) || check(Tok::Slash) || check(Tok::Percent))
            {
                std::string op = check(Tok::Star) ? "*" : (check(Tok::Slash) ? "/" : "%");
                advance();
                auto rhs = parseUnary();
                auto e = std::make_shared<Expr>();
                e->kind = ExprKind::Binary;
                e->text = op;
                e->left = lhs;
                e->right = rhs;
                e->line = lhs->line;
                lhs = e;
            }
            return lhs;
        }

        ExprPtr parseUnary()
        {
            if (check(Tok::Minus) || check(Tok::KwNot))
            {
                std::string op = check(Tok::Minus) ? "-" : "not";
                int ln = cur.line;
                advance();
                auto inner = parseUnary();
                auto e = std::make_shared<Expr>();
                e->kind = ExprKind::Unary;
                e->text = op;
                e->left = inner;
                e->line = ln;
                return e;
            }
            return parseCall();
        }

        ExprPtr parseCall()
        {
            auto lhs = parsePrimary();
            while (check(Tok::LParen) || check(Tok::Dot))
            {
                if (check(Tok::LParen))
                {
                    if (lhs->kind != ExprKind::Ident)
                        throw std::runtime_error("Only identifiers can be called as functions (line " +
                                                 std::to_string(cur.line) + ")");
                    advance();
                    std::vector<ExprPtr> args;
                    if (!check(Tok::RParen))
                    {
                        while (true)
                        {
                            args.push_back(parseExpr());
                            if (!match(Tok::Comma))
                                break;
                        }
                    }
                    expect(Tok::RParen, "')'");
                    auto e = std::make_shared<Expr>();
                    e->kind = ExprKind::Call;
                    e->text = lhs->text;
                    e->args = std::move(args);
                    e->line = lhs->line;
                    lhs = e;
                }
                else
                {
                    // '.' property access
                    advance();
                    if (!check(Tok::Ident))
                        throw std::runtime_error("Expected property name after '.' at line " +
                                                 std::to_string(cur.line));
                    auto e = std::make_shared<Expr>();
                    e->kind = ExprKind::Member;
                    e->left = lhs;
                    e->text = cur.text;
                    e->line = lhs->line;
                    advance();
                    lhs = e;
                }
            }
            return lhs;
        }

        ExprPtr parsePrimary()
        {
            auto e = std::make_shared<Expr>();
            e->line = cur.line;
            if (check(Tok::Number))
            {
                e->kind = ExprKind::NumberLit;
                e->num = cur.number;
                advance();
                return e;
            }
            if (check(Tok::String))
            {
                e->kind = ExprKind::StringLit;
                e->text = cur.text;
                advance();
                return e;
            }
            if (check(Tok::KwTrue) || check(Tok::KwFalse))
            {
                e->kind = ExprKind::BoolLit;
                e->boolean = check(Tok::KwTrue);
                advance();
                return e;
            }
            if (check(Tok::KwNull))
            {
                e->kind = ExprKind::NullLit;
                advance();
                return e;
            }
            if (check(Tok::Ident))
            {
                e->kind = ExprKind::Ident;
                e->text = cur.text;
                advance();
                return e;
            }
            if (match(Tok::LParen))
            {
                auto inner = parseExpr();
                expect(Tok::RParen, "')'");
                return inner;
            }
            throw std::runtime_error("Unexpected token at line " + std::to_string(cur.line));
        }
    };

    // ---------------------------------------------------------------------
    // Interpreter
    // ---------------------------------------------------------------------

    struct Env
    {
        std::unordered_map<std::string, Value> vars;
        Env *parent = nullptr;

        Value *find(const std::string &name)
        {
            auto it = vars.find(name);
            if (it != vars.end())
                return &it->second;
            if (parent)
                return parent->find(name);
            return nullptr;
        }
        void define(const std::string &name, Value v) { vars[name] = std::move(v); }
        bool assign(const std::string &name, Value v)
        {
            auto it = vars.find(name);
            if (it != vars.end())
            {
                it->second = std::move(v);
                return true;
            }
            if (parent)
                return parent->assign(name, std::move(v));
            return false;
        }
    };

    struct ReturnException
    {
        Value value;
    };

    struct Script::Impl
    {
        std::vector<FnDecl> functions;
        std::vector<FnDecl> events;
        std::vector<StmtPtr> topLevelStmts;
        std::unordered_map<std::string, BuiltinFn> builtins;
        PropertyGetFn propGet;
        PropertySetFn propSet;
        Env globals;

        const FnDecl *findFunction(const std::string &name) const
        {
            for (const auto &f : functions)
                if (f.name == name)
                    return &f;
            return nullptr;
        }
        const FnDecl *findEvent(const std::string &name) const
        {
            for (const auto &e : events)
                if (e.name == name)
                    return &e;
            return nullptr;
        }

        Value execCall(const std::string &name, std::vector<Value> args)
        {
            auto bit = builtins.find(name);
            if (bit != builtins.end())
                return bit->second(args);
            const FnDecl *fn = findFunction(name);
            if (!fn)
                throw std::runtime_error("Unknown function: " + name);
            return execFunction(*fn, args);
        }

        Value execFunction(const FnDecl &fn, const std::vector<Value> &args)
        {
            Env local;
            local.parent = &globals;
            for (size_t i = 0; i < fn.params.size(); ++i)
            {
                local.define(fn.params[i], i < args.size() ? args[i] : Value::makeNull());
            }
            try
            {
                for (const auto &s : fn.body)
                    execStmt(*s, local);
            }
            catch (ReturnException &r)
            {
                return r.value;
            }
            return Value::makeNull();
        }

        void execStmt(const Stmt &s, Env &env)
        {
            switch (s.kind)
            {
            case StmtKind::Let:
                env.define(s.name, evalExpr(*s.expr, env));
                break;
            case StmtKind::ExprStmt:
                evalExpr(*s.expr, env);
                break;
            case StmtKind::Block:
            {
                Env inner;
                inner.parent = &env;
                for (auto &b : s.body)
                    execStmt(*b, inner);
                break;
            }
            case StmtKind::If:
            {
                Value c = evalExpr(*s.cond, env);
                if (c.truthy())
                {
                    Env inner;
                    inner.parent = &env;
                    for (auto &b : s.body)
                        execStmt(*b, inner);
                }
                else if (!s.elseBody.empty())
                {
                    Env inner;
                    inner.parent = &env;
                    for (auto &b : s.elseBody)
                        execStmt(*b, inner);
                }
                break;
            }
            case StmtKind::While:
            {
                int safety = 0;
                while (true)
                {
                    Value c = evalExpr(*s.cond, env);
                    if (!c.truthy())
                        break;
                    Env inner;
                    inner.parent = &env;
                    for (auto &b : s.body)
                        execStmt(*b, inner);
                    if (++safety > 1000000)
                        throw std::runtime_error("while loop exceeded 1,000,000 iterations");
                }
                break;
            }
            case StmtKind::Return:
            {
                ReturnException r;
                r.value = s.expr ? evalExpr(*s.expr, env) : Value::makeNull();
                throw r;
            }
            }
        }

        Value evalExpr(const Expr &e, Env &env)
        {
            switch (e.kind)
            {
            case ExprKind::NumberLit:
                return Value::makeNumber(e.num);
            case ExprKind::StringLit:
                return Value::makeString(e.text);
            case ExprKind::BoolLit:
                return Value::makeBool(e.boolean);
            case ExprKind::NullLit:
                return Value::makeNull();
            case ExprKind::Ident:
            {
                Value *v = env.find(e.text);
                if (!v)
                    throw std::runtime_error("Undefined variable: " + e.text +
                                             " (line " + std::to_string(e.line) + ")");
                return *v;
            }
            case ExprKind::Assign:
            {
                Value v = evalExpr(*e.right, env);
                if (!env.assign(e.text, v))
                    env.define(e.text, v);
                return v;
            }
            case ExprKind::Call:
            {
                std::vector<Value> args;
                args.reserve(e.args.size());
                for (auto &a : e.args)
                    args.push_back(evalExpr(*a, env));
                return execCall(e.text, std::move(args));
            }
            case ExprKind::Member:
            {
                Value target = evalExpr(*e.left, env);
                if (!propGet)
                    throw std::runtime_error("No property accessor registered (line " +
                                             std::to_string(e.line) + ")");
                return propGet(target, e.text);
            }
            case ExprKind::MemberAssign:
            {
                Value target = evalExpr(*e.left, env);
                Value v = evalExpr(*e.right, env);
                if (!propSet)
                    throw std::runtime_error("No property accessor registered (line " +
                                             std::to_string(e.line) + ")");
                propSet(target, e.text, v);
                return v;
            }
            case ExprKind::Unary:
            {
                Value v = evalExpr(*e.left, env);
                if (e.text == "-")
                    return Value::makeNumber(-v.toNumber());
                if (e.text == "not")
                    return Value::makeBool(!v.truthy());
                throw std::runtime_error("Unknown unary operator: " + e.text);
            }
            case ExprKind::Binary:
            {
                const std::string &op = e.text;
                if (op == "and")
                {
                    Value l = evalExpr(*e.left, env);
                    if (!l.truthy())
                        return l;
                    return evalExpr(*e.right, env);
                }
                if (op == "or")
                {
                    Value l = evalExpr(*e.left, env);
                    if (l.truthy())
                        return l;
                    return evalExpr(*e.right, env);
                }
                Value l = evalExpr(*e.left, env);
                Value r = evalExpr(*e.right, env);
                if (op == "+")
                {
                    if (l.type == Value::Str || r.type == Value::Str)
                        return Value::makeString(l.toString() + r.toString());
                    return Value::makeNumber(l.toNumber() + r.toNumber());
                }
                if (op == "-")
                    return Value::makeNumber(l.toNumber() - r.toNumber());
                if (op == "*")
                    return Value::makeNumber(l.toNumber() * r.toNumber());
                if (op == "/")
                {
                    double rv = r.toNumber();
                    if (rv == 0.0)
                        throw std::runtime_error("Division by zero (line " +
                                                 std::to_string(e.line) + ")");
                    return Value::makeNumber(l.toNumber() / rv);
                }
                if (op == "%")
                {
                    double rv = r.toNumber();
                    if (rv == 0.0)
                        throw std::runtime_error("Modulo by zero");
                    return Value::makeNumber(std::fmod(l.toNumber(), rv));
                }
                if (op == "==" || op == "!=")
                {
                    bool eq;
                    if (l.type == Value::Null || r.type == Value::Null)
                        // null is equal only to null. This lets scripts test
                        // object handles with `h != null` (a handle is not null).
                        eq = (l.type == Value::Null && r.type == Value::Null);
                    else if (l.type == Value::Handle || r.type == Value::Handle)
                        // Two handles are the same object iff same kind + pointer.
                        eq = (l.type == Value::Handle && r.type == Value::Handle &&
                              l.handle == r.handle && l.str == r.str);
                    else if (l.type == Value::Str || r.type == Value::Str)
                        eq = (l.toString() == r.toString());
                    else
                        eq = (l.toNumber() == r.toNumber());
                    return Value::makeBool(op == "==" ? eq : !eq);
                }
                if (op == "<")
                    return Value::makeBool(l.toNumber() < r.toNumber());
                if (op == "<=")
                    return Value::makeBool(l.toNumber() <= r.toNumber());
                if (op == ">")
                    return Value::makeBool(l.toNumber() > r.toNumber());
                if (op == ">=")
                    return Value::makeBool(l.toNumber() >= r.toNumber());
                throw std::runtime_error("Unknown binary operator: " + op);
            }
            }
            return Value::makeNull();
        }
    };

    Script::Script() : impl(std::make_unique<Impl>()) {}
    Script::~Script() = default;

    std::string Script::compile(const std::string &source)
    {
        src = source;
        impl->functions.clear();
        impl->events.clear();
        impl->topLevelStmts.clear();
        impl->globals.vars.clear();
        try
        {
            Parser p(source);
            p.parseProgram(impl->functions, impl->events, impl->topLevelStmts);
            // Execute top-level statements immediately to initialise global variables.
            for (const auto &s : impl->topLevelStmts)
                impl->execStmt(*s, impl->globals);
        }
        catch (const std::exception &e)
        {
            return std::string("parse error: ") + e.what();
        }
        return "";
    }

    void Script::setBuiltin(const std::string &name, BuiltinFn fn)
    {
        impl->builtins[name] = std::move(fn);
    }

    void Script::setPropertyGetter(PropertyGetFn fn)
    {
        impl->propGet = std::move(fn);
    }

    void Script::setPropertySetter(PropertySetFn fn)
    {
        impl->propSet = std::move(fn);
    }

    bool Script::hasEvent(const std::string &name) const
    {
        return impl->findEvent(name) != nullptr;
    }

    std::string Script::callEvent(const std::string &name, const std::vector<Value> &args)
    {
        const FnDecl *e = impl->findEvent(name);
        if (!e)
            return "";
        try
        {
            impl->execFunction(*e, args);
        }
        catch (const std::exception &ex)
        {
            return std::string("runtime error: ") + ex.what();
        }
        return "";
    }

    // ---------------------------------------------------------------------
    // File reader
    // ---------------------------------------------------------------------

    std::string readScriptFile(const std::string &path, std::string *outFoundPath)
    {
        namespace fs = std::filesystem;
        std::vector<fs::path> candidates;
        candidates.emplace_back(path);
#ifdef ASSET_ROOT
        candidates.emplace_back(fs::path(ASSET_ROOT) / path);
#endif
        candidates.emplace_back(fs::path("./") / path);
        candidates.emplace_back(fs::path("../") / path);
        candidates.emplace_back(fs::path("../../") / path);

        for (auto &c : candidates)
        {
            std::ifstream in(c);
            if (in)
            {
                std::stringstream ss;
                ss << in.rdbuf();
                if (outFoundPath)
                    *outFoundPath = c.string();
                return ss.str();
            }
        }
        return "";
    }

    // ---------------------------------------------------------------------
    // Highlighter
    // ---------------------------------------------------------------------

    static const std::unordered_set<std::string> &keywordSet()
    {
        static const std::unordered_set<std::string> s = {
            "let", "if", "else", "while", "fn", "return", "on",
            "true", "false", "null", "and", "or", "not"};
        return s;
    }

    static const std::unordered_set<std::string> &builtinSet()
    {
        static const std::unordered_set<std::string> s = {
            "print",
            "time",
            "dt",
            "key",
            "sin", "cos", "tan", "sqrt", "abs", "floor", "ceil", "random",
            "self_x", "self_y", "self_z",
            "self_rx", "self_ry", "self_rz",
            "self_sx", "self_sy", "self_sz",
            "self_set_pos", "self_set_rot", "self_set_scale", "self_set_color",
            "self_apply_impulse", "self_apply_force", "self_set_velocity",
            "self_on_ground", "self_collided", "self_explode", "self_set_friction",
            "spawn_cube", "spawn_cow", "spawn_plane",
            "destroy", "destroy_self", "attach_script",
            "self", "transform", "rigidbody", "camera",
            "transform_of", "rigidbody_of",
        };
        return s;
    }

    std::vector<Token> highlight(const std::string &source)
    {
        std::vector<Token> out;
        int i = 0;
        const int n = static_cast<int>(source.size());
        while (i < n)
        {
            char c = source[i];

            if (c == '/' && i + 1 < n && source[i + 1] == '/')
            {
                int start = i;
                while (i < n && source[i] != '\n')
                    ++i;
                out.push_back({TokenKind::Comment, start, i - start});
                continue;
            }

            if (c == '"')
            {
                int start = i;
                ++i;
                while (i < n && source[i] != '"')
                {
                    if (source[i] == '\\' && i + 1 < n)
                        i += 2;
                    else
                        ++i;
                }
                if (i < n)
                    ++i; // closing "
                out.push_back({TokenKind::String, start, i - start});
                continue;
            }

            if (std::isdigit(static_cast<unsigned char>(c)) ||
                (c == '.' && i + 1 < n && std::isdigit(static_cast<unsigned char>(source[i + 1]))))
            {
                int start = i;
                while (i < n && (std::isdigit(static_cast<unsigned char>(source[i])) || source[i] == '.'))
                    ++i;
                out.push_back({TokenKind::Number, start, i - start});
                continue;
            }

            if (std::isalpha(static_cast<unsigned char>(c)) || c == '_')
            {
                int start = i;
                while (i < n && (std::isalnum(static_cast<unsigned char>(source[i])) || source[i] == '_'))
                    ++i;
                std::string word = source.substr(start, i - start);
                TokenKind kind = TokenKind::Identifier;
                if (keywordSet().count(word))
                    kind = TokenKind::Keyword;
                else if (builtinSet().count(word))
                    kind = TokenKind::Builtin;
                out.push_back({kind, start, i - start});
                continue;
            }

            if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
            {
                int start = i;
                while (i < n && (source[i] == ' ' || source[i] == '\t' ||
                                 source[i] == '\r' || source[i] == '\n'))
                    ++i;
                out.push_back({TokenKind::Whitespace, start, i - start});
                continue;
            }

            // operator / punctuation
            int start = i;
            // greedy two-char ops
            if (i + 1 < n)
            {
                std::string two = source.substr(i, 2);
                if (two == "==" || two == "!=" || two == "<=" || two == ">=")
                {
                    i += 2;
                    out.push_back({TokenKind::Operator, start, 2});
                    continue;
                }
            }
            const char *singleOps = "+-*/%<>=!";
            const char *singlePuncs = "(){},;.";
            if (std::strchr(singleOps, c))
                out.push_back({TokenKind::Operator, i, 1});
            else if (std::strchr(singlePuncs, c))
                out.push_back({TokenKind::Punctuation, i, 1});
            else
                out.push_back({TokenKind::Text, i, 1});
            ++i;
        }
        return out;
    }

} // namespace cowscript
