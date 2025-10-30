#include "l_precomp.h"
#include "l_script.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct token_array_s {
    pc_token_t *data;
    size_t count;
    size_t capacity;
} token_array_t;

typedef struct string_buffer_s {
    char *data;
    size_t length;
    size_t capacity;
} string_buffer_t;

struct pc_source_s {
    char *preprocessed_buffer;
    token_array_t tokens;
    size_t cursor;
    pc_diagnostic_t *diagnostics;
};

static void token_array_init(token_array_t *array)
{
    array->data = NULL;
    array->count = 0;
    array->capacity = 0;
}

static void token_array_destroy(token_array_t *array)
{
    free(array->data);
    array->data = NULL;
    array->count = 0;
    array->capacity = 0;
}

static bool token_array_reserve(token_array_t *array, size_t additional)
{
    if (!array) {
        return false;
    }

    if (array->count + additional <= array->capacity) {
        return true;
    }

    size_t new_capacity = array->capacity ? array->capacity : 64;
    while (new_capacity < array->count + additional) {
        new_capacity *= 2;
    }

    pc_token_t *new_data = (pc_token_t *)realloc(array->data, new_capacity * sizeof(pc_token_t));
    if (!new_data) {
        return false;
    }

    array->data = new_data;
    array->capacity = new_capacity;
    return true;
}

static bool token_array_push(token_array_t *array, const pc_token_t *token)
{
    if (!array || !token) {
        return false;
    }

    if (!token_array_reserve(array, 1)) {
        return false;
    }

    array->data[array->count++] = *token;
    return true;
}

static void string_buffer_init(string_buffer_t *buffer)
{
    buffer->data = NULL;
    buffer->length = 0;
    buffer->capacity = 0;
}

static void string_buffer_destroy(string_buffer_t *buffer)
{
    free(buffer->data);
    buffer->data = NULL;
    buffer->length = 0;
    buffer->capacity = 0;
}

static bool string_buffer_reserve(string_buffer_t *buffer, size_t additional)
{
    if (!buffer) {
        return false;
    }

    if (buffer->length + additional <= buffer->capacity) {
        return true;
    }

    size_t new_capacity = buffer->capacity ? buffer->capacity : 256;
    while (new_capacity < buffer->length + additional) {
        new_capacity *= 2;
    }

    char *new_data = (char *)realloc(buffer->data, new_capacity);
    if (!new_data) {
        return false;
    }

    buffer->data = new_data;
    buffer->capacity = new_capacity;
    return true;
}

static bool string_buffer_append(string_buffer_t *buffer, const char *text, size_t length)
{
    if (!buffer || (!text && length > 0)) {
        return false;
    }

    if (!string_buffer_reserve(buffer, length)) {
        return false;
    }

    if (text && length > 0) {
        memcpy(buffer->data + buffer->length, text, length);
    }
    buffer->length += length;
    return true;
}

static bool string_buffer_append_char(string_buffer_t *buffer, char ch)
{
    if (!buffer) {
        return false;
    }

    if (!string_buffer_reserve(buffer, 1)) {
        return false;
    }

    buffer->data[buffer->length++] = ch;
    return true;
}

static void string_buffer_null_terminate(string_buffer_t *buffer)
{
    if (!buffer) {
        return;
    }

    if (!string_buffer_reserve(buffer, 1)) {
        return;
    }

    buffer->data[buffer->length] = '\0';
}

static bool append_quoted_argument(string_buffer_t *buffer, const char *text)
{
    if (!buffer || !text) {
        return false;
    }

    if (!string_buffer_append_char(buffer, '\'')) {
        return false;
    }

    for (const char *p = text; *p; ++p) {
        if (*p == '\'') {
            if (!string_buffer_append(buffer, "'\\''", 4)) {
                return false;
            }
        } else {
            if (!string_buffer_append_char(buffer, *p)) {
                return false;
            }
        }
    }

    return string_buffer_append_char(buffer, '\'');
}

