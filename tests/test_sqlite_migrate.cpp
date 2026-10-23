// Тесты tim::migrate_db: честная миграция схемы с сохранением данных.
//
// Берём настоящие db/10-schema.sql и db/20-triggers.sql, откатываем их к
// схеме версии 2 (убираем created_at), кладём данные и мигрируем настоящим
// db/migrate-2-to-3.sql. Путь к каталогу db/ приходит из TIM_DB_DIR.

#include "tim_test.h"

#include "tim_sqlite_db.h"
#include "tim_sqlite_migrate.h"
#include "tim_sqlite_query.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

#ifndef TIM_DB_DIR
#   define TIM_DB_DIR "../db"
#endif


namespace
{

namespace fs = std::filesystem;

struct tmp_dir
{
    tmp_dir()
    {
        static int n = 0;
        _p = fs::path("/tmp") / ("tim_test_migrate_" + std::to_string(getpid())
                                 + "_" + std::to_string(++n));
        fs::create_directories(_p);
    }
    ~tmp_dir()
    {
        std::error_code ec;
        fs::remove_all(_p, ec);
    }
    fs::path _p;
};

std::string slurp(const fs::path &p)
{
    std::ifstream is(p);
    return std::string(std::istreambuf_iterator<char>(is), {});
}

void spit(const fs::path &p, const std::string &s)
{
    std::ofstream os(p);
    os << s;
}

// Создаёт БД схемы версии 2 с данными: u1 (2 поста), u2 (пост), u3 (только
// реакция), u4 (ничего).
bool make_v2(tim::sqlite_db &db, const fs::path &file)
{
    if (!db.open(file))
        return false;
    // Схема текущего репозитория (версия 3) -> убираем created_at, получаем 2.
    std::string schema = slurp(fs::path(TIM_DB_DIR) / "10-schema.sql");
    const std::string col = "created_at INTEGER NOT NULL DEFAULT (CAST(unixepoch('subsec') * 1000 AS INTEGER))";
    const auto pos = schema.find(col);
    if (pos == std::string::npos)
        return false;
    schema.replace(pos, col.size(), "dummy_v2 INTEGER");   // колонка-заглушка
    if (!db.exec(schema)
            || !db.exec_file(fs::path(TIM_DB_DIR) / "20-triggers.sql")
            || !db.exec("ALTER TABLE user DROP COLUMN dummy_v2")
            || !db.set_version(2))
        return false;
    return db.exec(
        "INSERT INTO user (id, nick) VALUES ('u1','a'),('u2','b'),('u3','c'),('u4','d');"
        "INSERT INTO post (id, user_id, timestamp, text) VALUES"
        "  ('p1','u1',1000,'x'),('p2','u1',500,'y'),('p3','u2',2000,'z');"
        "INSERT INTO reaction (id, post_id, user_id, timestamp) VALUES ('r1','p3','u3',1500);");
}

std::int64_t scalar(tim::sqlite_db &db, const std::string &sql)
{
    tim::sqlite_query q(&db, sql);
    bool done = false;
    if (!q.prepare() || !q.next(&done) || done)
        return -1;
    return q.to_int64(0);
}

fs::path migrations_dir()
{
    return fs::path(TIM_DB_DIR);
}

}


TIM_TEST_CASE(migrate_noop_when_up_to_date)
{
    tmp_dir t;
    tim::sqlite_db db;
    TIM_CHECK(make_v2(db, t._p / "tim.db"));
    TIM_CHECK(tim::migrate_db(db, 2, migrations_dir()));
    TIM_CHECK(!fs::exists(t._p / "tim.db.v2.bak"));
}

TIM_TEST_CASE(migrate_2_to_3_keeps_data_and_fills_created_at)
{
    tmp_dir t;
    tim::sqlite_db db;
    TIM_CHECK(make_v2(db, t._p / "tim.db"));

    TIM_CHECK(tim::migrate_db(db, 3, migrations_dir()));

    std::uint32_t v = 0;
    TIM_CHECK(db.get_version(v) && v == 3);
    TIM_CHECK(scalar(db, "SELECT COUNT(*) FROM user") == 4);
    TIM_CHECK(scalar(db, "SELECT COUNT(*) FROM post") == 3);
    TIM_CHECK(scalar(db, "SELECT COUNT(*) FROM reaction") == 1);
    TIM_CHECK(scalar(db, "SELECT nick = 'a' FROM user WHERE id = 'u1'") == 1);
    // Самая ранняя активность: пост (500) / пост (2000) / реакция (1500).
    TIM_CHECK(scalar(db, "SELECT created_at FROM user WHERE id = 'u1'") == 500);
    TIM_CHECK(scalar(db, "SELECT created_at FROM user WHERE id = 'u2'") == 2000);
    TIM_CHECK(scalar(db, "SELECT created_at FROM user WHERE id = 'u3'") == 1500);
    // Без активности — момент миграции (точно не 0).
    TIM_CHECK(scalar(db, "SELECT created_at FROM user WHERE id = 'u4'") > 1600000000000LL);
    TIM_CHECK(!db.is_transaction_active());
    // Резервная копия лежит рядом и содержит старую схему.
    TIM_CHECK(fs::exists(t._p / "tim.db.v2.bak"));
}

