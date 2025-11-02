#include "map_parser.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <iterator>
#include <utility>

#include "logging.hpp"

namespace bspc::map
{
namespace
{

enum class TokenType
{
    kEnd,
    kLBrace,
    kRBrace,
    kLParen,
    kRParen,
    kString,
    kNumber,
    kIdentifier,
};

struct Token
{
    TokenType type = TokenType::kEnd;
    std::string text;
    float number = 0.0f;
    std::size_t line = 1;
};

class Lexer
{
public:
    explicit Lexer(std::string_view text)
        : text_(text),
          position_(0),
          line_(1)
    {
    }

    Token Peek()
    {
        if (!lookahead_.has_value())
        {
            lookahead_ = Scan();
        }
        return *lookahead_;
    }

    Token Consume()
    {
        Token token = Peek();
        lookahead_.reset();
        return token;
    }

private:
    static bool IsWhitespace(char ch)
    {
        return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
    }

    static bool IsIdentifierChar(char ch)
    {
        return !IsWhitespace(ch) && ch != '{' && ch != '}' && ch != '(' && ch != ')' && ch != '"';
    }

    void SkipWhitespace()
    {
        while (position_ < text_.size())
        {
            char ch = text_[position_];
            if (ch == '\n')
            {
                ++line_;
            }
            if (!IsWhitespace(ch))
            {
                break;
            }
            ++position_;
        }
    }

    void SkipComment()
    {
        if (position_ + 1 >= text_.size())
        {
            return;
        }
        if (text_[position_] == '/' && text_[position_ + 1] == '/')
        {
            position_ += 2;
            while (position_ < text_.size())
            {
                char ch = text_[position_++];
                if (ch == '\n')
                {
                    ++line_;
                    break;
                }
            }
        }
    }

    Token ScanString()
    {
    std::string value;
    std::size_t start_line = line_;
    while (position_ < text_.size())
    {
        char ch = text_[position_++];
        if (ch == '\\')
        {
                if (position_ >= text_.size())
                {
                    break;
                }
                char escaped = text_[position_++];
                switch (escaped)
                {
                case 'n': value.push_back('\n'); break;
                case 'r': value.push_back('\r'); break;
                case 't': value.push_back('\t'); break;
                case '\\': value.push_back('\\'); break;
                case '"': value.push_back('"'); break;
                default: value.push_back(escaped); break;
                }
            }
            else if (ch == '"')
            {
                Token token;
                token.type = TokenType::kString;
                token.text = std::move(value);
                token.line = start_line;
                return token;
            }
            else
            {
                if (ch == '\n')
                {
                    ++line_;
                }
                value.push_back(ch);
            }
        }
        Token token;
        token.type = TokenType::kString;
        token.text = std::move(value);
        token.line = start_line;
        return token;
    }

    static bool LooksNumeric(std::string_view text)
    {
        if (text.empty())
        {
            return false;
        }
        bool has_digit = false;
        for (std::size_t i = 0; i < text.size(); ++i)
        {
            char ch = text[i];
            if (std::isdigit(static_cast<unsigned char>(ch)))
            {
                has_digit = true;
                continue;
            }
            if (ch == '+' || ch == '-' || ch == '.' || ch == 'e' || ch == 'E')
            {
                continue;
            }
            return false;
        }
        return has_digit;
    }

    static Token MakeNumberToken(std::string text, std::size_t line)
    {
        Token token;
        token.type = TokenType::kIdentifier;
        token.text = std::move(text);
        token.line = line;

        char *end = nullptr;
        token.number = std::strtof(token.text.c_str(), &end);
        if (end != nullptr && *end == '\0')
        {
            token.type = TokenType::kNumber;
        }
        return token;
    }

    Token ScanIdentifier()
    {
        std::size_t start = position_;
        std::size_t start_line = line_;
        while (position_ < text_.size() && IsIdentifierChar(text_[position_]))
        {
            ++position_;
        }
        std::string value(text_.substr(start, position_ - start));
        if (LooksNumeric(value))
        {
            return MakeNumberToken(std::move(value), start_line);
        }
        Token token;
        token.type = TokenType::kIdentifier;
        token.text = std::move(value);
        token.line = start_line;
        return token;
    }

    Token Scan()
    {
        while (position_ < text_.size())
        {
            SkipWhitespace();
            if (position_ >= text_.size())
            {
                break;
            }
            if (text_[position_] == '/' && position_ + 1 < text_.size() && text_[position_ + 1] == '/')
            {
                SkipComment();
                continue;
            }
            break;
        }

        if (position_ >= text_.size())
        {
            Token token;
            token.type = TokenType::kEnd;
            token.line = line_;
            return token;
        }

        char ch = text_[position_++];
        switch (ch)
        {
        case '{':
        {
            Token token;
            token.type = TokenType::kLBrace;
            token.line = line_;
            return token;
        }
        case '}':
        {
            Token token;
            token.type = TokenType::kRBrace;
            token.line = line_;
            return token;
        }
        case '(':
        {
            Token token;
            token.type = TokenType::kLParen;
            token.line = line_;
            return token;
        }
        case ')':
        {
            Token token;
            token.type = TokenType::kRParen;
            token.line = line_;
            return token;
        }
        case '"':
            return ScanString();
        default:
            --position_;
            return ScanIdentifier();
        }
    }

