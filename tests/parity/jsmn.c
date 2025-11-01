#define JSMN_PARENT_LINKS

#include "jsmn.h"

#include <limits.h>

static int jsmn_parse_primitive(jsmn_parser *parser, const char *js, size_t len, jsmntok_t *tokens, unsigned int num_tokens);
static int jsmn_parse_string(jsmn_parser *parser, const char *js, size_t len, jsmntok_t *tokens, unsigned int num_tokens);

static jsmntok_t *jsmn_alloc_token(jsmn_parser *parser, jsmntok_t *tokens, unsigned int num_tokens)
{
    if (parser->toknext >= num_tokens)
    {
        return NULL;
    }

    jsmntok_t *tok = &tokens[parser->toknext++];
    tok->start = tok->end = -1;
    tok->size = 0;
#ifdef JSMN_PARENT_LINKS
    tok->parent = -1;
#endif
    return tok;
}

static void jsmn_fill_token(jsmntok_t *token, jsmntype_t type, int start, int end)
{
    token->type = type;
    token->start = start;
    token->end = end;
    token->size = 0;
}

void jsmn_init(jsmn_parser *parser)
{
    parser->pos = 0;
    parser->toknext = 0;
    parser->toksuper = -1;
}

static int jsmn_parse_primitive(jsmn_parser *parser, const char *js, size_t len, jsmntok_t *tokens, unsigned int num_tokens)
{
    int start = (int)parser->pos;

    for (; parser->pos < len && js[parser->pos] != '\0'; parser->pos++)
    {
        char c = js[parser->pos];
        if (c == ':' || c == '\t' || c == '\r' || c == '\n' || c == ' ' || c == ',' || c == ']' || c == '}')
        {
            break;
        }
        if (c < 32 || c >= 127)
        {
            parser->pos = (unsigned int)start;
            return JSMN_ERROR_INVAL;
        }
    }

    if (tokens == NULL)
    {
        parser->pos--;
        return 0;
    }

    jsmntok_t *token = jsmn_alloc_token(parser, tokens, num_tokens);
    if (token == NULL)
    {
        parser->pos = (unsigned int)start;
        return JSMN_ERROR_NOMEM;
    }
    jsmn_fill_token(token, JSMN_PRIMITIVE, start, (int)parser->pos);

    parser->pos--;
    return 0;
}

static int jsmn_parse_string(jsmn_parser *parser, const char *js, size_t len, jsmntok_t *tokens, unsigned int num_tokens)
{
    int start = (int)parser->pos;

    parser->pos++;

    for (; parser->pos < len && js[parser->pos] != '\0'; parser->pos++)
    {
        char c = js[parser->pos];

        if (c == '"')
        {
            if (tokens == NULL)
            {
                return 0;
            }

            jsmntok_t *token = jsmn_alloc_token(parser, tokens, num_tokens);
            if (token == NULL)
            {
                parser->pos = (unsigned int)start;
                return JSMN_ERROR_NOMEM;
            }
            jsmn_fill_token(token, JSMN_STRING, start + 1, (int)parser->pos);
#ifdef JSMN_PARENT_LINKS
            token->parent = parser->toksuper;
#endif
            return 0;
        }

        if (c == '\\')
        {
            parser->pos++;
            if (parser->pos == len)
            {
                parser->pos = (unsigned int)start;
                return JSMN_ERROR_PART;
            }
            switch (js[parser->pos])
            {
                case '"':
                case '/':
                case '\\':
                case 'b':
                case 'f':
                case 'r':
                case 'n':
                case 't':
                    break;
                case 'u':
                    for (int i = 0; i < 4; i++)
                    {
                        if (js[parser->pos + 1] == '\0')
                        {
                            parser->pos = (unsigned int)start;
                            return JSMN_ERROR_PART;
                        }
                        char ch = js[parser->pos + 1];
                        if (!((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F') || (ch >= 'a' && ch <= 'f')))
                        {
                            parser->pos = (unsigned int)start;
                            return JSMN_ERROR_INVAL;
                        }
                        parser->pos++;
                    }
                    break;
                default:
                    parser->pos = (unsigned int)start;
                    return JSMN_ERROR_INVAL;
            }
        }
    }

    parser->pos = (unsigned int)start;
    return JSMN_ERROR_PART;
}

int jsmn_parse(jsmn_parser *parser, const char *js, size_t len, jsmntok_t *tokens, unsigned int num_tokens)
{
    int count = parser->toknext;

    for (; parser->pos < len && js[parser->pos] != '\0'; parser->pos++)
    {
        char c = js[parser->pos];
        jsmntok_t *token;
        int r;

        switch (c)
        {
            case '{':
            case '[':
                count++;
                if (tokens == NULL)
                {
                    break;
                }
                token = jsmn_alloc_token(parser, tokens, num_tokens);
                if (token == NULL)
                {
                    return JSMN_ERROR_NOMEM;
                }
                if (parser->toksuper != -1)
                {
                    tokens[parser->toksuper].size++;
#ifdef JSMN_PARENT_LINKS
                    token->parent = parser->toksuper;
#endif
                }
                token->type = (c == '{') ? JSMN_OBJECT : JSMN_ARRAY;
                token->start = (int)parser->pos;
                parser->toksuper = (int)(parser->toknext - 1);
                break;
            case '}':
            case ']':
                if (tokens == NULL)
                {
                    break;
                }
                token = &tokens[parser->toknext - 1];
                while (1)
                {
                    if (token->start != -1 && token->end == -1)
                    {
                        token->end = (int)parser->pos + 1;
                        parser->toksuper =
                            token->parent;
                        break;
                    }
                    if (token->parent == -1)
                    {
                        if (token->type != JSMN_OBJECT && token->type != JSMN_ARRAY)
                        {
                            return JSMN_ERROR_INVAL;
                        }
                        break;
                    }
                    token = &tokens[token->parent];
                }
                break;
            case '"':
                r = jsmn_parse_string(parser, js, len, tokens, num_tokens);
                if (r < 0)
                {
                    return r;
                }
                count++;
                if (parser->toksuper != -1 && tokens != NULL)
                {
                    tokens[parser->toksuper].size++;
                }
                break;
            case '\t':
            case '\r':
            case '\n':
            case ' ':
            case ':':
            case ',':
                break;
            case '-':
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
            case 't':
            case 'f':
            case 'n':
                r = jsmn_parse_primitive(parser, js, len, tokens, num_tokens);
                if (r < 0)
                {
                    return r;
                }
                count++;
                if (parser->toksuper != -1 && tokens != NULL)
                {
                    tokens[parser->toksuper].size++;
                }
                break;
            default:
                return JSMN_ERROR_INVAL;
        }
    }

    return count;
}
