#pragma once

#include <chrono>
#include <cstdint>


namespace tim
{

/** Префикс TCL-команд, набираемых в чате: например, /post, /react. */
static const char COMMAND_PREFIX = '/';
/** Имя приложения, используется для логов и баннеров. */
static const char APP_NAME[] = "tim";
/** Имя организации; исторически хранилось вместе с APP_NAME. */
static const char ORG_NAME[] = "mrsu";
/** Имя файла истории команд внутри data_dir. */
static const char HISTORY_FNAME[] = "history.txt";
/** Имя файла SSH host-key. Лежит в data_dir/SSH_DATA_SUBDIR. */
static const char SSH_HOST_KEY_FNAME[] = "host_ed25519";
/** Подкаталог в data_dir, в котором хранится SSH host-key. */
static const char SSH_DATA_SUBDIR[] = "ssh";

/**
 * SQLite-настройки.
 */
/** Таймаут на ожидание занятой БД между попытками. */
static const std::chrono::microseconds DB_BUSY_TIMEOUT(100000);
/** Количество попыток выполнения SQL при SQLITE_BUSY. */
static const std::size_t DB_BUSY_TRIES = 5;
/** Имя файла основной БД внутри data_dir. */
static const char DB_FILE_NAME[] = "tim.db";
/**
 * Ожидаемая бинарником версия схемы БД. Должна соответствовать содержимому
 * файла DB_SCHEMA_VERSION в корне репо (на момент сборки). При несовпадении
 * с PRAGMA user_version из открытой БД приложение закрывает БД и продолжает
 * работу без сохранения данных — оператору предложено перезапустить
 * db/create-db.sh.
 */
static const std::uint32_t EXPECTED_DB_SCHEMA_VERSION = 3;

/**
 * Максимальная глубина рекурсивного вызова ssh_inetd::dispatch.
 * Защищает стек от бесконечной рекурсии, если Tcl-скрипт неправильно
 * пользуется DISPATCH-обработчиком (например, бесконечно вызывает dispatch
 * из обработчика события). При достижении предела вложенный опрос
 * пропускается с предупреждением в журнале.
 */
static const std::size_t MAX_DISPATCH_DEPTH = 8;

/**
 * MQTT-настройки.
 */
/**
 * Предельный размер очереди неподтверждённых публикаций (outbox)
 * mqtt_client. Публикации с QoS > 0 хранятся в очереди до прихода
 * PUBACK; при переполнении отбрасывается самая старая запись.
 */
static const std::size_t MQTT_OUTBOX_LIMIT = 64;

/**
 * Настройки главного цикла.
 */
/**
 * Квант опроса главного цикла: столько миллисекунд exec() отдаёт
 * поочерёдно каждому из двух циклов событий (Mongoose и libssh).
 * Меньший квант снижает задержку, больший — холостой расход процессора.
 */
static const int POLL_QUANTUM_MS = 50;

/**
 * SSH-настройки.
 */
/**
 * Максимальное число одновременных SSH-сессий. Соединения сверх предела
 * принимаются и немедленно закрываются — иначе слушающий сокет остался бы
 * в состоянии готовности, а очередь ядра продолжила бы расти.
 */
static const std::size_t SSH_MAX_SESSIONS = 64;
/**
 * Предельное время от приёма TCP-соединения до создания прикладного
 * сервиса (рукопожатие, аутентификация, запросы pty/shell). Сессия,
 * не уложившаяся в срок, принудительно закрывается: без этого молчащий
 * клиент удерживал бы дескриптор неограниченно долго.
 */
static const std::chrono::seconds SSH_SESSION_SETUP_TIMEOUT(30);
/** TCP keepalive: секунды простоя до первой пробы. */
static const int SSH_KEEPALIVE_IDLE_S = 60;
/** TCP keepalive: интервал между пробами, секунды. */
static const int SSH_KEEPALIVE_INTVL_S = 15;
/** TCP keepalive: число проб без ответа до разрыва соединения. */
static const int SSH_KEEPALIVE_CNT = 4;

/**
 * Настройки чата.
 */
/** Сколько последних сообщений из БД выводится при входе в чат. */
static const int POST_HISTORY_LIMIT = 20;
}
