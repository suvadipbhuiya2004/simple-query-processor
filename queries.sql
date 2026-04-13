--- Table definitions ---

CREATE TABLE PartnerUniversity (
    university_id INT PRIMARY KEY,
    name VARCHAR(100) UNIQUE NOT NULL,
    country VARCHAR(50) NOT NULL
);

CREATE TABLE TextBook (
    textbook_id INT PRIMARY KEY,
    title VARCHAR(200) NOT NULL
);

-- ()
CREATE TABLE Course (
    course_id INT PRIMARY KEY,
    course_name VARCHAR(100) UNIQUE NOT NULL,
    program_type VARCHAR(20),
    duration INT,

    university_id INT NOT NULL,
    textbook_id INT NOT NULL,

    FOREIGN KEY (university_id) REFERENCES PartnerUniversity(university_id),
    FOREIGN KEY (textbook_id) REFERENCES TextBook(textbook_id)
);

CREATE TABLE Topic (
    topic_id INT PRIMARY KEY,
    topic_name VARCHAR(50) NOT NULL
);



CREATE TABLE Instructor (
    instructor_id INT PRIMARY KEY,
    name VARCHAR(100) NOT NULL
);

CREATE TABLE Student (
    student_id INT PRIMARY KEY,
    name VARCHAR(100) NOT NULL,
    age INT,
    nationality VARCHAR(50),
    country VARCHAR(50),
    skill_level VARCHAR(50),
    category VARCHAR(50)
);

CREATE TABLE Content (
    content_id INT PRIMARY KEY,
    course_id INT,
    type VARCHAR(20),
    data TEXT,  -- url or text content

    FOREIGN KEY (course_id) REFERENCES Course(course_id)
);

CREATE TABLE CategorizedInto (
    course_id INT,
    topic_id INT,

    PRIMARY KEY (course_id, topic_id),
    FOREIGN KEY (course_id) REFERENCES Course(course_id),
    FOREIGN KEY (topic_id) REFERENCES Topic(topic_id)
);

CREATE TABLE Teaches (
    course_id INT,
    instructor_id INT,

    PRIMARY KEY (course_id, instructor_id),
    FOREIGN KEY (course_id) REFERENCES Course(course_id),
    FOREIGN KEY (instructor_id) REFERENCES Instructor(instructor_id)
);

CREATE TABLE Enrollment (
    student_id INT,
    course_id INT,

    evaluation INT ,

    PRIMARY KEY (student_id, course_id),
    FOREIGN KEY (student_id) REFERENCES Student(student_id),
    FOREIGN KEY (course_id) REFERENCES Course(course_id)
);


--- Row insertions ---

INSERT INTO PartnerUniversity VALUES
(1, 'IITKGP', 'India'),
(2, 'MIT', 'USA'),
(3, 'Stanford', 'South Africa'),
(4, 'Jadavpur University', 'Nepal'),
(5, 'NIT Durgapur', 'Russia');

INSERT INTO TextBook VALUES
(1, 'Introduction to AI'),
(2, 'Introduction to ML'),
(3, 'Database management systems concepts'),
(4, 'Computer Networks Basics'),
(5, 'Advanced Operating Systems');

INSERT INTO Course VALUES
(1, 'GenAI', 'Certificate', 6, 1, 1),
(2, 'AI Basics', 'Certificate', 5, 1, 1),
(3, 'Advanced DBMS', 'Diploma', 8, 2, 2),
(4, 'Machine Learning', 'Degree', 12, 2, 1),
(5, 'Computer Networks', 'Certificate', 4, 3, 4),
(6, 'Advanced Operating Systems', 'Diploma', 7, 4, 5);

INSERT INTO Content VALUES
(1, 1, 'Video', 'https://videos/genai_intro'),
(2, 1, 'Notes', 'https://docs/genai_summary'),
(3, 2, 'Book', 'https://books/ai_basics'),
(4, 2, 'Video', 'https://videos/ai_advanced_topics'),
(5, 3, 'Book', 'https://books/indexing_optimization_techniques'),
(6, 3, 'Notes', 'https://docs/dbms_advanced_concepts'),
(7, 4, 'Video', 'https://videos/supervised_unsupervised_learning'),
(8, 4, 'Notes', 'https://docs/machine_learning_formulas'),
(9, 5, 'Book', 'https://books/tcp_ip_models'),
(10, 5, 'Video', 'https://videos/routing_basics'),
(11, 6, 'Notes', 'https://docs/process_synchronization_scheduling'),
(12, 6, 'Video', 'https://videos/advanced_os_case_studies');

INSERT INTO Topic VALUES
(1, 'AI'),
(2, 'ML'),
(3, 'DBMS'),
(4, 'CN'),
(5, 'OS');