static bool directory_from_path(const char *path, char *buffer, size_t buffer_size)
{
    if (!path || !buffer || buffer_size == 0) {
        return false;
    }

    const char *last_slash = strrchr(path, '/');
#if defined(_WIN32) || defined(__CYGWIN__)
    const char *last_backslash = strrchr(path, '\\');
    if (!last_slash || (last_backslash && last_backslash > last_slash)) {
        last_slash = last_backslash;
    }
#endif
    if (!last_slash) {
        if (buffer_size < 2) {
            return false;
        }
        buffer[0] = '.';
        buffer[1] = '\0';
        return true;
    }

    size_t length = (size_t)(last_slash - path);
    if (length >= buffer_size) {
        length = buffer_size - 1;
    }

    memcpy(buffer, path, length);
    buffer[length] = '\0';
    return true;
}

static bool asset_root_from_path(const char *path, char *buffer, size_t buffer_size)
{
    if (!path || !buffer || buffer_size == 0) {
        return false;
    }

    const char *needle = strstr(path, "dev_tools/");
    if (!needle) {
        needle = strstr(path, "dev-tools/");
    }
    if (!needle) {
        return false;
    }

    const char *assets = strstr(needle, "assets");
    if (!assets) {
        return false;
    }

    const char *end = assets + strlen("assets");
    size_t length = (size_t)(end - path);
    if (length >= buffer_size) {
        length = buffer_size - 1;
    }

    memcpy(buffer, path, length);
    buffer[length] = '\0';
    return true;
}

static bool run_cpp_on_file(const char *path, char **output)
{
    if (output) {
        *output = NULL;
    }

    if (!path || !output) {
        return false;
    }

    char include_dir[512];
    directory_from_path(path, include_dir, sizeof(include_dir));

    char asset_root[512];
    bool have_asset_root = asset_root_from_path(path, asset_root, sizeof(asset_root));

    string_buffer_t command;
    string_buffer_init(&command);

    bool ok = string_buffer_append(&command, "cpp -P -nostdinc -undef -DDMFLAGS=0 ", strlen("cpp -P -nostdinc -undef -DDMFLAGS=0 "));
    if (ok) {
        ok = string_buffer_append(&command, "-Dbalance(a,b,c)=balance(a,b,c) ", strlen("-Dbalance(a,b,c)=balance(a,b,c) "));
    }

    if (ok) {
        ok = string_buffer_append(&command, "-I", 2);
    }
    if (ok) {
        ok = append_quoted_argument(&command, include_dir);
    }
    if (ok) {
        ok = string_buffer_append(&command, " ", 1);
    }

    if (ok && have_asset_root) {
        ok = string_buffer_append(&command, "-I", 2);
        if (ok) {
            ok = append_quoted_argument(&command, asset_root);
        }
        if (ok) {
            ok = string_buffer_append(&command, " ", 1);
        }
    }

    if (ok) {
        ok = append_quoted_argument(&command, path);
    }

    if (!ok) {
        string_buffer_destroy(&command);
        return false;
    }

    string_buffer_null_terminate(&command);

    FILE *pipe = popen(command.data, "r");
    string_buffer_destroy(&command);
    if (!pipe) {
        return false;
    }

    string_buffer_t result;
    string_buffer_init(&result);

    char buffer[4096];
    size_t read_bytes = 0;
    while ((read_bytes = fread(buffer, 1, sizeof(buffer), pipe)) > 0) {
        if (!string_buffer_append(&result, buffer, read_bytes)) {
            string_buffer_destroy(&result);
            pclose(pipe);
            return false;
        }
    }

    pclose(pipe);
    string_buffer_null_terminate(&result);

    *output = result.data;
    return true;
}

typedef struct eval_stream_s {
    const char *text;
    size_t length;
    size_t position;
} eval_stream_t;

static void eval_stream_skip_whitespace(eval_stream_t *stream)
{
    if (!stream) {
        return;
    }

    while (stream->position < stream->length) {
        char ch = stream->text[stream->position];
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            stream->position++;
        } else {
            break;
        }
    }
}

static bool eval_stream_match(eval_stream_t *stream, const char *token)
{
    if (!stream || !token) {
        return false;
    }

    size_t len = strlen(token);
    if (stream->position + len > stream->length) {
        return false;
    }

    if (strncmp(stream->text + stream->position, token, len) == 0) {
        stream->position += len;
        return true;
    }

    return false;
}

