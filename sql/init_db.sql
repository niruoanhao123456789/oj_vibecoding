CREATE DATABASE IF NOT EXISTS oj_vibecoding
  CHARACTER SET utf8mb4
  COLLATE utf8mb4_unicode_ci;

CREATE USER IF NOT EXISTS 'oj'@'localhost' IDENTIFIED BY 'oj_password';
GRANT ALL PRIVILEGES ON oj_vibecoding.* TO 'oj'@'localhost';
FLUSH PRIVILEGES;
