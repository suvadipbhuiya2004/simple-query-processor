-- TABLE CREATION

CREATE TABLE departments (
    dept_id INT PRIMARY KEY,
    dept_name VARCHAR(40) UNIQUE NOT NULL
);

CREATE TABLE users (
    user_id INT PRIMARY KEY,
    username VARCHAR(40) NOT NULL,
    is_active BOOLEAN,
    created_at TIMESTAMP,
    dept_id INT
);

CREATE TABLE courses (
    course_id INT PRIMARY KEY,
    title VARCHAR(80) UNIQUE NOT NULL,
    level VARCHAR(20) NOT NULL
);

CREATE TABLE enrollments (
    enroll_id INT PRIMARY KEY,
    user_id INT,
    course_id INT,
    grade INT
);

CREATE TABLE events (
    event_id INT PRIMARY KEY,
    user_id INT,
    score DOUBLE,
    status ENUM('ok','warn','fail'),
    created_at TIMESTAMP,
    note TEXT
);

CREATE TABLE transactions (
    id INT,
    title VARCHAR(50),
    amount INT
);

CREATE TABLE tags (
    tag_id INT,
    label VARCHAR(30)
);



-- DATA RESET FOR RERUNS

DELETE FROM events;
DELETE FROM enrollments;
DELETE FROM users;
DELETE FROM courses;
DELETE FROM departments;
DELETE FROM transactions;
DELETE FROM tags;




-- INSERTIONS

INSERT INTO departments (dept_id, dept_name) VALUES
(1, 'Engineering'),
(2, 'Sales'),
(3, 'HR'),
(4, 'Finance'),
(5, 'Product'),
(6, 'Marketing'),
(7, 'Support'),
(8, 'Legal'),
(9, 'Operations'),
(10, 'IT'),
(11, 'R&D'),
(12, 'Admin'),
(13, 'Design'),
(14, 'QA'),
(15, 'Data Science');

INSERT INTO users (user_id, username, is_active, created_at, dept_id) VALUES
(1, 'alice', true, '2026-04-01 09:00:00', 1),
(2, 'bob', false, '2026-04-01 09:30:00', 1),
(3, 'charlie', true, '2026-04-01 10:00:00', 2),
(4, 'diana', true, '2026-04-01 10:30:00', 2),
(5, 'ethan', false, '2026-04-01 11:00:00', 3),
(6, 'fatima', true, '2026-04-01 11:30:00', 3),
(7, 'george', true, '2026-04-01 12:00:00', 4),
(8, 'hana', false, '2026-04-01 12:30:00', 4),
(9, 'ivan', true, '2026-04-01 13:00:00', 5),
(10, 'julia', true, '2026-04-01 13:30:00', 5),
(11, 'kevin', true, '2026-04-01 14:00:00', 6),
(12, 'lisa', false, '2026-04-01 14:30:00', 6),
(13, 'michael', true, '2026-04-01 15:00:00', 7),
(14, 'nina', true, '2026-04-01 15:30:00', 7),
(15, 'oscar', false, '2026-04-01 16:00:00', 8);

INSERT INTO courses (course_id, title, level) VALUES
(101, 'SQL Basics', 'Beginner'),
(102, 'Advanced SQL', 'Advanced'),
(103, 'Data Modeling', 'Intermediate'),
(104, 'Query Optimization', 'Advanced'),
(105, 'Database Design', 'Intermediate'),
(106, 'Big Data Analytics', 'Advanced'),
(107, 'Cloud Databases', 'Intermediate'),
(108, 'NoSQL Databases', 'Beginner'),
(109, 'Database Security', 'Advanced'),
(110, 'Distributed Systems', 'Advanced'),
(111, 'Data Warehousing', 'Intermediate'),
(112, 'ETL Processes', 'Beginner'),
(113, 'Database Administration', 'Intermediate'),
(114, 'SQL for Data Science', 'Beginner'),
(115, 'Graph Databases', 'Advanced');