    std::string_view text_;
    std::size_t position_;
    std::size_t line_;
    std::optional<Token> lookahead_;
};

float ExpectNumber(Lexer &lexer, ParseResult &result, std::string_view context)
{
    Token token = lexer.Consume();
    if (token.type != TokenType::kNumber)
    {
        log::Warning("expected numeric token for %.*s at line %zu", static_cast<int>(context.size()), context.data(), token.line);
        result.had_errors = true;
        return 0.0f;
    }
    return token.number;
}

bool ExpectToken(Lexer &lexer, TokenType expected, std::string_view context, ParseResult &result)
{
    Token token = lexer.Consume();
    if (token.type != expected)
    {
        log::Warning("expected %.*s at line %zu", static_cast<int>(context.size()), context.data(), token.line);
        result.had_errors = true;
        return false;
    }
    return true;
}

Vec3 ParseVertex(Lexer &lexer, ParseResult &result)
{
    Vec3 vertex{};
    vertex.x = ExpectNumber(lexer, result, "vertex x");
    vertex.y = ExpectNumber(lexer, result, "vertex y");
    vertex.z = ExpectNumber(lexer, result, "vertex z");
    if (!ExpectToken(lexer, TokenType::kRParen, ")", result))
    {
        // attempt recovery by consuming until we see ) or end
        return vertex;
    }
    return vertex;
}

bool ValidatePlane(const Plane &plane)
{
    const Vec3 &a = plane.vertices[0];
    const Vec3 &b = plane.vertices[1];
    const Vec3 &c = plane.vertices[2];
    const float ux = b.x - a.x;
    const float uy = b.y - a.y;
    const float uz = b.z - a.z;
    const float vx = c.x - a.x;
    const float vy = c.y - a.y;
    const float vz = c.z - a.z;
    const float nx = uy * vz - uz * vy;
    const float ny = uz * vx - ux * vz;
    const float nz = ux * vy - uy * vx;
    const float magnitude = std::sqrt(nx * nx + ny * ny + nz * nz);
    return magnitude > 1e-4f;
}

bool TextureImpliesLiquid(std::string_view texture)
{
    if (texture.empty())
    {
        return false;
    }
    char prefix = static_cast<char>(std::tolower(static_cast<unsigned char>(texture.front())));
    if (prefix == '*' || prefix == '!' || prefix == '~')
    {
        return true;
    }
    std::string candidate(texture);
    std::transform(candidate.begin(), candidate.end(), candidate.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    });

