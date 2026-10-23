#pragma once

#include <cstdint>
#include <filesystem>


namespace tim
{

class sqlite_db;

/**
 * Приводит схему открытой БД к версии `expected`, применяя по порядку
 * скрипты migrate-<N>-to-<N+1>.sql из каталога `migrations_dir`.
 * Перед миграцией делает резервную копию; при ошибке на шаге K БД остаётся
 * целой на версии K. \return true, если версия БД стала равна `expected`.
 */
bool migrate_db(tim::sqlite_db &db,
                std::uint32_t expected,
                const std::filesystem::path &migrations_dir);

}