INSERT INTO enrollments VALUES
(1, 1, 101, 88),
(2, 2, 101, 72),
(3, 3, 102, 91),
(4, 4, 103, 65),
(5, 5, 104, 39),
(6, 6, 102, 95),
(7, 7, 104, 78),
(8, 8, 103, 84),
(9, 9, 101, 69),
(10, 10, 104, 87),
(11, 11, 105, 92),
(12, 12, 106, 55),
(13, 13, 107, 80),
(14, 14, 108, 77),
(15, 15, 109, 82);

INSERT INTO transactions (id, title, amount) VALUES
(1, 'alpha', 10),
(2, 'beta', 20),
(3, 'gamma', 30),
(4, 'delta', 40),
(5, 'omega', 50),
(6, 'theta', 60),
(7, 'lambda', 70),
(8, 'sigma', 80),
(9, 'epsilon', 90),
(10, 'zeta', 100),
(11, 'eta', 110),
(12, 'iota', 120),
(13, 'kappa', 130),
(14, 'mu', 140),
(15, 'nu', 150);

INSERT INTO tags (tag_id, label) VALUES
(1, 'core'),
(2, 'analytics'),
(3, 'ops'),
(4, 'security'),
(5, 'cloud'),
(6, 'performance'),
(7, 'scalability'),
(8, 'usability'),
(9, 'reliability'),
(10, 'maintainability'),
(11, 'extensibility'),
(12, 'portability'),
(13, 'compatibility'),
(14, 'flexibility'),
(15, 'efficiency');

INSERT INTO events (event_id, user_id, score, status, created_at, note) VALUES
(1, 1, 91.5, 'ok', '2026-04-14 09:00:00', 'event-1'),
(2, 2, 77.2, 'warn', '2026-04-14 09:00:00', 'event-2'),
(3, 3, 65.0, 'fail', '2026-04-14 09:00:00', 'event-3'),
(4, 4, 83.4, 'ok', '2026-04-14 09:00:00', 'event-4'),
(5, 5, 79.1, 'warn', '2026-04-14 09:00:00', 'event-5'),
(6, 6, 58.8, 'fail', '2026-04-14 09:00:00', 'event-6'),
(7, 7, 95.0, 'ok', '2026-04-14 09:00:00', 'event-7'),
(8, 8, 73.6, 'warn', '2026-04-14 09:00:00', 'event-8'),
(9, 9, 62.4, 'fail', '2026-04-14 09:00:00', 'event-9'),
(10, 10, 88.2, 'ok', '2026-04-14 09:00:00', 'event-10'),
(11, 11, 81.3, 'warn', '2026-04-14 09:00:00', 'event-11'),
(12, 12, 59.9, 'fail', '2026-04-14 09:00:00', 'event-12'),
(13, 13, 92.7, 'ok', '2026-04-14 09:00:00', 'event-13'),
(14, 14, 78.5, 'warn', '2026-04-14 09:00:00', 'event-14'),
(15, 15, 67.2, 'fail', '2026-04-14 09:00:00', 'event-15');




-- ALTER TABLE: CONSTRAINTS

ALTER TABLE users ADD CONSTRAINT FOREIGN KEY (dept_id) REFERENCES departments(dept_id);
ALTER TABLE users ADD CONSTRAINT CHECK (user_id > 0);

ALTER TABLE enrollments ADD CONSTRAINT FOREIGN KEY (user_id) REFERENCES users(user_id);
ALTER TABLE enrollments ADD CONSTRAINT FOREIGN KEY (course_id) REFERENCES courses(course_id);
ALTER TABLE enrollments ADD CONSTRAINT CHECK (grade >= 0);

ALTER TABLE events ADD CONSTRAINT FOREIGN KEY (user_id) REFERENCES users(user_id);
ALTER TABLE events ADD CONSTRAINT CHECK (score >= 0);

ALTER TABLE tags ADD CONSTRAINT PRIMARY KEY (tag_id);
ALTER TABLE tags ADD CONSTRAINT UNIQUE (label);