static long long eval_to_integer(double value)
{
    if (value >= 0.0) {
        return (long long)(value + 0.5);
    }
    return (long long)(value - 0.5);
}

static double parse_expression(eval_stream_t *stream);

static double parse_primary(eval_stream_t *stream)
{
    eval_stream_skip_whitespace(stream);
    if (stream->position >= stream->length) {
        return 0.0;
    }

    char ch = stream->text[stream->position];
    if (ch == '(') {
        stream->position++;
        double value = parse_expression(stream);
        eval_stream_skip_whitespace(stream);
        if (stream->position < stream->length && stream->text[stream->position] == ')') {
            stream->position++;
        }
        return value;
    }

    if (isdigit((unsigned char)ch) || ch == '.') {
        const char *start = stream->text + stream->position;
        char *end = NULL;
        if (stream->position + 2 <= stream->length && stream->text[stream->position] == '0' &&
            (stream->text[stream->position + 1] == 'x' || stream->text[stream->position + 1] == 'X')) {
            unsigned long long value = strtoull(start, &end, 0);
            stream->position = (size_t)(end - stream->text);
            return (double)value;
        }
        double value = strtod(start, &end);
        stream->position = (size_t)(end - stream->text);
        return value;
    }

    return 0.0;
}

static double parse_unary(eval_stream_t *stream)
{
    eval_stream_skip_whitespace(stream);
    if (stream->position >= stream->length) {
        return 0.0;
    }

    if (eval_stream_match(stream, "+")) {
        return parse_unary(stream);
    }
    if (eval_stream_match(stream, "-")) {
        return -parse_unary(stream);
    }
    if (eval_stream_match(stream, "!")) {
        return parse_unary(stream) == 0.0 ? 1.0 : 0.0;
    }
    if (eval_stream_match(stream, "~")) {
        long long value = eval_to_integer(parse_unary(stream));
        return (double)(~value);
    }

    return parse_primary(stream);
}

static double parse_multiplicative(eval_stream_t *stream)
{
    double value = parse_unary(stream);
    while (true) {
        eval_stream_skip_whitespace(stream);
        if (eval_stream_match(stream, "*")) {
            value *= parse_unary(stream);
        } else if (eval_stream_match(stream, "/")) {
            double divisor = parse_unary(stream);
            if (divisor != 0.0) {
                value /= divisor;
            }
        } else if (eval_stream_match(stream, "%")) {
            long long lhs = eval_to_integer(value);
            long long rhs = eval_to_integer(parse_unary(stream));
            if (rhs != 0) {
                value = (double)(lhs % rhs);
            }
        } else {
            break;
        }
    }
    return value;
}

static double parse_additive(eval_stream_t *stream)
{
    double value = parse_multiplicative(stream);
    while (true) {
        eval_stream_skip_whitespace(stream);
        if (eval_stream_match(stream, "+")) {
            value += parse_multiplicative(stream);
        } else if (eval_stream_match(stream, "-")) {
            value -= parse_multiplicative(stream);
        } else {
            break;
        }
    }
    return value;
}

static double parse_shift(eval_stream_t *stream)
{
    double value = parse_additive(stream);
    while (true) {
        eval_stream_skip_whitespace(stream);
        if (eval_stream_match(stream, "<<")) {
            long long lhs = eval_to_integer(value);
            long long rhs = eval_to_integer(parse_additive(stream));
            value = (double)(lhs << rhs);
        } else if (eval_stream_match(stream, ">>")) {
            long long lhs = eval_to_integer(value);
            long long rhs = eval_to_integer(parse_additive(stream));
            value = (double)(lhs >> rhs);
        } else {
            break;
        }
    }
    return value;
}

static double parse_relational(eval_stream_t *stream)
{
    double value = parse_shift(stream);
    while (true) {
        eval_stream_skip_whitespace(stream);
        if (eval_stream_match(stream, "<=")) {
            double rhs = parse_shift(stream);
            value = value <= rhs ? 1.0 : 0.0;
        } else if (eval_stream_match(stream, ">=")) {
            double rhs = parse_shift(stream);
            value = value >= rhs ? 1.0 : 0.0;
        } else if (eval_stream_match(stream, "<")) {
            double rhs = parse_shift(stream);
            value = value < rhs ? 1.0 : 0.0;
        } else if (eval_stream_match(stream, ">")) {
            double rhs = parse_shift(stream);
            value = value > rhs ? 1.0 : 0.0;
        } else {
            break;
        }
    }
    return value;
}

