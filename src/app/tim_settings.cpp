#include "tim_settings.h"

#include "tim_file_tools.h"
#include "tim_string_tools.h"
#include "tim_trace.h"

#include <cstdlib>
#include <fstream>
#include <system_error>


namespace
{

/**
 * Возвращает каталог $HOME/.tim, в котором размещаются все файлы TIM.
 *
 * На случай отсутствия $HOME (крайне маловероятно на Linux) приземляемся
 * в текущий каталог — это лучше, чем писать в "/".
 *
 * \return Абсолютный путь к каталогу TIM-пользователя.
 */
std::filesystem::path tim_home_dir()
{
    const char *home = std::getenv("HOME");
    if (home && *home)
        return std::filesystem::path(home) / ".tim";
    return std::filesystem::current_path() / ".tim";
}

/**
 * Шаблон с комментариями, который пишется в config.json при первом запуске.
 * Единственный %s заполняется абсолютным путём data_dir (по умолчанию
 * $HOME/.tim).
 */
const char *const CONFIG_TEMPLATE =
R"({
    // SSH-порт сервера TIM.
    "ssh_port": 2222,

    // URL MQTT-брокера. Для TLS используйте mqtts://, для plaintext — mqtt://.
    "mqtt_url": "mqtts://127.0.0.1:8883",

    // Рабочий каталог: сюда складываются БД (tim.db) и SSH host-key.
    "data_dir": "%s",

    // Каталог со скриптами миграции схемы БД (migrate-N-to-M.sql).
    "migrations_dir": "%s",

    // Язык сообщений: "ru" или "en".
    "language": "ru"
}
)";

}


/**
 * Возвращает путь по умолчанию к файлу конфигурации (~/.tim/config.json).
 *
 * \return Абсолютный путь, построенный от $HOME.
 */
std::filesystem::path tim::settings::default_config_path()
{
    return tim_home_dir() / "config.json";
}

/**
 * Загружает настройки из JSON-файла.
 *
 * Если \a explicit_path пустой, читается default_config_path(); файла
 * нет — создаётся ~/.tim/ и туда пишется шаблон с комментариями.
 * Если \a explicit_path задан, но файла нет — логируется warning и
 * возвращаются значения по умолчанию (шаблон не пишется: оператор сам
 * выбрал путь). При ошибке парсинга также возвращаются значения по
 * умолчанию, процесс не завершается аварийно.
 *
 * \param explicit_path Путь к JSON-файлу или пустая строка для default.
 * \return Заполненная структура tim::settings.
 */
tim::settings tim::settings::load_or_create(const std::string &explicit_path)
{
    tim::settings s;
    s.data_dir = tim_home_dir();

    const bool using_default = explicit_path.empty();
    const std::filesystem::path path = using_default
        ? default_config_path()
        : std::filesystem::path(explicit_path);
    std::error_code ec;

    if (!std::filesystem::exists(path, ec))
    {
        if (!using_default)
        {
            // Оператор задал свой путь через --config, а файла там нет —
            // ничего не создаём, шаблон пишем только по умолчанию.
            TIM_TRACE(warning,
                      "Config file '%s' not found. Using defaults.",
                      path.string().c_str());
            return s;
        }
        // Первый запуск с дефолтным путём: создаём ~/.tim/ и кладём шаблон.
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec)
        {
            TIM_TRACE(warning,
                      "Failed to create config directory '%s': %s. Using defaults.",
                      path.parent_path().string().c_str(),
                      ec.message().c_str());
            return s;
        }
        const std::string body = tim::sprintf(CONFIG_TEMPLATE,
                                              s.data_dir.string().c_str(),
                                              s.migrations_dir.string().c_str());
        if (!tim::write_to_file(path, body))
            TIM_TRACE(warning,
                      "Failed to write default config to '%s'. Using defaults.",
                      path.string().c_str());
        else
            TIM_TRACE(info,
                      "Wrote default config to '%s'.",
                      path.string().c_str());
        return s;
    }

    // Файл есть — читаем и парсим с разрешёнными //- и /* */-комментариями.
    std::ifstream in(path);
    if (!in.is_open())
    {
        TIM_TRACE(warning,
                  "Failed to open config '%s'. Using defaults.",
                  path.string().c_str());
        return s;
    }

    nlohmann::json j = nlohmann::json::parse(in,
                                             /*cb=*/nullptr,
                                             /*allow_exceptions=*/false,
                                             /*ignore_comments=*/true);
    if (j.is_discarded())
    {
        TIM_TRACE(warning,
                  "Failed to parse config '%s'. Using defaults.",
                  path.string().c_str());
        return s;
    }

    // Каждое поле — необязательное; отсутствие в JSON оставляет default.
    if (j.contains("ssh_port"))
        s.ssh_port = j.value("ssh_port", s.ssh_port);
    if (j.contains("mqtt_url"))
        s.mqtt_url = j.value("mqtt_url", s.mqtt_url);
    if (j.contains("data_dir"))
        s.data_dir = j.value("data_dir", s.data_dir.string());
    if (j.contains("migrations_dir"))
        s.migrations_dir = j.value("migrations_dir", s.migrations_dir.string());
    if (j.contains("language"))
        s.language = j.value("language", s.language);

    return s;
}
