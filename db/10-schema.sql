-- Включаем Write-Ahead Logging (https://www.sqlite.org/wal.html)
PRAGMA journal_mode = WAL;

BEGIN;

-------------
-- Таблицы --
-------------

-- Пользователи
DROP TABLE IF EXISTS user;
CREATE TABLE user
(
    id VARCHAR PRIMARY KEY NOT NULL CHECK(id != '' AND id != '{00000000-0000-0000-0000-000000000000}'),

    pub_key VARCHAR UNIQUE, -- Public key.
    nick VARCHAR UNIQUE,
    icon VARCHAR,
    motto VARCHAR,

    -- Метка времени создания записи (мс). Появилась в схеме версии 3;
    -- для баз версии 2 см. db/migrate-2-to-3.sql.
    created_at INTEGER NOT NULL DEFAULT (CAST(unixepoch('subsec') * 1000 AS INTEGER))
);
CREATE UNIQUE INDEX user_pub_key ON user(pub_key);
CREATE UNIQUE INDEX user_nick ON user(nick);


-- Подписки.
-- Композитный (publisher_id, subscriber_id) — естественный ключ подписки,
-- поэтому делаем его PRIMARY KEY и не заводим отдельный UNIQUE-индекс.
DROP TABLE IF EXISTS subscription;
CREATE TABLE subscription
(
    publisher_id VARCHAR NOT NULL REFERENCES user(id) ON DELETE CASCADE,
    subscriber_id VARCHAR NOT NULL REFERENCES user(id) ON DELETE CASCADE CHECK(subscriber_id != publisher_id),
    PRIMARY KEY (publisher_id, subscriber_id)
);
-- PRIMARY KEY покрывает поиск по publisher_id; для обратной выборки
-- "на кого подписан этот subscriber" нужен отдельный индекс.
CREATE INDEX subscription_subscriber_id ON subscription(subscriber_id);


-- Сообщения пользователя.
-- user_id ссылается на user(id); ON DELETE CASCADE удаляет сообщения вместе
-- с удалением автора. Перед INSERT-ом поста сервис обязан гарантировать
-- наличие соответствующей строки в user (INSERT OR IGNORE).
DROP TABLE IF EXISTS post;
CREATE TABLE post
(
    id VARCHAR PRIMARY KEY NOT NULL CHECK(id != '' AND id != '{00000000-0000-0000-0000-000000000000}'),
    user_id VARCHAR NOT NULL REFERENCES user(id) ON DELETE CASCADE,

    -- Метка времени в миллисекундах. unixepoch('subsec') даёт секунды
    -- с дробной частью (SQLite 3.42+); *1000 и приведение к INTEGER
    -- сохраняют точность до миллисекунды. Прежнее strftime('%s','now')*1000
    -- округлялось до целой секунды, поэтому события в пределах одной
    -- секунды сравнивались как одновременные.
    timestamp INTEGER NOT NULL DEFAULT (CAST(unixepoch('subsec') * 1000 AS INTEGER)),

    text VARCHAR NOT NULL
);
CREATE INDEX post_user_id ON post(user_id);
CREATE INDEX post_timestamp ON post(timestamp);


-- Реакция на сообщение.
-- Один пользователь может оставить только одну реакцию на конкретное
-- сообщение (UNIQUE(post_id, user_id)); повторная реакция должна
-- выполняться как INSERT OR REPLACE по этой паре. UNIQUE-индекс по
-- (post_id, user_id) также покрывает поиск всех реакций к сообщению,
-- поэтому отдельный reaction_post_id не нужен.
DROP TABLE IF EXISTS reaction;
CREATE TABLE reaction
(
    id VARCHAR PRIMARY KEY NOT NULL CHECK(id != '' AND id != '{00000000-0000-0000-0000-000000000000}'),
    post_id VARCHAR NOT NULL REFERENCES post(id) ON DELETE CASCADE,
    user_id VARCHAR NOT NULL REFERENCES user(id) ON DELETE CASCADE,

    -- Метка времени в миллисекундах. unixepoch('subsec') даёт секунды
    -- с дробной частью (SQLite 3.42+); *1000 и приведение к INTEGER
    -- сохраняют точность до миллисекунды. Прежнее strftime('%s','now')*1000
    -- округлялось до целой секунды, поэтому события в пределах одной
    -- секунды сравнивались как одновременные.
    timestamp INTEGER NOT NULL DEFAULT (CAST(unixepoch('subsec') * 1000 AS INTEGER)),

    weight INTEGER DEFAULT 1,

    UNIQUE(post_id, user_id)
);
CREATE INDEX reaction_user_id ON reaction(user_id);
CREATE INDEX reaction_timestamp ON reaction(timestamp);

COMMIT;