static double parse_equality(eval_stream_t *stream)
{
    double value = parse_relational(stream);
    while (true) {
        eval_stream_skip_whitespace(stream);
        if (eval_stream_match(stream, "==")) {
            double rhs = parse_relational(stream);
            value = fabs(value - rhs) < 0.000001 ? 1.0 : 0.0;
        } else if (eval_stream_match(stream, "!=")) {
            double rhs = parse_relational(stream);
            value = fabs(value - rhs) < 0.000001 ? 0.0 : 1.0;
        } else {
            break;
        }
    }
    return value;
}

static double parse_bitwise_and(eval_stream_t *stream)
{
    double value = parse_equality(stream);
    while (true) {
        eval_stream_skip_whitespace(stream);
        if (eval_stream_match(stream, "&")) {
            long long lhs = eval_to_integer(value);
            long long rhs = eval_to_integer(parse_equality(stream));
            value = (double)(lhs & rhs);
        } else {
            break;
        }
    }
    return value;
}

static double parse_bitwise_xor(eval_stream_t *stream)
{
    double value = parse_bitwise_and(stream);
    while (true) {
        eval_stream_skip_whitespace(stream);
        if (eval_stream_match(stream, "^")) {
            long long lhs = eval_to_integer(value);
            long long rhs = eval_to_integer(parse_bitwise_and(stream));
            value = (double)(lhs ^ rhs);
        } else {
            break;
        }
    }
    return value;
}

static double parse_bitwise_or(eval_stream_t *stream)
{
    double value = parse_bitwise_xor(stream);
    while (true) {
        eval_stream_skip_whitespace(stream);
        if (eval_stream_match(stream, "|")) {
            long long lhs = eval_to_integer(value);
            long long rhs = eval_to_integer(parse_bitwise_xor(stream));
            value = (double)(lhs | rhs);
        } else {
            break;
        }
    }
    return value;
}

static double parse_logical_and(eval_stream_t *stream)
{
    double value = parse_bitwise_or(stream);
    while (true) {
        eval_stream_skip_whitespace(stream);
        if (eval_stream_match(stream, "&&")) {
            double rhs = parse_bitwise_or(stream);
            value = (value != 0.0 && rhs != 0.0) ? 1.0 : 0.0;
        } else {
            break;
        }
    }
    return value;
}

static double parse_logical_or(eval_stream_t *stream)
{
    double value = parse_logical_and(stream);
    while (true) {
        eval_stream_skip_whitespace(stream);
        if (eval_stream_match(stream, "||")) {
            double rhs = parse_logical_and(stream);
            value = (value != 0.0 || rhs != 0.0) ? 1.0 : 0.0;
        } else {
            break;
        }
    }
    return value;
}

static double parse_conditional(eval_stream_t *stream)
{
    double value = parse_logical_or(stream);
    eval_stream_skip_whitespace(stream);
    if (eval_stream_match(stream, "?")) {
        double true_expr = parse_conditional(stream);
        eval_stream_skip_whitespace(stream);
        if (eval_stream_match(stream, ":")) {
            double false_expr = parse_conditional(stream);
            return value != 0.0 ? true_expr : false_expr;
        }
        return value != 0.0 ? true_expr : 0.0;
    }
    return value;
}

static double parse_expression(eval_stream_t *stream)
{
    return parse_conditional(stream);
}

static bool evaluate_evalfloat_expression(const char *expression, size_t length, double *out_value)
{
    if (!expression || !out_value) {
        return false;
    }

    eval_stream_t stream;
    stream.text = expression;
    stream.length = length;
    stream.position = 0;

    double value = parse_expression(&stream);
    *out_value = value;
    return true;
}