-- ALTER TABLE: ADD / DROP / RENAME / TYPE / ADD CONSTRAINTS

ALTER TABLE transactions ADD COLUMN temp_flag INT;
UPDATE transactions SET temp_flag = 1;
ALTER TABLE transactions DROP COLUMN temp_flag;

ALTER TABLE transactions RENAME COLUMN title TO title_tmp;
ALTER TABLE transactions RENAME COLUMN title_tmp TO title;

ALTER TABLE transactions ALTER COLUMN amount TYPE FLOAT;
ALTER TABLE transactions ALTER COLUMN amount TYPE INT;

ALTER TABLE transactions ADD CONSTRAINT PRIMARY KEY (id);
ALTER TABLE transactions ADD CONSTRAINT UNIQUE (title);
ALTER TABLE transactions ADD CONSTRAINT CHECK (amount >= 0);




-- UPDATE + DELETE

UPDATE enrollments SET grade = 95 WHERE enroll_id = 1;
UPDATE enrollments SET grade = 81 WHERE enroll_id = 4;
UPDATE courses SET level = 'Expert' WHERE course_id = 104;
UPDATE events SET score = 77.7 WHERE event_id = 2;

DELETE FROM enrollments WHERE grade < 40;
DELETE FROM events WHERE score < 58;



-- SELECT QUERIES

SELECT * FROM users ORDER BY user_id LIMIT 5;

SELECT username, dept_id
FROM users
WHERE dept_id IN (1, 2, 3)
ORDER BY username DESC
LIMIT 7;

SELECT username
FROM users
WHERE user_id NOT IN (9, 10)
ORDER BY user_id;

SELECT DISTINCT dept_id FROM users ORDER BY dept_id;

SELECT u.username, d.dept_name
FROM users u
INNER JOIN departments d ON u.dept_id = d.dept_id
ORDER BY u.username;

SELECT u.username, d.dept_name
FROM users u
LEFT JOIN departments d ON u.dept_id = d.dept_id
ORDER BY u.user_id;

SELECT u.username, d.dept_name
FROM users u
RIGHT JOIN departments d ON u.dept_id = d.dept_id
ORDER BY d.dept_id;

SELECT u.username, d.dept_name
FROM users u
FULL OUTER JOIN departments d ON u.dept_id = d.dept_id
ORDER BY d.dept_id;

SELECT u.user_id, c.course_id
FROM users u
CROSS JOIN courses c
LIMIT 10;

SELECT dept_id, COUNT(*), AVG(user_id)
FROM users
GROUP BY dept_id
HAVING COUNT(*) >= 2
ORDER BY COUNT(*) DESC;

SELECT COUNT(*), SUM(score), AVG(score), MIN(score), MAX(score)
FROM events;

SELECT COUNT_DISTINCT(user_id)
FROM events;

SELECT COUNT_DISTINCT(user_id, status)
FROM events;

SELECT u.username, COUNT(*)
FROM users u
JOIN events e ON u.user_id = e.user_id
GROUP BY u.username
ORDER BY COUNT(*) DESC
LIMIT 5;

SELECT username
FROM users
WHERE user_id IN (
    SELECT user_id FROM enrollments
)
ORDER BY username;

SELECT username
FROM users
WHERE user_id NOT IN (
    SELECT user_id FROM enrollments WHERE grade >= 90
)
ORDER BY username;

SELECT u.username
FROM users u
WHERE EXISTS (
    SELECT e.enroll_id
    FROM enrollments e
    WHERE e.user_id = u.user_id
)
ORDER BY u.user_id;




-- PATH SELECT
PATH SELECT username
FROM users
WHERE user_id NOT IN (
    SELECT user_id FROM enrollments WHERE grade >= 90
)
ORDER BY username;

PATH SELECT u.username
FROM users u
WHERE EXISTS (
    SELECT e.enroll_id
    FROM enrollments e
    WHERE e.user_id = u.user_id
)
ORDER BY u.user_id;