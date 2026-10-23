#include "tim_user_db.h"

#include "tim_sqlite_query.h"
#include "tim_trace.h"
#include "tim_translator.h"


/**
 * Гарантирует наличие строки в таблице user с заданным id
 * (INSERT OR IGNORE INTO user(id)).
 *
 * Используется перед операциями, у которых внешний ключ ссылается
 * на user(id): post.user_id, reaction.user_id, subscription.*_id.
 *
 * \param db Открытое подключение к БД.
 * \param user_id_canon Канонизированный UUID пользователя
 *                     (tim::uuid::to_string()). Перенормализация здесь
 *                     не делается — вызывающий код обычно работает
 *                     со строковым представлением.
 * \return true, если строка существует после вызова (вставлена сейчас
 *         или была раньше); false при ошибке БД (уже залогирована).
 */
bool tim::ensure_user(tim::sqlite_db &db, const std::string &user_id_canon)
{
    tim::sqlite_query q(&db, "INSERT OR IGNORE INTO user (id, created_at) "
                       "VALUES (?, CAST(unixepoch('subsec') * 1000 AS INTEGER))");
    if (!q.prepare())
    {
        TIM_TRACE(error,
                  TIM_TR("Failed to prepare query '%s'."_en,
                         "Не могу подготовить запрос '%s' к базе данных."_ru),
                  q.sql().c_str());
        return false;
    }
    q.bind(1, user_id_canon);
    if (!q.exec())
    {
        TIM_TRACE(error,
                  TIM_TR("Failed to ensure user '%s' exists."_en,
                         "Не удалось создать запись пользователя '%s'."_ru),
                  user_id_canon.c_str());
        return false;
    }
    return true;
}
