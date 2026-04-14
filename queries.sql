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
(11, 'Admin'),
(12, 'R&D'),
(13, 'Design'),
(14, 'QA'),
(15, 'Data Science'),
(16, 'Security'),
(17, 'Compliance'),
(18, 'Customer Success'),
(19, 'Business Development'),
(20, 'Strategy'),
(21, 'Innovation'),
(22, 'Analytics'),
(23, 'Content'),
(24, 'Communications'),
(25, 'Public Relations'),
(26, 'Investor Relations'),
(27, 'Corporate Development'),
(28, 'Legal Affairs'),
(29, 'Facilities'),
(30, 'Executive');

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
(15, 'oscar', false, '2026-04-01 16:00:00', 8),
(16, 'paula', true, '2026-04-01 16:30:00', 8),
(17, 'quinn', true, '2026-04-01 17:00:00', 9),
(18, 'rachel', false, '2026-04-01 17:30:00', 9),
(19, 'steve', true, '2026-04-01 18:00:00', 10),
(20, 'tina', true, '2026-04-01 18:30:00', 10),
(21, 'uma', true, '2026-04-01 19:00:00', 11),
(22, 'victor', false, '2026-04-01 19:30:00', 11),
(23, 'wendy', true, '2026-04-01 20:00:00', 12),
(24, 'xavier', true, '2026-04-01 20:30:00', 12),
(25, 'yara', false, '2026-04-01 21:00:00', 13),
(26, 'zach', true, '2026-04-01 21:30:00', 13),
(27, 'amy', true, '2026-04-01 22:00:00', 14),
(28, 'brian', false, '2026-04-01 22:30:00', 14),
(29, 'carol', true, '2026-04-01 23:00:00', 15),
(30, 'daniel', true, '2026-04-01 23:30:00', 15),
(31, 'ella', true, '2026-04-02 00:00:00', 16),
(32, 'frank', false, '2026-04-02 00:30:00', 16),
(33, 'grace', true, '2026-04-02 01:00:00', 17),
(34, 'harry', true, '2026-04-02 01:30:00', 17),
(35, 'irina', false, '2026-04-02 02:00:00', 18),
(36, 'jack', true, '2026-04-02 02:30:00', 18),
(37, 'karen', true, '2026-04-02 03:00:00', 19),
(38, 'leo', false, '2026-04-02 03:30:00', 19),
(39, 'maria', true, '2026-04-02 04:00:00', 20),
(40, 'nick', true, '2026-04-02 04:30:00', 20),
(41, 'olivia', true, '2026-04-02 05:00:00', 21),
(42, 'peter', false, '2026-04-02 05:30:00', 21),
(43, 'quincy', true, '2026-04-02 06:00:00', 22),
(44, 'rose', true, '2026-04-02 06:30:00', 22),
(45, 'samuel', false, '2026-04-02 07:00:00', 23),
(46, 'tracy', true, '2026-04-02 07:30:00', 23),
(47, 'ursula', true, '2026-04-02 08:00:00', 24),
(48, 'viktor', false, '2026-04-02 08:30:00', 24),
(49, 'wanda', true, '2026-04-02 09:00:00', 25),
(50, 'xena', true, '2026-04-02 09:30:00', 25);

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
(115, 'Graph Databases', 'Advanced'),
(116, 'Time Series Databases', 'Intermediate'),
(117, 'In-Memory Databases', 'Advanced'),
(118, 'Database Scalability', 'Advanced'),
(119, 'SQL Performance Tuning', 'Advanced'),
(120, 'Database Backup and Recovery', 'Intermediate'),
(121, 'Database Migration', 'Intermediate'),
(122, 'SQL for Developers', 'Beginner'),
(123, 'Database Trends', 'Advanced'),
(124, 'Future of Databases', 'Advanced'),
(125, 'Database Case Studies', 'Intermediate'),
(126, 'SQL Challenges', 'Advanced'),
(127, 'Database Myths', 'Beginner'),
(128, 'Data Lakes', 'Intermediate'),
(129, 'Real-Time Databases', 'Advanced'),
(130, 'Database APIs', 'Intermediate'),
(131, 'Database Testing', 'Beginner'),
(132, 'Database Compliance', 'Advanced'),
(133, 'Database Ethics', 'Intermediate'),
(134, 'Database Careers', 'Beginner'),
(135, 'Database Future Trends', 'Advanced'),
(136, 'Database Research', 'Advanced'),
(137, 'Database Case Studies 2', 'Intermediate'),
(138, 'SQL for Analysts', 'Beginner'),
(139, 'Database Performance Monitoring', 'Advanced'),
(140, 'Database Cost Optimization', 'Intermediate');

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
(15, 15, 109, 82),
(16, 16, 110, 90),
(17, 17, 111, 68),
(18, 18, 112, 74),
(19, 19, 113, 85),
(20, 20, 114, 91),
(21, 21, 115, 89),
(22, 22, 116, 60),
(23, 23, 117, 94),
(24, 24, 118, 81),
(25, 25, 119, 73),
(26, 26, 120, 88),
(27, 27, 121, 79),
(28, 28, 122, 68),
(29, 29, 123, 90),
(30, 30, 124, 85),
(31, 31, 125, 91),
(32, 32, 126, 77),
(33, 33, 127, 82),
(34, 34, 128, 69),
(35, 35, 129, 94),
(36, 36, 130, 80),
(37, 37, 131, 86),
(38, 38, 132, 75),
(39, 39, 133, 88),
(40, 40, 134, 90),
(41, 41, 135, 93),
(42, 42, 136, 78),
(43, 43, 137, 84),
(44, 44, 138, 70),
(45, 45, 139, 91),
(46, 46, 140, 82),
(47, 47, 101, 87),
(48, 48, 102, 79),
(49, 49, 103, 85),
(50, 50, 104, 90);

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
(15, 'nu', 150),
(16, 'xi', 160),
(17, 'omicron', 170),
(18, 'pi', 180),
(19, 'rho', 190),
(20, 'tau', 200),
(21, 'upsilon', 210),
(22, 'phi', 220),
(23, 'chi', 230),
(24, 'psi', 240),
(25, 'omega2', 250),
(26, 'alpha2', 260),
(27, 'beta2', 270),
(28, 'gamma2', 280),
(29, 'delta2', 290),
(30, 'theta2', 300),
(31, 'lambda2', 310),
(32, 'sigma2', 320),
(33, 'epsilon2', 330),
(34, 'zeta2', 340),
(35, 'eta2', 350),
(36, 'iota2', 360),
(37, 'kappa2', 370),
(38, 'mu2', 380),
(39, 'nu2', 390),
(40, 'xi2', 400);

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
(15, 'efficiency'),
(16, 'resilience'),
(17, 'monitoring'),
(18, 'automation'),
(19, 'testing'),
(20, 'documentation'),
(21, 'community'),
(22, 'support'),
(23, 'training'),
(24, 'consulting'),
(25, 'integration'),
(26, 'migration'),
(27, 'backup'),
(28, 'recovery'),
(29, 'compliance'),
(30, 'governance'),
(31, 'innovation'),
(32, 'research'),
(33, 'development'),
(34, 'case studies'),
(35, 'best practices'),
(36, 'trends'),
(37, 'future outlook'),
(38, 'challenges'),
(39, 'opportunities'),
(40, 'success stories'),
(41, 'failure stories'),
(42, 'lessons learned'),
(43, 'tips and tricks'),
(44, 'common pitfalls'),
(45, 'advanced techniques'),
(46, 'beginner guides'),
(47, 'intermediate guides'),
(48, 'expert guides'),
(49, 'reference materials'),
(50, 'cheat sheets'),
(51, 'glossary'),
(52, 'faqs'),
(53, 'myths'),
(54, 'real-world examples'),
(55, 'case studies 2'),
(56, 'industry insights'),
(57, 'vendor comparisons'),
(58, 'tool reviews'),
(59, 'community highlights'),
(60, 'events');

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
(15, 15, 67.2, 'fail', '2026-04-14 09:00:00', 'event-15'),
(16, 16, 85.9, 'ok', '2026-04-14 09:00:00', 'event-16'),
(17, 17, 80.0, 'warn', '2026-04-14 09:00:00', 'event-17'),
(18, 18, 55.5, 'fail', '2026-04-14 09:00:00', 'event-18'),
(19, 19, 90.1, 'ok', '2026-04-14 09:00:00', 'event-19'),
(20, 20, 82.4, 'warn', '2026-04-14 09:00:00', 'event-20'),
(21, 21, 60.3, 'fail', '2026-04-14 09:00:00', 'event-21'),
(22, 22, 87.8, 'ok', '2026-04-14 09:00:00', 'event-22'),
(23, 23, 77.0, 'warn', '2026-04-14 09:00:00', 'event-23'),
(24, 24, 58.2, 'fail', '2026-04-14 09:00:00', 'event-24'),
(25, 25, 91.0, 'ok', '2026-04-14 09:00:00', 'event-25'),
(26, 26, 79.5, 'warn', '2026-04-14 09:00:00', 'event-26'),
(27, 27, 65.8, 'fail', '2026-04-14 09:00:00', 'event-27'),
(28, 28, 84.3, 'ok', '2026-04-14 09:00:00', 'event-28'),
(29, 29, 72.1, 'warn', '2026-04-14 09:00:00', 'event-29'),
(30, 30, 59.0, 'fail', '2026-04-14 09:00:00', 'event-30'),
(31, 31, 88.5, 'ok', '2026-04-14 09:00:00', 'event-31'),
(32, 32, 76.4, 'warn', '2026-04-14 09:00:00', 'event-32'),
(33, 33, 63.7, 'fail', '2026-04-14 09:00:00', 'event-33'),
(34, 34, 82.9, 'ok', '2026-04-14 09:00:00', 'event-34'),
(35, 35, 70.5, 'warn', '2026-04-14 09:00:00', 'event-35'),
(36, 36, 57.3, 'fail', '2026-04-14 09:00:00', 'event-36'),
(37, 37, 90.8, 'ok', '2026-04-14 09:00:00', 'event-37'),
(38, 38, 78.9, 'warn', '2026-04-14 09:00:00', 'event-38'),
(39, 39, 61.5, 'fail', '2026-04-14 09:00:00', 'event-39'),
(40, 40, 85.2, 'ok', '2026-04-14 09:00:00', 'event-40'),
(41, 41, 80.4, 'warn', '2026-04-14 09:00:00', 'event-41'),
(42, 42, 58.7, 'fail', '2026-04-14 09:00:00', 'event-42'),
(43, 43, 89.3, 'ok', '2026-04-14 09:00:00', 'event-43'),
(44, 44, 77.8, 'warn', '2026-04-14 09:00:00', 'event-44'),
(45, 45, 62.9, 'fail', '2026-04-14 09:00:00', 'event-45'),
(46, 46, 83.7, 'ok', '2026-04-14 09:00:00', 'event-46'),
(47, 47, 75.6, 'warn', '2026-04-14 09:00:00', 'event-47'),
(48, 48, 60.8, 'fail', '2026-04-14 09:00:00', 'event-48'),
(49, 49, 87.1, 'ok', '2026-04-14 09:00:00', 'event-49'),
(50, 50, 80.2, 'warn', '2026-04-14 09:00:00', 'event-50'),
(51, 1, 92.3, 'ok', '2026-04-15 09:00:00', 'event-51'),
(52, 2, 78.4, 'warn', '2026-04-15 09:00:00', 'event-52'),
(53, 3, 66.1, 'fail', '2026-04-15 09:00:00', 'event-53'),
(54, 4, 84.5, 'ok', '2026-04-15 09:00:00', 'event-54'),
(55, 5, 80.3, 'warn', '2026-04-15 09:00:00', 'event-55'),
(56, 6, 59.4, 'fail', '2026-04-15 09:00:00', 'event-56'),
(57, 7, 96.2, 'ok', '2026-04-15 09:00:00', 'event-57'),
(58, 8, 74.7, 'warn', '2026-04-15 09:00:00', 'event-58'),
(59, 9, 63.2, 'fail', '2026-04-15 09:00:00', 'event-59'),
(60, 10, 89.4, 'ok', '2026-04-15 09:00:00', 'event-60');




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
