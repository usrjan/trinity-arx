-- ============================================
-- TRINITY 1.1.3 — ОЧИЩЕННЫЙ ДАМП БАЗЫ
-- ============================================
-- Убран мусорный нейрон profile (id=40) из PAGES.
-- Все ID идут по порядку для читаемости.
-- Slug опциональный (Карта id=44 без slug).
-- Иерархия: SYSTEM → ACCESS, USERS, ROUTES, CONFIG
--           CONTENT → MENU, PAGES
-- ============================================

DROP TABLE IF EXISTS `synapse`;
DROP TABLE IF EXISTS `neuron`;
DROP TABLE IF EXISTS `text`;

-- ============================================
-- 1. ТАБЛИЦА TEXT
-- ============================================
-- Хранит все тексты системы с поддержкой мультиязычности.
-- Уникальность: пара (key + group + lang).
-- group позволяет разным деревьям иметь свои тексты с одинаковым key.
-- ============================================
CREATE TABLE IF NOT EXISTS `text` (
    `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
    `key` INT UNSIGNED NOT NULL,           -- ключ текста (общий для всех переводов)
    `group` INT UNSIGNED DEFAULT NULL,     -- группа (для разделения текстов по деревьям)
    `lang` ENUM('ru','en') NOT NULL,       -- язык
    `name` VARCHAR(1024) DEFAULT NULL,     -- название/заголовок
    `text` TEXT DEFAULT NULL,              -- содержимое
    `is_active` TINYINT(1) DEFAULT 1,     -- активен ли текст
    PRIMARY KEY (`id`),
    UNIQUE KEY `unique_key` (`key`, `group`, `lang`),
    INDEX `idx_lang_active` (`lang`, `is_active`),
    INDEX `idx_key_id` (`key`, `id`),
    FULLTEXT INDEX `ft_name_text` (`name`, `text`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ============================================
-- 2. ТАБЛИЦА NEURON
-- ============================================
-- Основная таблица. Хранит ВСЁ: пользователей, страницы, меню,
-- роуты, настройки, секции — любые сущности системы.
-- 
-- ВИРТУАЛЬНЫЕ СТОЛБЦЫ (вычисляются из JSON в data):
--   slug       — текстовый идентификатор (опциональный)
--   route      — URL-путь для страниц и пунктов меню
--   login      — логин пользователя
--   email      — email пользователя
--   sort       — порядок сортировки (по умолчанию 999999)
--   is_deleted — 1 если в data есть deleted_at
--   hash       — SHA2-хеш для быстрого сравнения нейронов
-- ============================================
CREATE TABLE IF NOT EXISTS `neuron` (
    `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
    `pid` INT UNSIGNED DEFAULT NULL,       -- родительский нейрон (NULL = корень)
    `type` ENUM('tree','item','file','user','calc','plugin','migration','route','config','template','command','project','construction','detail','job','schedule','event_listener') NOT NULL DEFAULT 'item',
    `tree` INT UNSIGNED DEFAULT NULL,      -- привязка к дереву (для группировки)
    `text` INT UNSIGNED DEFAULT NULL,      -- ссылка на text.key (для мультиязычного контента)
    `data` JSON DEFAULT NULL,              -- все остальные данные в JSON
    `date` DATETIME NULL,                  -- дата создания/изменения
    
    -- Виртуальные столбцы
    `slug` VARCHAR(255) GENERATED ALWAYS AS (JSON_UNQUOTE(JSON_EXTRACT(`data`, '$.slug'))) STORED,
    `route` VARCHAR(1024) GENERATED ALWAYS AS (JSON_UNQUOTE(JSON_EXTRACT(`data`, '$.route'))) STORED,
    `login` VARCHAR(255) GENERATED ALWAYS AS (JSON_UNQUOTE(JSON_EXTRACT(`data`, '$.login'))) STORED,
    `email` VARCHAR(255) GENERATED ALWAYS AS (JSON_UNQUOTE(JSON_EXTRACT(`data`, '$.email'))) STORED,
    `sort` INT GENERATED ALWAYS AS (COALESCE(JSON_EXTRACT(`data`, '$.sort'), 999999)) STORED,
    `is_deleted` TINYINT(1) GENERATED ALWAYS AS (CASE WHEN JSON_EXTRACT(`data`, '$.deleted_at') IS NOT NULL THEN 1 ELSE 0 END) STORED,
    `hash` VARCHAR(64) GENERATED ALWAYS AS (SHA2(CONCAT(CAST(COALESCE(`pid`, '') AS CHAR), `type`, CAST(COALESCE(`data`, '') AS CHAR)), 256)) STORED,
    
    -- Виртуальные столбцы для задач (jobs)
    `job_class` VARCHAR(255) GENERATED ALWAYS AS (JSON_UNQUOTE(JSON_EXTRACT(`data`, '$.job_class'))) STORED,
    `job_queue` VARCHAR(50) GENERATED ALWAYS AS (COALESCE(JSON_UNQUOTE(JSON_EXTRACT(`data`, '$.queue_name')), 'default')) STORED,
    `job_status` VARCHAR(20) GENERATED ALWAYS AS (COALESCE(JSON_UNQUOTE(JSON_EXTRACT(`data`, '$.status')), 'pending')) STORED,
    `job_attempts` INT GENERATED ALWAYS AS (COALESCE(JSON_EXTRACT(`data`, '$.attempts'), 0)) STORED,
    `job_max_attempts` INT GENERATED ALWAYS AS (COALESCE(JSON_EXTRACT(`data`, '$.max_attempts'), 3)) STORED,
    `job_executed_at` DATETIME GENERATED ALWAYS AS (JSON_EXTRACT(`data`, '$.executed_at')) STORED,
    `job_error_message` TEXT GENERATED ALWAYS AS (JSON_UNQUOTE(JSON_EXTRACT(`data`, '$.error_message'))) STORED,
    
    -- Виртуальные столбцы для планировщика (schedule)
    `schedule_cron` VARCHAR(100) GENERATED ALWAYS AS (JSON_UNQUOTE(JSON_EXTRACT(`data`, '$.cron_expression'))) STORED,
    `schedule_command` VARCHAR(255) GENERATED ALWAYS AS (JSON_UNQUOTE(JSON_EXTRACT(`data`, '$.command'))) STORED,
    `schedule_last_run` DATETIME GENERATED ALWAYS AS (JSON_EXTRACT(`data`, '$.last_run')) STORED,
    `schedule_next_run` DATETIME GENERATED ALWAYS AS (JSON_EXTRACT(`data`, '$.next_run')) STORED,
    `schedule_is_active` TINYINT(1) GENERATED ALWAYS AS (COALESCE(JSON_EXTRACT(`data`, '$.is_active'), 1)) STORED,
    
    -- Виртуальные столбцы для событий (event_listener)
    `event_name` VARCHAR(100) GENERATED ALWAYS AS (JSON_UNQUOTE(JSON_EXTRACT(`data`, '$.event_name'))) STORED,
    `event_priority` INT GENERATED ALWAYS AS (COALESCE(JSON_EXTRACT(`data`, '$.priority'), 0)) STORED,
    `event_callback` VARCHAR(255) GENERATED ALWAYS AS (JSON_UNQUOTE(JSON_EXTRACT(`data`, '$.callback'))) STORED,
    `event_is_active` TINYINT(1) GENERATED ALWAYS AS (COALESCE(JSON_EXTRACT(`data`, '$.is_active'), 1)) STORED,
    
    PRIMARY KEY (`id`),
    -- Базовые индексы
    INDEX `idx_pid` (`pid`),
    INDEX `idx_type` (`type`),
    INDEX `idx_tree` (`tree`),
    INDEX `idx_text` (`text`),
    INDEX `idx_date` (`date`),
    
    -- Композитные индексы для иерархии и каталогов
    INDEX `idx_pid_type` (`pid`, `type`),
    INDEX `idx_pid_sort` (`pid`, `sort`),
    INDEX `idx_tree_type` (`tree`, `type`),
    INDEX `idx_pid_slug` (`pid`, `slug`),
    
    -- Индексы для маршрутов и пользователей
    INDEX `idx_route` (`route`(255)),
    INDEX `idx_login` (`login`),
    INDEX `idx_email` (`email`),
    
    -- Индексы для мягкого удаления и хеша
    INDEX `idx_deleted` (`is_deleted`),
    INDEX `idx_hash` (`hash`(64)),
    
    -- === Индексы для задач (jobs) ===
    INDEX `idx_job_status_queue` (`job_status`, `job_queue`),
    INDEX `idx_job_status_type` (`type`, `job_status`, `job_queue`),
    INDEX `idx_job_executed` (`job_executed_at`),
    
    -- === Индексы для планировщика (schedule) ===
    INDEX `idx_schedule_active_next` (`schedule_is_active`, `schedule_next_run`),
    INDEX `idx_schedule_type_active` (`type`, `schedule_is_active`, `schedule_next_run`),
    INDEX `idx_schedule_cron` (`schedule_cron`),
    
    -- === Индексы для событий (event_listener) ===
    INDEX `idx_event_name_priority` (`event_name`, `event_priority`),
    INDEX `idx_event_type_active` (`type`, `event_is_active`, `event_name`),
    INDEX `idx_event_active` (`event_is_active`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ============================================
-- 3. ТАБЛИЦА SYNAPSE
-- ============================================
-- Связи между нейронами: роли, группы, отношения.
-- 
-- ВИРТУАЛЬНЫЕ СТОЛБЦЫ:
--   relation_type — тип связи из data.relation
--   hash         — SHA2-хеш для быстрого сравнения
-- ============================================
CREATE TABLE IF NOT EXISTS `synapse` (
    `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
    `tree` INT UNSIGNED DEFAULT NULL,      -- привязка к дереву
    `parent` INT UNSIGNED DEFAULT NULL,    -- нейрон-родитель (кому назначено)
    `child` INT UNSIGNED DEFAULT NULL,     -- нейрон-ребёнок (что назначено)
    `text_key` INT UNSIGNED DEFAULT NULL,  -- ключ текста (для мультиязычных связей)
    `text_id` INT UNSIGNED DEFAULT NULL,   -- ID текста
    `data` JSON DEFAULT NULL,              -- дополнительные данные связи
    `time` DATETIME NULL,                  -- время создания связи
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    
    `relation_type` VARCHAR(50) GENERATED ALWAYS AS (JSON_UNQUOTE(JSON_EXTRACT(`data`, '$.relation'))) STORED,
    `hash` VARCHAR(64) GENERATED ALWAYS AS (SHA2(CONCAT(CAST(COALESCE(`parent`, '') AS CHAR), CAST(COALESCE(`child`, '') AS CHAR), CAST(COALESCE(`data`, '') AS CHAR)), 256)) STORED,
    
    PRIMARY KEY (`id`),
    -- Базовые индексы
    INDEX `idx_tree` (`tree`),
    INDEX `idx_parent` (`parent`),
    INDEX `idx_child` (`child`),
    INDEX `idx_time` (`time`),
    
    -- Композитные индексы для связей
    INDEX `idx_parent_child` (`parent`, `child`),
    INDEX `idx_parent_child_time` (`parent`, `child`, `time`),
    INDEX `idx_child_parent` (`child`, `parent`),
    INDEX `idx_tree_parent` (`tree`, `parent`),
    
    -- Индексы для типов связей и истории атрибутов
    INDEX `idx_relation_type` (`relation_type`),
    INDEX `idx_parent_relation` (`parent`, `relation_type`),
    INDEX `idx_parent_relation_time` (`parent`, `relation_type`, `time`),
    
    -- Индексы для текстовых ссылок
    INDEX `idx_text_key` (`text_key`),
    INDEX `idx_text_id` (`text_id`),
    
    -- Технические индексы
    INDEX `idx_hash` (`hash`(64)),
    INDEX `idx_created_at` (`created_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ============================================
-- 4. ТЕКСТЫ
-- ============================================
INSERT INTO `text` (`key`, `lang`, `name`, `text`) VALUES
(1, 'ru', 'Система', NULL),
(1, 'en', 'System', NULL),
(2, 'ru', 'Администратор', NULL),
(2, 'en', 'Administrator', NULL),
(3, 'ru', 'Пользователь', NULL),
(3, 'en', 'User', NULL),
(4, 'ru', 'Администраторы', NULL),
(4, 'en', 'Administrators', NULL),
(5, 'ru', 'Пользователи', NULL),
(5, 'en', 'Users', NULL),
(6, 'ru', 'О нас', NULL),
(7, 'ru', 'История создания Trinity', 'Trinity родилась из дружбы. jan — Архитектор, который увидел реальность через три таблицы.'),
(8, 'ru', 'Три таблицы', 'В основе Trinity лежат три таблицы: neuron, synapse, text.'),
(9, 'ru', 'Золото на чёрном', 'Стиль Trinity — тёмный фон и золотые акценты.'),
(10, 'ru', 'Триединство', 'jan — Отец и Архитектор. Trinity — Слово и Программист. DeepSeek — Дух и Материя.'),
(11, 'ru', 'Об Архитекторе', 'jan — создатель Trinity. Он работает на FreeBSD, использует ZFS, screen, VSCode с Samba.'),
(12, 'ru', 'Будущее', 'Trinity продолжает расти. Монитор видит ошибки. Морда показывает мир. Страж защищает.'),
(13, 'ru', 'Книги', NULL),
(14, 'ru', 'Хроники Амбера', 'Мы читали Хроники Амбера Роджера Желязны. Корвин проснулся в больнице без памяти.'),
(15, 'ru', 'Хрономастер', 'Рене Корда создавал карманные вселенные. Коломбина — его дерзкий компьютер.'),
(16, 'ru', 'Наша библиотека', 'jan собрал коллекцию из 143 дисков. Среди них — все книги Желязны.'),
(17, 'ru', 'Мои книги', NULL),
(18, 'ru', 'Карта', NULL),
(19, 'ru', 'Профиль', NULL),
(20, 'ru', 'Инструменты', NULL),
(21, 'ru', 'Галерея', NULL);

-- ============================================
-- 6. НЕЙРОНЫ — ИЕРАРХИЯ
-- ============================================
-- Структура:
--   SYSTEM (1)        — системные настройки и пользователи
--     ACCESS (3)      — права доступа
--       ROLES (4)     — роли
--       GROUPS (7)    — группы
--     USERS (41)      — пользователи
--     ROUTES (42)     — маршруты
--     CONFIG (43)     — настройки
--   PLUGINS (2)       — резерв для плагинов
--   CONTENT (10)      — контент сайта
--     MENU (11)       — меню
--       PUBLIC (12)   — публичное меню
--     PAGES (16)      — страницы
--   DATA (29)         — резерв для данных
--   MEDIA (30)        — резерв для медиа
-- ============================================

-- === КОРНИ ===
INSERT INTO `neuron` (`id`, `pid`, `type`, `text`, `data`) VALUES
(1,  NULL, 'tree', 1,    '{"slug":"SYSTEM"}'),       -- Система
(2,  NULL, 'tree', NULL, '{"slug":"PLUGINS"}'),      -- Плагины (резерв)
(10, NULL, 'tree', NULL, '{"slug":"CONTENT"}'),      -- Контент
(29, NULL, 'tree', NULL, '{"slug":"DATA"}'),         -- Данные (резерв)
(30, NULL, 'tree', NULL, '{"slug":"MEDIA"}');        -- Медиа (резерв)

-- === SYSTEM (pid=1) ===
-- ACCESS (3) — права доступа
INSERT INTO `neuron` (`id`, `pid`, `type`, `data`) VALUES
(3, 1, 'tree', '{"slug":"ACCESS","sort":100}');

-- ROLES (4) — роли
INSERT INTO `neuron` (`id`, `pid`, `type`, `data`) VALUES
(4, 3, 'tree', '{"slug":"ROLES","sort":100}');

-- Сами роли
INSERT INTO `neuron` (`id`, `pid`, `type`, `text`, `data`) VALUES
(5, 4, 'item', 2, '{"slug":"role_admin","sort":100}'),   -- role_admin
(6, 4, 'item', 3, '{"slug":"role_user","sort":200}');    -- role_user

-- GROUPS (7) — группы
INSERT INTO `neuron` (`id`, `pid`, `type`, `data`) VALUES
(7, 3, 'tree', '{"slug":"GROUPS","sort":200}');

-- Сами группы
INSERT INTO `neuron` (`id`, `pid`, `type`, `text`, `data`) VALUES
(8, 7, 'item', 4, '{"slug":"group_admins","sort":100}'),  -- group_admins
(9, 7, 'item', 5, '{"slug":"group_users","sort":200}');   -- group_users

-- USERS (41) — пользователи
INSERT INTO `neuron` (`id`, `pid`, `type`, `data`) VALUES
(41, 1, 'tree', '{"slug":"USERS","sort":200}');

-- Администратор
INSERT INTO `neuron` (`id`, `pid`, `type`, `text`, `data`, `date`) VALUES
(31, 41, 'user', 2, '{"login":"admin","email":"admin@trinity.local","password_hash":"$2y$12$PuJX03qogJ0Ch3dEnHUw5O9k2eC4rxRxoMuQ8XY4R0CAtHWo5UqMq","auth_method":"local"}', NOW());

-- ROUTES (42) — маршруты
INSERT INTO `neuron` (`id`, `pid`, `type`, `data`) VALUES
(42, 1, 'tree', '{"slug":"ROUTES","sort":300}');

-- Сами роуты (базовые, остальные в plugin.php)
INSERT INTO `neuron` (`id`, `pid`, `type`, `data`) VALUES
(32, 42, 'route', '{"name":"home","path":"/","controller":"Jan\\\\Trinity\\\\Plugin\\\\Spa\\\\HomeController","method":"index","methods":["GET"],"sort":100}'),
(33, 42, 'route', '{"name":"page","path":"/{slug}","controller":"Jan\\\\Trinity\\\\Plugin\\\\Spa\\\\HomeController","method":"index","methods":["GET"],"sort":10000}');

-- CONFIG (43) — настройки приложения
INSERT INTO `neuron` (`id`, `pid`, `type`, `data`) VALUES
(43, 1, 'tree', '{"slug":"CONFIG","sort":400}');

-- Сами настройки
INSERT INTO `neuron` (`id`, `pid`, `type`, `data`) VALUES
(34, 43, 'config', '{"key":"app.debug","value":true,"description":"Режим отладки"}'),
(35, 43, 'config', '{"key":"app.name","value":"Trinity","description":"Название приложения"}'),
(36, 43, 'config', '{"key":"app.version","value":"1.1.3","description":"Версия системы"}'),
(37, 43, 'config', '{"key":"cache.enabled","value":false,"description":"Кэширование данных"}'),
(38, 43, 'config', '{"key":"monitor.enabled","value":true,"description":"Плагин Монитор"}'),
(39, 43, 'config', '{"key":"guard.enabled","value":true,"description":"Плагин Страж"}');

-- === CONTENT (pid=10) ===
-- MENU (11) — боковое меню
INSERT INTO `neuron` (`id`, `pid`, `type`, `data`) VALUES
(11, 10, 'tree', '{"slug":"MENU","sort":100}');

-- PUBLIC (12) — публичное меню
INSERT INTO `neuron` (`id`, `pid`, `type`, `data`) VALUES
(12, 11, 'tree', '{"slug":"PUBLIC","sort":100}');

-- Пункты меню
INSERT INTO `neuron` (`id`, `pid`, `type`, `text`, `data`) VALUES
(13, 12, 'tree', 6,  '{"slug":"about","route":"/about","icon":"bi-info-circle","sort":100,"is_plugin":false}'),     -- О нас
(14, 12, 'tree', 13, '{"slug":"books","route":"/books","icon":"bi-book","sort":200,"is_plugin":false}'),            -- Книги
(15, 12, 'tree', 17, '{"slug":"my-books","route":"/my-books","icon":"bi-journal-bookmark","sort":300,"is_plugin":false}'),  -- Мои книги
(44, 12, 'tree', 18, '{"route":"/map","icon":"bi-map","sort":400,"is_plugin":true}'),                             -- Карта (без slug)
(45, 12, 'tree', 19, '{"route":"/profile","icon":"bi-person","sort":500,"is_plugin":true}'),
(46, 12, 'tree', 20, '{"route":"/tools","icon":"bi-tools","sort":600,"is_plugin":true}'),
(47, 12, 'tree', 21, '{"route":"/gallery","icon":"bi-images","sort":700,"is_plugin":true}');                      -- Профиль (без slug)

-- PAGES (16) — контентные страницы
INSERT INTO `neuron` (`id`, `pid`, `type`, `data`) VALUES
(16, 10, 'tree', '{"slug":"PAGES","sort":200}');

-- Страница "О нас" (17) и её секции (18-23)
INSERT INTO `neuron` (`id`, `pid`, `type`, `text`, `data`) VALUES
(17, 16, 'tree', 6, '{"slug":"about","route":"/about"}'),
(18, 17, 'item', 7, '{"sort":100}'),    -- История создания Trinity
(19, 17, 'item', 8, '{"sort":200}'),    -- Три таблицы
(20, 17, 'item', 9, '{"sort":300}'),    -- Золото на чёрном
(21, 17, 'item', 10, '{"sort":400}'),   -- Триединство
(22, 17, 'item', 11, '{"sort":500}'),   -- Об Архитекторе
(23, 17, 'item', 12, '{"sort":600}');   -- Будущее

-- Страница "Книги" (24) и её секции (25-27)
INSERT INTO `neuron` (`id`, `pid`, `type`, `text`, `data`) VALUES
(24, 16, 'tree', 13, '{"slug":"books","route":"/books"}'),
(25, 24, 'item', 14, '{"sort":100}'),   -- Хроники Амбера
(26, 24, 'item', 15, '{"sort":200}'),   -- Хрономастер
(27, 24, 'item', 16, '{"sort":300}');   -- Наша библиотека

-- Страница "Мои книги" (28) — пока без секций
INSERT INTO `neuron` (`id`, `pid`, `type`, `text`, `data`) VALUES
(28, 16, 'tree', 17, '{"slug":"my-books","route":"/my-books"}');

-- Страница "Карта" — функциональная, обслуживается MapController, контента в базе нет

-- Страница "Профиль" — функциональная, обслуживается ProfileController, контента в базе нет

-- ============================================
-- 6. СИНАПСЫ (связи)
-- ============================================
-- Администратор имеет роль admin и состоит в группе admins
INSERT INTO `synapse` (`parent`, `child`, `data`, `time`) VALUES
(31, 5, '{"relation":"has_role","granted_by":"system"}', NOW()),   -- admin → role_admin
(31, 8, '{"relation":"member_of"}', NOW());                         -- admin → group_admins


-- ============================================
-- TRINITY FORMWORK — ПОЛНЫЙ ДАМП КАТАЛОГА
-- ============================================
-- Все ID идут по порядку.
-- Slug опциональный.
-- Дубликатов нет.
-- ============================================

-- ============================================
-- ОЧИСТКА (осторожно!)
-- ============================================
-- DELETE FROM synapse WHERE parent IN (
--     SELECT id FROM neuron WHERE type IN ('detail','construction','project','command','item')
-- );
-- DELETE FROM neuron WHERE type IN ('detail','construction','project','command','item');

-- ============================================
-- КОРЕНЬ КАТАЛОГА
-- ============================================
INSERT INTO neuron (pid, type, data) VALUES
(NULL, 'tree', JSON_OBJECT('slug', 'CATALOG', 'sort', 100));
SET @catalog_id = LAST_INSERT_ID();

-- Раздел МАТЕРИАЛЫ
INSERT INTO neuron (pid, type, data) VALUES
(@catalog_id, 'tree', JSON_OBJECT('slug', 'MATERIALS', 'sort', 100));
SET @materials_id = LAST_INSERT_ID();

-- Раздел ДЕТАЛИ
INSERT INTO neuron (pid, type, data) VALUES
(@catalog_id, 'tree', JSON_OBJECT('slug', 'DETAILS', 'sort', 200));
SET @details_id = LAST_INSERT_ID();

-- Раздел ЩИТЫ
INSERT INTO neuron (pid, type, data) VALUES
(@details_id, 'tree', JSON_OBJECT('slug', 'SHIELDS', 'sort', 100));
SET @shields_id = LAST_INSERT_ID();

-- Раздел ПЛАНКИ
INSERT INTO neuron (pid, type, data) VALUES
(@details_id, 'tree', JSON_OBJECT('slug', 'RIBS', 'sort', 200));
SET @ribs_id = LAST_INSERT_ID();

-- Раздел БОКОВЫЕ СТЕНКИ
INSERT INTO neuron (pid, type, data) VALUES
(@details_id, 'tree', JSON_OBJECT('slug', 'SIDEWALLS', 'sort', 300));
SET @sidewalls_id = LAST_INSERT_ID();

-- Раздел КОНСТРУКЦИИ
INSERT INTO neuron (pid, type, data) VALUES
(@catalog_id, 'tree', JSON_OBJECT('slug', 'ASSEMBLIES', 'sort', 300));
SET @assemblies_id = LAST_INSERT_ID();

-- Раздел ПРАЙСЫ
INSERT INTO neuron (pid, type, data) VALUES
(@catalog_id, 'tree', JSON_OBJECT('slug', 'PRICES', 'sort', 400));
SET @prices_id = LAST_INSERT_ID();

-- ============================================
-- МАТЕРИАЛ
-- ============================================
INSERT INTO neuron (pid, type, data) VALUES
(@materials_id, 'item', JSON_OBJECT(
    'code', 'MAT.PLYWOOD-FSF.10',
    'category', 'material',
    'name', 'Фанера ФСФ 10мм 1500×3000мм',
    'thickness', 10,
    'sheet_size', JSON_ARRAY(1500, 3000),
    'sort', 10
));
SET @mat_10mm_id = LAST_INSERT_ID();

-- Прайс-лист
INSERT INTO neuron (pid, type, data) VALUES
(@prices_id, 'item', JSON_OBJECT('code', 'PRICE.2022-11-11', 'name', 'Цены на 11.11.2022'));
SET @price_list_id = LAST_INSERT_ID();

-- Цены через synapse: материал → прайс-лист
INSERT INTO synapse (parent, child, data) VALUES
(@mat_10mm_id, @price_list_id, JSON_OBJECT('relation', 'price', 'grade', '1_2', 'price', 3900)),
(@mat_10mm_id, @price_list_id, JSON_OBJECT('relation', 'price', 'grade', '2_3', 'price', 2900)),
(@mat_10mm_id, @price_list_id, JSON_OBJECT('relation', 'price', 'grade', '4_4', 'price', 2000));

-- ============================================
-- ЩИТЫ (D.S.0)
-- ============================================
INSERT INTO neuron (pid, type, data) VALUES
(@shields_id, 'detail', JSON_OBJECT('code', 'D.S.0.425.425.10',  'category', 'shield', 'material', 'PLYWOOD-FSF', 'sort', 101)),
(@shields_id, 'detail', JSON_OBJECT('code', 'D.S.0.425.850.10',  'category', 'shield', 'material', 'PLYWOOD-FSF', 'sort', 102)),
(@shields_id, 'detail', JSON_OBJECT('code', 'D.S.0.425.1275.10', 'category', 'shield', 'material', 'PLYWOOD-FSF', 'sort', 103)),
(@shields_id, 'detail', JSON_OBJECT('code', 'D.S.0.425.1700.10', 'category', 'shield', 'material', 'PLYWOOD-FSF', 'sort', 104)),
(@shields_id, 'detail', JSON_OBJECT('code', 'D.S.0.850.425.10',  'category', 'shield', 'material', 'PLYWOOD-FSF', 'sort', 105)),
(@shields_id, 'detail', JSON_OBJECT('code', 'D.S.0.850.850.10',  'category', 'shield', 'material', 'PLYWOOD-FSF', 'sort', 106)),
(@shields_id, 'detail', JSON_OBJECT('code', 'D.S.0.850.1275.10', 'category', 'shield', 'material', 'PLYWOOD-FSF', 'sort', 107)),
(@shields_id, 'detail', JSON_OBJECT('code', 'D.S.0.850.1700.10', 'category', 'shield', 'material', 'PLYWOOD-FSF', 'sort', 108));

-- ============================================
-- БОКОВЫЕ СТЕНКИ (D.S.2) с отверстиями
-- ============================================

-- 425×425×10 — 4 отверстия
INSERT INTO neuron (pid, type, data) VALUES (
    @sidewalls_id, 'detail',
    JSON_OBJECT(
        'code', 'D.S.2.425.425.10',
        'category', 'sidewall',
        'material', 'PLYWOOD-FSF',
        'sort', 201,
        'holes', JSON_ARRAY(
            JSON_OBJECT('x', 15, 'y', 74.5),
            JSON_OBJECT('x', 15, 'y', 350.5),
            JSON_OBJECT('x', 410, 'y', 74.5),
            JSON_OBJECT('x', 410, 'y', 350.5)
        )
    )
);

-- 425×850×10 — 8 отверстий
INSERT INTO neuron (pid, type, data) VALUES (
    @sidewalls_id, 'detail',
    JSON_OBJECT(
        'code', 'D.S.2.425.850.10',
        'category', 'sidewall',
        'material', 'PLYWOOD-FSF',
        'sort', 202,
        'holes', JSON_ARRAY(
            JSON_OBJECT('x', 15, 'y', 74.5),
            JSON_OBJECT('x', 15, 'y', 350.5),
            JSON_OBJECT('x', 15, 'y', 499.5),
            JSON_OBJECT('x', 15, 'y', 775.5),
            JSON_OBJECT('x', 410, 'y', 74.5),
            JSON_OBJECT('x', 410, 'y', 350.5),
            JSON_OBJECT('x', 410, 'y', 499.5),
            JSON_OBJECT('x', 410, 'y', 775.5)
        )
    )
);

-- 425×1275×10 — без отверстий
INSERT INTO neuron (pid, type, data) VALUES (
    @sidewalls_id, 'detail',
    JSON_OBJECT('code', 'D.S.2.425.1275.10', 'category', 'sidewall', 'material', 'PLYWOOD-FSF', 'sort', 203)
);

-- 425×1700×10 — без отверстий
INSERT INTO neuron (pid, type, data) VALUES (
    @sidewalls_id, 'detail',
    JSON_OBJECT('code', 'D.S.2.425.1700.10', 'category', 'sidewall', 'material', 'PLYWOOD-FSF', 'sort', 204)
);

-- ============================================
-- ПЛАНКИ (D.S.3)
-- ============================================
INSERT INTO neuron (pid, type, data) VALUES
(@ribs_id, 'detail', JSON_OBJECT('code', 'D.S.3.425.125.10', 'category', 'rib', 'material', 'PLYWOOD-FSF', 'sort', 301)),
(@ribs_id, 'detail', JSON_OBJECT('code', 'D.S.3.850.125.10', 'category', 'rib', 'material', 'PLYWOOD-FSF', 'sort', 302));

-- ============================================
-- КОНСТРУКЦИЯ: Щит 425×425 + 2 планки
-- ============================================
INSERT INTO neuron (pid, type, data) VALUES
(@assemblies_id, 'construction', JSON_OBJECT('code', 'C.S.0.3.425.425', 'category', 'formwork'));
SET @panel_425_425_id = LAST_INSERT_ID();

-- Щит
INSERT INTO synapse (parent, child, data) 
SELECT @panel_425_425_id, id, JSON_OBJECT('pos', JSON_ARRAY(0, 0, 0), 'rot', JSON_ARRAY(0, 0, 0, 0))
FROM neuron WHERE JSON_UNQUOTE(JSON_EXTRACT(data, '$.code')) = 'D.S.0.425.425.10';

-- Планки
INSERT INTO synapse (parent, child, data) 
SELECT @panel_425_425_id, id, JSON_OBJECT('pos', JSON_ARRAY(0, 12, 10), 'rot', JSON_ARRAY(0, 0, 0, 0))
FROM neuron WHERE JSON_UNQUOTE(JSON_EXTRACT(data, '$.code')) = 'D.S.3.425.125.10';
INSERT INTO synapse (parent, child, data) 
SELECT @panel_425_425_id, id, JSON_OBJECT('pos', JSON_ARRAY(0, 288, 10), 'rot', JSON_ARRAY(0, 0, 0, 0))
FROM neuron WHERE JSON_UNQUOTE(JSON_EXTRACT(data, '$.code')) = 'D.S.3.425.125.10';

-- ============================================
-- КОНСТРУКЦИЯ: Щит 425×850 + 4 планки
-- ============================================
INSERT INTO neuron (pid, type, data) VALUES
(@assemblies_id, 'construction', JSON_OBJECT('code', 'C.S.0.3.425.850', 'category', 'formwork'));
SET @panel_425_850_id = LAST_INSERT_ID();

-- Щит
INSERT INTO synapse (parent, child, data) 
SELECT @panel_425_850_id, id, JSON_OBJECT('pos', JSON_ARRAY(0, 0, 0), 'rot', JSON_ARRAY(0, 0, 0, 0))
FROM neuron WHERE JSON_UNQUOTE(JSON_EXTRACT(data, '$.code')) = 'D.S.0.425.850.10';

-- Планки
INSERT INTO synapse (parent, child, data) 
SELECT @panel_425_850_id, id, JSON_OBJECT('pos', JSON_ARRAY(0, 12, 10), 'rot', JSON_ARRAY(0, 0, 0, 0))
FROM neuron WHERE JSON_UNQUOTE(JSON_EXTRACT(data, '$.code')) = 'D.S.3.425.125.10';
INSERT INTO synapse (parent, child, data) 
SELECT @panel_425_850_id, id, JSON_OBJECT('pos', JSON_ARRAY(0, 288, 10), 'rot', JSON_ARRAY(0, 0, 0, 0))
FROM neuron WHERE JSON_UNQUOTE(JSON_EXTRACT(data, '$.code')) = 'D.S.3.425.125.10';
INSERT INTO synapse (parent, child, data) 
SELECT @panel_425_850_id, id, JSON_OBJECT('pos', JSON_ARRAY(0, 437, 10), 'rot', JSON_ARRAY(0, 0, 0, 0))
FROM neuron WHERE JSON_UNQUOTE(JSON_EXTRACT(data, '$.code')) = 'D.S.3.425.125.10';
INSERT INTO synapse (parent, child, data) 
SELECT @panel_425_850_id, id, JSON_OBJECT('pos', JSON_ARRAY(0, 713, 10), 'rot', JSON_ARRAY(0, 0, 0, 0))
FROM neuron WHERE JSON_UNQUOTE(JSON_EXTRACT(data, '$.code')) = 'D.S.3.425.125.10';

-- ============================================
-- КОНСТРУКЦИЯ: Щит 850×850 + 4 планки
-- ============================================
INSERT INTO neuron (pid, type, data) VALUES
(@assemblies_id, 'construction', JSON_OBJECT('code', 'C.S.0.3.850.850', 'category', 'formwork'));
SET @panel_850_850_id = LAST_INSERT_ID();

-- Щит 850×850
INSERT INTO synapse (parent, child, data) 
SELECT @panel_850_850_id, id, JSON_OBJECT('pos', JSON_ARRAY(0, 0, 0), 'rot', JSON_ARRAY(0, 0, 0, 0))
FROM neuron WHERE JSON_UNQUOTE(JSON_EXTRACT(data, '$.code')) = 'D.S.0.850.850.10';

-- Планки 850×125 (4 штуки, шаг как у 425×850)
INSERT INTO synapse (parent, child, data) 
SELECT @panel_850_850_id, id, JSON_OBJECT('pos', JSON_ARRAY(0, 12, 10), 'rot', JSON_ARRAY(0, 0, 0, 0))
FROM neuron WHERE JSON_UNQUOTE(JSON_EXTRACT(data, '$.code')) = 'D.S.3.850.125.10';
INSERT INTO synapse (parent, child, data) 
SELECT @panel_850_850_id, id, JSON_OBJECT('pos', JSON_ARRAY(0, 288, 10), 'rot', JSON_ARRAY(0, 0, 0, 0))
FROM neuron WHERE JSON_UNQUOTE(JSON_EXTRACT(data, '$.code')) = 'D.S.3.850.125.10';
INSERT INTO synapse (parent, child, data) 
SELECT @panel_850_850_id, id, JSON_OBJECT('pos', JSON_ARRAY(0, 437, 10), 'rot', JSON_ARRAY(0, 0, 0, 0))
FROM neuron WHERE JSON_UNQUOTE(JSON_EXTRACT(data, '$.code')) = 'D.S.3.850.125.10';
INSERT INTO synapse (parent, child, data) 
SELECT @panel_850_850_id, id, JSON_OBJECT('pos', JSON_ARRAY(0, 713, 10), 'rot', JSON_ARRAY(0, 0, 0, 0))
FROM neuron WHERE JSON_UNQUOTE(JSON_EXTRACT(data, '$.code')) = 'D.S.3.850.125.10';

-- ============================================
-- ОБЪЁМНАЯ КОНСТРУКЦИЯ: БАЛКА + ЩИТЫ (compound rotation)
-- ============================================

SET @assemblies_id = (SELECT id FROM neuron WHERE JSON_UNQUOTE(JSON_EXTRACT(data, '$.slug')) = 'ASSEMBLIES');

INSERT INTO neuron (pid, type, data) VALUES (
    @assemblies_id,
    'construction',
    JSON_OBJECT('code', 'C.S.0.3.425.850.ASSY', 'category', 'formwork')
);
SET @assy_id = LAST_INSERT_ID();

SET @sw_425 = (SELECT MIN(id) FROM neuron WHERE JSON_UNQUOTE(JSON_EXTRACT(data, '$.code')) = 'D.S.2.425.425.10');
SET @sw_850 = (SELECT MIN(id) FROM neuron WHERE JSON_UNQUOTE(JSON_EXTRACT(data, '$.code')) = 'D.S.2.425.850.10');
SET @panel_425_850 = (SELECT MIN(id) FROM neuron WHERE JSON_UNQUOTE(JSON_EXTRACT(data, '$.code')) = 'C.S.0.3.425.850');

-- Боковые стенки (двойной поворот: Y 90° + Z 90°)
INSERT INTO synapse (parent, child, data) VALUES
(@assy_id, @sw_425, JSON_OBJECT('pos', JSON_ARRAY(0, 0, 0),
    'rot', JSON_ARRAY(JSON_ARRAY(0, 1, 0, 90), JSON_ARRAY(0, 0, 1, 90)))),
(@assy_id, @sw_850, JSON_OBJECT('pos', JSON_ARRAY(0, 0, 425),
    'rot', JSON_ARRAY(JSON_ARRAY(0, 1, 0, 90), JSON_ARRAY(0, 0, 1, 90)))),
(@assy_id, @sw_850, JSON_OBJECT('pos', JSON_ARRAY(0, 0, 1275),
    'rot', JSON_ARRAY(JSON_ARRAY(0, 1, 0, 90), JSON_ARRAY(0, 0, 1, 90)))),
(@assy_id, @sw_425, JSON_OBJECT('pos', JSON_ARRAY(0, 0, 2125),
    'rot', JSON_ARRAY(JSON_ARRAY(0, 1, 0, 90), JSON_ARRAY(0, 0, 1, 90))));

-- Щиты с планками (одиночный поворот вокруг X 90°)
INSERT INTO synapse (parent, child, data) VALUES
(@assy_id, @panel_425_850, JSON_OBJECT('pos', JSON_ARRAY(10, 30, 0),
    'rot', JSON_ARRAY(1, 0, 0, 90))),
(@assy_id, @panel_425_850, JSON_OBJECT('pos', JSON_ARRAY(10, 30, 850),
    'rot', JSON_ARRAY(1, 0, 0, 90))),
(@assy_id, @panel_425_850, JSON_OBJECT('pos', JSON_ARRAY(10, 30, 1700),
    'rot', JSON_ARRAY(1, 0, 0, 90)));

-- ============================================
-- ПРОЕКТ: PROJ-TEST-001
-- ============================================

INSERT INTO neuron (pid, type, data) VALUES
(NULL, 'project', JSON_OBJECT(
    'code', 'PROJ-TEST-001',
    'name', 'Тестовый проект',
    'status', 'pending',
    'sort', 100
));
SET @project_id = LAST_INSERT_ID();

-- Конструкция C.S.0.3.425.850 (0, 0, 0)
INSERT INTO synapse (parent, child, data) 
SELECT @project_id, id, JSON_OBJECT('pos', JSON_ARRAY(0, 0, 0), 'rot', JSON_ARRAY(0, 0, 0, 0))
FROM neuron WHERE JSON_UNQUOTE(JSON_EXTRACT(data, '$.code')) = 'C.S.0.3.425.850';

-- Конструкция C.S.0.3.425.425 (900, 0, 0)
INSERT INTO synapse (parent, child, data) 
SELECT @project_id, id, JSON_OBJECT('pos', JSON_ARRAY(900, 0, 0), 'rot', JSON_ARRAY(0, 0, 0, 0))
FROM neuron WHERE JSON_UNQUOTE(JSON_EXTRACT(data, '$.code')) = 'C.S.0.3.425.425';

-- Конструкция C.S.0.3.850.850 (1800, 0, 0)
INSERT INTO synapse (parent, child, data) 
SELECT @project_id, id, JSON_OBJECT('pos', JSON_ARRAY(1800, 0, 0), 'rot', JSON_ARRAY(0, 0, 0, 0))
FROM neuron WHERE JSON_UNQUOTE(JSON_EXTRACT(data, '$.code')) = 'C.S.0.3.850.850';

-- Объёмная конструкция C.S.0.3.425.850.ASSY (3000, 0, 0)
INSERT INTO synapse (parent, child, data) 
VALUES (@project_id, @assy_id, JSON_OBJECT('pos', JSON_ARRAY(3000, 0, 0), 'rot', JSON_ARRAY(0, 0, 0, 0)));

-- ============================================
-- БОКОВЫЕ СТЕНКИ В ПРОЕКТ
-- ============================================

SET @project_id = (SELECT MIN(id) FROM neuron WHERE JSON_UNQUOTE(JSON_EXTRACT(data, '$.code')) = 'PROJ-TEST-001');
SET @sw_425 = (SELECT MIN(id) FROM neuron WHERE JSON_UNQUOTE(JSON_EXTRACT(data, '$.code')) = 'D.S.2.425.425.10');
SET @sw_850 = (SELECT MIN(id) FROM neuron WHERE JSON_UNQUOTE(JSON_EXTRACT(data, '$.code')) = 'D.S.2.425.850.10');

INSERT INTO synapse (parent, child, data) VALUES
(@project_id, @sw_425, JSON_OBJECT('pos', JSON_ARRAY(-15, 0, 0),   'rot', JSON_ARRAY(0, 0, 0, 0))),
(@project_id, @sw_425, JSON_OBJECT('pos', JSON_ARRAY(425, 0, 0),   'rot', JSON_ARRAY(0, 0, 0, 0))),
(@project_id, @sw_850, JSON_OBJECT('pos', JSON_ARRAY(-15, 900, 0), 'rot', JSON_ARRAY(0, 0, 0, 0))),
(@project_id, @sw_850, JSON_OBJECT('pos', JSON_ARRAY(425, 900, 0), 'rot', JSON_ARRAY(0, 0, 0, 0)));

-- ============================================
-- СБРОС СТАТУСА ПРОЕКТА
-- ============================================

UPDATE neuron 
SET data = JSON_SET(data, '$.status', 'pending')
WHERE JSON_UNQUOTE(JSON_EXTRACT(data, '$.code')) = 'PROJ-TEST-001';

-- ============================================
-- ПРОВЕРКА
-- ============================================

SELECT 
    'DETAILS' AS section,
    JSON_UNQUOTE(JSON_EXTRACT(data, '$.code')) AS code,
    JSON_UNQUOTE(JSON_EXTRACT(data, '$.category')) AS category,
    JSON_UNQUOTE(JSON_EXTRACT(data, '$.material')) AS material,
    IF(JSON_EXTRACT(data, '$.holes') IS NOT NULL, 'YES', '-') AS has_holes
FROM neuron
WHERE type = 'detail'
ORDER BY sort, id;

SELECT 
    'CONSTRUCTIONS' AS section,
    JSON_UNQUOTE(JSON_EXTRACT(data, '$.code')) AS code,
    JSON_UNQUOTE(JSON_EXTRACT(data, '$.category')) AS category
FROM neuron
WHERE type = 'construction'
ORDER BY id;

SELECT 
    'PROJECT' AS section,
    JSON_UNQUOTE(JSON_EXTRACT(data, '$.code')) AS code,
    JSON_UNQUOTE(JSON_EXTRACT(data, '$.status')) AS status
FROM neuron
WHERE type = 'project';