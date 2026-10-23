#include "tim_sqlite_migrate.h"

#include "tim_sqlite_db.h"
#include "tim_sqlite_query.h"
#include "tim_trace.h"
#include "tim_translator.h"

#include <string>
#include <system_error>


namespace
{

std::filesystem::path step_path(const std::filesystem::path &dir, std::uint32_t from)
{
    return dir / ("migrate-" + std::to_string(from)
                  + "-to-" + std::to_string(from + 1) + ".sql");
}

// Копия перед миграцией. VACUUM INTO безопасен для режима WAL.
bool backup(tim::sqlite_db &db, std::uint32_t version)
{
    std::filesystem::path dst = db.path();
    dst += ".v" + std::to_string(version) + ".bak";

    std::error_code ec;
    std::filesystem::remove(dst, ec);   // VACUUM INTO не перезаписывает файл

    tim::sqlite_query q(&db, "VACUUM INTO ?");
    if (!q.prepare())
        return false;
    q.bind(1, dst.string());
    if (!q.exec())
        return TIM_TRACE(error,
                         TIM_TR("Failed to back up database to '%s'."_en,
                                "Не удалось сделать резервную копию базы данных в '%s'."_ru),
                         dst.string().c_str());

    TIM_TRACE(info,
              TIM_TR("Database backup saved to '%s'."_en,
                     "Резервная копия базы данных сохранена в '%s'."_ru),
              dst.string().c_str());
    return true;
}

}


bool tim::migrate_db(tim::sqlite_db &db,
                     std::uint32_t expected,
                     const std::filesystem::path &migrations_dir)
{
    std::uint32_t version = 0;
    if (!db.get_version(version))
        return false;

    if (version == expected)
        return true;

    if (version > expected)
        return TIM_TRACE(error,
                         TIM_TR("Database schema version %u is newer than supported %u; "
                                "update TIM. Downgrade is not possible."_en,
                                "Версия схемы БД %u новее поддерживаемой %u; "
                                "обновите TIM. Понижение версии невозможно."_ru),
                         version, expected);

    // 1. Вся цепочка скриптов должна существовать ДО того, как мы тронем БД.
    for (std::uint32_t v = version; v < expected; ++v)
    {
        std::error_code ec;
        const std::filesystem::path path = step_path(migrations_dir, v);
        if (!std::filesystem::is_regular_file(path, ec))
            return TIM_TRACE(error,
                             TIM_TR("Migration script '%s' not found; cannot upgrade "
                                    "database schema from %u to %u."_en,
                                    "Не найден скрипт миграции '%s'; нельзя обновить "
                                    "схему БД с версии %u до %u."_ru),
                             path.string().c_str(), version, expected);
    }

    // 2. Копия на всякий случай.
    if (!backup(db, version))
        return false;

    // 3. Применяем шаги по одному; каждый шаг атомарен и поднимает версию.
    for (std::uint32_t v = version; v < expected; ++v)
    {
        const std::filesystem::path path = step_path(migrations_dir, v);
        TIM_TRACE(info,
                  TIM_TR("Migrating database schema %u -> %u (%s) ..."_en,
                         "Миграция схемы БД %u -> %u (%s) ..."_ru),
                  v, v + 1, path.string().c_str());

        if (!db.exec_file(path))
        {
            // sqlite3_exec остановился на ошибке, BEGIN остался без COMMIT.
            db.rollback();
            return TIM_TRACE(error,
                             TIM_TR("Migration '%s' failed and was rolled back; "
                                    "database stays at version %u."_en,
                                    "Миграция '%s' не удалась и была отменена; "
                                    "БД осталась на версии %u."_ru),
                             path.string().c_str(), v);
        }

        // Страховка от забытого PRAGMA user_version в скрипте.
        std::uint32_t now = 0;
        if (!db.get_version(now) || now != v + 1)
            return TIM_TRACE(error,
                             TIM_TR("Migration '%s' did not set user_version to %u (now %u)."_en,
                                    "Миграция '%s' не установила user_version = %u (сейчас %u)."_ru),
                             path.string().c_str(), v + 1, now);
    }

    TIM_TRACE(info,
              TIM_TR("Database schema upgraded from %u to %u."_en,
                     "Схема БД обновлена с версии %u до %u."_ru),
              version, expected);
    return true;
}