TIM_TEST_CASE(migrate_keeps_foreign_keys_and_triggers_working)
{
    tmp_dir t;
    tim::sqlite_db db;
    TIM_CHECK(make_v2(db, t._p / "tim.db"));
    TIM_CHECK(tim::migrate_db(db, 3, migrations_dir()));

    // Каскад по-прежнему работает: удаляем u1 -> его посты исчезают.
    TIM_CHECK(db.exec("DELETE FROM user WHERE id = 'u1'"));
    TIM_CHECK(scalar(db, "SELECT COUNT(*) FROM post") == 1);
    // Триггер неизменяемости id не потерян.
    TIM_CHECK(!db.exec("UPDATE user SET id = 'zzz' WHERE id = 'u2'"));
    db.rollback();
}

TIM_TEST_CASE(migrate_failure_rolls_back_everything)
{
    tmp_dir t;
    tim::sqlite_db db;
    TIM_CHECK(make_v2(db, t._p / "tim.db"));

    // Подменённый скрипт: ALTER проходит, затем ошибка -> должно откатиться всё.
    const fs::path mdir = t._p / "mig";
    fs::create_directories(mdir);
    spit(mdir / "migrate-2-to-3.sql",
         "BEGIN;\n"
         "ALTER TABLE user ADD COLUMN created_at INTEGER NOT NULL DEFAULT 0;\n"
         "UPDATE no_such_table SET x = 1;\n"
         "PRAGMA user_version = 3;\n"
         "COMMIT;\n");

    TIM_CHECK(!tim::migrate_db(db, 3, mdir));

    std::uint32_t v = 0;
    TIM_CHECK(db.get_version(v) && v == 2);
    TIM_CHECK(!db.is_transaction_active());
    // Колонки нет: ALTER откатился вместе с остальным.
    TIM_CHECK(!db.exec("SELECT created_at FROM user"));
    TIM_CHECK(scalar(db, "SELECT COUNT(*) FROM user") == 4);
}

TIM_TEST_CASE(migrate_gap_without_script_does_not_touch_db)
{
    tmp_dir t;
    tim::sqlite_db db;
    TIM_CHECK(make_v2(db, t._p / "tim.db"));

    // Нужна цепочка 2->3->4, а есть только 2->3: ничего не применяется.
    const fs::path mdir = t._p / "mig";
    fs::create_directories(mdir);
    fs::copy_file(migrations_dir() / "migrate-2-to-3.sql", mdir / "migrate-2-to-3.sql");

    TIM_CHECK(!tim::migrate_db(db, 4, mdir));

    std::uint32_t v = 0;
    TIM_CHECK(db.get_version(v) && v == 2);
    TIM_CHECK(!fs::exists(t._p / "tim.db.v2.bak"));
}

TIM_TEST_CASE(migrate_refuses_downgrade)
{
    tmp_dir t;
    tim::sqlite_db db;
    TIM_CHECK(make_v2(db, t._p / "tim.db"));
    TIM_CHECK(db.set_version(7));
    TIM_CHECK(!tim::migrate_db(db, 3, migrations_dir()));
    std::uint32_t v = 0;
    TIM_CHECK(db.get_version(v) && v == 7);
}

TIM_TEST_CASE(migrate_chain_applies_several_steps)
{
    tmp_dir t;
    tim::sqlite_db db;
    TIM_CHECK(make_v2(db, t._p / "tim.db"));

    const fs::path mdir = t._p / "mig";
    fs::create_directories(mdir);
    fs::copy_file(migrations_dir() / "migrate-2-to-3.sql", mdir / "migrate-2-to-3.sql");
    spit(mdir / "migrate-3-to-4.sql",
         "BEGIN;\nCREATE TABLE extra (v INTEGER);\nPRAGMA user_version = 4;\nCOMMIT;\n");

    TIM_CHECK(tim::migrate_db(db, 4, mdir));
    std::uint32_t v = 0;
    TIM_CHECK(db.get_version(v) && v == 4);
    TIM_CHECK(scalar(db, "SELECT COUNT(*) FROM extra") == 0);
}

TIM_TEST_CASE(migrate_detects_script_that_forgot_user_version)
{
    tmp_dir t;
    tim::sqlite_db db;
    TIM_CHECK(make_v2(db, t._p / "tim.db"));

    const fs::path mdir = t._p / "mig";
    fs::create_directories(mdir);
    spit(mdir / "migrate-2-to-3.sql", "BEGIN;\nCREATE TABLE extra (v INTEGER);\nCOMMIT;\n");
    TIM_CHECK(!tim::migrate_db(db, 3, mdir));
}
