#include "tim_application.h"

#include "tim_application_p.h"

#include "tim_config.h"
#include "tim_settings.h"
#include "tim_ssh_inetd.h"
#include "tim_mqtt_client.h"
#include "tim_sqlite_db.h"
#include "tim_sqlite_migrate.h"
#include "tim_trace.h"
#include "tim_translator.h"

#include "fort.h"

// Services
#include "tim_a_inetd_service.h"
#include "tim_post_service.h"
#include "tim_prompt_service.h"
#include "tim_reaction_service.h"
#include "tim_user_service.h"

#include <cassert>
#include <cstring>
#include <locale>


// Public
/**
 * Создаёт приложение и инициализирует все подсистемы.
 *
 * \param config_path Путь к JSON-файлу настроек. Пусто = путь
 *                    по умолчанию (~/.tim/config.json). Используется
 *                    для пробрасывания аргумента --config из main().
 */
tim::application::application(const std::string &config_path)
    : _d()
{
    if (tim::p::application::instance())
        TIM_TRACE(fatal, "%s",
                  TIM_TR("tim::application is a singleton; second instantiation is forbidden."_en,
                         "tim::application существует в единственном экземпляре; повторное создание запрещено."_ru));

    tim::p::application::instance() = this;

    std::setbuf(stdout, nullptr);
    std::setbuf(stderr, nullptr);

#ifdef TIM_OS_LINUX
    std::locale::global(std::locale("en_US.UTF-8"));

    {
        struct sigaction action;
        std::memset(&action, 0, sizeof(action));
        action.sa_handler = &tim::p::application::signal_handler;
        action.sa_flags = 0;
        sigemptyset(&action.sa_mask);
        sigaction(SIGINT, &action, &_d->_old_sig_int);
        sigaction(SIGTERM, &action, &_d->_old_sig_term);
    }
#endif

    ft_set_default_border_style(FT_SOLID_ROUND_STYLE);

    // Конфиг читается первым: язык TIM_TR должен быть выставлен до того,
    // как сообщения об ошибках инициализации подсистем уйдут в лог.
    const tim::settings settings = tim::settings::load_or_create(config_path);
    tim::translator::set_language(settings.language);
    tim::p::application::data_dir() = settings.data_dir;

#ifdef TIM_DEBUG
    // mg_log_set(MG_LL_VERBOSE);
#endif
    mg_mgr_init(&_d->_mg);

    _d->_mqtt.reset(new tim::mqtt_client(&_d->_mg));
    if (!_d->_mqtt->start(settings.mqtt_url))
        TIM_TRACE(error, "%s",
                  TIM_TR("MQTT client failed to start; messaging will be unavailable."_en,
                         "Не удалось запустить MQTT-клиент; обмен сообщениями недоступен."_ru));

    _d->_db.reset(new tim::sqlite_db());
    if (!_d->_db->open(settings.data_dir / tim::DB_FILE_NAME))
        TIM_TRACE(error,
                  TIM_TR("Failed to open database file '%s'; persistence will be unavailable."_en,
                         "Не удалось открыть файл базы данных '%s'; хранение данных недоступно."_ru),
                  _d->_db->path().string().c_str());
    else
    {
        // Проверяем PRAGMA user_version. Если схема старее ожидаемой,
        // пробуем честную миграцию с сохранением данных (migrate_db).
        // Если миграция невозможна (нет скрипта, БД новее бинарника, ошибка
        // в скрипте), лучше явно закрыть БД и работать без сохранения
        // данных, чем втихую писать/читать несуществующие колонки.
        std::uint32_t db_version = 0;
        if (_d->_db->get_version(db_version)
                && db_version != tim::EXPECTED_DB_SCHEMA_VERSION
                && !tim::migrate_db(*_d->_db,
                                    tim::EXPECTED_DB_SCHEMA_VERSION,
                                    settings.migrations_dir))
        {
            TIM_TRACE(error,
                      TIM_TR("Database schema version %u does not match expected %u "
                             "and automatic migration failed; "
                             "rerun db/create-db.sh to regenerate '%s'. "
                             "Persistence is disabled for this run."_en,
                             "Версия схемы БД %u не совпадает с ожидаемой %u, "
                             "а автоматическая миграция не удалась; "
                             "перезапустите db/create-db.sh для пересоздания '%s'. "
                             "Хранение данных в этом запуске отключено."_ru),
                      db_version, tim::EXPECTED_DB_SCHEMA_VERSION,
                      _d->_db->path().string().c_str());
            _d->_db->close();
        }
    }

    const std::filesystem::path host_key_path =
        settings.data_dir / tim::SSH_DATA_SUBDIR / tim::SSH_HOST_KEY_FNAME;
    // Фабрика прикладных сервисов: при каждом новом аутентифицированном
    // SSH-соединении создаётся prompt_service, получающий MQTT, БД
    // и обработчик dispatch() для прокручивания event-loop-а во время
    // Tcl-скриптов.
    _d->_ssh_inetd = tim::ssh_inetd::start(
        settings.ssh_port,
        host_key_path,
        [this, mqtt = _d->_mqtt.get(), db = _d->_db.get()](const tim::ssh_session_info &info)
            -> std::unique_ptr<tim::a_inetd_service>
        {
            return std::make_unique<tim::prompt_service>(
                info, *mqtt, *db,
                [this]{ dispatch(); });
        });
    if (!_d->_ssh_inetd)
        TIM_TRACE(error,
                  TIM_TR("SSH inetd failed to start on port %u; clients cannot connect."_en,
                         "Не удалось запустить SSH-inetd на порту %u; клиенты не смогут подключиться."_ru),
                  settings.ssh_port);

    _d->_post_service.reset(new tim::post_service(*_d->_mqtt, *_d->_db));
    _d->_user_service.reset(new tim::user_service(*_d->_mqtt, *_d->_db));
    _d->_reaction_service.reset(new tim::reaction_service(*_d->_mqtt, *_d->_db));
}

