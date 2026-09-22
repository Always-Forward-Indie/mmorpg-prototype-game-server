#pragma once

// QueryRegistry: single source of truth for prepared-statement SQL.
// GENERATED from Database::prepareQueriesOn by /tmp/gen_registry.py -
// do not hand-edit SQL here; change the generator input and re-run.
// Database::prepareQueriesOn iterates this table, so registration
// order is preserved. Pure data (no pqxx): unit-testable without a DB.
#include <cstddef>
#include <string_view>

struct QueryDef
{
    std::string_view name;
    std::string_view sql;
};

const QueryDef *queryRegistryBegin();
const QueryDef *queryRegistryEnd();
inline std::size_t queryRegistrySize()
{
    return static_cast<std::size_t>(queryRegistryEnd() - queryRegistryBegin());
}