INSERT INTO Instructor VALUES
(1, 'Andrew Ng'),
(2, 'Prof. Hulk'),
(3, 'Dr. Strange'),
(4, 'Dr. Banner'),
(5, 'Prof. Xavier'),
(6, 'Krishna Singha');

INSERT INTO Student VALUES
(1, 'Amit', 17, 'Indian', 'India', 'Beginner', 'Student'),
(2, 'John', 65, 'American', 'USA', 'Advanced', 'Professional'),
(3, 'Soumen', 25, 'Chinese', 'China', 'Intermediate', 'Student'),
(4, 'Raj', 30, 'Indian', 'India', 'Advanced', 'Professional'),
(5, 'Hakuna Matata', 22, 'Japanese', 'Japan', 'Beginner', 'Student'),
(6, 'Anita', 28, 'Canadian', 'Canada', 'Intermediate', 'Professional'),
(7, 'Tom', 16, 'British', 'UK', 'Beginner', 'Student');

INSERT INTO CategorizedInto VALUES
(1, 1),
(1, 2),
(2, 1),
(2, 2),
(3, 3),
(4, 2),
(4, 1),
(5, 4),
(6, 5);

INSERT INTO Teaches VALUES
(1, 1),
(2, 1),
(3, 2),
(4, 1),
(5, 5),
(6, 6);

INSERT INTO Enrollment VALUES
(1, 1, 85),
(1, 2, 80),
(2, 1, 90),
(2, 2, 70),
(3, 2, 88),
(3, 1, 92),
(3, 3, 75),
(4, 4, 82),
(5, 5, 78),
(6, 6, 88),
(7, 4, 91);


SELECT c.course_name
FROM Course c
JOIN CategorizedInto ci ON c.course_id = ci.course_id
JOIN Topic t ON ci.topic_id = t.topic_id
WHERE c.program_type = 'Certificate'
  AND t.topic_name = 'AI'
  AND c.duration <= 6;

-- 2
SELECT c.course_name
FROM Course c
JOIN PartnerUniversity p ON c.university_id = p.university_id
JOIN CategorizedInto ci ON c.course_id = ci.course_id
JOIN Topic t ON ci.topic_id = t.topic_id
WHERE c.program_type = 'Certificate'
  AND t.topic_name = 'AI'
  AND c.duration <= 6
  AND p.name = 'IITKGP';

-- 3
SELECT s.name
FROM Student s
JOIN Enrollment e ON s.student_id = e.student_id
JOIN Course c ON e.course_id = c.course_id
WHERE c.course_name = 'GenAI'
  AND (s.age < 18 OR s.age > 60);

-- 4
SELECT DISTINCT s.name
FROM Student s
JOIN Enrollment e ON s.student_id = e.student_id
JOIN Course c ON e.course_id = c.course_id
JOIN CategorizedInto ci ON c.course_id = ci.course_id
JOIN Topic t ON ci.topic_id = t.topic_id
JOIN PartnerUniversity p ON c.university_id = p.university_id
WHERE s.nationality <> 'Indian'
  AND t.topic_name = 'AI'
  AND p.name = 'IITKGP';

-- 5
SELECT DISTINCT s.country
FROM Student s
JOIN Enrollment e ON s.student_id = e.student_id
JOIN Teaches t ON e.course_id = t.course_id
JOIN Instructor i ON t.instructor_id = i.instructor_id
WHERE i.name = 'Andrew Ng';

-- 6
SELECT DISTINCT i.name
FROM Instructor i
JOIN Teaches t ON i.instructor_id = t.instructor_id
JOIN Enrollment e ON t.course_id = e.course_id
JOIN Student s ON e.student_id = s.student_id
WHERE s.nationality = 'Indian';

-- 7
SELECT DISTINCT c.course_name
FROM Course c
JOIN Enrollment e1 ON c.course_id = e1.course_id
JOIN Enrollment e2 ON e1.student_id = e2.student_id
JOIN Course g ON e2.course_id = g.course_id
WHERE g.course_name = 'GenAI';

-- 9
SELECT c.course_name
FROM Course c
JOIN PartnerUniversity p ON c.university_id = p.university_id
JOIN Enrollment e ON c.course_id = e.course_id
WHERE p.name = 'IITKGP'
GROUP BY c.course_name
LIMIT 1;

-- 10
SELECT s.name
FROM Student s
JOIN Enrollment e ON s.student_id = e.student_id
JOIN Course c ON e.course_id = c.course_id
JOIN CategorizedInto ci ON c.course_id = ci.course_id
JOIN Topic t ON ci.topic_id = t.topic_id
WHERE s.nationality = 'Indian'
  AND t.topic_name = 'AI'
GROUP BY s.name
LIMIT 1;
