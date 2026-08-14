-- schema.sql
-- 按 SPEC 第 4 章创建数据表、索引、外键及初始管理员账号。
-- 前置：先执行 sql/init_db.sql 创建数据库 oj_vibecoding 与用户 'oj'@'localhost'。

USE oj_vibecoding;

-- 4.1 users 用户表
CREATE TABLE IF NOT EXISTS users (
    id         INT UNSIGNED     NOT NULL AUTO_INCREMENT,
    username   VARCHAR(64)      NOT NULL,
    password   VARCHAR(128)     NOT NULL,
    role       ENUM('student','teacher','admin') NOT NULL DEFAULT 'student',
    status     TINYINT          NOT NULL DEFAULT 1,
    created_at DATETIME         NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    UNIQUE KEY uk_username (username),
    KEY idx_role (role)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 4.2 sessions 会话表
CREATE TABLE IF NOT EXISTS sessions (
    token      CHAR(64)     NOT NULL,
    user_id    INT UNSIGNED NOT NULL,
    created_at DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
    expires_at DATETIME     NOT NULL,
    PRIMARY KEY (token),
    KEY idx_user_id (user_id),
    CONSTRAINT fk_sessions_user FOREIGN KEY (user_id) REFERENCES users (id)
        ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 4.3 problems 题目表
CREATE TABLE IF NOT EXISTS problems (
    id               INT UNSIGNED  NOT NULL AUTO_INCREMENT,
    title            VARCHAR(255)  NOT NULL,
    description      TEXT          NOT NULL,
    sample_in        TEXT          NOT NULL,
    sample_out       TEXT          NOT NULL,
    time_limit_ms    INT UNSIGNED  NOT NULL DEFAULT 1000,
    memory_limit_mb  INT UNSIGNED  NOT NULL DEFAULT 256,
    difficulty       TINYINT UNSIGNED NOT NULL DEFAULT 1,
    test_dir         VARCHAR(255)  NOT NULL,
    created_by       INT UNSIGNED  NULL,
    created_at       DATETIME      NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    UNIQUE KEY uk_title (title),
    KEY idx_created_by (created_by),
    CONSTRAINT fk_problems_creator FOREIGN KEY (created_by) REFERENCES users (id)
        ON DELETE SET NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 4.4 submissions 提交表
CREATE TABLE IF NOT EXISTS submissions (
    id            INT UNSIGNED  NOT NULL AUTO_INCREMENT,
    user_id       INT UNSIGNED  NOT NULL,
    problem_id    INT UNSIGNED  NOT NULL,
    language      ENUM('cpp','c') NOT NULL DEFAULT 'cpp',
    code          LONGTEXT      NOT NULL,
    status        VARCHAR(20)   NOT NULL DEFAULT 'PENDING',
    exec_time_ms  INT UNSIGNED  NULL,
    memory_kb     INT UNSIGNED  NULL,
    error_message TEXT          NULL,
    created_at    DATETIME      NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    KEY idx_user_id (user_id),
    KEY idx_problem_id (problem_id),
    KEY idx_status (status),
    KEY idx_user_created (user_id, created_at),
    CONSTRAINT fk_submissions_user    FOREIGN KEY (user_id)    REFERENCES users (id)
        ON DELETE CASCADE,
    CONSTRAINT fk_submissions_problem FOREIGN KEY (problem_id) REFERENCES problems (id)
        ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 4.5 classes 班级表（一名教师一个班）
CREATE TABLE IF NOT EXISTS classes (
    id          INT UNSIGNED  NOT NULL AUTO_INCREMENT,
    teacher_id  INT UNSIGNED  NOT NULL,
    name        VARCHAR(64)   NOT NULL DEFAULT '默认班级',
    invite_code VARCHAR(32)   NOT NULL,
    created_at  DATETIME      NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (id),
    UNIQUE KEY uk_teacher (teacher_id),
    UNIQUE KEY uk_invite_code (invite_code),
    CONSTRAINT fk_classes_teacher FOREIGN KEY (teacher_id) REFERENCES users (id)
        ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 4.6 class_members 班级成员表
CREATE TABLE IF NOT EXISTS class_members (
    class_id   INT UNSIGNED  NOT NULL,
    student_id INT UNSIGNED  NOT NULL,
    joined_at  DATETIME      NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (class_id, student_id),
    KEY idx_student (student_id),
    CONSTRAINT fk_cm_class   FOREIGN KEY (class_id)   REFERENCES classes (id)
        ON DELETE CASCADE,
    CONSTRAINT fk_cm_student FOREIGN KEY (student_id) REFERENCES users (id)
        ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 初始管理员账号（幂等：已存在则跳过）
INSERT INTO users (username, password, role, status)
    VALUES ('admin', 'admin123', 'admin', 1)
    ON DUPLICATE KEY UPDATE username = VALUES(username);
