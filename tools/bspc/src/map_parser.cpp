#include "map_parser.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "filesystem_helper.h"
#include "logging.hpp"

namespace bspc::map
{
namespace
{

constexpr float kPlaneEpsilon = 0.01f;
constexpr float kNormalEpsilon = 1e-4f;
constexpr float kBoundsLarge = 1.0e9f;

enum class TokenType
{
    kEnd,
    kString,
    kNumber,
    kIdentifier,
    kLBrace,
    kRBrace,
    kLParen,
    kRParen,
    kLBracket,
    kRBracket,
};

struct Token
{
    TokenType type = TokenType::kEnd;
    std::string text;
    double number = 0.0;
    std::size_t line = 1;
};

class Lexer
{
public:
    explicit Lexer(std::string_view text)
        : text_(text)
    {
    }

    Token Peek()
    {
        if (!lookahead_)
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

    std::size_t line() const
    {
        return line_;
    }

private:
    static bool IsWhitespace(char ch)
    {
        return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '\f';
    }

    static bool IsIdentifierChar(char ch)
    {
        if (IsWhitespace(ch))
        {
            return false;
        }

        switch (ch)
        {
        case '{':
        case '}':
        case '(': 
        case ')':
        case '[':
        case ']':
        case '"':
            return false;
        default:
            break;
        }
        return true;
    }

    void SkipWhitespaceAndComments()
    {
        while (position_ < text_.size())
        {
            char ch = text_[position_];
            if (IsWhitespace(ch))
            {
                if (ch == '\n')
                {
                    ++line_;
                }
                ++position_;
                continue;
            }

            if (ch == '/' && position_ + 1 < text_.size())
            {
                char next = text_[position_ + 1];
                if (next == '/')
                {
                    position_ += 2;
                    while (position_ < text_.size())
                    {
                        char comment = text_[position_++];
                        if (comment == '\n')
                        {
                            ++line_;
                            break;
                        }
                    }
                    continue;
                }
                if (next == '*')
                {
                    position_ += 2;
                    while (position_ + 1 < text_.size())
                    {
                        if (text_[position_] == '*' && text_[position_ + 1] == '/')
                        {
                            position_ += 2;
                            break;
                        }
                        if (text_[position_] == '\n')
                        {
                            ++line_;
                        }
                        ++position_;
                    }
                    continue;
                }
            }

            break;
        }
    }

    Token ScanString()
    {
        Token token;
        token.type = TokenType::kString;
        token.line = line_;
        ++position_;
        while (position_ < text_.size())
        {
            char ch = text_[position_++];
            if (ch == '\\' && position_ < text_.size())
            {
                char escaped = text_[position_++];
                switch (escaped)
                {
                case 'n': token.text.push_back('\n'); break;
                case 'r': token.text.push_back('\r'); break;
                case 't': token.text.push_back('\t'); break;
                case '\\': token.text.push_back('\\'); break;
                case '"': token.text.push_back('"'); break;
                default: token.text.push_back(escaped); break;
                }
                continue;
            }
            if (ch == '"')
            {
                return token;
            }
            if (ch == '\n')
            {
                ++line_;
            }
            token.text.push_back(ch);
        }
        return token;
    }

    static bool LooksNumeric(std::string_view text)
    {
        if (text.empty())
        {
            return false;
        }

        bool has_digit = false;
        bool has_decimal = false;
        bool has_exponent = false;
        for (std::size_t i = 0; i < text.size(); ++i)
        {
            const char ch = text[i];
            if (std::isdigit(static_cast<unsigned char>(ch)))
            {
                has_digit = true;
                continue;
            }
            if ((ch == '+' || ch == '-') && i == 0)
            {
                continue;
            }
            if (ch == '.' && !has_decimal)
            {
                has_decimal = true;
                continue;
            }
            if ((ch == 'e' || ch == 'E') && has_digit && !has_exponent)
            {
                has_exponent = true;
                has_digit = false;
                continue;
            }
            if ((ch == '+' || ch == '-') && (text[i - 1] == 'e' || text[i - 1] == 'E'))
            {
                continue;
            }
            return false;
        }
        return has_digit;
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
        Token token;
        token.text = std::move(value);
        token.line = start_line;
        if (LooksNumeric(token.text))
        {
            token.type = TokenType::kNumber;
            token.number = std::strtod(token.text.c_str(), nullptr);
        }
        else
        {
            token.type = TokenType::kIdentifier;
        }
        return token;
    }

    Token Scan()
    {
        SkipWhitespaceAndComments();
        if (position_ >= text_.size())
        {
            Token token;
            token.type = TokenType::kEnd;
            token.line = line_;
            return token;
        }

        const char ch = text_[position_++];
        Token token;
        token.line = line_;
        switch (ch)
        {
        case '{': token.type = TokenType::kLBrace; return token;
        case '}': token.type = TokenType::kRBrace; return token;
        case '(': token.type = TokenType::kLParen; return token;
        case ')': token.type = TokenType::kRParen; return token;
        case '[': token.type = TokenType::kLBracket; return token;
        case ']': token.type = TokenType::kRBracket; return token;
        case '"':
            --position_;
            return ScanString();
        default:
            --position_;
            return ScanIdentifier();
        }
    }

    std::string_view text_;
    std::size_t position_ = 0;
    std::size_t line_ = 1;
    std::optional<Token> lookahead_;
};

bool ApproximatelyEqual(float a, float b, float epsilon = kPlaneEpsilon)
{
    return std::fabs(a - b) <= epsilon;
}

Vec3 Cross(const Vec3 &a, const Vec3 &b)
{
    return Vec3{
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

float Length(const Vec3 &v)
{
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

Vec3 Normalize(const Vec3 &v)
{
    float len = Length(v);
    if (len < kNormalEpsilon)
    {
        return Vec3{0.0f, 0.0f, 0.0f};
    }
    return Vec3{v.x / len, v.y / len, v.z / len};
}

float Dot(const Vec3 &a, const Vec3 &b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

void UpdateBounds(Brush &brush, const Vec3 &point)
{
    if (!brush.has_bounds)
    {
        brush.mins = point;
        brush.maxs = point;
        brush.has_bounds = true;
        return;
    }

    brush.mins.x = std::min(brush.mins.x, point.x);
    brush.mins.y = std::min(brush.mins.y, point.y);
    brush.mins.z = std::min(brush.mins.z, point.z);

    brush.maxs.x = std::max(brush.maxs.x, point.x);
    brush.maxs.y = std::max(brush.maxs.y, point.y);
    brush.maxs.z = std::max(brush.maxs.z, point.z);
}

bool TextureImpliesLiquid(std::string_view name)
{
    if (name.empty())
    {
        return false;
    }

    std::string lower(name.begin(), name.end());
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    if (!lower.empty())
    {
        const char prefix = lower.front();
        if (prefix == '*' || prefix == '!' || prefix == '~')
        {
            return true;
        }
    }

    return lower.find("water") != std::string::npos ||
           lower.find("slime") != std::string::npos ||
           lower.find("lava") != std::string::npos ||
           lower.find("liquid") != std::string::npos ||
           lower.find("fog") != std::string::npos;
}

bool PlaneEquivalent(const Plane &a, const Plane &b)
{
    if (a.has_normal && b.has_normal)
    {
        if (!ApproximatelyEqual(a.distance, b.distance))
        {
            return false;
        }
        if (!ApproximatelyEqual(a.normal.x, b.normal.x, kNormalEpsilon) ||
            !ApproximatelyEqual(a.normal.y, b.normal.y, kNormalEpsilon) ||
            !ApproximatelyEqual(a.normal.z, b.normal.z, kNormalEpsilon))
        {
            return false;
        }
    }

    if (a.has_vertices && b.has_vertices)
    {
        for (int i = 0; i < 3; ++i)
        {
            bool matched = false;
            for (int j = 0; j < 3 && !matched; ++j)
            {
                const Vec3 &av = a.vertices[i];
                const Vec3 &bv = b.vertices[j];
                matched = ApproximatelyEqual(av.x, bv.x) &&
                          ApproximatelyEqual(av.y, bv.y) &&
                          ApproximatelyEqual(av.z, bv.z);
            }
            if (!matched)
            {
                return false;
            }
        }
    }

    return true;
}

bool BrushEquivalent(const Brush &a, const Brush &b)
{
    if (a.type != b.type || a.is_brush_primitive != b.is_brush_primitive)
    {
        return false;
    }

    if (a.type == Brush::Type::kPatch)
    {
        if (!a.patch || !b.patch)
        {
            return false;
        }
        const Patch &pa = *a.patch;
        const Patch &pb = *b.patch;
        if (!bspc::EqualsIgnoreCase(pa.texture, pb.texture) ||
            pa.width != pb.width ||
            pa.height != pb.height ||
            pa.points.size() != pb.points.size())
        {
            return false;
        }
        for (std::size_t i = 0; i < pa.points.size(); ++i)
        {
            const auto &ap = pa.points[i];
            const auto &bp = pb.points[i];
            if (!ApproximatelyEqual(ap.position.x, bp.position.x) ||
                !ApproximatelyEqual(ap.position.y, bp.position.y) ||
                !ApproximatelyEqual(ap.position.z, bp.position.z) ||
                !ApproximatelyEqual(ap.st[0], bp.st[0]) ||
                !ApproximatelyEqual(ap.st[1], bp.st[1]))
            {
                return false;
            }
        }
        return true;
    }

    if (a.planes.size() != b.planes.size())
    {
        return false;
    }

    for (const Plane &plane : a.planes)
    {
        bool found = false;
        for (const Plane &candidate : b.planes)
        {
            if (!bspc::EqualsIgnoreCase(plane.texture, candidate.texture))
            {
                continue;
            }
            if (PlaneEquivalent(plane, candidate))
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            return false;
        }
    }

    return true;
}

void ApplyBrushMerging(ParseResult &result, const Options &options);
void ApplyCsg(ParseResult &result, const Options &options);
void ApplyLiquidFiltering(ParseResult &result, const Options &options);
void ApplyBreathFirst(ParseResult &result, const Options &options);
void UpdateSummary(ParseResult &result);

class Parser
{
public:
    Parser(std::string_view text, const Options &options, std::string_view source)
        : lexer_(text),
          options_(options),
          source_name_(source)
    {
    }

    ParseResult Parse()
    {
        ParseResult result;
        result.had_errors = false;

        while (true)
        {
            Token token = lexer_.Peek();
            if (token.type == TokenType::kEnd)
            {
                break;
            }

            if (token.type != TokenType::kLBrace)
            {
                log::Warning("expected '{' starting entity in %.*s at line %zu", static_cast<int>(source_name_.size()),
                             source_name_.data(), token.line);
                result.had_errors = true;
                lexer_.Consume();
                continue;
            }

            lexer_.Consume();
            Entity entity;
            if (ParseEntity(entity, result))
            {
                result.entities.push_back(std::move(entity));
            }
        }

        ApplyBrushMerging(result, options_);
        ApplyCsg(result, options_);
        ApplyLiquidFiltering(result, options_);
        ApplyBreathFirst(result, options_);
        UpdateSummary(result);

        return result;
    }

private:
    bool ParseEntity(Entity &entity, ParseResult &result)
    {
        while (true)
        {
            Token token = lexer_.Peek();
            if (token.type == TokenType::kEnd)
            {
                log::Warning("unexpected end of file inside entity from %.*s", static_cast<int>(source_name_.size()),
                             source_name_.data());
                result.had_errors = true;
                return false;
            }

            if (token.type == TokenType::kRBrace)
            {
                lexer_.Consume();
                break;
            }

            if (token.type == TokenType::kString)
            {
                Token key = lexer_.Consume();
                Token value = lexer_.Consume();
                if (value.type != TokenType::kString)
                {
                    log::Warning("expected string property value for key %s at line %zu", key.text.c_str(), value.line);
                    result.had_errors = true;
                    return false;
                }
                entity.properties.push_back({std::move(key.text), std::move(value.text)});
                continue;
            }

            if (token.type == TokenType::kLBrace)
            {
                lexer_.Consume();
                Brush brush;
                if (ParseBrush(brush, result))
                {
                    entity.brushes.push_back(std::move(brush));
                }
                continue;
            }

            log::Warning("unexpected token while parsing entity at line %zu", token.line);
            lexer_.Consume();
            result.had_errors = true;
        }

        return true;
    }

    bool ParseBrush(Brush &brush, ParseResult &result)
    {
        Token next = lexer_.Peek();
        if (next.type == TokenType::kIdentifier)
        {
            std::string keyword = next.text;
            std::string lower = keyword;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });

            if (lower == "brushdef" || lower == "brushdef2" || lower == "brushdef3")
            {
                lexer_.Consume();
                brush.is_brush_primitive = true;
                return ParseBrushPrimitive(brush, result, lower);
            }
            if (lower == "patchdef2" || lower == "patchdef3")
            {
                lexer_.Consume();
                return ParsePatch(brush, result, lower == "patchdef2");
            }
        }

        return ParseLegacyBrush(brush, result);
    }

    bool ParseLegacyBrush(Brush &brush, ParseResult &result)
    {
        brush.type = Brush::Type::kSolid;
        while (true)
        {
            Token token = lexer_.Peek();
            if (token.type == TokenType::kRBrace)
            {
                lexer_.Consume();
                break;
            }
            if (token.type != TokenType::kLParen)
            {
                log::Warning("expected '(' starting brush plane at line %zu", token.line);
                result.had_errors = true;
                lexer_.Consume();
                continue;
            }

            Plane plane;
            if (!ParsePlaneVertices(plane, result))
            {
                ConsumeUntilBrushEnd();
                return false;
            }

            for (const Vec3 &vertex : plane.vertices)
            {
                UpdateBounds(brush, vertex);
            }

            Token texture = lexer_.Consume();
            if (texture.type != TokenType::kIdentifier && texture.type != TokenType::kString)
            {
                log::Warning("expected texture name after plane definition at line %zu", texture.line);
                result.had_errors = true;
                ConsumeUntilBrushEnd();
                return false;
            }
            plane.texture = texture.text;

            plane.shift[0] = static_cast<float>(ExpectNumber(result, "texture shift s"));
            plane.shift[1] = static_cast<float>(ExpectNumber(result, "texture shift t"));
            plane.rotation = static_cast<float>(ExpectNumber(result, "texture rotation"));
            plane.scale[0] = static_cast<float>(ExpectNumber(result, "texture scale s"));
            plane.scale[1] = static_cast<float>(ExpectNumber(result, "texture scale t"));

            Token extra = lexer_.Peek();
            if (extra.type == TokenType::kNumber)
            {
                plane.contents = static_cast<int>(lexer_.Consume().number);
                Token surf = lexer_.Consume();
                Token value = lexer_.Consume();
                if (surf.type != TokenType::kNumber || value.type != TokenType::kNumber)
                {
                    log::Warning("expected numeric surface flags/value at line %zu", surf.line);
                    result.had_errors = true;
                    ConsumeUntilBrushEnd();
                    return false;
                }
                plane.surface_flags = static_cast<int>(surf.number);
                plane.value = static_cast<int>(value.number);
            }

            if (TextureImpliesLiquid(plane.texture))
            {
                brush.contains_liquid = true;
            }

            brush.planes.push_back(std::move(plane));
        }

        brush.type = Brush::Type::kSolid;
        return !brush.planes.empty();
    }

    bool ParseBrushPrimitive(Brush &brush, ParseResult &result, std::string_view keyword)
    {
        brush.type = Brush::Type::kSolid;
        if (!ExpectToken(TokenType::kLBrace, "brush primitive open", result))
        {
            ConsumeUntilBrace();
            return false;
        }

        while (true)
        {
            Token token = lexer_.Peek();
            if (token.type == TokenType::kRBrace)
            {
                lexer_.Consume();
                break;
            }
            if (token.type == TokenType::kEnd)
            {
                log::Warning("unexpected end of file inside brush primitive");
                result.had_errors = true;
                return false;
            }
            if (token.type != TokenType::kLParen)
            {
                // skip unhandled tokens (such as epairs)
                lexer_.Consume();
                continue;
            }

            Plane plane;
            if (!ParsePlaneVertices(plane, result))
            {
                ConsumeUntilBrushPrimitiveEnd();
                return false;
            }

            for (const Vec3 &vertex : plane.vertices)
            {
                UpdateBounds(brush, vertex);
            }

            if (!ExpectToken(TokenType::kLParen, "texture matrix", result))
            {
                ConsumeUntilBrushPrimitiveEnd();
                return false;
            }
            if (!ExpectToken(TokenType::kLParen, "texture matrix row", result))
            {
                ConsumeUntilBrushPrimitiveEnd();
                return false;
            }
            for (int i = 0; i < 3; ++i)
            {
                plane.matrix[0][i] = static_cast<float>(ExpectNumber(result, "texture matrix s"));
            }
            if (!ExpectToken(TokenType::kRParen, "texture matrix row close", result))
            {
                ConsumeUntilBrushPrimitiveEnd();
                return false;
            }
            if (!ExpectToken(TokenType::kLParen, "texture matrix row", result))
            {
                ConsumeUntilBrushPrimitiveEnd();
                return false;
            }
            for (int i = 0; i < 3; ++i)
            {
                plane.matrix[1][i] = static_cast<float>(ExpectNumber(result, "texture matrix t"));
            }
            if (!ExpectToken(TokenType::kRParen, "texture matrix row close", result))
            {
                ConsumeUntilBrushPrimitiveEnd();
                return false;
            }
            if (!ExpectToken(TokenType::kRParen, "texture matrix close", result))
            {
                ConsumeUntilBrushPrimitiveEnd();
                return false;
            }
            plane.has_matrix = true;

            Token texture = lexer_.Consume();
            if (texture.type != TokenType::kIdentifier && texture.type != TokenType::kString)
            {
                log::Warning("expected texture name in brush primitive at line %zu", texture.line);
                result.had_errors = true;
                ConsumeUntilBrushPrimitiveEnd();
                return false;
            }
            plane.texture = texture.text;

            Token maybe_number = lexer_.Peek();
            if (maybe_number.type == TokenType::kNumber)
            {
                plane.contents = static_cast<int>(lexer_.Consume().number);
                Token surf = lexer_.Consume();
                Token value = lexer_.Consume();
                if (surf.type != TokenType::kNumber || value.type != TokenType::kNumber)
                {
                    log::Warning("expected numeric surface flags/value at line %zu", surf.line);
                    result.had_errors = true;
                    ConsumeUntilBrushPrimitiveEnd();
                    return false;
                }
                plane.surface_flags = static_cast<int>(surf.number);
                plane.value = static_cast<int>(value.number);
            }

            Token closing = lexer_.Peek();
            if (closing.type == TokenType::kRParen)
            {
                lexer_.Consume();
            }

            if (TextureImpliesLiquid(plane.texture))
            {
                brush.contains_liquid = true;
            }

            brush.planes.push_back(std::move(plane));
        }

        if (!ExpectToken(TokenType::kRBrace, "brush primitive close", result))
        {
            return false;
        }

        return !brush.planes.empty();
    }

    bool ParsePatch(Brush &brush, ParseResult &result, bool old_format)
    {
        brush.type = Brush::Type::kPatch;
        if (!ExpectToken(TokenType::kLBrace, "patch open", result))
        {
            ConsumeUntilBrace();
            return false;
        }

        Patch patch;
        Token texture = lexer_.Consume();
        if (texture.type != TokenType::kIdentifier && texture.type != TokenType::kString)
        {
            log::Warning("expected texture name for patch at line %zu", texture.line);
            result.had_errors = true;
            ConsumeUntilPatchEnd();
            return false;
        }
        patch.texture = texture.text;

        if (!ExpectToken(TokenType::kLParen, "patch dimensions", result))
        {
            ConsumeUntilPatchEnd();
            return false;
        }

        patch.width = static_cast<int>(ExpectNumber(result, "patch width"));
        patch.height = static_cast<int>(ExpectNumber(result, "patch height"));
        patch.contents = static_cast<int>(ExpectNumber(result, "patch contents"));
        patch.surface_flags = static_cast<int>(ExpectNumber(result, "patch surface"));
        patch.value = static_cast<int>(ExpectNumber(result, "patch value"));
        if (!old_format)
        {
            patch.type = static_cast<int>(ExpectNumber(result, "patch type"));
        }
        if (!ExpectToken(TokenType::kRParen, "patch dimensions close", result))
        {
            ConsumeUntilPatchEnd();
            return false;
        }

        if (!ExpectToken(TokenType::kLParen, "patch matrix", result))
        {
            ConsumeUntilPatchEnd();
            return false;
        }

        patch.points.reserve(static_cast<std::size_t>(patch.width * patch.height));
        for (int y = 0; y < patch.height; ++y)
        {
            if (!ExpectToken(TokenType::kLParen, "patch row", result))
            {
                ConsumeUntilPatchEnd();
                return false;
            }
            for (int x = 0; x < patch.width; ++x)
            {
                if (!ExpectToken(TokenType::kLParen, "patch control point", result))
                {
                    ConsumeUntilPatchEnd();
                    return false;
                }

                PatchPoint point;
                point.position.x = static_cast<float>(ExpectNumber(result, "patch x"));
                point.position.y = static_cast<float>(ExpectNumber(result, "patch y"));
                point.position.z = static_cast<float>(ExpectNumber(result, "patch z"));
                point.st[0] = static_cast<float>(ExpectNumber(result, "patch s"));
                point.st[1] = static_cast<float>(ExpectNumber(result, "patch t"));

                if (!ExpectToken(TokenType::kRParen, "patch control point close", result))
                {
                    ConsumeUntilPatchEnd();
                    return false;
                }

                patch.points.push_back(point);
            }
            if (!ExpectToken(TokenType::kRParen, "patch row close", result))
            {
                ConsumeUntilPatchEnd();
                return false;
            }
        }

        if (!ExpectToken(TokenType::kRParen, "patch matrix close", result))
        {
            ConsumeUntilPatchEnd();
            return false;
        }

        Token closing = lexer_.Peek();
        if (closing.type == TokenType::kRBrace)
        {
            lexer_.Consume();
        }
        if (!ExpectToken(TokenType::kRBrace, "patch outer close", result))
        {
            return false;
        }

        if (TextureImpliesLiquid(patch.texture))
        {
            brush.contains_liquid = true;
        }

        brush.patch = std::move(patch);
        return true;
    }

    bool ParsePlaneVertices(Plane &plane, ParseResult &result)
    {
    if (!ExpectToken(TokenType::kLParen, "plane vertex", result))
    {
        return false;
    }

    for (int i = 0; i < 3; ++i)
    {
        Token maybe_inner = lexer_.Peek();
        if (maybe_inner.type == TokenType::kLParen)
        {
            lexer_.Consume();
        }
        plane.vertices[i].x = static_cast<float>(ExpectNumber(result, "vertex x"));
        plane.vertices[i].y = static_cast<float>(ExpectNumber(result, "vertex y"));
        plane.vertices[i].z = static_cast<float>(ExpectNumber(result, "vertex z"));
        if (!ExpectToken(TokenType::kRParen, ")", result))
        {
            return false;
        }
        Token maybe_outer_close = lexer_.Peek();
        if (maybe_outer_close.type == TokenType::kRParen && i != 2)
        {
            lexer_.Consume();
        }
        if (i != 2)
        {
            if (!ExpectToken(TokenType::kLParen, "plane vertex", result))
            {
                return false;
                }
            }
        }

        Vec3 u{plane.vertices[1].x - plane.vertices[0].x,
                plane.vertices[1].y - plane.vertices[0].y,
                plane.vertices[1].z - plane.vertices[0].z};
        Vec3 v{plane.vertices[2].x - plane.vertices[0].x,
                plane.vertices[2].y - plane.vertices[0].y,
                plane.vertices[2].z - plane.vertices[0].z};
        Vec3 normal = Normalize(Cross(u, v));
        const float distance = Dot(normal, plane.vertices[0]);
        plane.normal = normal;
        plane.distance = distance;
        plane.has_vertices = true;
        plane.has_normal = true;

        return true;
    }

    bool ExpectToken(TokenType expected, std::string_view context, ParseResult &result)
    {
        Token token = lexer_.Consume();
        if (token.type != expected)
        {
            log::Warning("expected %.*s near line %zu", static_cast<int>(context.size()), context.data(), token.line);
            result.had_errors = true;
            return false;
        }
        return true;
    }

    double ExpectNumber(ParseResult &result, std::string_view context)
    {
        Token token = lexer_.Consume();
        if (token.type != TokenType::kNumber)
        {
            log::Warning("expected numeric value for %.*s at line %zu", static_cast<int>(context.size()), context.data(),
                         token.line);
            result.had_errors = true;
            return 0.0;
        }
        return token.number;
    }

    void ConsumeUntilBrushEnd()
    {
        while (true)
        {
            Token token = lexer_.Consume();
            if (token.type == TokenType::kRBrace || token.type == TokenType::kEnd)
            {
                break;
            }
        }
    }

    void ConsumeUntilBrushPrimitiveEnd()
    {
        int depth = 0;
        while (true)
        {
            Token token = lexer_.Consume();
            if (token.type == TokenType::kEnd)
            {
                break;
            }
            if (token.type == TokenType::kLBrace)
            {
                ++depth;
            }
            else if (token.type == TokenType::kRBrace)
            {
                if (depth == 0)
                {
                    break;
                }
                --depth;
            }
        }
    }

    void ConsumeUntilPatchEnd()
    {
        int depth = 0;
        while (true)
        {
            Token token = lexer_.Consume();
            if (token.type == TokenType::kEnd)
            {
                break;
            }
            if (token.type == TokenType::kLBrace)
            {
                ++depth;
            }
            else if (token.type == TokenType::kRBrace)
            {
                if (depth == 0)
                {
                    break;
                }
                --depth;
            }
        }
    }

    void ConsumeUntilBrace()
    {
        while (true)
        {
            Token token = lexer_.Consume();
            if (token.type == TokenType::kLBrace || token.type == TokenType::kRBrace || token.type == TokenType::kEnd)
            {
                break;
            }
        }
    }

    Lexer lexer_;
    const Options &options_;
    std::string source_name_;
};

bool BrushBoundingBoxContains(const Brush &outer, const Brush &inner)
{
    if (!outer.has_bounds || !inner.has_bounds)
    {
        return false;
    }

    const bool contains = outer.mins.x - kPlaneEpsilon <= inner.mins.x &&
                           outer.mins.y - kPlaneEpsilon <= inner.mins.y &&
                           outer.mins.z - kPlaneEpsilon <= inner.mins.z &&
                           outer.maxs.x + kPlaneEpsilon >= inner.maxs.x &&
                           outer.maxs.y + kPlaneEpsilon >= inner.maxs.y &&
                           outer.maxs.z + kPlaneEpsilon >= inner.maxs.z;
    if (!contains)
    {
        return false;
    }

    const bool strict = (outer.mins.x + kPlaneEpsilon < inner.mins.x) ||
                        (outer.mins.y + kPlaneEpsilon < inner.mins.y) ||
                        (outer.mins.z + kPlaneEpsilon < inner.mins.z) ||
                        (outer.maxs.x - kPlaneEpsilon > inner.maxs.x) ||
                        (outer.maxs.y - kPlaneEpsilon > inner.maxs.y) ||
                        (outer.maxs.z - kPlaneEpsilon > inner.maxs.z);
    return strict;
}

void ApplyBrushMerging(ParseResult &result, const Options &options);
void ApplyCsg(ParseResult &result, const Options &options);
void ApplyLiquidFiltering(ParseResult &result, const Options &options);
void ApplyBreathFirst(ParseResult &result, const Options &options);
void UpdateSummary(ParseResult &result);

void ApplyBrushMerging(ParseResult &result, const Options &options)
{
    if (options.nobrushmerge)
    {
        return;
    }

    bool merged_any = false;
    for (Entity &entity : result.entities)
    {
        std::vector<Brush> unique;
        unique.reserve(entity.brushes.size());
        for (Brush &brush : entity.brushes)
        {
            bool duplicate = false;
            for (const Brush &candidate : unique)
            {
                if (BrushEquivalent(candidate, brush))
                {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate)
            {
                merged_any = true;
                continue;
            }
            unique.push_back(std::move(brush));
        }
        entity.brushes.swap(unique);
    }

    if (merged_any)
    {
        result.preprocess.brush_merge_enabled = true;
    }
}

void ApplyCsg(ParseResult &result, const Options &options)
{
    if (options.nocsg)
    {
        return;
    }

    bool removed = false;
    for (Entity &entity : result.entities)
    {
        std::vector<Brush> filtered;
        filtered.reserve(entity.brushes.size());
        for (std::size_t i = 0; i < entity.brushes.size(); ++i)
        {
            bool culled = false;
            const Brush &candidate = entity.brushes[i];
            if (candidate.type == Brush::Type::kSolid)
            {
                for (std::size_t j = 0; j < entity.brushes.size(); ++j)
                {
                    if (i == j)
                    {
                        continue;
                    }
                    const Brush &other = entity.brushes[j];
                    if (other.type != Brush::Type::kSolid)
                    {
                        continue;
                    }
                    if (BrushBoundingBoxContains(other, candidate) && other.planes.size() >= candidate.planes.size())
                    {
                        culled = true;
                        removed = true;
                        break;
                    }
                }
            }
            if (!culled)
            {
                filtered.push_back(candidate);
            }
        }
        entity.brushes.swap(filtered);
    }

    if (removed)
    {
        result.preprocess.csg_enabled = true;
    }
}

void ApplyLiquidFiltering(ParseResult &result, const Options &options)
{
    if (!options.noliquids)
    {
        return;
    }

    bool filtered_any = false;
    for (Entity &entity : result.entities)
    {
        std::vector<Brush> filtered;
        filtered.reserve(entity.brushes.size());
        for (Brush &brush : entity.brushes)
        {
            if (brush.contains_liquid)
            {
                filtered_any = true;
                continue;
            }
            filtered.push_back(std::move(brush));
        }
        entity.brushes.swap(filtered);
    }

    if (filtered_any)
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
    std::size_t total_brushes = 0;
    std::size_t total_planes = 0;

    for (const Entity &entity : result.entities)
    {
        total_brushes += entity.brushes.size();
        for (const Brush &brush : entity.brushes)
        {
            if (brush.type == Brush::Type::kPatch)
            {
                if (brush.patch && !brush.patch->texture.empty())
                {
                    unique_materials.insert(brush.patch->texture);
                }
                continue;
            }
            total_planes += brush.planes.size();
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
    result.summary.brushes = total_brushes;
    result.summary.planes = total_planes;
    result.summary.unique_materials = unique_materials.size();
}

} // namespace

std::optional<std::string_view> Entity::FindProperty(std::string_view key) const
{
    for (const KeyValue &kv : properties)
    {
        if (bspc::EqualsIgnoreCase(kv.key, key))
        {
            return std::string_view(kv.value);
        }
    }
    return std::nullopt;
}

ParseResult ParseText(std::string_view map_text, const Options &options, std::string_view source_name)
{
    Parser parser(map_text, options, source_name);
    return parser.Parse();
}

std::optional<ParseResult> ParseMapFromFile(const InputFile &input, const Options &options)
{
    std::vector<std::byte> buffer;
    if (!ReadFile(input, buffer, true))
    {
        log::Warning("failed to read MAP source %s", input.original.c_str());
        return std::nullopt;
    }

    std::string text(buffer.size(), '\0');
    for (std::size_t i = 0; i < buffer.size(); ++i)
    {
        text[i] = static_cast<char>(std::to_integer<unsigned char>(buffer[i]));
    }

    return ParseText(text, options, input.original);
}

} // namespace bspc::map