    return candidate.find("liquid") != std::string::npos || candidate.find("water") != std::string::npos ||
           candidate.find("lava") != std::string::npos || candidate.find("slime") != std::string::npos;
}

bool ParsePlane(Lexer &lexer, ParseResult &result, Brush &brush)
{
    Plane plane;
    plane.vertices[0] = ParseVertex(lexer, result);

    Token next = lexer.Peek();
    if (next.type != TokenType::kLParen)
    {
        log::Warning("expected '(' starting second vertex at line %zu", next.line);
        result.had_errors = true;
        return false;
    }
    lexer.Consume();
    plane.vertices[1] = ParseVertex(lexer, result);

    next = lexer.Peek();
    if (next.type != TokenType::kLParen)
    {
        log::Warning("expected '(' starting third vertex at line %zu", next.line);
        result.had_errors = true;
        return false;
    }
    lexer.Consume();
    plane.vertices[2] = ParseVertex(lexer, result);

    Token texture_token = lexer.Consume();
    if (texture_token.type != TokenType::kIdentifier && texture_token.type != TokenType::kString)
    {
        log::Warning("expected texture name at line %zu", texture_token.line);
        result.had_errors = true;
        return false;
    }
    plane.texture = texture_token.text;

    plane.shift[0] = ExpectNumber(lexer, result, "texture shift x");
    plane.shift[1] = ExpectNumber(lexer, result, "texture shift y");
    plane.rotation = ExpectNumber(lexer, result, "texture rotation");
    plane.scale[0] = ExpectNumber(lexer, result, "texture scale x");
    plane.scale[1] = ExpectNumber(lexer, result, "texture scale y");

    if (!ValidatePlane(plane))
    {
        log::Warning("degenerate plane detected for texture %s", plane.texture.c_str());
        result.had_errors = true;
        return false;
    }

    if (TextureImpliesLiquid(plane.texture))
    {
        brush.contains_liquid = true;
    }

    brush.planes.push_back(std::move(plane));
    return true;
}

bool ParseBrush(Lexer &lexer, ParseResult &result, Brush &brush)
{
    while (true)
    {
        Token token = lexer.Peek();
        if (token.type == TokenType::kRBrace)
        {
            lexer.Consume();
            break;
        }
        if (token.type == TokenType::kLParen)
        {
            lexer.Consume();
            if (!ParsePlane(lexer, result, brush))
            {
                // attempt to recover by skipping until closing brace
                while (true)
                {
                    Token skip = lexer.Consume();
                    if (skip.type == TokenType::kRBrace || skip.type == TokenType::kEnd)
                    {
                        return false;
                    }
                }
            }
            continue;
        }

        log::Warning("unexpected token inside brush at line %zu", token.line);
        result.had_errors = true;
        lexer.Consume();
    }
    return !brush.planes.empty();
}

bool ParseEntity(Lexer &lexer, ParseResult &result, Entity &entity)
{
    while (true)
    {
        Token token = lexer.Peek();
        if (token.type == TokenType::kRBrace)
        {
            lexer.Consume();
            break;
        }
        if (token.type == TokenType::kString)
        {
            Token key = lexer.Consume();
            Token value = lexer.Consume();
            if (value.type != TokenType::kString)
            {
                log::Warning("expected quoted value for key %s at line %zu", key.text.c_str(), value.line);
                result.had_errors = true;
                return false;
            }
            entity.properties.push_back({std::move(key.text), std::move(value.text)});
            continue;
        }
        if (token.type == TokenType::kLBrace)
        {
            lexer.Consume();
            Brush brush;
            if (ParseBrush(lexer, result, brush))
            {
                entity.brushes.push_back(std::move(brush));
            }
            continue;
        }

        log::Warning("unexpected token in entity at line %zu", token.line);
        result.had_errors = true;
        lexer.Consume();
    }
    return true;
}

void ApplyBrushMerging(ParseResult &result, const Options &options)
{
    if (options.nobrushmerge)
    {
        return;
    }
    result.preprocess.brush_merge_enabled = true;
    // Placeholder for future merging implementation.
}

void ApplyCsg(ParseResult &result, const Options &options)
{
    if (options.nocsg)
    {
        return;
    }
    result.preprocess.csg_enabled = true;
}

void ApplyLiquidFiltering(ParseResult &result, const Options &options)
{
    if (!options.noliquids)
    {
        return;
    }

    bool removed_any = false;
    for (Entity &entity : result.entities)
    {
        auto end = std::remove_if(entity.brushes.begin(), entity.brushes.end(), [](const Brush &brush) {
            return brush.contains_liquid;
        });
        if (end != entity.brushes.end())
        {
            entity.brushes.erase(end, entity.brushes.end());
            removed_any = true;
        }
    }
    if (removed_any)
    {
        result.preprocess.liquids_filtered = true;
    }
}

void ApplyBreathFirst(ParseResult &result, const Options &options)
{
    result.preprocess.breath_first = options.breath_first;
}

void UpdateSummary(ParseResult &result)
{
    std::set<std::string> unique_materials;
    std::size_t brushes = 0;
    std::size_t planes = 0;
    for (const Entity &entity : result.entities)
    {
        brushes += entity.brushes.size();
        for (const Brush &brush : entity.brushes)
        {
            planes += brush.planes.size();
            for (const Plane &plane : brush.planes)
            {
                if (!plane.texture.empty())
                {
                    unique_materials.insert(plane.texture);
                }
            }
        }
    }

    result.summary.entities = result.entities.size();
    result.summary.brushes = brushes;
    result.summary.planes = planes;
    result.summary.unique_materials = unique_materials.size();
}

} // namespace

std::optional<std::string_view> Entity::FindProperty(std::string_view key) const
{
    for (const KeyValue &kv : properties)
    {
        if (bspc::EqualsIgnoreCase(kv.key, key))
        {
            return kv.value;
        }
    }
    return std::nullopt;
}

ParseResult ParseText(std::string_view map_text, const Options &options, std::string_view source_name)
{
    ParseResult result;
    Lexer lexer(map_text);

    while (true)
    {
        Token token = lexer.Peek();
        if (token.type == TokenType::kEnd)
        {
            break;
        }
        if (token.type != TokenType::kLBrace)
        {
            log::Warning("expected '{' starting entity in %.*s at line %zu", static_cast<int>(source_name.size()), source_name.data(), token.line);
            result.had_errors = true;
            lexer.Consume();
            continue;
        }
        lexer.Consume();
        Entity entity;
        if (ParseEntity(lexer, result, entity))
        {
            result.entities.push_back(std::move(entity));
        }
    }

    ApplyBrushMerging(result, options);
    ApplyCsg(result, options);
    ApplyLiquidFiltering(result, options);
    ApplyBreathFirst(result, options);
    UpdateSummary(result);

    return result;
}

std::optional<ParseResult> ParseMapFromFile(const InputFile &input, const Options &options)
{
    if (input.from_archive)
    {
        log::Warning("archive-backed MAP parsing not yet supported for %s", input.original.c_str());
        return std::nullopt;
    }

    if (input.path.empty())
    {
        log::Warning("no path provided for MAP input %s", input.original.c_str());
        return std::nullopt;
    }

    std::ifstream stream(input.path, std::ios::binary);
    if (!stream)
    {
        log::Warning("failed to open MAP file %s", input.path.generic_string().c_str());
        return std::nullopt;
    }

    std::string contents((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    return ParseText(contents, options, input.path.generic_string());
}

} // namespace bspc::map