static char *process_evalfloat_directives(const char *input)
{
    if (!input) {
        return NULL;
    }

    string_buffer_t output;
    string_buffer_init(&output);

    size_t length = strlen(input);
    for (size_t i = 0; i < length;) {
        if (strncmp(&input[i], "$evalfloat", 10) == 0) {
            size_t start = i + 10;
            while (start < length && isspace((unsigned char)input[start])) {
                start++;
            }
            if (start >= length || input[start] != '(') {
                string_buffer_append(&output, &input[i], 10);
                i += 10;
                continue;
            }

            size_t depth = 0;
            size_t end = start;
            do {
                if (end >= length) {
                    break;
                }
                if (input[end] == '(') {
                    depth++;
                } else if (input[end] == ')') {
                    if (depth == 0) {
                        break;
                    }
                    depth--;
                    if (depth == 0) {
                        break;
                    }
                }
                end++;
            } while (end < length);

            if (end >= length || input[end] != ')') {
                string_buffer_append(&output, &input[i], length - i);
                break;
            }

            const char *expr = &input[start + 1];
            size_t expr_length = end - (start + 1);
            double value = 0.0;
            if (!evaluate_evalfloat_expression(expr, expr_length, &value)) {
                string_buffer_append(&output, &input[i], (end + 1) - i);
            } else {
                char number[64];
                if (fabs(value) < 0.00001) {
                    value = 0.0;
                }
                snprintf(number, sizeof(number), "%.2f", value);
                string_buffer_append(&output, number, strlen(number));
            }

            i = end + 1;
        } else {
            string_buffer_append_char(&output, input[i]);
            i++;
        }
    }

    string_buffer_null_terminate(&output);
    return output.data;
}

static bool is_identifier_start(char ch)
{
    return isalpha((unsigned char)ch) || ch == '_';
}

static bool is_identifier_char(char ch)
{
    return isalnum((unsigned char)ch) || ch == '_';
}

static bool is_number_start(char ch)
{
    return (ch >= '0' && ch <= '9') || ch == '.';
}

static bool tokenize_number(const char *text, size_t length, size_t *position, int line, pc_token_t *token)
{
    size_t start = *position;
    size_t i = start;
    bool is_hex = false;
    bool is_float = false;

    if (i + 2 <= length && text[i] == '0' && (text[i + 1] == 'x' || text[i + 1] == 'X')) {
        is_hex = true;
        i += 2;
        while (i < length && isxdigit((unsigned char)text[i])) {
            ++i;
        }
    } else {
        bool seen_digits = false;
        while (i < length && isdigit((unsigned char)text[i])) {
            ++i;
            seen_digits = true;
        }
        if (i < length && text[i] == '.') {
            is_float = true;
            ++i;
            while (i < length && isdigit((unsigned char)text[i])) {
                ++i;
            }
        }
        if (!seen_digits && !is_float) {
            return false;
        }
        if (i < length && (text[i] == 'e' || text[i] == 'E')) {
            is_float = true;
            ++i;
            if (i < length && (text[i] == '+' || text[i] == '-')) {
                ++i;
            }
            while (i < length && isdigit((unsigned char)text[i])) {
                ++i;
            }
        }
    }

    size_t token_length = i - start;
    if (token_length == 0 || token_length >= sizeof(token->string)) {
        return false;
    }

    memcpy(token->string, text + start, token_length);
    token->string[token_length] = '\0';
    token->type = TT_NUMBER;
    token->line = line;
    token->linescrossed = 0;
    token->next = NULL;

    if (is_hex) {
        token->subtype = TT_INTEGER | TT_HEX | TT_LONG;
        token->intvalue = strtoull(token->string, NULL, 16);
        token->floatvalue = (long double)token->intvalue;
    } else if (!is_float && token->string[0] == '0' && token_length > 1) {
        token->subtype = TT_INTEGER | TT_OCTAL;
        token->intvalue = strtoull(token->string, NULL, 8);
        token->floatvalue = (long double)token->intvalue;
    } else if (is_float) {
        token->subtype = TT_FLOAT | TT_DECIMAL;
        token->floatvalue = strtold(token->string, NULL);
        token->intvalue = (unsigned long int)token->floatvalue;
    } else {
        token->subtype = TT_INTEGER | TT_DECIMAL;
        token->intvalue = strtoull(token->string, NULL, 10);
        token->floatvalue = (long double)token->intvalue;
    }

    *position = i;
    return true;
}

