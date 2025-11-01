#ifndef TESTS_SUPPORT_ASSET_ENV_H
#define TESTS_SUPPORT_ASSET_ENV_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef struct asset_env_s {
    char asset_root[PATH_MAX];
    char previous_cwd[PATH_MAX];
    bool have_previous_cwd;
    bool created_syn;
    bool created_match;
    bool created_rchat;
} asset_env_t;

bool asset_env_initialise(asset_env_t *env);
void asset_env_cleanup(asset_env_t *env);

#ifdef __cplusplus
}
#endif

#endif /* TESTS_SUPPORT_ASSET_ENV_H */
