#include "asset_env.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <sys/types.h>

#ifndef _WIN32
#include <unistd.h>
#endif

#ifdef _WIN32
#include <direct.h>
#define chdir _chdir
#define getcwd _getcwd
#define unlink _unlink
#endif

#ifndef PROJECT_SOURCE_DIR
#error "PROJECT_SOURCE_DIR must be defined so regression tests can resolve asset paths."
#endif

static int asset_env_setenv(const char *name, const char *value)
{
#ifdef _WIN32
    return _putenv_s(name, value);
#else
    return setenv(name, value, 1);
#endif
}

static void asset_env_unsetenv(const char *name)
{
#ifdef _WIN32
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

static bool copy_file(const char *src, const char *dst)
{
    FILE *input = fopen(src, "rb");
    if (input == NULL)
    {
        return false;
    }

    FILE *output = fopen(dst, "wb");
    if (output == NULL)
    {
        fclose(input);
        return false;
    }

    char buffer[4096];
    size_t read_bytes;
    bool ok = true;
    while ((read_bytes = fread(buffer, 1, sizeof(buffer), input)) > 0)
    {
        if (fwrite(buffer, 1, read_bytes, output) != read_bytes)
        {
            ok = false;
            break;
        }
    }

    fclose(output);
    fclose(input);

    if (!ok)
    {
        unlink(dst);
    }

    return ok;
}

static bool ensure_asset_bridge(asset_env_t *env, const char *filename, bool *created_flag)
{
    char destination[PATH_MAX];
    snprintf(destination, sizeof(destination), "%s/bots/%s", env->asset_root, filename);

    struct stat info;
    if (stat(destination, &info) == 0)
    {
        return true;
    }

    char source[PATH_MAX];
    snprintf(source, sizeof(source), "%s/%s", env->asset_root, filename);
    if (stat(source, &info) != 0)
    {
        return false;
    }

    if (!copy_file(source, destination))
    {
        return false;
    }

    if (created_flag != NULL)
    {
        *created_flag = true;
    }

    return true;
}

bool asset_env_initialise(asset_env_t *env)
{
    if (env == NULL)
    {
        return false;
    }

    memset(env, 0, sizeof(*env));

    int written = snprintf(env->asset_root,
                           sizeof(env->asset_root),
                           "%s/dev_tools/assets",
                           PROJECT_SOURCE_DIR);
    if (written <= 0 || (size_t)written >= sizeof(env->asset_root))
    {
        return false;
    }

    char character_path[PATH_MAX];
    snprintf(character_path, sizeof(character_path), "%s/bots/babe_c.c", env->asset_root);
    FILE *probe = fopen(character_path, "rb");
    if (probe == NULL)
    {
        return false;
    }
    fclose(probe);

    if (getcwd(env->previous_cwd, sizeof(env->previous_cwd)) != NULL)
    {
        env->have_previous_cwd = true;
    }

    if (chdir(env->asset_root) != 0)
    {
        asset_env_cleanup(env);
        return false;
    }

    if (asset_env_setenv("GLADIATOR_ASSET_DIR", env->asset_root) != 0)
    {
        asset_env_cleanup(env);
        return false;
    }

    if (!ensure_asset_bridge(env, "syn.c", &env->created_syn))
    {
        asset_env_cleanup(env);
        return false;
    }

    if (!ensure_asset_bridge(env, "match.c", &env->created_match))
    {
        asset_env_cleanup(env);
        return false;
    }

    if (!ensure_asset_bridge(env, "rchat.c", &env->created_rchat))
    {
        asset_env_cleanup(env);
        return false;
    }

    return true;
}

void asset_env_cleanup(asset_env_t *env)
{
    if (env == NULL)
    {
        return;
    }

    if (env->created_syn)
    {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/bots/syn.c", env->asset_root);
        unlink(path);
        env->created_syn = false;
    }

    if (env->created_match)
    {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/bots/match.c", env->asset_root);
        unlink(path);
        env->created_match = false;
    }

    if (env->created_rchat)
    {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/bots/rchat.c", env->asset_root);
        unlink(path);
        env->created_rchat = false;
    }

    asset_env_unsetenv("GLADIATOR_ASSET_DIR");

    if (env->have_previous_cwd)
    {
        chdir(env->previous_cwd);
        env->have_previous_cwd = false;
    }
}
