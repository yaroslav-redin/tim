-- Миграция схемы БД TIM: версия 2 -> 3.
-- Весь скрипт выполняется в одной транзакции вместе со сменой user_version.

BEGIN;

-- SQLite не позволяет добавить колонку с неконстантным DEFAULT,
-- поэтому добавляем с DEFAULT 0 и заполняем ниже.
ALTER TABLE user ADD COLUMN created_at INTEGER NOT NULL DEFAULT 0;

-- Время создания старых пользователей неизвестно: берём время их самой
-- ранней активности (пост или реакция), иначе момент миграции.
UPDATE user SET created_at = COALESCE(
    (SELECT MIN(t)
       FROM (SELECT timestamp AS t FROM post     WHERE user_id = user.id
             UNION ALL
             SELECT timestamp AS t FROM reaction WHERE user_id = user.id)),
    CAST(unixepoch('subsec') * 1000 AS INTEGER));

PRAGMA user_version = 3;

COMMIT;