static bool tokenize_identifier(const char *text, size_t length, size_t *position, int line, pc_token_t *token)
{
    size_t start = *position;
    size_t i = start;
    while (i < length && is_identifier_char(text[i])) {
        ++i;
    }

    size_t token_length = i - start;
    if (token_length == 0 || token_length >= sizeof(token->string)) {
        return false;
    }

    memcpy(token->string, text + start, token_length);
    token->string[token_length] = '\0';
    token->type = TT_NAME;
    token->subtype = (int)token_length;
    token->line = line;
    token->linescrossed = 0;
    token->next = NULL;
    token->intvalue = 0;
    token->floatvalue = 0.0L;

    *position = i;
    return true;
}

static bool tokenize_string_literal(const char *text, size_t length, size_t *position, int *line, pc_token_t *token)
{
    size_t start = *position;
    size_t i = start + 1; // Skip opening quote.
    size_t out_index = 0;

    while (i < length) {
        char ch = text[i++];
        if (ch == '\\' && i < length) {
            char next = text[i++];
            switch (next) {
            case 'n':
                ch = '\n';
                break;
            case 't':
                ch = '\t';
                break;
            case 'r':
                ch = '\r';
                break;
            case '\\':
                ch = '\\';
                break;
            case '\"':
                ch = '\"';
                break;
            default:
                ch = next;
                break;
            }
        } else if (ch == '"') {
            break;
        } else if (ch == '\n') {
            (*line)++;
        }

        if (out_index + 1 < sizeof(token->string)) {
            token->string[out_index++] = ch;
        }
    }

    token->string[out_index] = '\0';
    token->type = TT_STRING;
    token->subtype = (int)out_index;
    token->line = *line;
    token->linescrossed = 0;
    token->next = NULL;
    token->intvalue = 0;
    token->floatvalue = 0.0L;

    *position = i;
    return true;
}

static bool tokenize_punctuation(const char *text, size_t length, size_t *position, int line, pc_token_t *token)
{
    static const char *punctuations[] = {
        "<<=", ">>=", "++", "--", "->", "<=", ">=", "==", "!=", "&&", "||", "+=", "-=", "*=", "/=", "%=", "<<", ">>",
        "&=", "|=", "^=", "::", NULL
    };

    size_t i = *position;
    for (size_t idx = 0; punctuations[idx]; ++idx) {
        size_t len = strlen(punctuations[idx]);
        if (i + len <= length && strncmp(&text[i], punctuations[idx], len) == 0) {
            memcpy(token->string, punctuations[idx], len + 1);
            token->type = TT_PUNCTUATION;
            token->subtype = 0;
            token->line = line;
            token->linescrossed = 0;
            token->next = NULL;
            token->intvalue = 0;
            token->floatvalue = 0.0L;
            *position = i + len;
            return true;
        }
    }

    token->string[0] = text[i];
    token->string[1] = '\0';
    token->type = TT_PUNCTUATION;
    token->subtype = 0;
    token->line = line;
    token->linescrossed = 0;
    token->next = NULL;
    token->intvalue = 0;
    token->floatvalue = 0.0L;
    *position = i + 1;
    return true;
}

static bool tokenize_text(const char *text, token_array_t *tokens)
{
    if (!text || !tokens) {
        return false;
    }

    size_t length = strlen(text);
    size_t position = 0;
    int line = 1;

    while (position < length) {
        char ch = text[position];
        if (ch == '\r') {
            position++;
            continue;
        }
        if (ch == '\n') {
            line++;
            position++;
            continue;
        }
        if (isspace((unsigned char)ch)) {
            position++;
            continue;
        }
        if (ch == '/' && position + 1 < length && text[position + 1] == '/') {
            position += 2;
            while (position < length && text[position] != '\n') {
                position++;
            }
            continue;
        }
        if (ch == '/' && position + 1 < length && text[position + 1] == '*') {
            position += 2;
            while (position + 1 < length && !(text[position] == '*' && text[position + 1] == '/')) {
                if (text[position] == '\n') {
                    line++;
                }
                position++;
            }
            if (position + 1 < length) {
                position += 2;
            }
            continue;
        }
        if (ch == '"') {
            pc_token_t token;
            if (!tokenize_string_literal(text, length, &position, &line, &token)) {
                return false;
            }
            if (!token_array_push(tokens, &token)) {
                return false;
            }
            continue;
        }
        if (is_identifier_start(ch)) {
            pc_token_t token;
            if (!tokenize_identifier(text, length, &position, line, &token)) {
                return false;
            }
            if (!token_array_push(tokens, &token)) {
                return false;
            }
            continue;
        }
        if (is_number_start(ch)) {
            pc_token_t token;
            if (!tokenize_number(text, length, &position, line, &token)) {
                return false;
            }
            if (!token_array_push(tokens, &token)) {
                return false;
            }
            continue;
        }

        if (ch == '\\' && position + 1 < length && text[position + 1] == '\n') {
            position += 2;
            line++;
            continue;
        }

        pc_token_t token;
        if (!tokenize_punctuation(text, length, &position, line, &token)) {
            return false;
        }
        if (!token_array_push(tokens, &token)) {
            return false;
        }
    }

    return true;
}

