#ifndef TESTS_PARITY_BOTLIB_CONTRACT_LOADER_H
#define TESTS_PARITY_BOTLIB_CONTRACT_LOADER_H

#include <stddef.h>

typedef struct botlib_contract_message_s
{
    int severity;
    char *text;
} botlib_contract_message_t;

typedef struct botlib_contract_return_code_s
{
    int value;
} botlib_contract_return_code_t;

typedef struct botlib_contract_scenario_s
{
    char *name;
    botlib_contract_message_t *messages;
    size_t message_count;
    botlib_contract_return_code_t *return_codes;
    size_t return_count;
} botlib_contract_scenario_t;

typedef struct botlib_contract_export_s
{
    char *name;
    botlib_contract_scenario_t *scenarios;
    size_t scenario_count;
} botlib_contract_export_t;

typedef struct botlib_contract_catalogue_s
{
    botlib_contract_export_t *exports;
    size_t export_count;
} botlib_contract_catalogue_t;

int BotlibContract_Load(const char *path, botlib_contract_catalogue_t *catalogue);
void BotlibContract_Free(botlib_contract_catalogue_t *catalogue);

const botlib_contract_export_t *BotlibContract_FindExport(const botlib_contract_catalogue_t *catalogue, const char *name);
const botlib_contract_scenario_t *BotlibContract_FindScenario(const botlib_contract_export_t *entry, const char *scenario_name);
const botlib_contract_message_t *BotlibContract_FindMessageWithSeverity(const botlib_contract_scenario_t *scenario, int severity);
const botlib_contract_return_code_t *BotlibContract_FindReturnCode(const botlib_contract_scenario_t *scenario, int value);
const botlib_contract_message_t *BotlibContract_FindMessageContaining(const botlib_contract_scenario_t *scenario, const char *needle);

#endif
