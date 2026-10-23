#!/bin/sh
# Сборка и запуск тестов TIM.
# Использование:
#   ./build.sh         # debug-сборка и запуск
#   ./build.sh release # релиз-сборка и запуск
set -e

cd "$(dirname "$0")"

MODE=${1:-debug}
CXX=${CXX:-g++}
CC=${CC:-gcc}

if [ "$MODE" = "release" ]; then
    OPT="-O2"
else
    OPT="-g -O0"
fi

INCLUDES="-I../src -I../src/mqtt -I../src/types -I../src/signal -I../src/tools \
          -I../src/db/sqlite -I../src/3rdparty/sqlite -I../src/translator \
          -I../src/app -I../src/3rdparty/jsoncpp -I../src/3rdparty/mongoose \
          -I../src/srv"

DEFINES="-DTIM_DB_DIR=\"$(pwd)/../db\" -DTIM_OS_LINUX -DJSON_NOEXCEPTION=1 -DJSON_DIAGNOSTICS=1 -DJSON_DIAGNOSTIC_POSITIONS=1"

# Те же флаги SQLite, что и в основной сборке (см. build/linux/rules.mk).
SQLITE_DEFINES="-DSQLITE_THREADSAFE=0 \
    -DSQLITE_DEFAULT_MEMSTATUS=0 \
    -DSQLITE_DEFAULT_WAL_SYNCHRONOUS=1 \
    -DSQLITE_LIKE_DOESNT_MATCH_BLOBS \
    -DSQLITE_MAX_EXPR_DEPTH=0 \
    -DSQLITE_OMIT_DECLTYPE \
    -DSQLITE_OMIT_DEPRECATED \
    -DSQLITE_OMIT_PROGRESS_CALLBACK \
    -DSQLITE_OMIT_SHARED_CACHE \
    -DSQLITE_USE_ALLOCA \
    -DHAVE_FDATASYNC=1 \
    -DHAVE_GMTIME_R=1 \
    -DHAVE_ISNAN=1 \
    -DHAVE_LOCALTIME_R=1 \
    -DHAVE_LOCALTIME_S=1 \
    -DHAVE_MALLOC_USABLE_SIZE=1 \
    -DHAVE_STRCHRNUL=1 \
    -DHAVE_USLEEP=1 \
    -DHAVE_UTIME=1 \
    -DSQLITE_BYTEORDER=1234 \
    -DSQLITE_DEFAULT_CACHE_SIZE=16384 \
    -DSQLITE_DEFAULT_FOREIGN_KEYS=1 \
    -DSQLITE_DEFAULT_PAGE_SIZE=4096 \
    -DSQLITE_TEMP_STORE=3 \
    -DSQLITE_USE_URI=1 \
    -DSQLITE_ENABLE_BATCH_ATOMIC_WRITE \
    -DSQLITE_ENABLE_UPDATE_DELETE_LIMIT=1 \
    -DSQLITE_CORE"

# sqlite3.c — собираем один раз отдельным шагом (большой файл).
$CC -std=c11 $OPT $SQLITE_DEFINES -c -o sqlite3.o ../src/3rdparty/sqlite/sqlite3.c

$CXX -std=c++17 -Wall -Wextra -Werror -Wno-unused-parameter $OPT $INCLUDES $DEFINES \
    -o tim_test \
    test_main.cpp \
    test_mqtt_topic.cpp \
    test_service_base.cpp \
    test_signal.cpp \
    test_sqlite_migrate.cpp \
    test_sqlite_tx.cpp \
    test_uuid.cpp \
    ../src/mqtt/tim_mqtt_topic.cpp \
    ../src/signal/tim_signal_connection.cpp \
    ../src/srv/tim_service.cpp \
    ../src/types/tim_uuid.cpp \
    ../src/db/sqlite/tim_sqlite_db.cpp \
    ../src/db/sqlite/tim_sqlite_migrate.cpp \
    ../src/db/sqlite/tim_sqlite_query.cpp \
    ../src/db/sqlite/tim_sqlite_tx.cpp \
    ../src/tools/tim_file_tools.cpp \
    ../src/tools/tim_severity.cpp \
    ../src/tools/tim_string_tools.cpp \
    ../src/tools/tim_trace.cpp \
    ../src/translator/tim_translator.cpp \
    sqlite3.o

echo "----"
./tim_test
