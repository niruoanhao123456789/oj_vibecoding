-- migrate_password_hash.sql
-- 一次性迁移：把历史明文密码编码为 "<时间戳盐>:<sha256(密码+盐)>"。
--
-- 盐取自注册时间戳（秒）+ 用户 id，保证每个用户盐唯一；
-- 已含 ':' 的行（即已按新格式存储）自动跳过，可重复执行。
--
-- 用法：mysql -uoj -poj_password oj_vibecoding < sql/migrate_password_hash.sql

USE oj_vibecoding;

UPDATE users
SET password = CONCAT(UNIX_TIMESTAMP(created_at), id, ':',
                      SHA2(CONCAT(password, UNIX_TIMESTAMP(created_at), id), 256))
WHERE password NOT LIKE '%:%' AND password <> '';
