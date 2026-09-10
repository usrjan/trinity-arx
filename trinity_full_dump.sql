-- ============================================
-- TRINITY 1.2.2 — ПОЛНЫЙ ДАМП БАЗЫ ДАННЫХ
-- ============================================

DROP TABLE IF EXISTS `synapse`;
DROP TABLE IF EXISTS `neuron`;
DROP TABLE IF EXISTS `text`;

-- ТАБЛИЦА TEXT
CREATE TABLE IF NOT EXISTS `text` (
    `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
    `key` INT UNSIGNED NOT NULL,
    `group` INT UNSIGNED DEFAULT NULL,
    `lang` ENUM('ru','en') NOT NULL,
    `name` VARCHAR(1024) DEFAULT NULL,
    `text` TEXT DEFAULT NULL,
    `is_active` TINYINT(1) DEFAULT 1,
    PRIMARY KEY (`id`),
    UNIQUE KEY `unique_key` (`key`, `group`, `lang`),
    INDEX `idx_lang_active` (`lang`, `is_active`),
    INDEX `idx_key_id` (`key`, `id`),
    FULLTEXT INDEX `ft_name_text` (`name`, `text`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ТАБЛИЦА NEURON
CREATE TABLE IF NOT EXISTS `neuron` (
    `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
    `pid` INT UNSIGNED DEFAULT NULL,
    `type` ENUM('tree','item','file','user','calc','plugin','migration','route','config','template','detail','construction','project','command') NOT NULL DEFAULT 'item',
    `tree` INT UNSIGNED DEFAULT NULL,
    `text` INT UNSIGNED DEFAULT NULL,
    `data` JSON DEFAULT NULL,
    `date` DATETIME NULL,
    `slug` VARCHAR(255) GENERATED ALWAYS AS (JSON_UNQUOTE(JSON_EXTRACT(`data`, '$.slug'))) STORED,
    `route` VARCHAR(1024) GENERATED ALWAYS AS (JSON_UNQUOTE(JSON_EXTRACT(`data`, '$.route'))) STORED,
    `login` VARCHAR(255) GENERATED ALWAYS AS (JSON_UNQUOTE(JSON_EXTRACT(`data`, '$.login'))) STORED,
    `email` VARCHAR(255) GENERATED ALWAYS AS (JSON_UNQUOTE(JSON_EXTRACT(`data`, '$.email'))) STORED,
    `sort` INT GENERATED ALWAYS AS (COALESCE(JSON_EXTRACT(`data`, '$.sort'), 999999)) STORED,
    `is_deleted` TINYINT(1) GENERATED ALWAYS AS (CASE WHEN JSON_EXTRACT(`data`, '$.deleted_at') IS NOT NULL THEN 1 ELSE 0 END) STORED,
    `hash` VARCHAR(64) GENERATED ALWAYS AS (SHA2(CONCAT(CAST(COALESCE(`pid`, '') AS CHAR), `type`, CAST(COALESCE(`data`, '') AS CHAR)), 256)) STORED,
    PRIMARY KEY (`id`),
    INDEX `idx_pid` (`pid`),
    INDEX `idx_type` (`type`),
    INDEX `idx_tree` (`tree`),
    INDEX `idx_text` (`text`),
    INDEX `idx_slug_pid` (`pid`, `slug`),
    INDEX `idx_route` (`route`(255)),
    INDEX `idx_sort` (`pid`, `sort`),
    INDEX `idx_login` (`login`),
    INDEX `idx_email` (`email`),
    INDEX `idx_deleted` (`is_deleted`),
    INDEX `idx_hash` (`hash`(64))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ТАБЛИЦА SYNAPSE
CREATE TABLE IF NOT EXISTS `synapse` (
    `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
    `tree` INT UNSIGNED DEFAULT NULL,
    `parent` INT UNSIGNED DEFAULT NULL,
    `child` INT UNSIGNED DEFAULT NULL,
    `text_key` INT UNSIGNED DEFAULT NULL,
    `text_id` INT UNSIGNED DEFAULT NULL,
    `data` JSON DEFAULT NULL,
    `time` DATETIME NULL,
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    `relation_type` VARCHAR(50) GENERATED ALWAYS AS (JSON_UNQUOTE(JSON_EXTRACT(`data`, '$.relation'))) STORED,
    `hash` VARCHAR(64) GENERATED ALWAYS AS (SHA2(CONCAT(CAST(COALESCE(`parent`, '') AS CHAR), CAST(COALESCE(`child`, '') AS CHAR), CAST(COALESCE(`data`, '') AS CHAR)), 256)) STORED,
    PRIMARY KEY (`id`),
    INDEX `idx_tree` (`tree`),
    INDEX `idx_parent` (`parent`),
    INDEX `idx_child` (`child`),
    INDEX `idx_parent_child` (`parent`, `child`),
    INDEX `idx_relation_type` (`relation_type`),
    INDEX `idx_hash` (`hash`(64))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ============================================
-- КАТАЛОГ ФОРМУОРК
-- ============================================

INSERT INTO neuron (pid, type, data) VALUES (NULL, 'tree', JSON_OBJECT('slug','CATALOG','sort',100));
SET @catalog = LAST_INSERT_ID();
INSERT INTO neuron (pid, type, data) VALUES (@catalog, 'tree', JSON_OBJECT('slug','MATERIALS','sort',100));
SET @materials = LAST_INSERT_ID();
INSERT INTO neuron (pid, type, data) VALUES (@catalog, 'tree', JSON_OBJECT('slug','DETAILS','sort',200));
SET @details = LAST_INSERT_ID();
INSERT INTO neuron (pid, type, data) VALUES (@details, 'tree', JSON_OBJECT('slug','SHIELDS','sort',100));
SET @shields = LAST_INSERT_ID();
INSERT INTO neuron (pid, type, data) VALUES (@details, 'tree', JSON_OBJECT('slug','RIBS','sort',200));
SET @ribs = LAST_INSERT_ID();
INSERT INTO neuron (pid, type, data) VALUES (@details, 'tree', JSON_OBJECT('slug','SIDEWALLS','sort',300));
SET @sidewalls = LAST_INSERT_ID();
INSERT INTO neuron (pid, type, data) VALUES (@catalog, 'tree', JSON_OBJECT('slug','ASSEMBLIES','sort',300));
SET @assemblies = LAST_INSERT_ID();
INSERT INTO neuron (pid, type, data) VALUES (@catalog, 'tree', JSON_OBJECT('slug','PRICES','sort',400));
SET @prices = LAST_INSERT_ID();

-- МАТЕРИАЛ
INSERT INTO neuron (pid, type, data) VALUES (@materials, 'item', JSON_OBJECT('code','MAT.PLYWOOD-FSF.10','category','material','name','Фанера ФСФ 10мм 1500×3000мм','thickness',10,'sheet_size',JSON_ARRAY(1500,3000),'sort',10));
SET @mat_10mm = LAST_INSERT_ID();
INSERT INTO neuron (pid, type, data) VALUES (@prices, 'item', JSON_OBJECT('code','PRICE.2022-11-11','name','Цены на 11.11.2022'));
SET @price_list = LAST_INSERT_ID();
INSERT INTO synapse (parent, child, data) VALUES (@mat_10mm, @price_list, JSON_OBJECT('relation','price','grade','1_2','price',3900));
INSERT INTO synapse (parent, child, data) VALUES (@mat_10mm, @price_list, JSON_OBJECT('relation','price','grade','2_3','price',2900));
INSERT INTO synapse (parent, child, data) VALUES (@mat_10mm, @price_list, JSON_OBJECT('relation','price','grade','4_4','price',2000));

-- ЩИТЫ D.S.0
INSERT INTO neuron (pid, type, data) VALUES
(@shields, 'detail', JSON_OBJECT('code','D.S.0.425.425.10','category','shield','material','PLYWOOD-FSF','sort',101)),
(@shields, 'detail', JSON_OBJECT('code','D.S.0.425.850.10','category','shield','material','PLYWOOD-FSF','sort',102)),
(@shields, 'detail', JSON_OBJECT('code','D.S.0.425.1275.10','category','shield','material','PLYWOOD-FSF','sort',103)),
(@shields, 'detail', JSON_OBJECT('code','D.S.0.425.1700.10','category','shield','material','PLYWOOD-FSF','sort',104)),
(@shields, 'detail', JSON_OBJECT('code','D.S.0.850.425.10','category','shield','material','PLYWOOD-FSF','sort',105)),
(@shields, 'detail', JSON_OBJECT('code','D.S.0.850.850.10','category','shield','material','PLYWOOD-FSF','sort',106)),
(@shields, 'detail', JSON_OBJECT('code','D.S.0.850.1275.10','category','shield','material','PLYWOOD-FSF','sort',107)),
(@shields, 'detail', JSON_OBJECT('code','D.S.0.850.1700.10','category','shield','material','PLYWOOD-FSF','sort',108));

-- БОКОВЫЕ СТЕНКИ D.S.2
INSERT INTO neuron (pid, type, data) VALUES
(@sidewalls, 'detail', JSON_OBJECT('code','D.S.2.425.425.10','category','sidewall','material','PLYWOOD-FSF','sort',201,'holes',JSON_ARRAY(JSON_OBJECT('x',15,'y',74.5),JSON_OBJECT('x',15,'y',350.5),JSON_OBJECT('x',410,'y',74.5),JSON_OBJECT('x',410,'y',350.5))),
(@sidewalls, 'detail', JSON_OBJECT('code','D.S.2.425.850.10','category','sidewall','material','PLYWOOD-FSF','sort',202,'holes',JSON_ARRAY(JSON_OBJECT('x',15,'y',74.5),JSON_OBJECT('x',15,'y',350.5),JSON_OBJECT('x',15,'y',499.5),JSON_OBJECT('x',15,'y',775.5),JSON_OBJECT('x',410,'y',74.5),JSON_OBJECT('x',410,'y',350.5),JSON_OBJECT('x',410,'y',499.5),JSON_OBJECT('x',410,'y',775.5))),
(@sidewalls, 'detail', JSON_OBJECT('code','D.S.2.425.1275.10','category','sidewall','material','PLYWOOD-FSF','sort',203)),
(@sidewalls, 'detail', JSON_OBJECT('code','D.S.2.425.1700.10','category','sidewall','material','PLYWOOD-FSF','sort',204));

-- ПЛАНКИ D.S.3
INSERT INTO neuron (pid, type, data) VALUES
(@ribs, 'detail', JSON_OBJECT('code','D.S.3.425.125.10','category','rib','material','PLYWOOD-FSF','sort',301)),
(@ribs, 'detail', JSON_OBJECT('code','D.S.3.850.125.10','category','rib','material','PLYWOOD-FSF','sort',302));

-- КОНСТРУКЦИЯ C.S.0.3.425.425
INSERT INTO neuron (pid, type, data) VALUES (@assemblies, 'construction', JSON_OBJECT('code','C.S.0.3.425.425','category','formwork'));
SET @c425425 = LAST_INSERT_ID();
SET @d0425425 = (SELECT id FROM neuron WHERE JSON_UNQUOTE(JSON_EXTRACT(data,'$.code'))='D.S.0.425.425.10');
SET @d3425125 = (SELECT id FROM neuron WHERE JSON_UNQUOTE(JSON_EXTRACT(data,'$.code'))='D.S.3.425.125.10');
INSERT INTO synapse (parent, child, data) VALUES (@c425425, @d0425425, JSON_OBJECT('pos',JSON_ARRAY(0,0,0)));
INSERT INTO synapse (parent, child, data) VALUES (@c425425, @d3425125, JSON_OBJECT('pos',JSON_ARRAY(0,12,10)));
INSERT INTO synapse (parent, child, data) VALUES (@c425425, @d3425125, JSON_OBJECT('pos',JSON_ARRAY(0,288,10)));

-- КОНСТРУКЦИЯ C.S.0.3.425.850
INSERT INTO neuron (pid, type, data) VALUES (@assemblies, 'construction', JSON_OBJECT('code','C.S.0.3.425.850','category','formwork'));
SET @c425850 = LAST_INSERT_ID();
SET @d0425850 = (SELECT id FROM neuron WHERE JSON_UNQUOTE(JSON_EXTRACT(data,'$.code'))='D.S.0.425.850.10');
INSERT INTO synapse (parent, child, data) VALUES (@c425850, @d0425850, JSON_OBJECT('pos',JSON_ARRAY(0,0,0)));
INSERT INTO synapse (parent, child, data) VALUES (@c425850, @d3425125, JSON_OBJECT('pos',JSON_ARRAY(0,12,10)));
INSERT INTO synapse (parent, child, data) VALUES (@c425850, @d3425125, JSON_OBJECT('pos',JSON_ARRAY(0,288,10)));
INSERT INTO synapse (parent, child, data) VALUES (@c425850, @d3425125, JSON_OBJECT('pos',JSON_ARRAY(0,437,10)));
INSERT INTO synapse (parent, child, data) VALUES (@c425850, @d3425125, JSON_OBJECT('pos',JSON_ARRAY(0,713,10)));

-- ПРОЕКТ
INSERT INTO neuron (pid, type, data) VALUES (NULL, 'project', JSON_OBJECT('code','PROJ-TEST-001','name','Тестовый проект','status','pending','sort',100));
SET @project = LAST_INSERT_ID();
INSERT INTO synapse (parent, child, data) VALUES (@project, @c425850, JSON_OBJECT('pos',JSON_ARRAY(0,0,0)));
INSERT INTO synapse (parent, child, data) VALUES (@project, @c425425, JSON_OBJECT('pos',JSON_ARRAY(900,0,0)));

-- БОКОВЫЕ СТЕНКИ В ПРОЕКТ
SET @sw425 = (SELECT id FROM neuron WHERE JSON_UNQUOTE(JSON_EXTRACT(data,'$.code'))='D.S.2.425.425.10');
SET @sw850 = (SELECT id FROM neuron WHERE JSON_UNQUOTE(JSON_EXTRACT(data,'$.code'))='D.S.2.425.850.10');
INSERT INTO synapse (parent, child, data) VALUES (@project, @sw425, JSON_OBJECT('pos',JSON_ARRAY(-15,0,0)));
INSERT INTO synapse (parent, child, data) VALUES (@project, @sw425, JSON_OBJECT('pos',JSON_ARRAY(425,0,0)));
INSERT INTO synapse (parent, child, data) VALUES (@project, @sw850, JSON_OBJECT('pos',JSON_ARRAY(-15,900,0)));
INSERT INTO synapse (parent, child, data) VALUES (@project, @sw850, JSON_OBJECT('pos',JSON_ARRAY(425,900,0)));