static pc_source_t *create_source_from_text(char *buffer)
{
    if (!buffer) {
        return NULL;
    }

    pc_source_t *source = (pc_source_t *)calloc(1, sizeof(pc_source_t));
    if (!source) {
        free(buffer);
        return NULL;
    }

    token_array_init(&source->tokens);
    if (!tokenize_text(buffer, &source->tokens)) {
        token_array_destroy(&source->tokens);
        free(source);
        free(buffer);
        return NULL;
    }

    source->preprocessed_buffer = buffer;
    source->cursor = 0;
    source->diagnostics = NULL;
    return source;
}

static pc_source_t *load_source_from_file_internal(const char *path)
{
    char *preprocessed = NULL;
    if (!run_cpp_on_file(path, &preprocessed)) {
        return NULL;
    }

    char *evaluated = process_evalfloat_directives(preprocessed);
    free(preprocessed);
    if (!evaluated) {
        return NULL;
    }

    return create_source_from_text(evaluated);
}

static pc_source_t *load_source_from_memory_internal(const char *name, const char *buffer, size_t buffer_size)
{
    (void)name;
    if (!buffer) {
        return NULL;
    }

    char template[] = "/tmp/gladiator_source_XXXXXX";
    int fd = mkstemp(template);
    if (fd < 0) {
        return NULL;
    }

    FILE *file = fdopen(fd, "wb");
    if (!file) {
        close(fd);
        unlink(template);
        return NULL;
    }

    size_t written = fwrite(buffer, 1, buffer_size, file);
    fclose(file);
    if (written != buffer_size) {
        unlink(template);
        return NULL;
    }

    pc_source_t *source = load_source_from_file_internal(template);
    unlink(template);
    return source;
}

void PC_InitLexer(void)
{
}

void PC_ShutdownLexer(void)
{
}

pc_source_t *PC_LoadSourceFile(const char *path)
{
    return load_source_from_file_internal(path);
}

pc_source_t *PC_LoadSourceMemory(const char *name, const char *buffer, size_t buffer_size)
{
    return load_source_from_memory_internal(name, buffer, buffer_size);
}

void PC_FreeSource(pc_source_t *source)
{
    if (!source) {
        return;
    }

    token_array_destroy(&source->tokens);
    free(source->preprocessed_buffer);
    free(source);
}

int PC_ReadToken(pc_source_t *source, pc_token_t *token)
{
    if (!source || !token) {
        return 0;
    }

    if (source->cursor >= source->tokens.count) {
        return 0;
    }

    *token = source->tokens.data[source->cursor++];
    return 1;
}

int PC_PeekToken(pc_source_t *source, pc_token_t *token)
{
    if (!source || !token) {
        return 0;
    }

    if (source->cursor >= source->tokens.count) {
        return 0;
    }

    *token = source->tokens.data[source->cursor];
    return 1;
}

void PC_UnreadToken(pc_source_t *source)
{
    if (!source) {
        return;
    }

    if (source->cursor > 0) {
        source->cursor--;
    }
}

const pc_diagnostic_t *PC_GetDiagnostics(const pc_source_t *source)
{
    return source ? source->diagnostics : NULL;
}