/**
 * Сворачивает подсистемы в обратном порядке и освобождает ресурсы
 * libmongoose. Восстанавливает прежние signal-обработчики SIGINT/SIGTERM.
 */
tim::application::~application()
{
    // Завершаем подсистемы, пока mg_mgr (цикл событий) ещё существует:
    // mqtt_client при уничтожении обращается к таймерам из _mg.
    _d->_reaction_service.reset();
    _d->_user_service.reset();
    _d->_post_service.reset();
    _d->_ssh_inetd.reset();
    _d->_mqtt.reset();
    _d->_db.reset();

    mg_mgr_free(&_d->_mg);

#ifdef TIM_OS_LINUX
    sigaction(SIGINT, &_d->_old_sig_int, nullptr);
    sigaction(SIGTERM, &_d->_old_sig_term, nullptr);
#endif
}

/** \return Имя приложения (например, "tim"). */
const std::string &tim::application::name()
{
    return tim::p::application::name();
}

/**
 * Выставляет имя приложения. Должно быть вызвано один раз до
 * конструирования application — используется для размещения данных.
 *
 * \param name Новое имя приложения.
 */
void tim::application::set_name(const std::string &name)
{
    tim::p::application::name() = name;
}

/** \return Имя организации (например, "mrsu"). */
const std::string &tim::application::org_name()
{
    return tim::p::application::org_name();
}

/**
 * Выставляет имя организации. Должно быть вызвано один раз до
 * конструирования application.
 *
 * \param name Новое имя организации.
 */
void tim::application::set_org_name(const std::string &name)
{
    tim::p::application::org_name() = name;
}

/**
 * \return Каталог, в котором размещаются БД, SSH host-key, история
 *         команд и TLS-сертификаты. Выставляется в конструкторе application
 *         из tim::settings::load_or_create(); после этого доступен
 *         как глобальная константа времени жизни приложения.
 */
const std::filesystem::path &tim::application::data_dir()
{
    return tim::p::application::data_dir();
}

/**
 * Один шаг event-loop без блокировки. Вызывается из DISPATCH-обработчика
 * LIL между Tcl-операторами, чтобы внешние подсистемы продолжали
 * работать во время выполнения скрипта.
 */
void tim::application::dispatch()
{
    mg_mgr_poll(&_d->_mg, 0);
    if (_d->_ssh_inetd)
    {
        _d->_ssh_inetd->dispatch(0);
        // Этот метод вызывается LIL-ом между Tcl-операторами через
        // DISPATCH-обработчик. Если в этот момент инициирован выход
        // (SIGINT установил _quit), прерываем все выполняющиеся скрипты —
        // иначе длительный `while 1 {}` блокировал бы возврат из exec().
        if (_d->_quit)
            _d->_ssh_inetd->interrupt_all();
    }
}

/**
 * Основной цикл приложения. Чередует mg_mgr_poll и ssh_inetd::dispatch
 * с квантом tim::POLL_QUANTUM_MS. Возвращается, когда quit() выставил
 * флаг выхода.
 */
void tim::application::exec()
{
    // Чередуем опрос mongoose (MQTT) и libssh (входящие SSH-соединения).
    // Квант на каждый цикл — приемлемая задержка для чата, при этом
    // холостой цикл опроса не выполняется.
    while (!_d->_quit)
    {
        mg_mgr_poll(&_d->_mg, tim::POLL_QUANTUM_MS);
        if (_d->_ssh_inetd)
            _d->_ssh_inetd->dispatch(tim::POLL_QUANTUM_MS);
    }

    // Сообщение о причине выхода печатается здесь, а не в обработчике
    // сигнала: журналирование не входит в список async-signal-safe
    // операций.
    const int sig = tim::p::application::exit_signal().load();
    if (sig)
        TIM_TRACE(debug, "Exiting on signal %d ...", sig);
}

/**
 * Запрашивает выход из exec(); срабатывает на следующем кванте цикла.
 * Также используется обработчиком SIGINT/SIGTERM.
 */
void tim::application::quit()
{
    _d->_quit = true;
}


// Private
#ifdef TIM_OS_LINUX

void tim::p::application::signal_handler(int sig_num)
{
    switch (sig_num)
    {
        case SIGTERM:
        case SIGINT:
            // Только атомарные записи: журналирование и любые вызовы
            // с выделением памяти в обработчике сигнала недопустимы.
            if (tim::application *app = tim::p::application::instance())
            {
                tim::p::application::exit_signal().store(sig_num);
                app->quit();
            }
            break;

        default:
            break;
    }
}

#endif
